#include "street_sources.h"
#include <jansson.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *street_provider_name(StreetProvider p) {
    switch (p) {
        case STREET_PROVIDER_AUTO: return "Auto";
        case STREET_PROVIDER_PANORAMAX: return "Panoramax";
        case STREET_PROVIDER_MAPILLARY: return "Mapillary";
        case STREET_PROVIDER_KARTAVIEW: return "KartaView 360";
        default: return "Unknown";
    }
}
bool street_mapillary_token_valid(const char *t) {
    if (!t || !t[0]) return false;
    size_t n = strlen(t);
    if (n > 250) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (!isalnum(c) && c != '_' && c != '-' && c != '.' &&
            c != '~' && c != '|') return false;
    }
    return true;
}
static bool escaped_token(const char *src, char *out, size_t cap) {
    if (!street_mapillary_token_valid(src)) return false;
    size_t n = 0;
    for (; *src; src++) {
        const char *rep = *src == '|' ? "%7C" : NULL;
        if (rep) {
            if (n + 3 >= cap) return false;
            memcpy(out + n, rep, 3); n += 3;
        } else {
            if (n + 1 >= cap) return false;
            out[n++] = *src;
        }
    }
    if (n >= cap) return false;
    out[n] = 0;
    return true;
}
bool street_source_url(StreetProvider p, double lat, double lon,
                       double r, const char *token, char *url, size_t cap) {
    if (!url || !cap || !isfinite(lat) || !isfinite(lon) ||
        fabs(lat) > 89 || fabs(lon) > 179 ||
        !isfinite(r) || r <= 0 || r > .08) return false;
    int n = -1;
    if (p == STREET_PROVIDER_PANORAMAX)
        return pano_search_url(lat, lon, r, url, cap);
    if (p == STREET_PROVIDER_MAPILLARY) {
        char safe[770];
        if (!escaped_token(token, safe, sizeof(safe))) return false;
        n = snprintf(url, cap, "https://graph.mapillary.com/images?"
            "bbox=%.6f,%.6f,%.6f,%.6f&limit=60"
            "&fields=id,computed_geometry,geometry,camera_type,thumb_1024_url,"
            "computed_compass_angle,captured_at,creator,sequence"
            "&access_token=%s",
            lon - r, lat - r, lon + r, lat + r, safe);
    } else if (p == STREET_PROVIDER_KARTAVIEW) {
        unsigned meters = (unsigned)fmax(30, fmin(500, r * 111000));
        n = snprintf(url, cap,
            "https://api.openstreetcam.org/2.0/photo/?lat=%.6f&lng=%.6f"
            "&radius=%u&itemsPerPage=60&page=1", lat, lon, meters);
    }
    return n > 0 && (size_t)n < cap;
}
static void textfield(char *dst, size_t size, json_t *v, const char *fallback) {
    const char *s = json_string_value(v);
    if (!s || !*s) s = fallback ? fallback : "";
    if (size) snprintf(dst, size, "%s", s);
}
static bool coordinates(json_t *coord, double *lat, double *lon) {
    if (!json_is_array(coord) || json_array_size(coord) < 2) return false;
    json_t *a = json_array_get(coord, 0);
    json_t *b = json_array_get(coord, 1);
    if (!json_is_number(a) || !json_is_number(b)) return false;
    *lon = json_number_value(a); *lat = json_number_value(b);
    return isfinite(*lon) && isfinite(*lat) && fabs(*lon) <= 180 && fabs(*lat) <= 90;
}
static bool remote_url(const char *s) {
    if (!s || strncmp(s, "https://", 8) != 0 || strlen(s) > 1000) return false;
    for (const char *p = s; *p; p++)
        if ((unsigned char)*p < 33 || (unsigned char)*p > 126 ||
            *p == '#' || *p == '\\') return false;
    return true;
}
static bool parse_mapillary(json_t *v, PanoLiveImage *p) {
    const char *id = json_string_value(json_object_get(v, "id"));
    const char *camera = json_string_value(json_object_get(v, "camera_type"));
    const char *url = json_string_value(json_object_get(v, "thumb_1024_url"));
    if (!id || !*id || strlen(id) >= sizeof(p->id) ||
        !camera || strcmp(camera, "spherical") ||
        !remote_url(url)) return false;
    for (const char *c = id; *c; c++) if (!isdigit((unsigned char)*c)) return false;
    json_t *loc = json_object_get(v, "computed_geometry");
    if (!json_is_object(loc)) loc = json_object_get(v, "geometry");
    double lat, lon;
    if (!coordinates(json_object_get(loc, "coordinates"), &lat, &lon)) return false;
    memset(p, 0, sizeof(*p));
    p->provider = STREET_PROVIDER_MAPILLARY;
    snprintf(p->id, sizeof(p->id), "%s", id);
    snprintf(p->image_url, sizeof(p->image_url), "%s", url);
    p->lat = lat; p->lon = lon;
    json_t *az = json_object_get(v, "computed_compass_angle");
    if (json_is_number(az)) p->azimuth = (float)json_number_value(az);
    json_t *creator = json_object_get(v, "creator");
    textfield(p->credit, sizeof(p->credit),
              json_object_get(creator, "username"), "Mapillary contributor");
    snprintf(p->license, sizeof(p->license), "Mapillary");
    textfield(p->sequence, sizeof(p->sequence),
              json_object_get(v, "sequence"), "");
    json_t *time = json_object_get(v, "captured_at");
    if (json_is_integer(time))
        p->captured_at_ms = (long long)json_integer_value(time);
    return true;
}
static bool numstring(json_t *v, char *out, size_t cap) {
    if (json_is_string(v)) {
        textfield(out, cap, v, "");
    } else if (json_is_integer(v)) {
        snprintf(out, cap, "%lld", (long long)json_integer_value(v));
    } else return false;
    if (!out[0]) return false;
    for (const char *p = out; *p; p++) if (!isdigit((unsigned char)*p)) return false;
    return true;
}
static bool numberfield(json_t *v, double *out) {
    if (json_is_number(v)) *out = json_number_value(v);
    else if (json_is_string(v)) {
        const char *s = json_string_value(v);
        char *end = NULL; *out = strtod(s, &end);
        if (end == s || *end) return false;
    } else return false;
    return isfinite(*out);
}
/* KartaView's older storageN.openstreetcam.org image URLs increasingly return
 * 404. Its public image CDN accepts an unpadded base64-encoded storage URL,
 * as used by the osm-editor-kit street-imagery adapter. */
