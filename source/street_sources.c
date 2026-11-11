#include "street_sources.h"
#include "street_nav.h"
#include <jansson.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *street_provider_name(StreetProvider p) {
    switch (p) {
        case STREET_PROVIDER_AUTO: return "Automatic";
        case STREET_PROVIDER_PANORAMAX: return "Panoramax";
        case STREET_PROVIDER_MAPILLARY: return "Mapillary";
        case STREET_PROVIDER_KARTAVIEW: return "KartaView";
        case STREET_PROVIDER_GOOGLE: return "Google (experimental)";
        case STREET_PROVIDER_BING: return "Bing (experimental)";
        default: return "Unknown";
    }
}

bool street_mapillary_token_valid(const char *t) {
    if (!t || !*t || strlen(t) > 250) return false;
    for (; *t; t++)
        if (!isalnum((unsigned char)*t) && *t != '_' && *t != '-' &&
            *t != '.' && *t != '~' && *t != '|') return false;
    return true;
}

static bool escaped_token(const char *input, char *out, size_t cap) {
    if (!street_mapillary_token_valid(input) || cap < 4) return false;
    size_t i = 0;
    for (; *input; input++) {
        if (*input == '|') {
            if (i + 3 >= cap) return false;
            memcpy(out + i, "%7C", 3);
            i += 3;
        } else {
            if (i + 1 >= cap) return false;
            out[i++] = *input;
        }
    }
    out[i] = 0;
    return true;
}

bool street_source_url(StreetProvider source, double lat, double lon,
                       double radius, const char *token, char *url, size_t capacity) {
    if (!url || !capacity || !isfinite(lat) || !isfinite(lon) ||
        fabs(lat) > 89 || fabs(lon) > 179 ||
        !isfinite(radius) || radius <= 0 || radius > .08) return false;
    if (source == STREET_PROVIDER_PANORAMAX)
        return pano_search_url(lat, lon, radius, url, capacity);
    int n = -1;
    if (source == STREET_PROVIDER_MAPILLARY) {
        char safe[770];
        if (!escaped_token(token, safe, sizeof(safe))) return false;
        n = snprintf(url, capacity,
            "https://graph.mapillary.com/images?"
            "bbox=%.6f,%.6f,%.6f,%.6f&limit=24"
            "&fields=id,computed_geometry,geometry,camera_type,thumb_1024_url,"
            "computed_compass_angle,captured_at,creator,sequence"
            "&access_token=%s",
            lon - radius, lat - radius, lon + radius, lat + radius, safe);
    } else if (source == STREET_PROVIDER_GOOGLE) {
        if (fabs(lat) > 85.05112878) return false;
        const double pi = 3.141592653589793;
        unsigned x = (unsigned)((lon + 180) / 360 * 131072);
        unsigned y = (unsigned)fmax(0, fmin(131071,
            (1 - asinh(tan(lat * pi / 180)) / pi) * 65536));
        n = snprintf(url, capacity, "https://www.google.com/maps/photometa/ac/v1"
            "?pb=!1m1!1smaps_sv.tactile!6m3!1i%u!2i%u!3i17!8b1", x, y);
    } else if (source == STREET_PROVIDER_BING) {
        n = snprintf(url, capacity, "https://t.ssl.ak.tiles.virtualearth.net"
            "/tiles/cmd/StreetSideBubbleMetaData?count=32"
            "&north=%.6f&south=%.6f&east=%.6f&west=%.6f",
            lat + radius, lat - radius, lon + radius, lon - radius);
    } else if (source == STREET_PROVIDER_KARTAVIEW) {
        n = snprintf(url, capacity,
            "https://kartaview.org/1.0/list/nearby-photos/");
    }
    return n > 0 && (size_t)n < capacity;
}

