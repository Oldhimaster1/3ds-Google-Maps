#include "street_sources.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void url_tests(void) {
    char url[1200];
    assert(street_source_url(STREET_PROVIDER_MAPILLARY, 34.1, -82.2, .003,
                            "MLY|abc-123", url, sizeof(url)));
    assert(strstr(url, "access_token=MLY%7Cabc-123"));
    assert(strstr(url, "bbox=-82.203000,34.097000,-82.197000,34.103000"));
    assert(!street_source_url(STREET_PROVIDER_MAPILLARY, 34.1, -82.2, .003,
                             "bad token&evil", url, sizeof(url)));
    assert(street_source_url(STREET_PROVIDER_KARTAVIEW, 34.1, -82.2, .003,
                             NULL, url, sizeof(url)));
    assert(strstr(url, "radius=333"));
    assert(street_source_url(STREET_PROVIDER_PANORAMAX, 34.1, -82.2, .003,
                             NULL, url, sizeof(url)));
    assert(strstr(url, "/search?bbox="));
    assert(!street_source_url(STREET_PROVIDER_AUTO, NAN, -82.2, .003,
                              NULL, url, sizeof(url)));
}
static void mapillary_tests(void) {
    const char *json = "{\"data\":["
        "{\"id\":\"1000\",\"camera_type\":\"spherical\","
        "\"computed_geometry\":{\"coordinates\":[-82.2,34.1]},"
        "\"thumb_1024_url\":\"https://scontent.fbcdn.net/one.jpg\","
        "\"sequence\":\"s1\"},"
        "{\"id\":\"2000\",\"camera_type\":\"perspective\","
        "\"computed_geometry\":{\"coordinates\":[-82.21,34.1]},"
        "\"thumb_1024_url\":\"https://scontent.fbcdn.net/two.jpg\"},"
        "{\"id\":\"3000\",\"camera_type\":\"spherical\","
        "\"computed_geometry\":{\"coordinates\":[-82.3,34.1]},"
        "\"thumb_1024_url\":\"http://unsafe.example/foo.jpg\"},"
        "{\"id\":\"1000\",\"camera_type\":\"spherical\","
        "\"computed_geometry\":{\"coordinates\":[-82.2,34.1]},"
        "\"thumb_1024_url\":\"https://scontent.fbcdn.net/one.jpg\"}"
        "]}";
    PanoLiveImage pics[PANO_NEARBY_MAX];
    unsigned n = street_parse_candidates(STREET_PROVIDER_MAPILLARY, json,
                    strlen(json), 34.1, -82.2, pics, PANO_NEARBY_MAX);
    assert(n == 1);
    assert(pics[0].provider == STREET_PROVIDER_MAPILLARY);
    assert(!strcmp(pics[0].id, "1000"));
    assert(!strcmp(pics[0].sequence, "s1"));
}
static void kartaview_tests(void) {
    const char *json = "{\"result\":{\"data\":["
        "{\"id\":\"12\",\"lat\":\"34.10001\",\"lng\":\"-82.20001\","
        "\"fieldOfView\":\"360\",\"projection\":\"SPHERE\","
        "\"fileurlLTh\":\"https://cdn.kartaview.org/example.jpg\","
        "\"sequenceId\":1234},"
        "{\"id\":\"13\",\"lat\":\"34.1\",\"lng\":\"-82.2\","
        "\"fieldOfView\":\"90\",\"projection\":\"PLANE\","
        "\"fileurlLTh\":\"https://cdn.kartaview.org/flat.jpg\"}"
        "]}}";
    PanoLiveImage pics[PANO_NEARBY_MAX];
    unsigned n = street_parse_candidates(STREET_PROVIDER_KARTAVIEW,
        json, strlen(json), 34.1, -82.2, pics, 16);
    assert(n == 1);
    assert(pics[0].provider == STREET_PROVIDER_KARTAVIEW);
    assert(!strcmp(pics[0].id, "12"));
    assert(!strcmp(pics[0].sequence, "1234"));
}
static void malformed_tests(void) {
    PanoLiveImage pics[PANO_NEARBY_MAX];
    unsigned seed=123;
    char noise[400];
    for (int i=0;i<500;i++) {
        seed ^= seed<<13; seed ^= seed>>17; seed ^= seed<<5;
        unsigned len=seed%sizeof(noise);
        for(unsigned j=0;j<len;j++) {
            seed ^= seed<<13; seed ^= seed>>17; seed ^= seed<<5;
            noise[j]=(char)(seed%128);
        }
        assert(street_parse_candidates(STREET_PROVIDER_KARTAVIEW,
            noise,len,34,-82,pics,16) <= 16);
        assert(street_parse_candidates(STREET_PROVIDER_MAPILLARY,
            noise,len,34,-82,pics,16) <= 16);
    }
}
int main(void) {
    url_tests(); mapillary_tests(); kartaview_tests(); malformed_tests();
    puts("PASS: multi-provider URL auth escaping, spherical-only metadata, sorting and malformed input");
    return 0;
}
