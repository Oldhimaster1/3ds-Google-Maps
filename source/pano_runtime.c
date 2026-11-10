#include "pano_runtime.h"
#include "street_sources.h"
#include "explore.h"
#include "network.h"
#include "stb_image.h"
#include <3ds.h>
#include <curl/curl.h>
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANO_PIXELS (1024u * 512u)
#define PANO_IMAGE_MAX (4u * 1024u * 1024u)
#define PANO_JSON_MAX (512u * 1024u)
#define PANO_CFG "sdmc:/3ds_google_maps/streetview.cfg"
#define MAPILLARY_TOKEN_FILE "sdmc:/3ds_google_maps/mapillary-token.txt"

typedef enum { JOB_NONE, JOB_SEARCH, JOB_STEP, JOB_UPGRADE } Job;
typedef enum { RES_OK, RES_EMPTY, RES_NETWORK, RES_IMAGE, RES_MEMORY } Result;
static struct {
    Thread thread;
    atomic_bool cancel, done;
    Job job;
    Result result;
    bool preview;
    bool upgrade_pending;
    int queued_step, direction;
    unsigned count, index, new_index;
    double latitude, longitude;
    PanoLiveImage candidates[PANO_NEARBY_MAX];
    PanoLiveImage selected, incoming;
    u16 *pixels;
    char token[256];
    bool has_live;
    char status[120];
} s;
static struct {
    StreetProvider provider;
    bool coverage;
    bool loaded;
    unsigned count;
    PanoLiveImage items[PANO_NEARBY_MAX];
} prefs = { STREET_PROVIDER_AUTO, false, false, 0 };