static void copy_text(char *dest, size_t capacity, const char *value, const char *fallback) {
    if (!value || !*value) value = fallback ? fallback : "";
    if (capacity) snprintf(dest, capacity, "%s", value);
}
static const char *strfield(json_t *obj, const char *key) {
    return json_string_value(json_object_get(obj, key));
}
static bool numberfield(json_t *v, double *out) {
    if (json_is_number(v)) *out = json_number_value(v);
    else if (json_is_string(v)) {
        const char *s = json_string_value(v);
        char *end = NULL;
        *out = strtod(s, &end);
        if (!s || end == s || *end) return false;
    } else return false;
    return isfinite(*out);
}
static bool coordinates(json_t *coord, double *lat, double *lon) {
    if (!json_is_array(coord) || json_array_size(coord) < 2 ||
        !numberfield(json_array_get(coord, 0), lon) ||
        !numberfield(json_array_get(coord, 1), lat))
        return false;
    return fabs(*lat) <= 90 && fabs(*lon) <= 180;
}
static bool remote_url(const char *url) {
    if (!url || strncmp(url, "https://", 8) || strlen(url) > 1000) return false;
    const char *host = url + 8;
    if (*host == '/' || *host == ':' || !*host) return false;
    for (const char *p = host; *p; p++) {
        if ((unsigned char)*p < 33 || (unsigned char)*p > 126 ||
            *p == '\\' || *p == '#' || *p == '@') return false;
    }
    return true;
}
static bool idstring(json_t *v, char *out, size_t capacity) {
    const char *s = json_string_value(v);
    if (s) {
        if (strlen(s) >= capacity) return false;
        copy_text(out, capacity, s, "");
    } else if (json_is_integer(v)) {
        snprintf(out, capacity, "%lld", (long long)json_integer_value(v));
    } else return false;
    if (!*out) return false;
    for (const char *p = out; *p; p++)
        if (!isdigit((unsigned char)*p)) return false;
    return true;
}

static bool parse_mapillary(json_t *entry, PanoLiveImage *out) {
    if (!json_is_object(entry) || !out) return false;
    json_t *camera = json_object_get(entry, "camera_type");
    const char *kind = json_string_value(camera);
    const char *url = strfield(entry, "thumb_1024_url");
    if (!remote_url(url)) return false;
    json_t *loc = json_object_get(entry, "computed_geometry");
    if (!json_is_object(loc)) loc = json_object_get(entry, "geometry");
    double lat, lon;
    if (!coordinates(json_object_get(loc, "coordinates"), &lat, &lon)) return false;
    memset(out, 0, sizeof(*out));
    if (!idstring(json_object_get(entry, "id"), out->id, sizeof(out->id))) return false;
    out->provider = STREET_PROVIDER_MAPILLARY;
    out->lat = lat; out->lon = lon;
    out->is_pano = kind && (!strcmp(kind, "spherical") || !strcmp(kind, "360"));
    copy_text(out->image_url, sizeof(out->image_url), url, "");
    json_t *heading = json_object_get(entry, "computed_compass_angle");
    if (json_is_number(heading)) out->azimuth = (float)json_number_value(heading);
    json_t *creator = json_object_get(entry, "creator");
    copy_text(out->credit, sizeof(out->credit), strfield(creator, "username"), "Mapillary contributor");
    copy_text(out->license, sizeof(out->license), "Mapillary Terms", "");
    const char *seq = strfield(entry, "sequence");
    if (seq) copy_text(out->sequence, sizeof(out->sequence), seq, "");
    return true;
}

static bool base64_url(const char *text, char *out, size_t capacity) {
    const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = strlen(text), at = 0;
    const unsigned char *s = (const unsigned char *)text;
    for (size_t pos = 0; pos < len; pos += 3) {
        unsigned a = s[pos], b = pos + 1 < len ? s[pos + 1] : 0;
        unsigned c = pos + 2 < len ? s[pos + 2] : 0;
        if (at + 4 >= capacity) return false;
        out[at++] = alphabet[a >> 2];
        out[at++] = alphabet[((a & 3) << 4) | (b >> 4)];
        if (pos + 1 < len) out[at++] = alphabet[((b & 15) << 2) | (c >> 6)];
        if (pos + 2 < len) out[at++] = alphabet[c & 63];
    }
    out[at] = 0; return true;
}
/* The old storage hosts are not reliably reachable. KartaView's official
 * image proxy accepts a base64-encoded absolute storage URL. */
