#include "gps.h"
#include "gps_server.h"

#include <3ds.h>
#include <malloc.h>
#include <errno.h>
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

static int gps_socket = -1;
static bool gps_initialized = false;
static bool soc_initialized = false;
static u32* soc_buffer = NULL;

static double current_lat = 0.0;
static double current_lon = 0.0;
static bool has_fix = false;

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

static double parse_nmea_coord(const char* str, char direction) {
    if (!str || strlen(str) < 4) return 0.0;

    double raw = atof(str);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);
    double decimal = degrees + (minutes / 60.0);

    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

static bool parse_nmea_sentence(const char* sentence) {
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // GGA: $GPGGA / $GNGGA
    if (strncmp(buf, "$GPGGA", 6) == 0 || strncmp(buf, "$GNGGA", 6) == 0) {
        char* tokens[15];
        int token_count = 0;
        char* token = strtok(buf, ",");

        while (token && token_count < 15) {
            tokens[token_count++] = token;
            token = strtok(NULL, ",");
        }

        if (token_count >= 7) {
            // tokens[2] lat, [3] N/S, [4] lon, [5] E/W, [6] fix quality
            if (strlen(tokens[2]) > 0 && strlen(tokens[4]) > 0) {
                int fix_quality = atoi(tokens[6]);
                if (fix_quality > 0) {
                    current_lat = parse_nmea_coord(tokens[2], tokens[3][0]);
                    current_lon = parse_nmea_coord(tokens[4], tokens[5][0]);
                    has_fix = true;
                    return true;
                }
            }
        }
    }

    // RMC: $GPRMC / $GNRMC
    if (strncmp(buf, "$GPRMC", 6) == 0 || strncmp(buf, "$GNRMC", 6) == 0) {
        char* tokens[15];
        int token_count = 0;
        char* token = strtok(buf, ",");

        while (token && token_count < 15) {
            tokens[token_count++] = token;
            token = strtok(NULL, ",");
        }

        if (token_count >= 7) {
            // tokens[2] status A/V, [3] lat, [4] N/S, [5] lon, [6] E/W
            if (tokens[2][0] == 'A' && strlen(tokens[3]) > 0 && strlen(tokens[5]) > 0) {
                current_lat = parse_nmea_coord(tokens[3], tokens[4][0]);
                current_lon = parse_nmea_coord(tokens[5], tokens[6][0]);
                has_fix = true;
                return true;
            }
        }
    }

    return false;
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

void gps_inject_fix(double lat, double lon) {
    ensure_fix_lock();
    LightLock_Lock(&g_fix_lock);
    current_lat = lat;
    current_lon = lon;
    has_fix     = true;
    LightLock_Unlock(&g_fix_lock);
}

void gps_clear_fix(void) {
    ensure_fix_lock();
    LightLock_Lock(&g_fix_lock);
    has_fix = false;
    LightLock_Unlock(&g_fix_lock);
}

const char *gps_get_nmea_host(void) { return GPS_HOST; }
int         gps_get_nmea_port(void) { return GPS_PORT; }

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

    printf("Connecting to GPS at %s:%d...\n", GPS_HOST, GPS_PORT);

    if (!soc_initialized) {
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
    server_addr.sin_port = htons(GPS_PORT);

    if (inet_aton(GPS_HOST, &server_addr.sin_addr) == 0) {
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
    has_fix = false;
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
                if (buffer_pos > 0) {
                    nmea_buffer[buffer_pos] = '\0';
                    (void)parse_nmea_sentence(nmea_buffer);
                    buffer_pos = 0;
                }
            } else if (buffer_pos < GPS_BUFFER_SIZE - 1) {
                nmea_buffer[buffer_pos++] = c;
            }
        }
    } else if (bytes_read == 0) {
        printf("GPS: Connection closed\n");
        gps_cleanup();
    }
    // bytes_read < 0 with EAGAIN/EWOULDBLOCK is normal for non-blocking sockets
}

bool gps_has_fix(void) {
    return has_fix;
}

bool gps_get_position(double *lat, double *lon) {
    if (!has_fix) return false;
    ensure_fix_lock();
    LightLock_Lock(&g_fix_lock);
    if (lat) *lat = current_lat;
    if (lon) *lon = current_lon;
    LightLock_Unlock(&g_fix_lock);
    return true;
}

bool gps_is_connected(void) {
    if (g_source_mode == GPS_SOURCE_HTTPS_SERVER) {
        return gps_server_is_running();
    }
    return gps_initialized && gps_socket >= 0;
}
