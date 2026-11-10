#include "panorama_live.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define ID_A "00000000-1111-2222-3333-444444444444"
#define ID_B "55555555-1111-2222-3333-444444444444"
static void test_urls(void) {
    char url[256];
    assert(pano_search_url(48.85, 2.29, .002, url, sizeof(url)));
    assert(strstr(url, "bbox=2.288000,48.848000,2.292000,48.852000"));
    assert(!pano_search_url(NAN, 2.29, .002, url, sizeof(url)));
    assert(!pano_search_url(48.85, 2.29, .002, url, 5));
    assert(pano_image_url(ID_A, url, sizeof(url)));
    assert(strstr(url, "/sd.jpg"));
    assert(pano_metadata_url(ID_A, url, sizeof(url)));
    assert(strstr(url, "/pictures/" ID_A));
    assert(!pano_image_url("invalid.jpg", url, sizeof(url)));
}
static void test_search(void) {
    const char *entry =
        "{\"id\":\"" ID_A "\",\"geometry\":{\"coordinates\":[2.2902,48.8502]},"
        "\"properties\":{\"pers:interior_orientation\":{\"field_of_view\":360},"
        "\"license\":\"CC-BY-SA-4.0\"},"
        "\"links\":[{\"rel\":\"next\",\"id\":\"" ID_B "\"}]}";
    PanoLiveImage item = {0};
    assert(pano_parse_image(entry, strlen(entry), &item));
    assert(!strcmp(item.id, ID_A));
    assert(!strcmp(item.next_id, ID_B));
    assert(!strcmp(item.license, "CC-BY-SA-4.0"));
    assert(!pano_parse_image("{", 1, &item));
    const char *collection =
        "{\"features\":["
        "{\"id\":\"" ID_A "\",\"geometry\":{\"coordinates\":[2.2902,48.8502]},"
        "\"properties\":{\"pers:interior_orientation\":{\"field_of_view\":360}}},"
        "{\"id\":\"" ID_B "\",\"geometry\":{\"coordinates\":[2.29,48.85]},"
        "\"properties\":{\"pers:interior_orientation\":{\"field_of_view\":360}}},"
        "{\"id\":\"" ID_A "\",\"geometry\":{\"coordinates\":[2.29,48.85]},"
        "\"properties\":{\"pers:interior_orientation\":{\"field_of_view\":120}}}]}";
    PanoLiveImage items[PANO_NEARBY_MAX];
    unsigned n = pano_parse_nearby(collection, strlen(collection),
                                    48.85, 2.29, items, PANO_NEARBY_MAX);
    assert(n == 2);
    assert(!strcmp(items[0].id, ID_B));
    assert(!strcmp(items[1].id, ID_A));
    assert(pano_parse_nearby("invalid", 7, 48.85, 2.29, items, 16) == 0);
    assert(pano_parse_nearby(collection, strlen(collection), 48.85, 2.29,
                             items, 1) == 1);
}
static void test_malformed_inputs(void) {
    PanoLiveImage items[PANO_NEARBY_MAX];
    unsigned seed = 0x5d1e11u;
    char garbage[512];
    for (int k = 0; k < 2000; k++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        unsigned len = seed % sizeof(garbage);
        for (unsigned i = 0; i < len; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            garbage[i] = (char)(1 + (seed % 126));
        }
        assert(pano_parse_nearby(garbage, len, 0, 0,
                                 items, PANO_NEARBY_MAX) <= PANO_NEARBY_MAX);
    }
}

static void test_gyro(void) {
    float pitch = 0, yaw = 0;
    pano_apply_gyro(100, 200, .1f, .05f, true, &pitch, &yaw);
    assert(pitch < 0 && yaw < 0);
    float prev = yaw;
    pano_apply_gyro(0, 0, .1f, .05f, true, &pitch, &yaw);
    assert(yaw == prev);
    pano_apply_gyro(100, 200, .1f, .05f, false, &pitch, &yaw);
    assert(fabsf(yaw) < .00001f && fabsf(pitch) < .00001f);
    pano_apply_gyro(100, 200, .1f, 100.0f, true, &pitch, &yaw);
    assert(yaw == 0);
}
int main(void) {
    test_urls(); test_search(); test_malformed_inputs(); test_gyro();
    puts("Street View: URL, STAC parsing, 360 filter, nearest sort, sequence and gyro passed");
    return 0;
}