static bool kartaview_asset(const char *path, char *out, size_t capacity) {
    if (!path || !*path || strlen(path) > 450) return false;
    char absolute[640];
    if (remote_url(path)) snprintf(absolute, sizeof(absolute), "%s", path);
    else if (!strncmp(path, "storage", 7)) {
        const char *digit = path + 7, *slash = digit;
        while (isdigit((unsigned char)*slash)) slash++;
        if (slash == digit || *slash != '/') return false;
        int n = snprintf(absolute, sizeof(absolute),
            "https://storage%.*s.openstreetcam.org%s",
            (int)(slash - digit), digit, slash);
        if (n <= 0 || (size_t)n >= sizeof(absolute)) return false;
    } else {
        for (const char *p = path; *p; p++)
            if (!isalnum((unsigned char)*p) && *p!='/' && *p!='-' &&
                *p!='_' && *p!='.' && *p!='%') return false;
        snprintf(absolute, sizeof(absolute), "https://kartaview.org/%s",
                 *path == '/' ? path + 1 : path);
    }
    char encoded[900];
    if (!base64_url(absolute, encoded, sizeof(encoded))) return false;
    int n = snprintf(out, capacity, "https://cdn.kartaview.org/pr:sharp/%s", encoded);
    return n > 0 && (size_t)n < capacity;
}
static bool parse_kartaview(json_t *entry, PanoLiveImage *out) {
    if (!json_is_object(entry) || !out) return false;
    double lat, lon;
    if (!numberfield(json_object_get(entry, "lat"), &lat) ||
        !numberfield(json_object_get(entry, "lng"), &lon) ||
        fabs(lat)>90 || fabs(lon)>180) return false;
    const char *path = strfield(entry, "lth_name");
    if (!path || !*path) path = strfield(entry, "name");
    char url[1024];
    if (!kartaview_asset(path, url, sizeof(url))) return false;
    memset(out, 0, sizeof(*out));
    if (!idstring(json_object_get(entry, "id"), out->id, sizeof(out->id))) return false;
    out->provider = STREET_PROVIDER_KARTAVIEW;
    out->lat = lat; out->lon = lon;
    out->is_pano = false;
    const char *kind = strfield(entry, "projection");
    json_t *angle = json_object_get(entry, "fieldOfView");
    double fov = 0;
    if ((kind && (!strcmp(kind, "EQUIRECTANGULAR") || !strcmp(kind, "SPHERE"))) ||
        (numberfield(angle, &fov) && fov >= 340))
        out->is_pano = true;
    copy_text(out->image_url, sizeof(out->image_url), url, "");
    copy_text(out->credit, sizeof(out->credit), "KartaView contributor", "");
    copy_text(out->license, sizeof(out->license), "CC BY-SA (verify per photo)", "");
    json_t *seq = json_object_get(entry, "sequence_id");
    if (json_is_string(seq)) copy_text(out->sequence, sizeof(out->sequence),
                                     json_string_value(seq), "");
    else if (json_is_integer(seq))
        snprintf(out->sequence, sizeof(out->sequence), "%lld",
                 (long long)json_integer_value(seq));
    return true;
}

/* Protocol layout references: sk-zk/streetlevel (MIT). */
static bool google_fields(json_t *entry, char id[48], double *lat, double *lon) {
    json_t *record = json_array_get(entry, 0);
    json_t *identity = json_array_get(record, 0);
    const char *value = json_string_value(json_array_get(identity, 1));
    if (json_integer_value(json_array_get(identity, 0)) != 2 ||
        !value || strlen(value) != 22 ||
        strspn(value, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                     "0123456789_-") != 22) return false;
    json_t *coords = json_array_get(json_array_get(record, 2), 0);
    if (!numberfield(json_array_get(coords, 2), lat) || fabs(*lat) > 90 ||
        !numberfield(json_array_get(coords, 3), lon) || fabs(*lon) > 180)
        return false;
    memcpy(id, value, 23);
    return true;
}

