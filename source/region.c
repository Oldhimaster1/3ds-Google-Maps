#include "region.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

RegionProgressMetrics region_progress_metrics(unsigned initial_saved, unsigned saved,
                                               unsigned total, uint64_t elapsed_ms) {
    RegionProgressMetrics metrics = {0};
    if (saved > total) saved = total;
    if (initial_saved > saved) initial_saved = saved;
    unsigned session_saved = saved - initial_saved;
    unsigned remaining = total - saved;
    if (session_saved && elapsed_ms) {
        metrics.tiles_per_second = session_saved * 1000.0 / elapsed_ms;
        metrics.eta_known = true;
        metrics.eta_seconds = remaining ?
            (uint64_t)ceil(remaining / metrics.tiles_per_second) : 0;
    } else if (!remaining) {
        metrics.eta_known = true;
    }
    return metrics;
}

unsigned region_retry_delay_ms(unsigned attempt, long http_status,
                               unsigned retry_after_ms, bool invalid_payload) {
    if (attempt >= 2) return 0;
    if (http_status == 429 || http_status == 503) {
        if (retry_after_ms) return retry_after_ms;
        return 500u << attempt;
    }
    if (invalid_payload || http_status == 0 || http_status >= 500)
        return 250u << attempt;
    return 0;
}

static int compare_tiles(const void *left, const void *right);

static bool same_tile(const TilePackEntry *a, const TilePackEntry *b) {
    return a->zoom == b->zoom && a->x == b->x && a->y == b->y;
}

void region_close(RegionJob *job) {
    if (job->file) fclose(job->file);
    free(job->entries);
    memset(job, 0, sizeof(*job));
}

bool region_open(RegionJob *job, const char *path, const TilePackEntry *plan, unsigned count) {
    memset(job, 0, sizeof(*job));
    if (!plan) {
        FILE *probe = fopen(path, "rb");
        if (!probe) return false;
        uint32_t header[4];
        bool ok = fread(header, sizeof(header), 1, probe) == 1 &&
                  !memcmp(header, "3DTP", 4) && header[1] == 1;
        fclose(probe);
        if (!ok) return false;
        count = header[2];
    }
    if (!count || count > REGION_LIMIT) return false;
    job->entries = malloc(count * sizeof(*plan));
    if (!job->entries) return false;
    job->count = count;
    job->data_offset = 16 + count * sizeof(*plan);
    uint32_t header[4] = {0, 1, count, job->data_offset};
    memcpy(header, "3DTP", 4);

    FILE *f = job->file = fopen(path, "rb+");
    if (f) {
        uint32_t stored[4];
        if (fread(stored, sizeof(stored), 1, f) != 1 || memcmp(stored, header, sizeof(header)) ||
            fread(job->entries, sizeof(*plan), count, f) != count ||
            fseek(f, 0, SEEK_END) != 0) goto fail;
        long end = ftell(f);
        if (end < (long)job->data_offset) goto fail;
        for (unsigned i = 0; i < count; i++) {
            TilePackEntry *e = &job->entries[i];
            if ((plan && !same_tile(e, &plan[i])) || e->zoom < 1 || e->zoom > 18 ||
                e->x >= (1u << e->zoom) || e->y >= (1u << e->zoom)) goto fail;
            if (i && compare_tiles(&job->entries[i - 1], e) >= 0) goto fail;
            if (e->size > 1024 * 1024 ||
                (uint64_t)job->data_offset + e->offset + e->size > (uint64_t)end)
                e->size = 0;
        }
        return true;
    }

    if (!plan) goto fail;
    f = job->file = fopen(path, "wb+");
    if (!f) goto fail;
    memcpy(job->entries, plan, count * sizeof(*plan));
    if (fwrite(header, sizeof(header), 1, f) != 1 ||
        fwrite(plan, sizeof(*plan), count, f) != count || fflush(f) != 0) {
        region_close(job);
        unlink(path);
        return false;
    }
    return true;
fail:
    region_close(job);
    return false;
}

