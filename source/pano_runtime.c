#include "pano_runtime.h"
#include "street_sources.h"
#include "explore.h"
#include "network.h"
#include "logging.h"
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
               OUT_OF_MEMORY, TOKEN_MISSING, ACCESS_DENIED, RATE_LIMITED } PanoResult;

static struct {
    Thread thread;
    atomic_bool cancelled, done;
    Job job;
    PanoResult outcome;
    bool initialized, live, has_preview, upgrade_pending;
    bool coverage_enabled;
    StreetProvider pref;
    double lat, lon;
    int direction, new_index;
    unsigned count, index, coverage_count;
    bool search_network_error;
    double search_lat, search_lon;
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
    if (!url_safe(url) || atomic_load(&state.cancelled)) return false;
    NetworkRequestInfo info;
    bool ok = network_request_ex(handle, url, max, &state.cancelled, bytes, size, &info);
    if (!ok && !atomic_load(&state.cancelled))
        log_write("[STREET] Request failed (HTTP %ld, curl %d)\n",
                  info.http_status,info.curl_code);
    if (!ok && (info.http_status == 401 || info.http_status == 403))
        state.outcome = ACCESS_DENIED;
    if (!ok && info.http_status == 429)
        state.outcome = RATE_LIMITED;
    return ok;
}

static bool query_source(void *handle, StreetProvider provider,
                         double radius, const char *token, unsigned *count) {
    char url[1400];
    if (!street_source_url(provider,state.lat,state.lon,radius,token,url,sizeof(url)))
        return false;
    CURL *curl=handle;
    /* Search requests are latency-sensitive and must never block the UI. */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, provider==STREET_PROVIDER_KARTAVIEW ? 9L : 6L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    if (provider==STREET_PROVIDER_MAPILLARY) {
        /* Never send a token over an unverified TLS connection, and don't
         * automatically follow a redirect containing the token. */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        FILE *cert=fopen("sdmc:/3ds_google_maps/cacert.pem", "rb");
        if (cert) {
            fclose(cert);
            curl_easy_setopt(curl, CURLOPT_CAINFO, "sdmc:/3ds_google_maps/cacert.pem");
        }
    } else {
        /* Existing unauthenticated public-image fallback. A production
         * release must migrate all requests to verified certificates. */
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    }
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
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,NULL);
        curl_easy_setopt(curl,CURLOPT_HTTPGET,1L);
        curl_easy_setopt(curl,CURLOPT_HTTPHEADER,NULL);
        curl_slist_free_all(headers);
    } else {
        ok=get(handle,url,PANO_JSON_MAX,&response,&length);
    }
    if (!ok) {
        state.search_network_error = true;
        return false;
    }
    *count=street_parse_candidates(provider,(const char*)response,length,
                                state.lat,state.lon,state.candidates,PANO_NEARBY_MAX);
    free(response);
    return true;
}

static u16 rgb565(const unsigned char *c) {
    return (u16)(((c[0]>>3)<<11)|((c[1]>>2)<<5)|(c[2]>>3));
}

static unsigned tiled_index(unsigned x, unsigned y) {
    unsigned m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|
               ((x&4)<<2)|((y&4)<<3);
    return ((y>>3)*128+(x>>3))*64+m;
}