static bool parse_google(json_t *array, json_t *entry, PanoLiveImage *out) {
    memset(out, 0, sizeof(*out));
    if (!google_fields(entry, out->id, &out->lat, &out->lon)) return false;
    out->provider = STREET_PROVIDER_GOOGLE;
    out->is_pano = true;
    strcpy(out->credit, "Imagery: Google Street View");
    snprintf(out->image_url, sizeof(out->image_url),
        "https://streetviewpixels-pa.googleapis.com/v1/tile"
        "?cb_client=maps_sv.tactile&panoid=%s&x=0&y=0&zoom=0", out->id);
    json_t *angles = json_array_get(json_array_get(json_array_get(entry, 0), 2), 2);
    double heading = json_number_value(json_array_get(angles, 0));
    out->azimuth = (float)heading;
    const double rad = 0.017453292519943295;
    double front = 0, back = 0;
    json_t *links = json_array_get(entry, 1);
    size_t i;
    json_t *link;
    json_array_foreach(links, i, link) {
        if (!json_is_integer(link) || json_integer_value(link) < 0) continue;
        unsigned long long n = (unsigned long long)json_integer_value(link);
        if (n >= json_array_size(array)) continue;
        char id[48];
        double lat, lon;
        if (!google_fields(json_array_get(array, n), id, &lat, &lon) ||
            !strcmp(id, out->id)) continue;
        double x = (lon - out->lon) * cos(out->lat * rad);
        double y = lat - out->lat;
        double distance = hypot(x, y);
        if (distance == 0) continue;
        double direction = (sin(heading * rad) * x + cos(heading * rad) * y)
                           / distance;
        if (out->link_count < PANO_LINK_MAX) {
            PanoLiveLink *dst = &out->links[out->link_count++];
            memset(dst, 0, sizeof(*dst));
            strcpy(dst->id, id);
            dst->lat = lat;
            dst->lon = lon;
            dst->has_location = true;
            dst->bearing = street_nav_bearing(out->lat, out->lon, lat, lon);
        }
        if (direction > front) {
            front = direction;
            strcpy(out->next_id, id);
        } else if (direction < back) {
            back = direction;
            strcpy(out->prev_id, id);
        }
    }
    return true;
}

static bool bing_id(json_t *v, char *id, size_t capacity) {
    if (!json_is_integer(v)) return false;
    json_int_t n = json_integer_value(v);
    if (n < 1 || n > 4294967295LL) return false;
    snprintf(id, capacity, "%llu", (unsigned long long)n);
    return true;
}

static bool parse_bing(json_t *entry, PanoLiveImage *out) {
    memset(out, 0, sizeof(*out));
    if (!bing_id(json_object_get(entry, "id"), out->id, sizeof(out->id)) ||
        !numberfield(json_object_get(entry, "la"), &out->lat) || fabs(out->lat) > 90 ||
        !numberfield(json_object_get(entry, "lo"), &out->lon) || fabs(out->lon) > 180)
        return false;
    out->provider = STREET_PROVIDER_BING;
    out->is_pano = true;
    strcpy(out->credit, "Imagery: Microsoft Bing Streetside");
    out->azimuth = (float)json_number_value(json_object_get(entry, "he"));
    bing_id(json_object_get(entry, "ne"), out->next_id, sizeof(out->next_id));
    bing_id(json_object_get(entry, "pr"), out->prev_id, sizeof(out->prev_id));
    json_t *neighbors = json_object_get(entry, "nbn");
    size_t i;
    json_t *neighbor;
    double best = 0;
    char next[sizeof(out->next_id)] = {0};
    json_array_foreach(neighbors, i, neighbor) {
        char id[48];
        double bearing;
        if (!bing_id(json_object_get(neighbor, "id"), id, sizeof(id)) ||
            !strcmp(id, out->id) ||
            !numberfield(json_object_get(neighbor, "az"), &bearing)) continue;
        if (out->link_count < PANO_LINK_MAX) {
            bool already = false;
            for (unsigned k=0; k<out->link_count; k++)
                if (!strcmp(out->links[k].id,id)) already = true;
            if (!already) {
                PanoLiveLink *dst = &out->links[out->link_count++];
                memset(dst,0,sizeof(*dst));
                strcpy(dst->id,id);
                dst->bearing = (float)bearing;
            }
        }
        if (!strcmp(id, out->prev_id)) continue;
        double forward = cos((bearing - out->azimuth) * .017453292519943295);
        if (forward > best) {
            best = forward;
            strcpy(next, id);
        }
    }
    if (!out->next_id[0]) strcpy(out->next_id, next);
    return true;
}

bool street_bing_face_url(const char *id, unsigned face, char *url, size_t cap) {
    if (!id || !*id || strlen(id) > 10 || strspn(id, "0123456789") != strlen(id) ||
        face >= 6) return false;
    unsigned long long value = strtoull(id, NULL, 10);
    if (!value || value > 4294967295ULL) return false;
    char quad[17] = {0};
    for (int i = 15; i >= 0; --i) {
        quad[i] = '0' + (value & 3);
        value >>= 2;
    }
    unsigned key = face + 1;
    int n = snprintf(url, cap, "https://t.ssl.ak.tiles.virtualearth.net"
        "/tiles/hs%s%u%u.jpg?g=13716", quad, key / 4, key % 4);
    return n > 0 && (size_t)n < cap;
}

