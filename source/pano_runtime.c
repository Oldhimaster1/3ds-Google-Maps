#include "pano_runtime.h"
#include "explore.h"
#include "network.h"
#include "stb_image.h"
#include "logging.h"
#include <3ds.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Limits deliberately bound Old 3DS memory usage. All panoramas are rendered
 * at 1024x512 RGB565 regardless of the upstream source resolution. */
#define PANO_PIXELS (1024u * 512u)
#define PANO_IMAGE_MAX (5u * 1024u * 1024u)
#define PANO_JSON_MAX (512u * 1024u)

typedef enum { JOB_NONE, JOB_SEARCH, JOB_STEP } PanoJobType;
typedef enum { PANO_OK, PANO_NO_COVERAGE, PANO_NETWORK_ERROR,
               PANO_INVALID_IMAGE, PANO_OUT_OF_MEMORY } PanoResult;

static struct {
    Thread thread;
    atomic_bool cancel, done;
    PanoJobType job;
    PanoResult outcome;
    double requested_lat, requested_lon;
    int direction;
    int new_index;
    unsigned count, index;
    PanoLiveImage candidates[PANO_NEARBY_MAX];
    PanoLiveImage selected, new_selected;
    u16 *pending;
    bool has_live;
    char status[128];
} s_pano;

static bool http_get(void *curl, const char *url, unsigned limit, u8 **buf, u32 *size) {
    return network_request(curl, url, limit, &s_pano.cancel, buf, size);
}

static bool load_candidate(void *curl, const PanoLiveImage *candidate) {
    char url[180];
    if (!pano_image_url(candidate->id, url, sizeof(url))) return false;
    u8 *jpeg = NULL;
    u32 size = 0;
    if (!http_get(curl, url, PANO_IMAGE_MAX, &jpeg, &size)) return false;
    int w = 0, h = 0, comp = 0;
    bool shape = stbi_info_from_memory(jpeg, (int)size, &w, &h, &comp) &&
                 w >= 512 && w <= 2048 && h >= 256 && h <= 1024 &&
                 w == h * 2;
    if (!shape) {
        free(jpeg);
        s_pano.outcome = PANO_INVALID_IMAGE;
        return false;
    }
    unsigned char *rgb = stbi_load_from_memory(jpeg, (int)size, &w, &h, &comp, 3);
    free(jpeg);
    if (!rgb) {
        s_pano.outcome = PANO_INVALID_IMAGE;
        return false;
    }
    u16 *tiled = malloc(PANO_PIXELS * sizeof(*tiled));
    if (!tiled) {
        stbi_image_free(rgb);
        s_pano.outcome = PANO_OUT_OF_MEMORY;
        return false;
    }
    /* Match prepare_explore_assets.py: vertically flipped, 8x8 Morton-tiled
     * RGB565. Use nearest-neighbour scaling to keep CPU costs bounded. */
    for (unsigned y = 0; y < 512; y++) {
        unsigned source_y = (unsigned)(h - 1) - y * (unsigned)h / 512;
        for (unsigned x = 0; x < 1024; x++) {
            unsigned source_x = x * (unsigned)w / 1024;
            const unsigned char *p = rgb + (source_y * (unsigned)w + source_x) * 3;
            unsigned m = (x & 1) | ((y & 1) << 1) |
                         ((x & 2) << 1) | ((y & 2) << 2) |
                         ((x & 4) << 2) | ((y & 4) << 3);
            unsigned index = ((y >> 3) * 128 + (x >> 3)) * 64 + m;
            tiled[index] = (u16)(((p[0] >> 3) << 11) |
                                 ((p[1] >> 2) << 5) | (p[2] >> 3));
        }
    }
    stbi_image_free(rgb);
    s_pano.pending = tiled;
    s_pano.new_selected = *candidate;
    s_pano.outcome = PANO_OK;
    return true;
}

static bool lookup_picture(void *curl, const char *id, PanoLiveImage *out) {
    char url[180];
    if (!pano_metadata_url(id, url, sizeof(url))) return false;
    u8 *bytes = NULL;
    u32 len = 0;
    if (!http_get(curl, url, PANO_JSON_MAX, &bytes, &len)) return false;
    bool ok = pano_parse_image((const char *)bytes, len, out);
    free(bytes);
    return ok;
}

