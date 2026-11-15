#ifndef MAP_ROUTING_SERVICE_H
#define MAP_ROUTING_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#define ROUTE_GEOMETRY_LIMIT 16000

typedef struct {
    double lat;
    double lon;
} RouteCoordinate;

typedef struct {
    RouteCoordinate *geometry;
    size_t geometry_count;
    double distance_m;
    double duration_s;
    char summary[80];
} RoutePlan;

void route_plan_reset(RoutePlan *plan);
bool route_plan_parse_json(const char *json, size_t size, RoutePlan *plan);

#endif
