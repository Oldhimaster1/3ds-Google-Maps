#ifndef GPS_H
#define GPS_H

#include <stdbool.h>

// Connect to GPS2IP / NMEA TCP stream.
bool gps_init(void);

// Close connection and free resources.
void gps_cleanup(void);

// Poll socket and parse any received NMEA sentences.
void gps_update(void);

// Whether we currently have a valid fix.
bool gps_has_fix(void);

// Retrieve last fix in decimal degrees (returns false if no fix).
bool gps_get_position(double* lat, double* lon);

// Whether the TCP connection is currently established.
bool gps_is_connected(void);

#endif // GPS_H
