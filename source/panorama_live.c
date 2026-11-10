#include "panorama_live.h"
#include <jansson.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool valid_uuid(const char *s) {
    if (!s || strlen(s) != 36) return false;
    for (unsigned i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

static bool url_id(const char *id, const char *suffix, char *out, size_t capacity) {
    if (!valid_uuid(id) || !out || capacity == 0) return false;
    int n = snprintf(out, capacity, PANO_API_BASE "/pictures/%s/%s", id, suffix);
    return n > 0 && (size_t)n < capacity;
}

bool pano_image_url(const char *id, char *out, size_t capacity) {
    return url_id(id, "sd.jpg", out, capacity);
}

bool pano_metadata_url(const char *id, char *out, size_t capacity) {
    if (!valid_uuid(id) || !out || !capacity) return false;
    int n = snprintf(out, capacity, PANO_API_BASE "/pictures/%s", id);
    return n > 0 && (size_t)n < capacity;
}

bool pano_search_url(double lat, double lon, double radius, char *out, size_t capacity) {
    if (!out || !capacity || !isfinite(lat) || !isfinite(lon) ||
        lat < -89 || lat > 89 || lon < -179.5 || lon > 179.5 ||
        radius <= 0 || radius > 0.1 ||
        lat - radius < -90 || lat + radius > 90 ||
        lon - radius < -180 || lon + radius > 180)
        return false;
    int n = snprintf(out, capacity, PANO_API_BASE
        "/search?bbox=%.6f,%.6f,%.6f,%.6f&limit=24",
        lon - radius, lat - radius, lon + radius, lat + radius);
    return n > 0 && (size_t)n < capacity;
}

static void copy_string(char *out, size_t cap, const char *src, const char *fallback) {
    if (!src || !*src) src = fallback;
    if (!src) src = "";
    if (cap) snprintf(out, cap, "%s", src);
}

static bool parse_picture(json_t *feature, PanoLiveImage *out) {
    if (!json_is_object(feature) || !out) return false;
    const char *id = json_string_value(json_object_get(feature, "id"));
    json_t *geo = json_object_get(feature, "geometry");
    json_t *coord = json_object_get(geo, "coordinates");
    json_t *props = json_object_get(feature, "properties");
    json_t *lens = json_object_get(props, "pers:interior_orientation");
    json_t *fov = json_object_get(lens, "field_of_view");
    /* Panoramax includes normal photographs too: don't wrap them on a sphere. */
    if (!valid_uuid(id) || !json_is_array(coord) || json_array_size(coord) < 2 ||
        !json_is_number(json_array_get(coord, 0)) ||
        !json_is_number(json_array_get(coord, 1)) ||
        !json_is_number(fov) || json_number_value(fov) < 340.0)
        return false;
    double lon = json_number_value(json_array_get(coord, 0));
    double lat = json_number_value(json_array_get(coord, 1));
    if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180)
        return false;

    memset(out, 0, sizeof(*out));
    out->provider = STREET_PROVIDER_PANORAMAX;
    copy_string(out->id, sizeof(out->id), id, "");
    pano_image_url(id, out->image_url, sizeof(out->image_url));
    snprintf(out->preview_url, sizeof(out->preview_url),
             PANO_API_BASE "/pictures/%s/thumb.jpg", id);
    out->lat = lat;
    out->lon = lon;
    json_t *az = json_object_get(props, "view:azimuth");
    if (json_is_number(az)) out->azimuth = (float)json_number_value(az);
    copy_string(out->license, sizeof(out->license),
        json_string_value(json_object_get(props, "license")), "Check source license");
    json_t *producer = json_object_get(props, "geovisio:producer");
    const char *credit = json_string_value(producer);
    if (!credit) credit = json_string_value(json_object_get(producer, "name"));
    if (!credit) credit = json_string_value(json_object_get(props, "author"));
    if (!credit) credit = json_string_value(json_object_get(props, "copyright"));
    json_t *providers = json_object_get(props, "providers");
    if (!credit && json_is_array(providers)) {
        json_t *provider = json_array_get(providers, 0);
        credit = json_string_value(json_object_get(provider, "name"));
        if (!credit) credit = json_string_value(json_object_get(provider, "label"));
    }
    copy_string(out->credit, sizeof(out->credit), credit, "Panoramax contributor");

    json_t *links = json_object_get(feature, "links");
    if (json_is_array(links)) {
        size_t i; json_t *link;
        json_array_foreach(links, i, link) {
            const char *rel = json_string_value(json_object_get(link, "rel"));
            const char *target = json_string_value(json_object_get(link, "id"));
            if (!rel || !valid_uuid(target)) continue;
            if (!strcmp(rel, "next"))
                copy_string(out->next_id, sizeof(out->next_id), target, "");
            if (!strcmp(rel, "prev"))
                copy_string(out->prev_id, sizeof(out->prev_id), target, "");
        }
    }
    return true;
}

bool pano_parse_image(const char *json, size_t length, PanoLiveImage *out) {
    if (!json || !out || length > 512 * 1024) return false;
    json_t *root = json_loadb(json, length, JSON_REJECT_DUPLICATES, NULL);
    if (!root) return false;
    bool ok = parse_picture(root, out);
    json_decref(root);
    return ok;
}

static double dist2(double lat, double lon, const PanoLiveImage *p) {
    const double rad = 0.017453292519943295;
    double dx = (p->lon - lon) * cos(lat * rad);
    double dy = p->lat - lat;
    return dx * dx + dy * dy;
}

unsigned pano_parse_nearby(const char *json, size_t length,
                           double lat, double lon,
                           PanoLiveImage *items, unsigned capacity) {
    if (!json || !items || !capacity || length > 512 * 1024 ||
        !isfinite(lat) || !isfinite(lon)) return 0;
    if (capacity > PANO_NEARBY_MAX) capacity = PANO_NEARBY_MAX;
    json_t *root = json_loadb(json, length, JSON_REJECT_DUPLICATES, NULL);
    if (!root) return 0;
    json_t *features = json_object_get(root, "features");
    unsigned count = 0;
    if (json_is_array(features)) {
        size_t i; json_t *feature;
        json_array_foreach(features, i, feature) {
            PanoLiveImage sample;
            if (!parse_picture(feature, &sample)) continue;
            unsigned pos = 0;
            double d = dist2(lat, lon, &sample);
            while (pos < count && dist2(lat, lon, &items[pos]) <= d) pos++;
            if (pos >= capacity) continue;
            if (count < capacity) count++;
            for (unsigned move = count - 1; move > pos; move--)
                items[move] = items[move - 1];
            items[pos] = sample;
        }
    }
    json_decref(root);
    return count;
}

void pano_apply_gyro(float raw_pitch, float raw_yaw, float coefficient,
                     float dt, bool invert, float *pitch, float *yaw) {
    if (!pitch || !yaw || !isfinite(coefficient) || coefficient <= 0 ||
        !isfinite(dt) || dt <= 0 || dt > 0.1f) return;
    const float rad = 0.017453292519943295f;
    const float sign = invert ? -1.0f : 1.0f;
    float pitch_dps = raw_pitch * coefficient;
    float yaw_dps = raw_yaw * coefficient;
    if (fabsf(pitch_dps) > 2.0f) *pitch += sign * pitch_dps * rad * dt;
    if (fabsf(yaw_dps) > 2.0f) *yaw += sign * yaw_dps * rad * dt;
}