bool region_append(RegionJob *job, unsigned index, const void *bytes, unsigned size) {
    if (!job->file || index >= job->count || !size || size > 1024 * 1024) return false;
    FILE *f = job->file;
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long pos = ftell(f);
    if (pos < (long)job->data_offset || (uint64_t)pos + size > INT32_MAX) return false;
    if (fwrite(bytes, 1, size, f) != size || fflush(f) != 0) return false;
    TilePackEntry entry = job->entries[index];
    entry.offset = (uint32_t)pos - job->data_offset;
    entry.size = size;
    if (fseek(f, 16 + index * sizeof(entry), SEEK_SET) != 0 ||
        fwrite(&entry, sizeof(entry), 1, f) != 1 || fflush(f) != 0) return false;
    job->entries[index] = entry;
    return true;
}

bool region_finish(RegionJob *job, const char *part, const char *final) {
    if (!job->file || access(final, F_OK) == 0) return false;
    for (unsigned i = 0; i < job->count; i++)
        if (!job->entries[i].size) return false;
    int status = fclose(job->file);
    job->file = NULL;
    return status == 0 && rename(part, final) == 0;
}

static int compare_tiles(const void *left, const void *right) {
    const TilePackEntry *a = left, *b = right;
    if (a->zoom != b->zoom) return (int)a->zoom - b->zoom;
    if (a->x != b->x) return a->x < b->x ? -1 : 1;
    return a->y < b->y ? -1 : a->y != b->y;
}

static void project(double lat, double lon, int world, double *x, double *y) {
    lat = fmax(-85.05112878, fmin(85.05112878, lat));
    *x = (lon + 180) * world / 360;
    *y = (1 - asinh(tan(lat * M_PI / 180)) / M_PI) * world / 2;
}

unsigned region_plan(TilePackEntry *tiles, unsigned capacity, int min_zoom, int max_zoom,
                     const RegionPoint *path, unsigned points,
                     int reference_zoom, int padding) {
    if (!points || min_zoom < 1 || max_zoom > 18 || min_zoom > max_zoom) return 0;
    if (!capacity || capacity > REGION_LIMIT || reference_zoom < 1 || reference_zoom > 18 ||
        padding < 0 || padding > 2048) return 0;
    for (unsigned i = 0; i < points; i++)
        if (!isfinite(path[i].lat) || fabs(path[i].lat) > 90 || !isfinite(path[i].lon) || fabs(path[i].lon) > 180) return 0;
    unsigned slots = 2;
    while (slots < capacity * 2) slots <<= 1;
    uint16_t *table = calloc(slots, sizeof(*table));
    if (!table) return 0;
    unsigned count = 0;
    for (int z = min_zoom; z <= max_zoom; z++) {
        int world = 1 << z;
        double x0, y0;
        project(path[0].lat, path[0].lon, world, &x0, &y0);
        for (unsigned i = 0; i < points; i++) {
            double x1, y1;
            project(path[i].lat, path[i].lon, world, &x1, &y1);
            if (x1 - x0 > world / 2.0) x1 -= world;
            if (x1 - x0 < -world / 2.0) x1 += world;
            double span = fmax(fabs(x1 - x0), fabs(y1 - y0));
            if (!isfinite(span) || span > capacity) goto too_many;
            int steps = (int)ceil(span * 2);
            if (!steps) steps = 1;
            for (int step = 0; step <= steps; step++) {
                double t = (double)step / steps;
                double x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
                double radius = points == 1 ? ldexp(padding / 256.0, z - reference_zoom) : 0.75;
                int left = (int)floor(x - radius), right = (int)floor(x + radius);
                if (right - left + 1 > world) right = left + world - 1;
                int top = (int)fmax(0, floor(y - radius)), bottom = (int)fmin(world - 1, floor(y + radius));
                for (int tx = left; tx <= right; tx++) for (int ty = top; ty <= bottom; ty++) {
                    TilePackEntry e = { .zoom = z, .x = (tx % world + world) % world, .y = ty };
                    unsigned hash = (e.x * 73856093u ^ e.y * 19349663u ^ e.zoom * 83492791u) & (slots - 1);
                    while (table[hash] && !same_tile(&tiles[table[hash] - 1], &e))
                        hash = (hash + 1) & (slots - 1);
                    if (table[hash]) continue;
                    if (count == capacity) goto too_many;
                    tiles[count++] = e;
                    table[hash] = count;
                }
            }
            project(path[i].lat, path[i].lon, world, &x0, &y0);
        }
    }
    free(table);
    qsort(tiles, count, sizeof(*tiles), compare_tiles);
    return count;
too_many:
    free(table);
    return capacity + 1;
}
