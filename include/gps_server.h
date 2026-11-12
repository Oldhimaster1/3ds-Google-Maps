#ifndef GPS_SERVER_H
#define GPS_SERVER_H

/*
 * gps_server.h  —  HTTPS GPS bridge for Nintendo 3DS
 *
 * The 3DS hosts a tiny HTTPS server.  A phone browser navigates to
 * https://<3DS-IP>:8443. The per-session self-signed certificate is NOT
 * automatically trusted by Safari. Browser pairing remains experimental until
 * the complete geolocation and delivery flow is verified on the target phone.
 * The page uses navigator.geolocation.watchPosition() and POST /location.
 *
 * The server runs on a background libctru thread so the render loop is never
 * blocked.  GPS fixes are written atomically to the shared GPS state
 * in gps.c via gps_inject_fix().
 *
 * Usage (called automatically through gps.c when GPS_SOURCE_HTTPS_SERVER):
 *   gps_server_start(8443)   — start listening (called by gps_init())
 *   gps_server_stop()        — shutdown        (called by gps_cleanup())
 *   gps_server_update()      — no-op (threaded) (called by gps_update())
 *   gps_server_is_running()  — true after successful start
 *   gps_server_get_url()     — private QR URL with a per-session fragment token
 *   gps_server_get_fix_count() — how many /location POSTs arrived
 */

#include <stdbool.h>
#include <stddef.h>

/* Start the HTTPS server on the given port.  Spawns a background thread.
 * Returns true on success (socket bound, thread started). */
bool gps_server_start(int port);

/* Stop the server, join the background thread, free all resources. */
void gps_server_stop(void);

/* True while the background server thread is alive and accepting. */
bool gps_server_is_running(void);

/* No-op — kept for API symmetry; the server is fully threaded. */
void gps_server_update(void);

/* Fill buf with the URL the phone should visit, with a private fragment token; never log or share it.
 * Writes an empty string if the server is not running. */
void gps_server_get_url(char *buf, size_t len);

/* Number of successful GPS fix POSTs received since the server started. */
unsigned int gps_server_get_fix_count(void);

#endif /* GPS_SERVER_H */