static bool decode_bing(void *handle, const PanoLiveImage *p) {
    unsigned char *faces[6] = {0};
    u16 *pixels = NULL;
    for (unsigned face=0; face<6; face++) {
        char url[180];
        u8 *jpeg=NULL;
        u32 bytes=0;
        if (!street_bing_face_url(p->id,face,url,sizeof(url)) ||
            !get(handle,url,256*1024,&jpeg,&bytes)) goto done;
        int w,h,comp;
        if (stbi_info_from_memory(jpeg,(int)bytes,&w,&h,&comp) && w==256 && h==256)
            faces[face]=stbi_load_from_memory(jpeg,(int)bytes,&w,&h,&comp,3);
        free(jpeg);
        if (!faces[face]) { state.outcome=INVALID_IMAGE; goto done; }
    }
    pixels=malloc(PANO_PIXELS*sizeof(*pixels));
    if (!pixels) { state.outcome=OUT_OF_MEMORY; goto done; }

    /* Face order is front, right, back, left, up, down. The outer
     * pixel on each side is an overlap border, leaving 254 samples. */
    const float pi=3.14159265358979323846f;
    float sin_lon[1024], cos_lon[1024];
    for (unsigned x=0;x<1024;x++) {
        float lon=(2*(x+.5f)/1024-1)*pi;
        sin_lon[x]=sinf(lon);
        cos_lon[x]=cosf(lon);
    }
    for (unsigned y=0;y<512;y++) {
        if (atomic_load(&state.cancelled)) goto done;
        float lat=((y+.5f)/512-.5f)*pi;
        float dy=sinf(lat), radius=cosf(lat);
        for (unsigned x=0;x<1024;x++) {
            float dx=sin_lon[x]*radius,dz=cos_lon[x]*radius;
            float ax=fabsf(dx),ay=fabsf(dy),az=fabsf(dz);
            float s,t,depth;
            unsigned face;
            if (ay>=ax && ay>=az) {
                face=dy>0 ? 4 : 5;
                s=dx; t=dy>0 ? dz : -dz; depth=ay;
            } else if (ax>az) {
                face=dx>0 ? 1 : 3;
                s=dx>0 ? -dz : dz; t=-dy; depth=ax;
            } else {
                face=dz>0 ? 0 : 2;
                s=dz>0 ? dx : -dx; t=-dy; depth=az;
            }
            unsigned sx=1+(unsigned)((s/depth+1)*126.5f);
            unsigned sy=1+(unsigned)((t/depth+1)*126.5f);
            pixels[tiled_index(x,y)]=rgb565(faces[face]+(sy*256+sx)*3);
        }
    }
    state.pending=pixels;
    pixels=NULL;
    state.next_selected=*p;
    state.has_preview=state.upgrade_pending=false;
    state.outcome=OK;
done:
    free(pixels);
    for (unsigned face=0;face<6;face++) stbi_image_free(faces[face]);
    return state.outcome==OK;
}

