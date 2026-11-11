#ifndef GPS_PAIRING_H
#define GPS_PAIRING_H
#include <stdbool.h>
void gps_pairing_token(const unsigned char entropy[16], char out[33]);
/* Exact, case-insensitive HTTP header name; exactly one 128-bit bearer token. */
bool gps_pairing_authorized(const char *headers, const char token[33]);
#endif
