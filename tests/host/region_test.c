#include "region.h"

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void log_write(const char *format, ...) { (void)format; }

static TilePackEntry *find(TilePackEntry *plan, unsigned count, int z, unsigned x, unsigned y) {
    for (unsigned i = 0; i < count; i++)
        if (plan[i].zoom == z && plan[i].x == x && plan[i].y == y) return &plan[i];
    return NULL;
}

static void test_planning(void) {
    TilePackEntry plan[REGION_LIMIT];
    const RegionPoint equator = {0, 0};
    unsigned count = region_plan(plan, REGION_LIMIT, 9, 11, &equator, 1, 10, 420);
    assert(count > 12 && count < REGION_LIMIT);
    assert(find(plan, count, 10, 512, 512));
    for (unsigned i = 1; i < count; i++) {
        TilePackEntry a = plan[i - 1], b = plan[i];
        assert(a.zoom < b.zoom || (a.zoom == b.zoom &&
               (a.x < b.x || (a.x == b.x && a.y < b.y))));
    }
    const RegionPoint dateline[] = {{0, 179.9}, {0, -179.9}};
    count = region_plan(plan, REGION_LIMIT, 10, 10, dateline, 2, 10, 0);
    assert(count && count < 16);
    assert(find(plan, count, 10, 0, 512) && find(plan, count, 10, 1023, 512));
    assert(!find(plan, count, 10, 512, 512));

    const RegionPoint corridor[] = {{0, 0}, {0, 3}, {3, 3}};
    count = region_plan(plan, REGION_LIMIT, 10, 10, corridor, 3, 10, 0);
    assert(count > 20 && count < 55);
    assert(find(plan, count, 10, 520, 512) && find(plan, count, 10, 520, 503));
    assert(!find(plan, count, 10, 514, 505));

    const RegionPoint polar = {90, -180};
    count = region_plan(plan, REGION_LIMIT, 18, 18, &polar, 1, 18, 420);
    assert(count && count < 16);
    for (unsigned i = 0; i < count; i++)
        assert(plan[i].x < (1u << 18) && plan[i].y < (1u << 18));
    const RegionPoint invalid = {NAN, 0};
    assert(!region_plan(plan, REGION_LIMIT, 10, 12, &invalid, 1, 10, 420));
    assert(region_plan(plan, 2, 10, 12, &equator, 1, 10, 420) == 3);
}

static void test_resume(void) {
    char root[] = "/tmp/map-region-test-XXXXXX";
    assert(mkdtemp(root));
    char part[128], final[128];
    snprintf(part, sizeof(part), "%s/area.part", root);
    snprintf(final, sizeof(final), "%s/area.tilepack", root);
    TilePackEntry plan[] = {
        {.zoom = 2, .x = 1, .y = 0},
        {.zoom = 2, .x = 1, .y = 1},
        {.zoom = 2, .x = 1, .y = 2},
    };
    RegionJob job;
    assert(region_open(&job, part, plan, 3));
    assert(region_append(&job, 0, "one", 3));
    assert(region_append(&job, 2, "three", 5));
    assert(!region_finish(&job, part, final));
    region_close(&job);

    // A truncated last payload is scheduled again while the earlier tile survives.
    assert(truncate(part, 16 + 3 * sizeof(TilePackEntry) + 5) == 0);
    assert(region_open(&job, part, NULL, 0));
    assert(job.count == 3 && job.entries[0].size == 3);
    assert(!job.entries[1].size && !job.entries[2].size);
    assert(region_append(&job, 2, "three", 5));
    assert(region_append(&job, 1, "two", 3));
    assert(region_finish(&job, part, final));
    region_close(&job);

    assert(access(part, F_OK) != 0);
    TilePack pack;
    assert(tilepack_open(&pack, final));
    assert(pack.tile_count == 3);
    assert(!tilepack_find(&pack, 2, 2, 1));
    size_t bytes;
    unsigned char *data = tilepack_read_tile(&pack, 2, 1, 2, &bytes);
    assert(data && bytes == 5 && !memcmp(data, "three", 5));
    free(data);
    tilepack_close(&pack);
    assert(unlink(final) == 0 && rmdir(root) == 0);
}

