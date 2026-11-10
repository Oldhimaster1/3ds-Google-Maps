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
    assert(strstr(url,"limit=80"));
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
    const char *google = ")]}'\n[null,[null,["
        "[[[2,\"abcdefghijklmnopqrstuv\"],null,[[null,null,40.7,-74],null,[0]]],[1,99]],"
        "[[[2,\"abcdefghijklmnopqrstuw\"],null,[[null,null,40.701,-74],null,[0]]],[0]]"
        "]]]";
    n=street_parse_candidates(STREET_PROVIDER_GOOGLE,google,strlen(google),
                               40.7,-74,images,PANO_NEARBY_MAX);
    assert(n==2 && images[0].is_pano);
    assert(!strcmp(images[0].next_id,images[1].id));
    assert(!strcmp(images[1].prev_id,images[0].id));
    assert(strstr(images[0].image_url,"zoom=0"));
    assert(street_google_find_id(google,strlen(google),images[1].id,&images[0]));
    assert(!strcmp(images[0].id,images[1].id));
    const char *bing = "[{\"elapsed\":1},"
        "{\"id\":1340983819,\"la\":40.758,\"lo\":-73.986,"
        "\"pr\":10,\"he\":300,\"nbn\":[{\"id\":42,\"az\":302}]}]";
    n=street_parse_candidates(STREET_PROVIDER_BING,bing,strlen(bing),
                               40.7,-74,images,PANO_NEARBY_MAX);
    assert(n==1 && images[0].is_pano && !strcmp(images[0].prev_id,"10"));
    assert(!strcmp(images[0].next_id,"42"));
    assert(street_bing_face_url(images[0].id,5,url,sizeof(url)));
    assert(strstr(url,"hs103332313022002312.jpg"));
    assert(!street_bing_face_url("12&x=1",5,url,sizeof(url)));
    assert(!street_bing_face_url("4294967296",5,url,sizeof(url)));
    puts("PASS provider URL escaping, Panoramax limits, Mapillary 360, KartaView flat/CDN, Google links, Bing metadata and face keys");
    return 0;
}