static void read_prefs(void) {
    if (prefs.loaded) return;
    prefs.loaded = true;
    FILE *f = fopen(PANO_CFG, "r");
    if (!f) return;
    unsigned provider = 0, coverage = 0;
    if (fscanf(f, "%u %u", &provider, &coverage) == 2 &&
        provider < STREET_PROVIDER_COUNT) {
        prefs.provider = (StreetProvider)provider;
        prefs.coverage = coverage != 0;
    }
    fclose(f);
}
static void write_prefs(void) {
    FILE *f = fopen(PANO_CFG, "w");
    if (f) {
        fprintf(f, "%u %u\n", (unsigned)prefs.provider, (unsigned)prefs.coverage);
        fclose(f);
    }
}
void pano_runtime_set_provider(StreetProvider provider) {
    read_prefs();
    if (provider < STREET_PROVIDER_COUNT && prefs.provider != provider) {
        prefs.provider = provider;
        write_prefs();
    }
}
StreetProvider pano_runtime_provider(void) { read_prefs(); return prefs.provider; }
void pano_runtime_set_coverage(bool enabled) {
    read_prefs();
    prefs.coverage = enabled;
    write_prefs();
}
bool pano_runtime_coverage_enabled(void) { read_prefs(); return prefs.coverage; }
unsigned pano_runtime_coverage(PanoLiveImage *items, unsigned max_items) {
    if (!items || !max_items) return 0;
    unsigned n = prefs.count < max_items ? prefs.count : max_items;
    memcpy(items, prefs.items, n * sizeof(*items));
    return n;
}
const PanoLiveImage *pano_runtime_coverage_view(unsigned *count) {
    if (count) *count = prefs.count;
    return prefs.items;
}
static void read_token(void) {
    s.token[0] = 0;
    FILE *f = fopen(MAPILLARY_TOKEN_FILE, "rb");
    if (!f) return;
    char raw[256] = {0};
    size_t n = fread(raw, 1, sizeof(raw) - 1, f);
    fclose(f);
    while (n && (raw[n-1] == '\n' || raw[n-1] == '\r' ||
                 raw[n-1] == ' ')) raw[--n] = 0;
    if (street_mapillary_token_valid(raw))
        snprintf(s.token, sizeof(s.token), "%s", raw);
}
static bool http_get(void *handle, const char *url, unsigned limit,
                     StreetProvider provider, u8 **buf, u32 *size) {
    if (!handle || !url || strncmp(url, "https://", 8)) return false;
    CURL *curl = (CURL *)handle;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    /* Never send a Mapillary access token using unchecked TLS. The existing
     * map tile transport still needs its own separate TLS migration. */
    if (provider == STREET_PROVIDER_MAPILLARY) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        /* Optional trusted CA bundle for 3DS mbedTLS builds without a system
         * CA store. Never downgrade verification to make token requests work. */
        static const char *ca_path = "sdmc:/3ds_google_maps/cacert.pem";
        FILE *ca = fopen(ca_path, "rb");
        if (ca) {
            fclose(ca);
            curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
        }
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    return network_request(handle, url, limit, &s.cancel, buf, size);
}
static bool load_pixels(void *handle, const char *url, StreetProvider provider) {
    u8 *jpeg = NULL;
    u32 size = 0;
    if (!http_get(handle, url, PANO_IMAGE_MAX, provider, &jpeg, &size)) return false;
    int w = 0, h = 0, comp = 0;
    bool shape = stbi_info_from_memory(jpeg, (int)size, &w, &h, &comp) &&
                 w >= 128 && w <= 2048 && h >= 64 && h <= 1024 && w == h * 2;
    if (!shape) {
        free(jpeg);
        s.result = RES_IMAGE;
        return false;
    }
    /* Decode to RGB rather than RGBA: saves roughly 2 MiB at 2048×1024.
     * Converted RGB565 uses an additional fixed 1 MiB buffer. */
    u8 *rgb = stbi_load_from_memory(jpeg, (int)size, &w, &h, &comp, 3);
    free(jpeg);
    if (!rgb) { s.result = RES_IMAGE; return false; }
    u16 *tiled = malloc(PANO_PIXELS * sizeof(u16));
    if (!tiled) {
        stbi_image_free(rgb);
        s.result = RES_MEMORY;
        return false;
    }
    for (unsigned y = 0; y < 512 && !atomic_load(&s.cancel); y++) {
        unsigned sy = (unsigned)(h - 1) - y * (unsigned)h / 512u;
        for (unsigned x = 0; x < 1024; x++) {
            unsigned sx = x * (unsigned)w / 1024u;
            const u8 *p = rgb + (sy * (unsigned)w + sx) * 3u;
            unsigned morton = (x & 1u) | ((y & 1u) << 1u) |
                              ((x & 2u) << 1u) | ((y & 2u) << 2u) |
                              ((x & 4u) << 2u) | ((y & 4u) << 3u);
            unsigned addr = ((y >> 3u) * 128u + (x >> 3u)) * 64u + morton;
            tiled[addr] = (u16)(((p[0] >> 3u) << 11u) |
                                ((p[1] >> 2u) << 5u) | (p[2] >> 3u));
        }
    }
    stbi_image_free(rgb);
    if (atomic_load(&s.cancel)) { free(tiled); return false; }
    s.pixels = tiled;
    s.result = RES_OK;
    return true;
}
static bool load_picture(void *handle, const PanoLiveImage *image, bool quick) {
    const char *url = quick && image->preview_url[0] ?
                      image->preview_url : image->image_url;
    if (!url[0]) return false;
    s.result = RES_NETWORK;
    if (!load_pixels(handle, url, image->provider)) return false;
    s.incoming = *image;
    s.preview = quick && image->preview_url[0];
    return true;
}
static bool try_candidates(void *handle, unsigned first) {
    unsigned end = s.count < first + 3 ? s.count : first + 3;
    for (unsigned i = first; i < end && !atomic_load(&s.cancel); i++) {
        s.new_index = i;
        if (load_picture(handle, &s.candidates[i], true)) return true;
        if (s.candidates[i].preview_url[0] &&
            load_picture(handle, &s.candidates[i], false)) return true;
    }
    return false;
}
static bool lookup_panorama(void *curl, const char *id, PanoLiveImage *out) {
    char url[160];
    if (!pano_metadata_url(id, url, sizeof(url))) return false;
    u8 *data = NULL;
    u32 len = 0;
    if (!http_get(curl, url, PANO_JSON_MAX,
                  STREET_PROVIDER_PANORAMAX, &data, &len)) return false;
    bool ok = pano_parse_image((const char *)data, len, out);
    free(data);
    return ok;
}
static void worker(void *userdata) {
    (void)userdata;
    s.result = RES_NETWORK;
    void *curl = network_alloc_tile_curl();
    if (!curl) goto done;

    if (s.job == JOB_SEARCH) {
        u64 search_started = osGetTime();
        StreetProvider chosen = pano_runtime_provider();
        StreetProvider order[3];
        unsigned providers = 0;
        if (chosen != STREET_PROVIDER_AUTO) order[providers++] = chosen;
        else {
            /* Mapillary generally has stronger US coverage, but requires
             * a user-supplied token. Panoramax and KartaView need no key. */
            if (s.token[0]) order[providers++] = STREET_PROVIDER_MAPILLARY;
            order[providers++] = STREET_PROVIDER_PANORAMAX;
            order[providers++] = STREET_PROVIDER_KARTAVIEW;
        }
        bool had_connection = false;
        for (unsigned pi = 0; pi < providers && !atomic_load(&s.cancel) &&
             osGetTime() - search_started < 13000; pi++) {
            StreetProvider p = order[pi];
            if (p == STREET_PROVIDER_MAPILLARY && !s.token[0]) continue;
            /* Fast first search: one small bbox; try a second only when empty. */
            static const double radii[] = { .003, .015 };
            for (unsigned ri = 0; ri < 2 && !atomic_load(&s.cancel) &&
                 osGetTime() - search_started < 13000; ri++) {
                char url[1200];
                if (!street_source_url(p, s.latitude, s.longitude, radii[ri],
                                       s.token, url, sizeof(url))) break;
                u8 *json = NULL;
                u32 len = 0;
                if (!http_get(curl, url, PANO_JSON_MAX, p, &json, &len))
                    break;
                had_connection = true;
                s.count = street_parse_candidates(p, (const char *)json, len,
                            s.latitude, s.longitude, s.candidates, PANO_NEARBY_MAX);
                free(json);
                if (s.count) {
                    if (try_candidates(curl, 0)) goto finished;
                    /* If first few bad images, try next provider rather than
                     * spending minutes decoding broken files. */
                    break;
                }
            }
        }
        if (s.result != RES_OK)
            s.result = had_connection ? RES_EMPTY : RES_NETWORK;
    } else if (s.job == JOB_STEP) {
        PanoLiveImage target;
        bool found = false;
        if (s.selected.provider == STREET_PROVIDER_PANORAMAX) {
            const char *id = s.direction > 0 ?
                            s.selected.next_id : s.selected.prev_id;
            if (*id && !atomic_load(&s.cancel))
                found = lookup_panorama(curl, id, &target);
        }
        if (!found && s.count > 1) {
            int idx = (int)s.index + (s.direction > 0 ? 1 : -1);
            if (idx >= 0 && idx < (int)s.count) {
                s.new_index = (unsigned)idx;
                target = s.candidates[idx];
                found = true;
            }
        }
        if (!found) s.result = RES_EMPTY;
        else if (!load_picture(curl, &target, true) &&
                 target.preview_url[0])
            load_picture(curl, &target, false);
    } else if (s.job == JOB_UPGRADE) {
        s.new_index = s.index;
        load_picture(curl, &s.selected, false);
        s.preview = false;
    }
finished:
    network_free_tile_curl(curl);
done:
    atomic_store(&s.done, true);
}
static bool launch(Job job) {
    if (s.thread) return false;
    s.job = job;
    s.pixels = NULL;
    s.result = RES_NETWORK;
    atomic_store(&s.cancel, false);
    atomic_store(&s.done, false);
    s.thread = threadCreate(worker, NULL, 96 * 1024, 0x31, -2, false);
    return s.thread != NULL;
}
bool pano_runtime_start(double lat, double lon) {
    if (s.thread) return false;
    read_prefs();
    if (!is_connected()) {
        snprintf(s.status, sizeof(s.status), "Offline; showing demo panoramas");
        return false;
    }
    read_token();
    if (prefs.provider == STREET_PROVIDER_MAPILLARY && !s.token[0]) {
        snprintf(s.status, sizeof(s.status), "Add mapillary-token.txt to SD");
        return false;
    }
    s.latitude = lat;
    s.longitude = lon;
    if (!s.has_live) s.count = 0;
    s.upgrade_pending = false;
    snprintf(s.status, sizeof(s.status), "Finding %s panoramas...",
             street_provider_name(prefs.provider));
    if (launch(JOB_SEARCH)) return true;
    snprintf(s.status, sizeof(s.status), "Could not start image search");
    return false;
}
bool pano_runtime_step(int direction) {
    if (!direction || !s.has_live) return false;
    if (s.thread) {
        if (s.job == JOB_UPGRADE) {
            /* Navigation wins over optional quality improvements. */
            s.queued_step = direction;
            atomic_store(&s.cancel, true);
            return true;
        }
        return false;
    }
    s.direction = direction;
    s.upgrade_pending = false;
    snprintf(s.status, sizeof(s.status), "Loading nearby panorama...");
    return launch(JOB_STEP);
}
void pano_runtime_poll(void) {
    if (s.thread && atomic_load(&s.done)) {
        threadJoin(s.thread, U64_MAX);
        threadFree(s.thread);
        s.thread = NULL;
        Job completed = s.job;
        bool loaded = s.result == RES_OK && s.pixels &&
                      explore_load_street_pixels(s.pixels);
        if (loaded) {
            s.selected = s.incoming;
            s.index = s.new_index;
            s.has_live = true;
            s.upgrade_pending = s.preview;
            snprintf(s.status, sizeof(s.status), "%s 360  %.4f %.4f%s",
                     street_provider_name(s.selected.provider),
                     s.selected.lat, s.selected.lon,
                     s.preview ? " preview" : "");
            if (completed == JOB_SEARCH) {
                /* Available map markers from the latest successful query.
                 * This is nearby 360° imagery, not comprehensive road coverage. */
                prefs.count = s.count;
                memcpy(prefs.items, s.candidates,
                       s.count * sizeof(s.candidates[0]));
            }
        } else if (completed == JOB_SEARCH) {
            snprintf(s.status, sizeof(s.status),
                     s.result == RES_EMPTY ? "No 360 images nearby" :
                     s.result == RES_MEMORY ? "Low memory; demo remains" :
                     "Imagery unavailable; demo remains");
        } else if (completed == JOB_STEP) {
            snprintf(s.status, sizeof(s.status), "No next panorama available");
        } /* Failure to upgrade doesn't replace the good preview. */
        free(s.pixels);
        s.pixels = NULL;
        s.job = JOB_NONE;
    }
    if (!s.thread && s.queued_step) {
        int direction = s.queued_step;
        s.queued_step = 0;
        pano_runtime_step(direction);
    } else if (!s.thread && s.upgrade_pending && s.has_live) {
        s.upgrade_pending = false;
        launch(JOB_UPGRADE);
    }
}
void pano_runtime_stop(void) {
    if (s.thread) {
        atomic_store(&s.cancel, true);
        threadJoin(s.thread, U64_MAX);
        threadFree(s.thread);
        s.thread = NULL;
    }
    free(s.pixels);
    s.pixels = NULL;
    s.has_live = false;
    s.count = 0;
    s.job = JOB_NONE;
    s.queued_step = 0;
    s.upgrade_pending = false;
    s.status[0] = 0;
    memset(s.token, 0, sizeof(s.token));
}
bool pano_runtime_busy(void) { return s.thread != NULL; }
bool pano_runtime_has_live(void) { return s.has_live; }
const char *pano_runtime_status(void) { return s.status; }
const PanoLiveImage *pano_runtime_current(void) {
    return s.has_live ? &s.selected : NULL;
}
