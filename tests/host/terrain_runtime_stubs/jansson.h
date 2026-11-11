#ifndef MOCK_JANSSON_H
#define MOCK_JANSSON_H
#include <stddef.h>
typedef struct json_t json_t;
typedef struct {int line;} json_error_t;
json_t *json_loadb(const char *,size_t,size_t,json_error_t *);
json_t *json_object_get(const json_t *,const char *);
int json_is_array(const json_t *);
size_t json_array_size(const json_t *);
json_t *json_array_get(const json_t *,size_t);
int json_is_number(const json_t *);
double json_number_value(const json_t *);
const char *json_string_value(const json_t *);
void json_decref(json_t *);
#define json_array_foreach(a,i,v) for((i)=0;(i)<json_array_size(a) && (((v)=json_array_get((a),(i))),1);(i)++)
#endif
