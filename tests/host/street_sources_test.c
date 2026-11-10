#include "street_sources.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MAPI "123456789012345678"
int main(void) {
    char url[1500];
    assert(street_mapillary_token_valid("MLY|1234|abcd"));
    assert(!street_mapillary_token_valid("SECRET\nINJECTION"));
    assert(street_source_url(STREET_PROVIDER_MAPILLARY,40.7,-74,.003,
                             "MLY|1234|abcd",url,sizeof(url)));
    assert(strstr(url,"access_token=MLY%7C1234%7Cabcd"));
    assert(!street_source_url(STREET_PROVIDER_MAPILLARY,40.7,-74,.003,
                             "bad\n",url,sizeof(url)));
    assert(street_source_url(STREET_PROVIDER_PANORAMAX,40.7,-74,.003,
                             NULL,url,sizeof(url)));
    assert(strstr(url,"limit=24"));
    assert(street_source_url(STREET_PROVIDER_KARTAVIEW,40.7,-74,.003,
                             NULL,url,sizeof(url)));
    assert(strstr(url,"nearby-photos/"));
    PanoLiveImage images[PANO_NEARBY_MAX];
    const char *map=
        "{\"data\":["
        "{\"id\":\"" MAPI "\",\"camera_type\":\"spherical\","
        "\"computed_geometry\":{\"coordinates\":[-74.0002,40.7002]},"
        "\"thumb_1024_url\":\"https://image.example/photo.jpg\"},"
        "{\"id\":\"9\",\"camera_type\":\"perspective\","
        "\"geometry\":{\"coordinates\":[-74,40.7]},"
        "\"thumb_1024_url\":\"https://image.example/flat.jpg\"}]}";
    unsigned n=street_parse_candidates(STREET_PROVIDER_MAPILLARY,map,strlen(map),
                                       40.7,-74,images,PANO_NEARBY_MAX);
    assert(n==2);
    assert(images[0].is_pano);
    assert(!images[1].is_pano);
    const char *kv=
        "{\"status\":{\"httpCode\":200},\"currentPageItems\":["
        "{\"id\":13,\"lat\":\"40.7\",\"lng\":\"-74\","
        "\"lth_name\":\"storage8/files/photo/test.jpg\"}]}";
    n=street_parse_candidates(STREET_PROVIDER_KARTAVIEW,kv,strlen(kv),
                               40.7,-74,images,PANO_NEARBY_MAX);
    assert(n==1 && !images[0].is_pano);
    assert(strstr(images[0].image_url,"cdn.kartaview.org/pr:sharp/"));
    assert(street_parse_candidates(STREET_PROVIDER_MAPILLARY,"{",1,0,0,
                                   images,PANO_NEARBY_MAX)==0);
    puts("PASS provider URL escaping, Panoramax limits, Mapillary 360, KartaView flat/CDN");
    return 0;
}
