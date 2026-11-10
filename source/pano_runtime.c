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
#include <sys/stat.h>

#define PANO_PIXELS (1024u * 512u)
#define PANO_JPEG_MAX (4u * 1024u * 1024u)
#define PANO_JSON_MAX (512u * 1024u)
#define PANO_PREF_PATH "sdmc:/3ds_google_maps/street_provider.cfg"
#define PANO_COVER_PATH "sdmc:/3ds_google_maps/street_coverage.cfg"
#define PANO_TOKEN_PATH "sdmc:/3ds_google_maps/mapillary.token"

typedef enum { JOB_NONE, JOB_SEARCH, JOB_NEXT, JOB_UPGRADE } Job;
typedef enum { OK, NO_COVERAGE, NETWORK_ERROR, INVALID_IMAGE,
               OUT_OF_MEMORY, TOKEN_MISSING } Result;

static struct {
    Thread thread;
    atomic_bool cancelled, done;
    Job job;
    Result outcome;
    bool initialized, live, has_preview, upgrade_pending;
    bool coverage_enabled;
    StreetProvider pref;
    StreetProvider used;
    double lat, lon;
    int direction, new_index;
    unsigned count, index, coverage_count;
    PanoLiveImage candidates[PANO_NEARBY_MAX];
    PanoLiveImage coverage[PANO_NEARBY_MAX];
    PanoLiveImage selected, next_selected;
    u16 *pending;
    char status[124];
} state;

static void initialize(void) {
    if (state.initialized) return;
    state.initialized = true;
    state.pref = STREET_PROVIDER_AUTO;
    FILE *f = fopen(PANO_PREF_PATH, "r");
    if (f) {
        int n = -1;
        if (fscanf(f, "%d", &n)==1 && n>=0 && n<STREET_PROVIDER_COUNT)
            state.pref = (StreetProvider)n;
        fclose(f);
    }
    f = fopen(PANO_COVER_PATH, "r");
    if (f) {
        int n=0;
        if (fscanf(f,"%d",&n)==1) state.coverage_enabled = !!n;
        fclose(f);
    }
}
static void write_option(const char *path, int n) {
    mkdir("sdmc:/3ds_google_maps", 0777);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", n); fclose(f); }
}
void pano_runtime_set_provider(StreetProvider provider) {
    initialize();
    if (provider>=STREET_PROVIDER_AUTO && provider<STREET_PROVIDER_COUNT) {
        state.pref = provider;
        write_option(PANO_PREF_PATH, provider);
    }
}
StreetProvider pano_runtime_provider(void) {
    initialize(); return state.pref;
}
void pano_runtime_set_coverage(bool enabled) {
    initialize();
    state.coverage_enabled = enabled;
    write_option(PANO_COVER_PATH, enabled ? 1 : 0);
}
bool pano_runtime_coverage_enabled(void) {
    initialize(); return state.coverage_enabled;
}
unsigned pano_runtime_coverage(PanoLiveImage *items, unsigned cap) {
    if (!items || !cap) return 0;
    if (cap>state.coverage_count) cap=state.coverage_count;
    memcpy(items, state.coverage, cap*sizeof(*items));
    return cap;
}
unsigned pano_runtime_coverage_count(void) { return state.coverage_count; }
const PanoLiveImage *pano_runtime_coverage_at(unsigned i) {
    return i<state.coverage_count ? &state.coverage[i] : NULL;
}
static bool mapillary_token(char out[256]) {
    FILE *f = fopen(PANO_TOKEN_PATH, "r");
    if (!f) return false;
    bool ok = fgets(out,256,f) != NULL;
    fclose(f);
    if (!ok) return false;
    out[strcspn(out,"\r\n")]=0;
    return street_mapillary_token_valid(out);
}

static bool url_safe(const char *url) {
    if (!url || strncmp(url, "https://",8) || strlen(url)>1000) return false;
    for (const char *p=url+8;*p;p++)
        if ((unsigned char)*p<=32 || (unsigned char)*p>126 ||
            *p=='#' || *p=='@' || *p=='\\') return false;
    return true;
}

