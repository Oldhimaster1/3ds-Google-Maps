#include "gps.h"
#include "gps_motion.h"
#include "gps_server.h"
#include "network.h"

#include <3ds.h>
#include <malloc.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/* GPS source: TCP NMEA stream (e.g., GPS2IP). Default matches your setup.
 * Change GPS_HOST to the IP your phone uses when it creates a WiFi hotspot. */
#define GPS_HOST "172.20.10.1"
#define GPS_PORT 23

static GpsSourceMode g_source_mode = GPS_SOURCE_NMEA_CLIENT;

#define GPS_CONNECT_TIMEOUT_MS 3000

#define GPS_BUFFER_SIZE 512
#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

static char gps_host[16] = GPS_HOST;
static int gps_port = GPS_PORT;

static int gps_socket = -1;
static bool gps_initialized = false;
static bool soc_initialized = false;
static u32* soc_buffer = NULL;

static double current_lat = 0.0;
static double current_lon = 0.0;
static bool has_fix = false;
static u64 fix_time;
static double fix_accuracy = -1;
static GpsMotion gps_motion;
static bool discard_sentence;

/* LightLock protects current_lat / current_lon / has_fix so the HTTPS server
 * background thread can safely call gps_inject_fix() concurrently. */
static LightLock g_fix_lock;
static bool g_fix_lock_init = false;

static void ensure_fix_lock(void) {
    if (!g_fix_lock_init) {
        LightLock_Init(&g_fix_lock);
        g_fix_lock_init = true;
    }
}

static char nmea_buffer[GPS_BUFFER_SIZE];
static int buffer_pos = 0;

static bool parse_nmea_coord(const char *field, const char *direction,
                             char positive, char negative, double limit, double *out) {
    if (!field[0] || !direction[0] || direction[1] ||
        (direction[0] != positive && direction[0] != negative)) return false;
    char *end;
    double raw = strtod(field, &end);
    if (end == field || *end || !isfinite(raw) || raw < 0 || raw > limit * 100) return false;
    double degrees = floor(raw / 100);
    double minutes = raw - degrees * 100;
    if (minutes >= 60) return false;
    *out = (degrees + minutes / 60) * (direction[0] == negative ? -1 : 1);
    return fabs(*out) <= limit;
}

static bool parse_nmea_sentence(char *sentence) {
    if (sentence[0] != '$') return false;
    char *checksum = strchr(sentence, '*');
    if (checksum) {
        unsigned value = 0;
        for (char *p = sentence + 1; p < checksum; p++) value ^= (unsigned char)*p;
        char *end;
        unsigned long expected = strtoul(checksum + 1, &end, 16);
        if (end != checksum + 3 || *end || expected != value) return false;
        *checksum = 0;
    }

    char *fields[7];
    for (int i = 0; i < 7; i++) {
        fields[i] = sentence;
        char *comma = strchr(sentence, ',');
        if (!comma) {
            if (i != 6) return false;
            break;
        }
        *comma = 0;
        sentence = comma + 1;
    }

    if (strlen(fields[0]) != 6) return false;
    bool gga = strcmp(fields[0] + 3, "GGA") == 0;
    bool rmc = strcmp(fields[0] + 3, "RMC") == 0;
    if ((!gga && !rmc) || (gga && atoi(fields[6]) <= 0) ||
        (rmc && strcmp(fields[2], "A") != 0)) return false;
    int i = gga ? 2 : 3;
    double lat, lon;
    if (!parse_nmea_coord(fields[i], fields[i+1], 'N', 'S', 90, &lat) ||
        !parse_nmea_coord(fields[i+2], fields[i+3], 'E', 'W', 180, &lon)) return false;
    gps_inject_fix(lat, lon);
    return true;
}

/* =========================================================================
 * Source-mode API
 * ========================================================================= */

void gps_set_source_mode(GpsSourceMode mode) {
    g_source_mode = mode;
}

GpsSourceMode gps_get_source_mode(void) {
    return g_source_mode;
}