static bool karta_proxy_url(const char *url, char *out, size_t capacity) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (!remote_url(url)) return false;
    if (strstr(url, "cdn.kartaview.org/") == url + 8) {
        return snprintf(out, capacity, "%s", url) > 0 && strlen(url) < capacity;
    }
    char b64[1024];
    size_t n = strlen(url), w = 0;
    if (n > 600 || capacity < 64) return false;
    for (size_t i = 0; i < n; i += 3) {
        unsigned a = (unsigned char)url[i];
        unsigned b = i + 1 < n ? (unsigned char)url[i + 1] : 0;
        unsigned c = i + 2 < n ? (unsigned char)url[i + 2] : 0;
        if (w + 4 >= sizeof(b64)) return false;
        b64[w++] = alphabet[a >> 2];
        b64[w++] = alphabet[((a & 3u) << 4) | (b >> 4)];
        if (i + 1 < n) b64[w++] = alphabet[((b & 15u) << 2) | (c >> 6)];
        if (i + 2 < n) b64[w++] = alphabet[c & 63u];
    }
    b64[w] = 0;
    int result = snprintf(out, capacity, "https://cdn.kartaview.org/pr:sharp/%s", b64);
    return result > 0 && (size_t)result < capacity;
}

static bool karta_normalize_path(const char *path, char *url, size_t cap) {
    if (!path || !*path) return false;
    if (!strncmp(path, "http://storage", 14)) {
        int n = snprintf(url, cap, "https://%s", path + 7);
        return n > 0 && (size_t)n < cap && remote_url(url);
    }
    if (!strncmp(path, "https://", 8)) {
        int n = snprintf(url, cap, "%s", path);
        return n > 0 && (size_t)n < cap && remote_url(url);
    }
    while (*path == '/') path++;
    /* Older KartaView API exposes bucket paths such as "5/img/....jpg". */
    unsigned bucket = 0, digits = 0;
    const char *q = path;
    while (*q >= '0' && *q <= '9' && digits < 3) {
        bucket = bucket * 10 + (unsigned)(*q++ - '0');
        digits++;
    }
    if (!digits || *q != '/') return false;
    for (const char *p = q + 1; *p; p++)
        if ((unsigned char)*p < 33 || *p == '#' || *p == '\\') return false;
    int n = snprintf(url, cap, "https://storage%u.openstreetcam.org/%s",
                     bucket, q + 1);
    return n > 0 && (size_t)n < cap && remote_url(url);
}

