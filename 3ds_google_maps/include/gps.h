#ifndef GPS_H
#define GPS_H

#include <stdbool.h>

// Initialize GPS connection to GPS2IP server
bool gps_init(void);

// Cleanup GPS connection
void gps_cleanup(void);

// Update GPS data (call every frame)
void gps_update(void);

// Check if we have a valid GPS fix
bool gps_has_fix(void);

// Get current position (returns false if no fix)
bool gps_get_position(double* lat, double* lon);

// Check if GPS is connected
bool gps_is_connected(void);

#endif // GPS_H