static void pano_worker(void *unused) {
    (void)unused;
    s_pano.outcome = PANO_NETWORK_ERROR;
    void *curl = network_alloc_tile_curl();
    if (!curl) goto end;
    if (s_pano.job == JOB_SEARCH) {
        /* Prefer the nearest usable 360-degree photo, expanding up to
         * around 5 km only if the smaller searches find nothing. */
        static const double radii[] = { 0.002, 0.01, 0.045 };
        for (unsigned i = 0; i < 3 && !atomic_load(&s_pano.cancel); i++) {
            char url[220];
            if (!pano_search_url(s_pano.requested_lat, s_pano.requested_lon,
                                 radii[i], url, sizeof(url))) break;
            u8 *data = NULL;
            u32 len = 0;
            if (!http_get(curl, url, PANO_JSON_MAX, &data, &len)) break;
            s_pano.count = pano_parse_nearby((const char *)data, len,
                                s_pano.requested_lat, s_pano.requested_lon,
                                s_pano.candidates, PANO_NEARBY_MAX);
            free(data);
            if (s_pano.count) {
                /* A single bad or oversized image must not hide a nearby
                 * valid panorama. Try a few of the nearest alternatives. */
                for (unsigned candidate = 0; candidate < s_pano.count &&
                     candidate < 4 && !atomic_load(&s_pano.cancel); candidate++) {
                    s_pano.new_index = (int)candidate;
                    if (load_candidate(curl, &s_pano.candidates[candidate])) break;
                }
                break;
            }
        }
        if (!s_pano.count && s_pano.outcome == PANO_NETWORK_ERROR)
            s_pano.outcome = PANO_NO_COVERAGE;
    } else if (s_pano.job == JOB_STEP) {
        const char *next = s_pano.direction > 0 ?
            s_pano.selected.next_id : s_pano.selected.prev_id;
        PanoLiveImage selected;
        bool found = false;
        if (*next && !atomic_load(&s_pano.cancel))
            found = lookup_picture(curl, next, &selected);
        /* If there is no sequence link, browse the local nearby results. */
        if (!found && s_pano.count > 1) {
            int next_index = (int)s_pano.index + (s_pano.direction > 0 ? 1 : -1);
            if (next_index >= 0 && next_index < (int)s_pano.count) {
                selected = s_pano.candidates[next_index];
                s_pano.new_index = next_index;
                found = true;
            }
        }
        if (found && !atomic_load(&s_pano.cancel))
            load_candidate(curl, &selected);
        else if (!found) s_pano.outcome = PANO_NO_COVERAGE;
    }
    network_free_tile_curl(curl);
end:
    atomic_store(&s_pano.done, true);
}

static bool launch(PanoJobType job) {
    if (s_pano.thread) return false;
    s_pano.job = job;
    s_pano.pending = NULL;
    s_pano.outcome = PANO_NETWORK_ERROR;
    atomic_store(&s_pano.cancel, false);
    atomic_store(&s_pano.done, false);
    s_pano.thread = threadCreate(pano_worker, NULL, 128 * 1024, 0x31, -2, false);
    return s_pano.thread != NULL;
}

bool pano_runtime_start(double lat, double lon) {
    if (s_pano.thread || !is_connected()) {
        snprintf(s_pano.status, sizeof(s_pano.status),
                 "Wi-Fi needed for live panoramas");
        return false;
    }
    s_pano.requested_lat = lat;
    s_pano.requested_lon = lon;
    s_pano.count = 0;
    s_pano.has_live = false;
    snprintf(s_pano.status, sizeof(s_pano.status), "Searching Panoramax near map center...");
    if (launch(JOB_SEARCH)) return true;
    snprintf(s_pano.status, sizeof(s_pano.status), "Could not start panorama search");
    return false;
}

bool pano_runtime_step(int direction) {
    if (s_pano.thread || !s_pano.has_live || !direction) return false;
    s_pano.direction = direction;
    s_pano.new_index = (int)s_pano.index;
    snprintf(s_pano.status, sizeof(s_pano.status),
             "Loading %s street panorama...", direction > 0 ? "next" : "previous");
    if (launch(JOB_STEP)) return true;
    snprintf(s_pano.status, sizeof(s_pano.status), "Could not start panorama load");
    return false;
}

void pano_runtime_poll(void) {
    if (!s_pano.thread || !atomic_load(&s_pano.done)) return;
    threadJoin(s_pano.thread, U64_MAX);
    threadFree(s_pano.thread);
    s_pano.thread = NULL;
    if (s_pano.outcome == PANO_OK && s_pano.pending &&
        explore_load_street_pixels(s_pano.pending)) {
        s_pano.selected = s_pano.new_selected;
        s_pano.index = (unsigned)s_pano.new_index;
        s_pano.has_live = true;
        snprintf(s_pano.status, sizeof(s_pano.status),
                 "Live 360 panorama  %.4f, %.4f",
                 s_pano.selected.lat, s_pano.selected.lon);
    } else if (s_pano.outcome == PANO_NO_COVERAGE) {
        snprintf(s_pano.status, sizeof(s_pano.status),
                 "No 360 panoramas found nearby");
    } else if (s_pano.outcome == PANO_INVALID_IMAGE) {
        snprintf(s_pano.status, sizeof(s_pano.status),
                 "Unsupported panorama dimensions or image");
    } else if (s_pano.outcome == PANO_OUT_OF_MEMORY) {
        snprintf(s_pano.status, sizeof(s_pano.status), "Not enough memory for panorama");
    } else {
        snprintf(s_pano.status, sizeof(s_pano.status),
                 "Panoramax unavailable; bundled demo still works");
    }
    free(s_pano.pending);
    s_pano.pending = NULL;
    s_pano.job = JOB_NONE;
}

void pano_runtime_stop(void) {
    if (s_pano.thread) {
        atomic_store(&s_pano.cancel, true);
        threadJoin(s_pano.thread, U64_MAX);
        threadFree(s_pano.thread);
        s_pano.thread = NULL;
    }
    free(s_pano.pending);
    s_pano.pending = NULL;
    s_pano.has_live = false;
    s_pano.count = 0;
    s_pano.job = JOB_NONE;
    s_pano.status[0] = 0;
}

bool pano_runtime_busy(void) { return s_pano.thread != NULL; }
bool pano_runtime_has_live(void) { return s_pano.has_live; }
const char *pano_runtime_status(void) { return s_pano.status; }
const PanoLiveImage *pano_runtime_current(void) {
    return s_pano.has_live ? &s_pano.selected : NULL;
}
