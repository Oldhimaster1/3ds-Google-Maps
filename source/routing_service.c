#include "routing_service.h"

#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool json_coord(json_t *value, double limit, double *out) {
    double number;
    if (json_is_number(value)) {
        number = json_number_value(value);
    } else if (json_is_string(value)) {
        const char *text = json_string_value(value);
        char *end;
        number = strtod(text, &end);
        if (end == text || *end) return false;
    } else {
        return false;
    }
    if (!isfinite(number) || fabs(number) > limit) return false;
    *out = number;
    return true;
}

static bool parse_polyline(json_t *root, RouteCoordinate **out_points, size_t *out_count) {
    const char *poly = json_string_value(json_object_get(root, "poly"));
    if (!poly || !*poly) return false;

    size_t count = 1;
    for (const char *p = poly; *p; p++) {
        if (*p == ';' && p[1]) count++;
    }
    if (count < 2 || count > ROUTE_GEOMETRY_LIMIT) return false;

    RouteCoordinate *points = calloc(count, sizeof(*points));
    if (!points) return false;

    const char *cursor = poly;
    bool valid = true;
    for (size_t i = 0; i < count && valid; i++) {
        char *end;
        points[i].lat = strtod(cursor, &end);
        if (end == cursor || *end != ',') {
            valid = false;
            break;
        }
        cursor = end + 1;
        points[i].lon = strtod(cursor, &end);
        if (end == cursor || !isfinite(points[i].lat) || !isfinite(points[i].lon) ||
            fabs(points[i].lat) > 90 || fabs(points[i].lon) > 180) {
            valid = false;
            break;
        }
        if (i + 1 < count) {
            if (*end != ';') valid = false;
            else cursor = end + 1;
        } else if (*end) {
            valid = false;
        }
    }

    if (!valid) {
        free(points);
        return false;
    }
    *out_points = points;
    *out_count = count;
    return true;
}

static bool parse_geojson(json_t *route, RouteCoordinate **out_points, size_t *out_count) {
    json_t *geometry = json_object_get(route, "geometry");
    json_t *coordinates = json_is_object(geometry) ?
                          json_object_get(geometry, "coordinates") : NULL;
    if (!json_is_array(coordinates)) return false;

    size_t count = json_array_size(coordinates);
    if (count < 2 || count > ROUTE_GEOMETRY_LIMIT) return false;

    RouteCoordinate *points = calloc(count, sizeof(*points));
    if (!points) return false;
    for (size_t i = 0; i < count; i++) {
        json_t *pair = json_array_get(coordinates, i);
        if (!json_is_array(pair) || json_array_size(pair) < 2 ||
            !json_coord(json_array_get(pair, 0), 180, &points[i].lon) ||
            !json_coord(json_array_get(pair, 1), 90, &points[i].lat)) {
            free(points);
            return false;
        }
    }
    *out_points = points;
    *out_count = count;
    return true;
}

void route_plan_reset(RoutePlan *plan) {
    if (!plan) return;
    free(plan->geometry);
    memset(plan, 0, sizeof(*plan));
}

bool route_plan_parse_json(const char *json, size_t size, RoutePlan *plan) {
    if (!json || !size || !plan) return false;

    json_error_t error;
    json_t *root = json_loadb(json, size, JSON_REJECT_DUPLICATES, &error);
    if (!root || !json_is_object(root)) {
        if (root) json_decref(root);
        return false;
    }

    RoutePlan parsed = {0};
    json_t *routes = json_object_get(root, "routes");
    json_t *route = json_is_array(routes) ? json_array_get(routes, 0) : NULL;
    const char *poly = json_string_value(json_object_get(root, "poly"));
    bool valid = false;

    if (poly) {
        valid = parse_polyline(root, &parsed.geometry, &parsed.geometry_count);
    } else if (json_is_object(route)) {
        valid = parse_geojson(route, &parsed.geometry, &parsed.geometry_count);
    }
    if (!valid) {
        json_decref(root);
        return false;
    }

    json_t *metrics = poly ? root : route;
    const char *distance_key = poly ? "distance_m" : "distance";
    const char *duration_key = poly ? "duration_s" : "duration";
    json_t *distance_value = json_object_get(metrics, distance_key);
    json_t *duration_value = json_object_get(metrics, duration_key);
    if (!json_is_number(distance_value) || !json_is_number(duration_value)) {
        route_plan_reset(&parsed);
        json_decref(root);
        return false;
    }
    parsed.distance_m = json_number_value(distance_value);
    parsed.duration_s = json_number_value(duration_value);
    if (!isfinite(parsed.distance_m) || parsed.distance_m < 0 ||
        !isfinite(parsed.duration_s) || parsed.duration_s < 0) {
        route_plan_reset(&parsed);
        json_decref(root);
        return false;
    }

    const char *summary = json_string_value(json_object_get(root, "next"));
    if (!summary && json_is_object(route)) {
        json_t *legs = json_object_get(route, "legs");
        json_t *leg = json_is_array(legs) ? json_array_get(legs, 0) : NULL;
        json_t *steps = json_is_object(leg) ? json_object_get(leg, "steps") : NULL;
        json_t *step = json_is_array(steps) ? json_array_get(steps, 0) : NULL;
        if (json_is_object(step)) summary = json_string_value(json_object_get(step, "name"));
    }
    snprintf(parsed.summary, sizeof(parsed.summary), "%s",
             summary && *summary ? summary : "Route");

    route_plan_reset(plan);
    *plan = parsed;
    json_decref(root);
    return true;
}
