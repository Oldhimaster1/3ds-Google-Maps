#include "routing_service.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_direct_response(void) {
    const char json[] =
        "{\"routes\":[{\"distance\":1234.5,\"duration\":321,"
        "\"geometry\":{\"coordinates\":[[-74.0,40.0],[-73.99,40.01]]},"
        "\"legs\":[{\"steps\":[{\"name\":\"Main Street\"}]}]}]}";
    RoutePlan plan = {0};
    assert(route_plan_parse_json(json, strlen(json), &plan));
    assert(plan.geometry_count == 2);
    assert(fabs(plan.geometry[0].lat - 40.0) < 1e-9);
    assert(fabs(plan.geometry[1].lon + 73.99) < 1e-9);
    assert(fabs(plan.distance_m - 1234.5) < 1e-9);
    assert(fabs(plan.duration_s - 321.0) < 1e-9);
    assert(strcmp(plan.summary, "Main Street") == 0);
    route_plan_reset(&plan);
}

static void test_proxy_response(void) {
    const char json[] =
        "{\"poly\":\"40.0,-74.0;40.01,-73.99\","
        "\"distance_m\":2000,\"duration_s\":600,\"next\":\"Proxy route\"}";
    RoutePlan plan = {0};
    assert(route_plan_parse_json(json, strlen(json), &plan));
    assert(plan.geometry_count == 2);
    assert(fabs(plan.geometry[1].lat - 40.01) < 1e-9);
    assert(strcmp(plan.summary, "Proxy route") == 0);
    route_plan_reset(&plan);
}

static void test_invalid_responses(void) {
    const char *invalid[] = {
        "",
        "{\"routes\":[]}",
        "{\"routes\":[{\"geometry\":{\"coordinates\":[]},\"distance\":1,\"duration\":1}]}",
        "{\"routes\":[{\"geometry\":{\"coordinates\":[[-74,40],[\"bad\",41]]},\"distance\":1,\"duration\":1}]}",
        "{\"poly\":\"40,-74;40.1,-73.9\",\"distance_m\":-1,\"duration_s\":1}",
        "{\"poly\":\"40,-74;40.1,-73.9\",\"distance_m\":1,\"duration_s\":null}"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        RoutePlan plan = {0};
        assert(!route_plan_parse_json(invalid[i], strlen(invalid[i]), &plan));
        route_plan_reset(&plan);
    }
}

static void test_failed_replace_preserves_plan(void) {
    const char valid[] =
        "{\"poly\":\"40.0,-74.0;40.01,-73.99\","
        "\"distance_m\":2000,\"duration_s\":600}";
    RoutePlan plan = {0};
    assert(route_plan_parse_json(valid, strlen(valid), &plan));
    RouteCoordinate *geometry = plan.geometry;
    const char invalid[] = "{\"routes\":[]}";
    assert(!route_plan_parse_json(invalid, strlen(invalid), &plan));
    assert(plan.geometry == geometry);
    assert(plan.geometry_count == 2);
    route_plan_reset(&plan);
}

int main(void) {
    test_direct_response();
    test_proxy_response();
    test_invalid_responses();
    test_failed_replace_preserves_plan();
    puts("Routing service parsing and ownership tests passed");
    return 0;
}