static bool parse_kartaview(json_t *v, PanoLiveImage *p) {
    if (!json_is_object(v)) return false;
    const char *proj = json_string_value(json_object_get(v, "projection"));
    double fov = 0;
    if (!numberfield(json_object_get(v, "fieldOfView"), &fov) ||
        fov < 335 || (proj && strcmp(proj, "SPHERE") &&
        strcmp(proj, "EQUIRECTANGULAR"))) return false;
    double lat, lon;
    if (!numberfield(json_object_get(v, "lat"), &lat) ||
        !numberfield(json_object_get(v, "lng"), &lon) ||
        fabs(lat) > 90 || fabs(lon) > 180) return false;
    memset(p, 0, sizeof(*p));
    if (!numstring(json_object_get(v, "id"), p->id, sizeof(p->id))) return false;
    const char *path = json_string_value(json_object_get(v, "fileurlLTh"));
    char url[768];
    bool found_url = karta_normalize_path(path, url, sizeof(url));
    if (!found_url) {
        path = json_string_value(json_object_get(v, "fileurlProc"));
        found_url = karta_normalize_path(path, url, sizeof(url));
    }
    if (!found_url) {
        path = json_string_value(json_object_get(v, "fileurl"));
        found_url = karta_normalize_path(path, url, sizeof(url));
    }
    if (!found_url) return false;
    p->provider = STREET_PROVIDER_KARTAVIEW;
    p->lat = lat; p->lon = lon;
    if (!karta_proxy_url(url, p->image_url, sizeof(p->image_url)))
        snprintf(p->image_url, sizeof(p->image_url), "%s", url);
    snprintf(p->credit, sizeof(p->credit), "KartaView contributor");
    snprintf(p->license, sizeof(p->license), "KartaView");
    numstring(json_object_get(v, "sequenceId"), p->sequence, sizeof(p->sequence));
    json_t *index = json_object_get(v, "sequenceIndex");
    if (json_is_integer(index))
        p->sequence_index = (int)json_integer_value(index);
    else if (json_is_string(index))
        p->sequence_index = atoi(json_string_value(index));
    return true;
}
static double dist2(double lat, double lon, const PanoLiveImage *p) {
    double dx = (p->lon - lon) * cos(lat * .017453292519943295);
    double dy = p->lat - lat;
    return dx * dx + dy * dy;
}
unsigned street_parse_candidates(StreetProvider source,
                       const char *json, size_t len, double lat, double lon,
                       PanoLiveImage *out, unsigned capacity) {
    if (!json || !out || !capacity || len > 512u * 1024u ||
        !isfinite(lat) || !isfinite(lon)) return 0;
    if (capacity > PANO_NEARBY_MAX) capacity = PANO_NEARBY_MAX;
    if (source == STREET_PROVIDER_PANORAMAX)
        return pano_parse_nearby(json, len, lat, lon, out, capacity);
    json_t *root = json_loadb(json, len, JSON_REJECT_DUPLICATES, NULL);
    if (!root) return 0;
    json_t *arr = NULL;
    if (source == STREET_PROVIDER_MAPILLARY)
        arr = json_object_get(root, "data");
    else if (source == STREET_PROVIDER_KARTAVIEW)
        arr = json_object_get(json_object_get(root, "result"), "data");
    unsigned count = 0;
    if (json_is_array(arr)) {
        size_t i;
        json_t *entry;
        json_array_foreach(arr, i, entry) {
            PanoLiveImage candidate;
            bool ok = source == STREET_PROVIDER_MAPILLARY ?
                parse_mapillary(entry, &candidate) :
                parse_kartaview(entry, &candidate);
            if (!ok) continue;
            bool exists = false;
            for (unsigned k = 0; k < count; k++)
                if (!strcmp(out[k].id, candidate.id)) { exists = true; break; }
            if (exists) continue;
            unsigned pos = 0;
            double d = dist2(lat, lon, &candidate);
            while (pos < count && dist2(lat, lon, &out[pos]) <= d) pos++;
            if (pos >= capacity) continue;
            if (count < capacity) count++;
            for (unsigned k = count - 1; k > pos; k--) out[k] = out[k - 1];
            out[pos] = candidate;
        }
    }
    json_decref(root);
    return count;
}