static bool get(void *handle, const char *url, size_t max, u8 **bytes, u32 *size) {
    if (!url_safe(url)) return false;
    return network_request(handle, url, max, &state.cancelled, bytes, size);
}

static bool query_source(void *handle, StreetProvider provider,
                         double radius, const char *token, unsigned *count) {
    char url[1400];
    if (!street_source_url(provider,state.lat,state.lon,radius,token,url,sizeof(url)))
        return false;
    CURL *curl=handle;
    if (provider==STREET_PROVIDER_MAPILLARY) {
        /* A private token must not be sent over an unauthenticated TLS
         * session, even though older tile code disables verification. */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    if (provider==STREET_PROVIDER_KARTAVIEW)
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 9L);
    u8 *response=NULL;
    u32 length=0;
    bool ok=false;
    if (provider==STREET_PROVIDER_KARTAVIEW) {
        /* KartaView's maintained 1.0 endpoint uses POST form parameters.
         * Never query its notoriously slow whole-city bbox endpoint. */
        char form[160];
        unsigned meters=(unsigned)fmax(45,fmin(140,radius*111000.0));
        snprintf(form,sizeof(form),
                 "ipp=32&page=1&coordinate=%.6f,%.6f&radius=%u",
                 state.lat,state.lon,meters);
        struct curl_slist *headers=NULL;
        headers=curl_slist_append(headers,
                         "Content-Type: application/x-www-form-urlencoded");
        curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,form);
        curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,(long)strlen(form));
        ok=get(handle,url,PANO_JSON_MAX,&response,&length);
        curl_easy_setopt(curl,CURLOPT_HTTPGET,1L);
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,NULL);
        curl_easy_setopt(curl,CURLOPT_HTTPHEADER,NULL);
        curl_slist_free_all(headers);
    } else {
        ok=get(handle,url,PANO_JSON_MAX,&response,&length);
    }
    if (!ok) return false;
    *count=street_parse_candidates(provider,(const char*)response,length,
                                state.lat,state.lon,state.candidates,PANO_NEARBY_MAX);
    free(response);
    return true;
}