bool street_google_find_id(const char *json, size_t len, const char *id,
                           PanoLiveImage *out) {
    if (len >= 4 && !memcmp(json, ")]}'", 4)) { json += 4; len -= 4; }
    json_t *root = json_loadb(json, len, JSON_REJECT_DUPLICATES, NULL);
    if (!root) return false;
    json_t *array = json_array_get(json_array_get(root, 1), 1);
    bool found = false;
    size_t i;
    json_t *entry;
    json_array_foreach(array, i, entry) {
        const char *value = json_string_value(json_array_get(
            json_array_get(json_array_get(entry, 0), 0), 1));
        if (value && !strcmp(value, id)) {
            found = parse_google(array, entry, out);
            break;
        }
    }
    json_decref(root);
    return found;
}

/* Do not assume that the first returned Bing metadata item matches the ID. */
bool street_bing_find_id(const char *json, size_t len, const char *id,
                         PanoLiveImage *out) {
    if (!json || !id || !out || len > 512u*1024u) return false;
    json_t *root = json_loadb(json,len,JSON_REJECT_DUPLICATES,NULL);
    if (!json_is_array(root)) { json_decref(root); return false; }
    bool found = false;
    size_t index;
    json_t *entry;
    json_array_foreach(root,index,entry) {
        PanoLiveImage candidate;
        if (parse_bing(entry,&candidate) && !strcmp(id,candidate.id)) {
            *out = candidate;
            found = true;
            break;
        }
    }
    json_decref(root);
    return found;
}

static double score(double lat, double lon, const PanoLiveImage *p) {
    const double rad = .017453292519943295;
    double dx = (p->lon - lon) * cos(lat * rad) * 111320;
    double dy = (p->lat - lat) * 111320;
    return hypot(dx,dy) + (p->is_pano ? 0 : 250.0);
}
unsigned street_parse_candidates(StreetProvider source, const char *json,
                    size_t len, double lat, double lon,
                    PanoLiveImage *items, unsigned capacity) {
    if (!json || !items || !capacity || len > 512u*1024u ||
        !isfinite(lat) || !isfinite(lon)) return 0;
    if (capacity > PANO_NEARBY_MAX) capacity = PANO_NEARBY_MAX;
    if (source == STREET_PROVIDER_PANORAMAX)
        return pano_parse_nearby(json, len, lat, lon, items, capacity);
    if (source == STREET_PROVIDER_GOOGLE && len >= 4 && !memcmp(json, ")]}'", 4)) {
        json += 4;
        len -= 4;
    }
    json_t *root = json_loadb(json, len, JSON_REJECT_DUPLICATES, NULL);
    if (!root) return 0;
    json_t *array = source == STREET_PROVIDER_MAPILLARY ?
        json_object_get(root,"data") : json_object_get(root,"currentPageItems");
    if (source == STREET_PROVIDER_KARTAVIEW && !json_is_array(array))
        array = json_object_get(json_object_get(root,"result"),"data");
    if (source == STREET_PROVIDER_BING) array = root;
    if (source == STREET_PROVIDER_GOOGLE)
        array = json_array_get(json_array_get(root, 1), 1);
    unsigned count = 0;
    if (json_is_array(array)) {
        size_t i; json_t *entry;
        json_array_foreach(array, i, entry) {
            PanoLiveImage candidate;
            bool ok;
            switch (source) {
                case STREET_PROVIDER_GOOGLE: ok = parse_google(array, entry, &candidate); break;
                case STREET_PROVIDER_BING: ok = parse_bing(entry, &candidate); break;
                case STREET_PROVIDER_MAPILLARY: ok = parse_mapillary(entry, &candidate); break;
                case STREET_PROVIDER_KARTAVIEW: ok = parse_kartaview(entry, &candidate); break;
                default: ok = false; break;
            }
            if (!ok) continue;
            bool exists = false;
            for (unsigned n=0; n<count; n++)
                if (!strcmp(items[n].id,candidate.id)) { exists = true; break; }
            if (exists) continue;
            double d = score(lat, lon, &candidate);
            unsigned pos=0;
            while (pos<count && score(lat,lon,&items[pos]) <= d) pos++;
            if (pos>=capacity) continue;
            if (count<capacity) count++;
            for (unsigned n=count-1;n>pos;n--) items[n]=items[n-1];
            items[pos]=candidate;
        }
    }
    json_decref(root);
    return count;
}
