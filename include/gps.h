#ifndef GPS_H
#define GPS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * GPS source mode.
 * GPS_SOURCE_NMEA_CLIENT  —  outbound TCP connection to a remote NMEA server
 *                            (GPS2IP on iOS, GPS Forwarder on Android, etc.)
 * GPS_SOURCE_HTTPS_SERVER —  3DS acts as an HTTPS server; a phone browser
 *                            visits https://<3DS-IP>:8443 and streams GPS
 *                            fixes back via POST /location.
 */
typedef enum {
    GPS_SOURCE_NMEA_CLIENT  = 0,
    GPS_SOURCE_HTTPS_SERVER = 1
} GpsSourceMode;

/* Switch the source mode.  Only effective before calling gps_init(). */
void          gps_set_source_mode(GpsSourceMode mode);
GpsSourceMode gps_get_source_mode(void);

/* Connect/start the selected GPS source. */
bool gps_init(void);

/* Disconnect/stop and free all resources. */
void gps_cleanup(void);

/* Poll for NMEA data (client mode) or flush incoming fixes (server mode). */
void gps_update(void);

/* Whether we currently have a valid fix. */
bool gps_has_fix(void);

/* Retrieve last fix in decimal degrees (returns false if no fix). */
bool gps_get_position(double *lat, double *lon);

/*
 * Whether the GPS source is "active":
 *   NMEA_CLIENT  mode → TCP socket connected
 *   HTTPS_SERVER mode → server thread running
 */
bool gps_is_connected(void);

/*
 * Inject a GPS fix from an external source (called by gps_server.c).
 * Thread-safe: may be called from the server background thread.
 */
void gps_inject_fix(double lat, double lon);

/* Clear the current fix (sets has_fix to false). */
void gps_clear_fix(void);

/* NMEA client mode: hostname and port of the remote NMEA TCP server. */
const char *gps_get_nmea_host(void);
int         gps_get_nmea_port(void);

/*
 * HTTPS server mode helpers (thin wrappers around gps_server_*).
 * Fill buf with the URL to show the user, e.g. "https://172.20.10.2:8443".
 */
void         gps_get_server_url(char *buf, size_t len);
unsigned int gps_get_server_fix_count(void);

#endif /* GPS_H */