static bool decode_candidate(void *handle, const PanoLiveImage *p, bool preview) {
    const char *url=(preview && p->preview_url[0]) ?
                         p->preview_url : p->image_url;
    if (!url_safe(url)) return false;
    u8 *jpeg=NULL;
    u32 bytes=0;
    if (!get(handle,url,PANO_JPEG_MAX,&jpeg,&bytes)) return false;
    int width=0,height=0,comp=0;
    bool valid=stbi_info_from_memory(jpeg,(int)bytes,&width,&height,&comp) &&
               width>=240 && height>=120 &&
               width<=2048 && height<=1024;
    if (valid && p->is_pano) {
        double ratio=width/(double)height;
        if (ratio<1.92 || ratio>2.08) valid=false;
    }
    if (!valid) {
        free(jpeg);state.outcome=INVALID_IMAGE;return false;
    }
    unsigned char *rgb=stbi_load_from_memory(jpeg,(int)bytes,
                                             &width,&height,&comp,3);
    free(jpeg);
    if (!rgb) {state.outcome=INVALID_IMAGE;return false;}
    u16 *tiled=malloc(PANO_PIXELS*sizeof(*tiled));
    if (!tiled) {
        stbi_image_free(rgb);state.outcome=OUT_OF_MEMORY;return false;
    }
    unsigned fit_width=1024,fit_height=512;
    if (!p->is_pano) {
        if ((unsigned long long)width*512 > (unsigned long long)height*1024)
            fit_height=(unsigned)((unsigned long long)height*1024/width);
        else fit_width=(unsigned)((unsigned long long)width*512/height);
        if (!fit_width) fit_width=1;
        if (!fit_height) fit_height=1;
    }
    unsigned pad_x=(1024-fit_width)/2, pad_y=(512-fit_height)/2;
    for (unsigned y=0;y<512;y++) {
        bool row=y>=pad_y && y-pad_y<fit_height;
        unsigned sy=row ? (unsigned)(height-1) -
                          (y-pad_y)*(unsigned)height/fit_height : 0;
        for (unsigned x=0;x<1024;x++) {
            u16 pixel=0;
            if (row && x>=pad_x && x-pad_x<fit_width) {
                unsigned sx=(x-pad_x)*(unsigned)width/fit_width;
                const unsigned char *c=rgb+(sy*(unsigned)width+sx)*3;
                pixel=(u16)(((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3));
            }
            unsigned m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|
                       ((x&4)<<2)|((y&4)<<3);
            tiled[((y>>3)*128+(x>>3))*64+m]=pixel;
        }
    }
    stbi_image_free(rgb);
    state.pending=tiled;
    state.next_selected=*p;
    state.has_preview=preview && p->preview_url[0] &&
                        strcmp(p->preview_url,p->image_url);
    state.upgrade_pending=state.has_preview;
    state.outcome=OK;
    return true;
}

static bool lookup_next(void *handle, const char *id, PanoLiveImage *out) {
    char url[180];
    if (!pano_metadata_url(id,url,sizeof(url)))return false;
    u8 *data=NULL;u32 size=0;
    if (!get(handle,url,PANO_JSON_MAX,&data,&size))return false;
    bool ok=pano_parse_image((const char*)data,size,out);
    free(data);return ok;
}

static bool search_provider(void *curl, StreetProvider src,
                            const char *token) {
    static const double radii[]={.0025,.012};
    for (unsigned i=0;i<2&&!atomic_load(&state.cancelled);i++) {
        unsigned count=0;
        if (!query_source(curl,src,radii[i],token,&count))return false;
        if (count) {
            state.count=count;
            state.used=src;
            for (unsigned k=0;k<count&&k<3&&!atomic_load(&state.cancelled);k++) {
                state.new_index=(int)k;
                if (decode_candidate(curl,&state.candidates[k],
                                      src==STREET_PROVIDER_PANORAMAX))
                    return true;
                if (state.pending) { free(state.pending);state.pending=NULL; }
            }
            return false;
        }
    }
    return false;
}

static void worker(void *unused) {
    (void)unused;
    state.outcome=NETWORK_ERROR;
    void *curl=network_alloc_tile_curl();
    if (!curl) goto end;
    if (state.job==JOB_SEARCH) {
        char token[256]={0};
        bool has_token=mapillary_token(token);
        StreetProvider selected=state.pref;
        StreetProvider order[2];
        unsigned count=0;
        if (selected==STREET_PROVIDER_AUTO) {
            if (has_token)order[count++]=STREET_PROVIDER_MAPILLARY;
            order[count++]=STREET_PROVIDER_PANORAMAX;
        } else order[count++]=selected;
        if (selected==STREET_PROVIDER_MAPILLARY&&!has_token) {
            state.outcome=TOKEN_MISSING;
        } else {
            state.outcome=NO_COVERAGE;
            for (unsigned i=0;i<count&&!atomic_load(&state.cancelled);i++) {
                StreetProvider p=order[i];
                if (search_provider(curl,p,token))break;
            }
        }
        memset(token,0,sizeof(token));
    } else if (state.job==JOB_NEXT) {
        PanoLiveImage p={0};
        bool found=false;
        if (state.selected.provider==STREET_PROVIDER_PANORAMAX) {
            const char *id=state.direction>0?state.selected.next_id:
                                                state.selected.prev_id;
            if (*id && !atomic_load(&state.cancelled))
                found=lookup_next(curl,id,&p);
        }
        if (!found && state.count>1) {
            int next=(int)state.index+(state.direction>0?1:-1);
            if (next>=0 && next<(int)state.count) {
                p=state.candidates[next];state.new_index=next;found=true;
            }
        }
        state.outcome=NO_COVERAGE;
        if (found && !atomic_load(&state.cancelled))
            decode_candidate(curl,&p,p.provider==STREET_PROVIDER_PANORAMAX);
    } else if (state.job==JOB_UPGRADE) {
        state.outcome=NETWORK_ERROR;
        decode_candidate(curl,&state.selected,false);
        state.upgrade_pending=false;
    }
    network_free_tile_curl(curl);
end:
    atomic_store(&state.done,true);
}
static bool launch(Job job) {
    if (state.thread) return false;
    state.job=job;
    state.pending=NULL;
    state.outcome=NETWORK_ERROR;
    atomic_store(&state.cancelled,false);
    atomic_store(&state.done,false);
    state.thread=threadCreate(worker,NULL,128*1024,0x31,-2,false);
    return state.thread!=NULL;
}
static void cancel_upgrade(void) {
    if (state.thread && state.job==JOB_UPGRADE) {
        atomic_store(&state.cancelled,true);
        threadJoin(state.thread,U64_MAX);
        threadFree(state.thread);
        state.thread=NULL;
        free(state.pending);state.pending=NULL;
        state.upgrade_pending=false;
    }
}

bool pano_runtime_start(double lat,double lon) {
    initialize();
    if (state.thread) cancel_upgrade();
    if (state.thread) return false;
    if (!is_connected()) {
        snprintf(state.status,sizeof(state.status),"Connect Wi-Fi for live street photos");
        return false;
    }
    state.lat=lat;state.lon=lon;
    snprintf(state.status,sizeof(state.status),"Searching near map center...");
    if (launch(JOB_SEARCH))return true;
    snprintf(state.status,sizeof(state.status),"Could not start street image search");
    return false;
}
bool pano_runtime_step(int direction) {
    if (state.thread)cancel_upgrade();
    if (state.thread || !state.live || !direction)return false;
    state.direction=direction;state.new_index=(int)state.index;
    snprintf(state.status,sizeof(state.status),"Loading nearby street photo...");
    return launch(JOB_NEXT);
}
void pano_runtime_poll(void) {
    if (!state.thread || !atomic_load(&state.done))return;
    Job job=state.job;
    threadJoin(state.thread,U64_MAX);
    threadFree(state.thread);state.thread=NULL;
    if (state.outcome==OK && state.pending &&
        explore_load_street_pixels(state.pending,state.next_selected.is_pano)) {
        state.selected=state.next_selected;
        state.index=(unsigned)state.new_index;
        state.live=true;
        if (job==JOB_SEARCH) {
            state.coverage_count=state.count;
            memcpy(state.coverage,state.candidates,
                   state.coverage_count*sizeof(*state.coverage));
        }
        snprintf(state.status,sizeof(state.status),"%s %s  %.4f,%.4f",
            street_provider_name(state.selected.provider),
            state.selected.is_pano?"360":"photo",
            state.selected.lat,state.selected.lon);
    } else if (state.outcome==TOKEN_MISSING)
        snprintf(state.status,sizeof(state.status),"Mapillary: configure mapillary.token");
    else if (state.outcome==NO_COVERAGE)
        snprintf(state.status,sizeof(state.status),"No imagery nearby; try other source");
    else if (state.outcome==INVALID_IMAGE)
        snprintf(state.status,sizeof(state.status),"Photo format unsupported on Old 3DS");
    else if (state.outcome==OUT_OF_MEMORY)
        snprintf(state.status,sizeof(state.status),"Not enough RAM for street photo");
    else
        snprintf(state.status,sizeof(state.status),"Network error; offline photos available");
    free(state.pending);state.pending=NULL;
    state.job=JOB_NONE;
    if (state.live && state.upgrade_pending && job!=JOB_UPGRADE) {
        state.upgrade_pending=false;
        launch(JOB_UPGRADE);
    }
}
void pano_runtime_stop(void) {
    if (state.thread) {
        atomic_store(&state.cancelled,true);
        threadJoin(state.thread,U64_MAX);
        threadFree(state.thread);
        state.thread=NULL;
    }
    free(state.pending);state.pending=NULL;
    state.live=false;state.count=0;state.job=JOB_NONE;
    state.upgrade_pending=false;
    state.status[0]=0;
    /* Keep coverage markers until the next successful search. */
}
bool pano_runtime_busy(void) {return state.thread!=NULL && state.job!=JOB_UPGRADE;}
bool pano_runtime_has_live(void) {return state.live;}
const char *pano_runtime_status(void) {return state.status;}
const PanoLiveImage *pano_runtime_current(void) {return state.live?&state.selected:NULL;}
