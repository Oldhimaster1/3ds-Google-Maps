#include "gps.h"
#include "logging.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define GPS_HOST "172.20.10.1"
#define GPS_PORT 23
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
static char nmea_buffer[GPS_BUFFER_SIZE];
static int buffer_pos = 0;

// Parse NMEA coordinate format (DDMM.MMMM) to decimal degrees
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

// Parse a GPGGA or GPRMC sentence
static bool parse_nmea_sentence(const char* sentence) {
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    // Check for valid sentence types
    if (strncmp(buf, "$GPGGA", 6) == 0 || strncmp(buf, "$GNGGA", 6) == 0) {
        // $GPGGA,HHMMSS.ss,DDMM.MMMM,N,DDDMM.MMMM,E,Q,Sats,HDOP,Alt,M,...
        char* tokens[15];
        int token_count = 0;
        char* token = strtok(buf, ",");
        
        while (token && token_count < 15) {
            tokens[token_count++] = token;
            token = strtok(NULL, ",");
        }
        
        if (token_count >= 6) {
            // tokens[2] = latitude, tokens[3] = N/S
            // tokens[4] = longitude, tokens[5] = E/W
            // tokens[6] = fix quality (0 = no fix)
            
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
    else if (strncmp(buf, "$GPRMC", 6) == 0 || strncmp(buf, "$GNRMC", 6) == 0) {
        // $GPRMC,HHMMSS.ss,A,DDMM.MMMM,N,DDDMM.MMMM,E,Speed,Course,DDMMYY,...
        char* tokens[15];
        int token_count = 0;
        char* token = strtok(buf, ",");
        
        while (token && token_count < 15) {
            tokens[token_count++] = token;
            token = strtok(NULL, ",");
        }
        
        if (token_count >= 7) {
            // tokens[2] = status (A = valid, V = invalid)
            // tokens[3] = latitude, tokens[4] = N/S
            // tokens[5] = longitude, tokens[6] = E/W
            
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

bool gps_init(void) {
    if (gps_initialized) return true;
    
    printf("Connecting to GPS at %s:%d...\n", GPS_HOST, GPS_PORT);
    log_write("GPS: Connecting to %s:%d\n", GPS_HOST, GPS_PORT);
    
    // Initialize SOC service if not already done
    if (!soc_initialized) {
        soc_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
        if (soc_buffer == NULL) {
            printf("GPS: Failed to allocate SOC buffer\n");
            log_write("GPS: Failed to allocate SOC buffer\n");
            return false;
        }
        
        Result ret = socInit(soc_buffer, SOC_BUFFERSIZE);
        if (R_FAILED(ret)) {
            printf("GPS: socInit failed: 0x%08lX\n", ret);
            log_write("GPS: socInit failed: 0x%08lX\n", ret);
            free(soc_buffer);
            soc_buffer = NULL;
            return false;
        }
        soc_initialized = true;
        log_write("GPS: SOC initialized\n");
    }
    
    // Create socket
    gps_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (gps_socket < 0) {
        printf("GPS: Failed to create socket\n");
        log_write("GPS: Failed to create socket\n");
        return false;
    }
    
    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(GPS_PORT);
    
    if (inet_aton(GPS_HOST, &server_addr.sin_addr) == 0) {
        printf("GPS: Invalid address\n");
        log_write("GPS: Invalid address\n");
        close(gps_socket);
        gps_socket = -1;
        return false;
    }
    
    // Connect to GPS server
    if (connect(gps_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("GPS: Failed to connect: %d\n", errno);
        log_write("GPS: Failed to connect: errno=%d\n", errno);
        close(gps_socket);
        gps_socket = -1;
        return false;
    }
    
    // Set socket to non-blocking
    int flags = fcntl(gps_socket, F_GETFL, 0);
    fcntl(gps_socket, F_SETFL, flags | O_NONBLOCK);
    
    printf("GPS: Connected!\n");
    log_write("GPS: Connected successfully!\n");
    gps_initialized = true;
    buffer_pos = 0;
    
    return true;
}

void gps_cleanup(void) {
    if (gps_socket >= 0) {
        close(gps_socket);
        gps_socket = -1;
    }
    gps_initialized = false;
    has_fix = false;
    
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
    if (!gps_initialized || gps_socket < 0) return;
    
    // Read available data from socket
    char temp[128];
    ssize_t bytes_read = recv(gps_socket, temp, sizeof(temp) - 1, 0);
    
    if (bytes_read > 0) {
        temp[bytes_read] = '\0';
        
        // Append to buffer and process complete sentences
        for (int i = 0; i < bytes_read; i++) {
            char c = temp[i];
            
            if (c == '\n' || c == '\r') {
                if (buffer_pos > 0) {
                    nmea_buffer[buffer_pos] = '\0';
                    if (parse_nmea_sentence(nmea_buffer)) {
                        log_write("GPS fix: %.6f, %.6f\n", current_lat, current_lon);
                    }
                    buffer_pos = 0;
                }
            } else if (buffer_pos < GPS_BUFFER_SIZE - 1) {
                nmea_buffer[buffer_pos++] = c;
            }
        }
    }
    else if (bytes_read == 0) {
        // Connection closed
        printf("GPS: Connection closed\n");
        log_write("GPS: Connection closed by server\n");
        gps_cleanup();
    }
    // bytes_read < 0 with EAGAIN/EWOULDBLOCK is normal for non-blocking
}

bool gps_has_fix(void) {
    return has_fix;
}

bool gps_get_position(double* lat, double* lon) {
    if (!has_fix) return false;
    
    if (lat) *lat = current_lat;
    if (lon) *lon = current_lon;
    
    return true;
}

bool gps_is_connected(void) {
    return gps_initialized && gps_socket >= 0;
}