void gps_inject_fix_with_motion(double lat, double lon, double accuracy, double speed, double heading) {
    ensure_fix_lock();
    LightLock_Lock(&g_fix_lock);
    if (!isfinite(lat) || !isfinite(lon) || fabs(lat) > 90 || fabs(lon) > 180) {
        LightLock_Unlock(&g_fix_lock);
        return;
    }
    fix_time = osGetTime();
    fix_accuracy = isfinite(accuracy) && accuracy >= 0 ? accuracy : -1;
    current_lat = lat;
    current_lon = lon;
    gps_motion_update(&gps_motion,lat,lon,
         fix_accuracy>=0?fix_accuracy:8.0,speed,heading,fix_time);
    has_fix     = true;
    LightLock_Unlock(&g_fix_lock);
}

void gps_inject_fix_with_accuracy(double lat, double lon, double accuracy) {
    gps_inject_fix_with_motion(lat,lon,accuracy,NAN,NAN);
}
void gps_inject_fix(double lat, double lon) {
    gps_inject_fix_with_accuracy(lat, lon, -1);
}

void gps_clear_fix(void) {
    ensure_fix_lock();
    LightLock_Lock(&g_fix_lock);
    has_fix = false;
    gps_motion_reset(&gps_motion);
    LightLock_Unlock(&g_fix_lock);
}

const char *gps_get_nmea_host(void) { return gps_host; }
int         gps_get_nmea_port(void) { return gps_port; }

void gps_get_server_url(char *buf, size_t len) {
    gps_server_get_url(buf, len);
}

unsigned int gps_get_server_fix_count(void) {
    return gps_server_get_fix_count();
}

/* =========================================================================
 * Core GPS API  — dispatches on g_source_mode
 * ========================================================================= */