static bool decode_candidate(void *handle, const PanoLiveImage *p, bool preview) {
    if (p->provider==STREET_PROVIDER_BING) return decode_bing(handle,p);
    const char *url=(preview && p->preview_url[0]) ?
                         p->preview_url : p->image_url;
    if (!url_safe(url)) return false;
    u8 *jpeg=NULL;
    u32 bytes=0;
    bool google_tile = p->provider == STREET_PROVIDER_GOOGLE &&
        strncmp(url, "https://streetviewpixels-pa.googleapis.com/v1/tile?",
                sizeof("https://streetviewpixels-pa.googleapis.com/v1/tile?") - 1) == 0;
    CURL *curl = handle;
    if (google_tile) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Windows NT 11.0; Win64; x64; rv:151.0) Gecko/20100101 Firefox/151.0");
    }
    bool downloaded = get(handle,url,PANO_JPEG_MAX,&jpeg,&bytes);
    if (google_tile) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "3DS-Google-Maps/1.0 (Nintendo 3DS)");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    }
    if (!downloaded) return false;
    int width=0,height=0,comp=0;
    bool valid=stbi_info_from_memory(jpeg,(int)bytes,&width,&height,&comp) &&
               width>=240 && height>=120 &&
               width<=2048 && height<=1024;
    bool padded_google=p->provider==STREET_PROVIDER_GOOGLE && width==512 && height==512;
    if (valid && p->is_pano && !padded_google) {
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
    if (padded_google) height=256;
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
        if (atomic_load(&state.cancelled)) {
            stbi_image_free(rgb);
            free(tiled);
            return false;
        }
        bool row=y>=pad_y && y-pad_y<fit_height;
        unsigned sy=row ? (unsigned)(height-1) -
                          (y-pad_y)*(unsigned)height/fit_height : 0;
        for (unsigned x=0;x<1024;x++) {
            u16 pixel=0;
            if (row && x>=pad_x && x-pad_x<fit_width) {
                unsigned sx=(x-pad_x)*(unsigned)width/fit_width;
                const unsigned char *c=rgb+(sy*(unsigned)width+sx)*3;
                pixel=rgb565(c);
            }
            tiled[tiled_index(x,y)]=pixel;
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
    static const double radii[]={.003,.015,.045};
    /* A few city blocks is insufficient for sparse crowdsourced 360
     * coverage. Expand to roughly a 5-km search only if the first
     * attempts yield nothing. KartaView accepts only a small radius. */
    const unsigned passes = (src==STREET_PROVIDER_KARTAVIEW ||
                             src==STREET_PROVIDER_GOOGLE) ? 1 : 3;
    for (unsigned i=0;i<passes&&!atomic_load(&state.cancelled);i++) {
        unsigned count=0;
        if (!query_source(curl,src,radii[i],token,&count)) {
            if (state.outcome == NO_COVERAGE) state.outcome = NETWORK_ERROR;
            return false;
        }
        if (count) {
            state.count=count;
            if (state.outcome == NO_COVERAGE) state.outcome = NETWORK_ERROR;
            for (unsigned k=0;k<count&&k<3&&!atomic_load(&state.cancelled);k++) {
                state.new_index=(int)k;
                bool preview=src==STREET_PROVIDER_PANORAMAX;
                if (decode_candidate(curl,&state.candidates[k],preview) ||
                    (preview && decode_candidate(curl,&state.candidates[k],false)))
                    return true;
                if ((src==STREET_PROVIDER_GOOGLE || src==STREET_PROVIDER_BING) &&
                    (state.outcome==ACCESS_DENIED || state.outcome==RATE_LIMITED))
                    return false;
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
    curl_easy_setopt((CURL*)curl, CURLOPT_TIMEOUT, 6L);
    curl_easy_setopt((CURL*)curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt((CURL*)curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt((CURL*)curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    if (state.job==JOB_SEARCH) {
        char token[256]={0};
        bool has_token=mapillary_token(token);
        StreetProvider selected=state.pref;
        StreetProvider order[3];
        unsigned count=0;
        state.search_network_error=false;
        if (selected==STREET_PROVIDER_AUTO) {
            if (has_token)order[count++]=STREET_PROVIDER_MAPILLARY;
            order[count++]=STREET_PROVIDER_PANORAMAX;
            /* Only run KartaView's slower search as a final fallback,
             * avoiding long waits when Panoramax already has images. */
            order[count++]=STREET_PROVIDER_KARTAVIEW;
        } else order[count++]=selected;
        if (selected==STREET_PROVIDER_MAPILLARY&&!has_token) {
            state.outcome=TOKEN_MISSING;
        } else {
            state.outcome=NO_COVERAGE;
            for (unsigned i=0;i<count&&!atomic_load(&state.cancelled);i++) {
                StreetProvider p=order[i];
                if (search_provider(curl,p,token))break;
            }
            if (state.outcome!=OK && state.outcome!=INVALID_IMAGE &&
                state.outcome!=OUT_OF_MEMORY && state.outcome!=ACCESS_DENIED &&
                state.outcome!=RATE_LIMITED && state.search_network_error)
                state.outcome=NETWORK_ERROR;
        }
        memset(token,0,sizeof(token));
    } else if (state.job==JOB_NEXT) {
        PanoLiveImage p={0};
        bool found=false;
        state.outcome=NO_COVERAGE;
        StreetProvider provider=state.selected.provider;
        if (provider==STREET_PROVIDER_PANORAMAX ||
            provider==STREET_PROVIDER_GOOGLE || provider==STREET_PROVIDER_BING) {
            const char *id=state.direction>0?state.selected.next_id:
                                                state.selected.prev_id;
            if (*id && !atomic_load(&state.cancelled)) {
                /* Reuse search metadata when the neighboring sequence photo
                 * is already among nearby results: skips an HTTPS request. */
                for (unsigned i=0;i<state.coverage_count;i++)
                    if (!strcmp(id,state.coverage[i].id)) {
                        p=state.coverage[i];
                        state.new_index=(int)i;
                        found=true;
                        break;
                    }
                if (!found && provider==STREET_PROVIDER_PANORAMAX)
                    found=lookup_next(curl,id,&p);
                else if (!found) {
                    char url[256];
                    bool valid;
                    if (provider==STREET_PROVIDER_GOOGLE) {
                        valid=street_source_url(provider,state.search_lat,state.search_lon,.003,
                                                NULL,url,sizeof(url));
                    } else {
                        int length=snprintf(url,sizeof(url),
                            "https://t.ssl.ak.tiles.virtualearth.net/tiles/cmd/"
                            "StreetSideBubbleMetaData?id=%s",id);
                        valid=length>0 && (size_t)length<sizeof(url);
                    }
                    u8 *bytes=NULL;
                    u32 size=0;
                    if (valid && get(curl,url,PANO_JSON_MAX,&bytes,&size)) {
                        if (provider==STREET_PROVIDER_GOOGLE)
                            found=street_google_find_id((const char*)bytes,size,id,&p);
                        else
                            found=street_parse_candidates(provider,(const char*)bytes,size,
                                state.selected.lat,state.selected.lon,&p,1)==1 &&
                                !strcmp(p.id,id);
                        free(bytes);
                    }
                }
            }
        }
        if (!found && state.coverage_count>1 && provider!=STREET_PROVIDER_GOOGLE &&
            provider!=STREET_PROVIDER_BING) {
            int next=(int)state.index+(state.direction>0?1:-1);
            if (next>=0 && next<(int)state.coverage_count) {
                p=state.coverage[next];state.new_index=next;found=true;
            }
        }
        if (found && !atomic_load(&state.cancelled)) {
            state.outcome=NETWORK_ERROR;
            bool preview=p.provider==STREET_PROVIDER_PANORAMAX;
            if (!decode_candidate(curl,&p,preview) && preview)
                decode_candidate(curl,&p,false);
        }
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
            state.search_lat=state.lat;
            state.search_lon=state.lon;
            state.coverage_count=state.count;
            memcpy(state.coverage,state.candidates,
                   state.coverage_count*sizeof(*state.coverage));
        }
        snprintf(state.status,sizeof(state.status),"%s %s  %.4f,%.4f",
            street_provider_name(state.selected.provider),
            state.selected.is_pano?"360":"photo",
            state.selected.lat,state.selected.lon);
    } else if (job==JOB_UPGRADE && state.live) {
        /* HD upgrade failure is nonfatal: the preview remains usable. */
        snprintf(state.status,sizeof(state.status),"%s %s (preview)",
                 street_provider_name(state.selected.provider),
                 state.selected.is_pano ? "360" : "photo");
    } else if (state.outcome==TOKEN_MISSING)
        snprintf(state.status,sizeof(state.status),"Mapillary: configure mapillary.token");
    else if (state.outcome==NO_COVERAGE)
        snprintf(state.status,sizeof(state.status),
                 state.pref==STREET_PROVIDER_AUTO ?
                 "No matching photos from enabled sources" :
                 "No matching photos from selected provider");
    else if (state.outcome==ACCESS_DENIED)
        snprintf(state.status,sizeof(state.status),"Imagery provider denied access (HTTP 401/403)");
    else if (state.outcome==RATE_LIMITED)
        snprintf(state.status,sizeof(state.status),"Imagery provider rate limited (HTTP 429)");
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