static void test_progress_metrics(void) {
    RegionProgressMetrics metrics = region_progress_metrics(0, 20, 60, 10000);
    assert(fabs(metrics.tiles_per_second - 2.0) < 0.001);
    assert(metrics.eta_known && metrics.eta_seconds == 20);

    metrics = region_progress_metrics(60, 70, 100, 5000);
    assert(fabs(metrics.tiles_per_second - 2.0) < 0.001);
    assert(metrics.eta_known && metrics.eta_seconds == 15);

    metrics = region_progress_metrics(10, 10, 100, 5000);
    assert(!metrics.eta_known && metrics.tiles_per_second == 0);

    metrics = region_progress_metrics(100, 100, 100, 0);
    assert(metrics.eta_known && metrics.eta_seconds == 0);

    assert(region_retry_delay_ms(0, 429, 7000, false) == 7000);
    assert(region_retry_delay_ms(1, 503, 0, false) == 1000);
    assert(region_retry_delay_ms(0, 500, 0, false) == 250);
    assert(region_retry_delay_ms(0, 200, 0, true) == 250);
    assert(region_retry_delay_ms(0, 404, 0, false) == 0);
    assert(region_retry_delay_ms(2, 429, 7000, false) == 0);
}

typedef struct {
    RegionJob *job;
    atomic_uint next;
    pthread_mutex_t append_lock;
} ConcurrentAppend;

static void *append_worker(void *context) {
    ConcurrentAppend *append = context;
    for (;;) {
        unsigned i = atomic_fetch_add(&append->next, 1);
        if (i >= append->job->count) break;
        uint32_t payload = i ^ 0xa5a55a5a;
        pthread_mutex_lock(&append->append_lock);
        bool ok = region_append(append->job, i, &payload, sizeof(payload));
        pthread_mutex_unlock(&append->append_lock);
        assert(ok);
    }
    return NULL;
}

static void test_serialized_concurrent_append(void) {
    char root[] = "/tmp/map-region-concurrent-XXXXXX";
    assert(mkdtemp(root));
    char part[128], final[128];
    snprintf(part, sizeof(part), "%s/area.part", root);
    snprintf(final, sizeof(final), "%s/area.tilepack", root);
    enum { TILE_COUNT = 128 };
    TilePackEntry plan[TILE_COUNT] = {0};
    for (unsigned i = 0; i < TILE_COUNT; i++) {
        plan[i].zoom = 8;
        plan[i].x = i;
        plan[i].y = 100;
    }
    RegionJob job;
    assert(region_open(&job, part, plan, TILE_COUNT));
    ConcurrentAppend append = {.job = &job};
    atomic_init(&append.next, 0);
    assert(pthread_mutex_init(&append.append_lock, NULL) == 0);
    pthread_t threads[2];
    assert(pthread_create(&threads[0], NULL, append_worker, &append) == 0);
    assert(pthread_create(&threads[1], NULL, append_worker, &append) == 0);
    assert(pthread_join(threads[0], NULL) == 0);
    assert(pthread_join(threads[1], NULL) == 0);
    pthread_mutex_destroy(&append.append_lock);
    assert(region_finish(&job, part, final));
    region_close(&job);

    TilePack pack;
    assert(tilepack_open(&pack, final));
    for (unsigned i = 0; i < TILE_COUNT; i++) {
        size_t size = 0;
        uint32_t *payload = (uint32_t *)tilepack_read_tile(&pack, 8, i, 100, &size);
        assert(payload && size == sizeof(*payload) && *payload == (i ^ 0xa5a55a5a));
        free(payload);
    }
    tilepack_close(&pack);
    assert(unlink(final) == 0 && rmdir(root) == 0);
}

int main(void) {
    test_planning();
    test_resume();
    test_progress_metrics();
    test_serialized_concurrent_append();
    puts("Region planning, resume, progress, retry and concurrent append tests passed.");
    return 0;
}