bool gps_init(void) {
    ensure_fix_lock();

    /* ---- HTTPS server mode -------------------------------------------- */
    if (g_source_mode == GPS_SOURCE_HTTPS_SERVER) {
        return gps_server_start(8443);
    }

    /* ---- NMEA TCP client mode (original behaviour) --------------------- */
    if (gps_initialized) return true;

    snprintf(gps_host, sizeof(gps_host), "%s", GPS_HOST);
    gps_port = GPS_PORT;
    FILE *config = fopen("sdmc:/3ds_google_maps/gps.cfg", "r");
    if (config) {
        char host[16];
        unsigned port;
        struct in_addr address;
        if (fscanf(config, "%15s %u", host, &port) == 2 &&
            port > 0 && port <= 65535 && inet_aton(host, &address)) {
            snprintf(gps_host, sizeof(gps_host), "%s", host);
            gps_port = (int)port;
        }
        fclose(config);
    }
    printf("Connecting to GPS at %s:%d...\n", gps_host, gps_port);

    if (!soc_initialized && !network_socket_service_ready()) {
        soc_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
        if (!soc_buffer) {
            printf("GPS: Failed to allocate SOC buffer\n");
            return false;
        }

        Result ret = socInit(soc_buffer, SOC_BUFFERSIZE);
        if (R_FAILED(ret)) {
            printf("GPS: socInit failed: 0x%08lX\n", ret);
            free(soc_buffer);
            soc_buffer = NULL;
            return false;
        }

        soc_initialized = true;
    }

    gps_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (gps_socket < 0) {
        printf("GPS: Failed to create socket\n");
        return false;
    }

    // Non-blocking connect with an explicit timeout to avoid hanging the UI.
    int flags = fcntl(gps_socket, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(gps_socket, F_SETFL, flags | O_NONBLOCK);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(gps_port);

    if (inet_aton(gps_host, &server_addr.sin_addr) == 0) {
        printf("GPS: Invalid address\n");
        close(gps_socket);
        gps_socket = -1;
        return false;
    }

    int connect_rc = connect(gps_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (connect_rc < 0 && errno != EINPROGRESS) {
        printf("GPS: Failed to connect: errno=%d\n", errno);
        close(gps_socket);
        gps_socket = -1;
        return false;
    }

    if (connect_rc < 0 && errno == EINPROGRESS) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(gps_socket, &wfds);

        struct timeval tv;
        tv.tv_sec = GPS_CONNECT_TIMEOUT_MS / 1000;
        tv.tv_usec = (GPS_CONNECT_TIMEOUT_MS % 1000) * 1000;

        int sel = select(gps_socket + 1, NULL, &wfds, NULL, &tv);
        if (sel <= 0) {
            if (sel == 0) {
                printf("GPS: Connect timed out (%dms)\n", GPS_CONNECT_TIMEOUT_MS);
            } else {
                printf("GPS: Connect select() failed: errno=%d\n", errno);
            }
            close(gps_socket);
            gps_socket = -1;
            return false;
        }

        int so_error = 0;
        socklen_t so_error_len = sizeof(so_error);
        if (getsockopt(gps_socket, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0 || so_error != 0) {
            printf("GPS: Connect failed: errno=%d\n", (so_error != 0) ? so_error : errno);
            close(gps_socket);
            gps_socket = -1;
            return false;
        }
    }

    printf("GPS: Connected!\n");
    gps_initialized = true;
    buffer_pos = 0;
    discard_sentence = false;
    gps_clear_fix();
    return true;
}

void gps_cleanup(void) {
    /* ---- HTTPS server mode -------------------------------------------- */
    if (g_source_mode == GPS_SOURCE_HTTPS_SERVER) {
        gps_server_stop();
        gps_clear_fix();
        return;
    }

    /* ---- NMEA TCP client mode ----------------------------------------- */
    if (gps_socket >= 0) {
        close(gps_socket);
        gps_socket = -1;
    }
    gps_initialized = false;
    gps_clear_fix();

    if (soc_initialized) {
        socExit();
        soc_initialized = false;
    }
    if (soc_buffer) {
        free(soc_buffer);
        soc_buffer = NULL;
    }
}

void gps_update(void) {
    /* ---- HTTPS server mode: server thread handles everything ----------- */
    if (g_source_mode == GPS_SOURCE_HTTPS_SERVER) {
        gps_server_update(); /* no-op */
        return;
    }

    /* ---- NMEA TCP client mode ----------------------------------------- */
    if (!gps_initialized || gps_socket < 0) return;

    char temp[128];
    ssize_t bytes_read = recv(gps_socket, temp, sizeof(temp) - 1, 0);

    if (bytes_read > 0) {
        temp[bytes_read] = '\0';

        for (int i = 0; i < bytes_read; i++) {
            char c = temp[i];

            if (c == '\n' || c == '\r') {
                if (buffer_pos > 0 && !discard_sentence) {
                    nmea_buffer[buffer_pos] = '\0';
                    (void)parse_nmea_sentence(nmea_buffer);
                }
                buffer_pos = 0;
                discard_sentence = false;
            } else if (buffer_pos < GPS_BUFFER_SIZE - 1) {
                nmea_buffer[buffer_pos++] = c;
            } else {
                discard_sentence = true;
            }
        }
    } else if (bytes_read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        printf("GPS: Connection closed\n");
        gps_cleanup();
    }
    // bytes_read < 0 with EAGAIN/EWOULDBLOCK is normal for non-blocking sockets
}

bool gps_has_fix(void) {
    return gps_get_position(NULL, NULL);
}

bool gps_get_fix(GpsFix *fix) {
    ensure_fix_lock();
    LightLock_Lock(&g_fix_lock);
    u64 age = osGetTime() - fix_time;
    bool valid = has_fix && age <= 10000;
    if (valid && fix) {
        *fix = (GpsFix){ current_lat, current_lon, fix_accuracy, (unsigned)age,
               gps_motion.speed_mps, gps_motion.heading_deg,
               gps_motion.has_speed,gps_motion.has_heading };
    }
    LightLock_Unlock(&g_fix_lock);
    return valid;
}

bool gps_get_position(double *lat, double *lon) {
    GpsFix fix;
    if (!gps_get_fix(&fix)) return false;
    if (lat) *lat = fix.lat;
    if (lon) *lon = fix.lon;
    return true;
}

bool gps_is_connected(void) {
    if (g_source_mode == GPS_SOURCE_HTTPS_SERVER) {
        return gps_server_is_running();
    }
    return gps_initialized && gps_socket >= 0;
}
