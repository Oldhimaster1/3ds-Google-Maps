#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "map_tiles.h"
#include "network.h"
#include "gps.h"

#define SCREEN_WIDTH_TOP    400
#define SCREEN_HEIGHT_TOP   240
#define SCREEN_WIDTH_BOTTOM 320
#define SCREEN_HEIGHT_BOTTOM 240

#define TILE_SIZE 256
#define DEFAULT_ZOOM 10
#define DEFAULT_LAT 40.7128  // New York City
#define DEFAULT_LON -74.0060

typedef struct {
    double lat;
    double lon;
    int zoom;
    int offset_x;
    int offset_y;
    bool dragging;
    int drag_start_x;
    int drag_start_y;
    bool gps_tracking;  // Follow GPS position
} MapState;

static MapState map_state;
static C3D_RenderTarget* top_screen;
static bool g_should_exit = false;

// Function prototypes
void init_graphics(void);
void cleanup_graphics(void);
void update_input(void);
void render_map(void);
void handle_touch_input(touchPosition* touch);
void zoom_in(void);
void zoom_out(void);
void pan_map(int dx, int dy);

static bool url_encode_component(const char* input, char* output, size_t output_size);
typedef struct {
    double lat;
    double lon;
    char name[96];
} GeocodeResult;

static int parse_geocode_results_json(const char* json, u32 json_size, GeocodeResult* out_results, int max_results);
static void open_address_search(void);
static void open_menu(void);

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    cfguInit();
    httpcInit(0);
    acInit();
    
    // Initialize console for debug output
    consoleInit(GFX_BOTTOM, NULL);
    
    // Initialize graphics
    init_graphics();
    
    // Initialize network
    printf("Initializing network...\n");
    if (!network_init()) {
        printf("Failed to initialize network!\n");
        printf("Please ensure WiFi is enabled and connected.\n");
        printf("Press START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            u32 kDown = hidKeysDown();
            if (kDown & KEY_START) break;
        }
        goto cleanup;
    }
    
    // Initialize map state
    map_state.lat = DEFAULT_LAT;
    map_state.lon = DEFAULT_LON;
    map_state.zoom = DEFAULT_ZOOM;
    map_state.offset_x = 0;
    map_state.offset_y = 0;
    map_state.dragging = false;
    map_state.gps_tracking = false;
    
    // Initialize map tiles system
    printf("Initializing map tiles...\n");
    if (!map_tiles_init()) {
        printf("Failed to initialize map tiles!\n");
        printf("Press START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            u32 kDown = hidKeysDown();
            if (kDown & KEY_START) break;
        }
        goto cleanup;
    }
    
    printf("3DS Google Maps v1.0\n");
    printf("Controls:\n");
    printf("D-Pad: Pan map\n");
    printf("X/Y: Zoom in/out\n");
    printf("SELECT: Search address\n");
    printf("R: Menu\n");
    printf("B: Connect to GPS\n");
    printf("A: Toggle GPS tracking\n");
    printf("Touch: Drag to pan\n");
    printf("START: Exit\n");
    printf("\nStarting at NYC (%.4f, %.4f)\n", DEFAULT_LAT, DEFAULT_LON);
    
    // Clear console after showing initial info
    svcSleepThread(2000000000LL); // 2 seconds
    consoleClear();
    
    // Main loop
    static int frame_counter = 0;
    while (aptMainLoop() && !g_should_exit) {
        frame_counter++;

        // Update GPS if connected
        gps_update();

        // If GPS tracking is on and we have a fix, update position
        if (map_state.gps_tracking && gps_has_fix()) {
            double gps_lat, gps_lon;
            if (gps_get_position(&gps_lat, &gps_lon)) {
                map_state.lat = gps_lat;
                map_state.lon = gps_lon;
            }
        }

        // Status every ~1s
        if (frame_counter % 60 == 0) {
            consoleClear();
            printf("=== 3DS Maps ===\n");
            printf("Pos: %.4f, %.4f\n", map_state.lat, map_state.lon);
            printf("Zoom: %d\n", map_state.zoom);
            printf("GPS: %s\n", gps_is_connected() ? "Connected" : "Not connected");
            if (gps_is_connected()) {
                printf("Fix: %s\n", gps_has_fix() ? "YES" : "No");
                printf("Tracking: %s\n", map_state.gps_tracking ? "ON" : "OFF");
            }
            printf("\nB=Connect GPS  A=Toggle Track\n");
            printf("X/Y=Zoom  D-Pad=Pan  START=Exit\n");
        }

        update_input();
        if (g_should_exit) break;

        render_map();
    }

cleanup:
    // Cleanup
    gps_cleanup();
    map_tiles_cleanup();
    network_cleanup();
    cleanup_graphics();
    httpcExit();
    acExit();
    cfguExit();
    gfxExit();
    
    return 0;
}

void init_graphics(void) {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    
    // Create render target for top screen only (bottom screen is used for console)
    top_screen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
}

void cleanup_graphics(void) {
    C2D_Fini();
    C3D_Fini();
}

void update_input(void) {
    hidScanInput();
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();
    
    // Exit on START
    if (kDown & KEY_START) {
        g_should_exit = true;
        return;
    }
    
    // Zoom controls (X = zoom in, Y = zoom out)
    if (kDown & KEY_X) {
        zoom_in();
    }
    if (kDown & KEY_Y) {
        zoom_out();
    }

    // Address search
    if (kDown & KEY_SELECT) {
        open_address_search();
    }

    // Menu
    if (kDown & KEY_R) {
        open_menu();
        return;
    }

    // GPS controls
    if (kDown & KEY_B) {
        if (!gps_is_connected()) {
            printf("Connecting to GPS...\n");
            if (gps_init()) {
                printf("GPS connected!\n");
            } else {
                printf("GPS connection failed!\n");
            }
        } else {
            printf("GPS already connected\n");
        }
    }
    if (kDown & KEY_A) {
        if (gps_is_connected()) {
            map_state.gps_tracking = !map_state.gps_tracking;
            printf("GPS tracking %s\n", map_state.gps_tracking ? "ON" : "OFF");
        } else {
            printf("Connect GPS first (press B)\n");
        }
    }
    
    // Pan controls with D-Pad (disable if GPS tracking)
    if (!map_state.gps_tracking) {
        int pan_speed = 10;
        if (kHeld & KEY_DUP) {
            pan_map(0, -pan_speed);
        }
        if (kHeld & KEY_DDOWN) {
            pan_map(0, pan_speed);
        }
        if (kHeld & KEY_DLEFT) {
            pan_map(-pan_speed, 0);
        }
        if (kHeld & KEY_DRIGHT) {
            pan_map(pan_speed, 0);
        }
    }
    
    // Touch input (disable if GPS tracking)
    if (!map_state.gps_tracking) {
        touchPosition touch;
        hidTouchRead(&touch);
        if (touch.px > 0 && touch.py > 0) {
            handle_touch_input(&touch);
        } else {
            // End dragging when touch is released
            if (map_state.dragging) {
                map_state.dragging = false;
            }
        }
    }
}

void handle_touch_input(touchPosition* touch) {
    if (!map_state.dragging) {
        // Start dragging
        map_state.dragging = true;
        map_state.drag_start_x = touch->px;
        map_state.drag_start_y = touch->py;
    } else {
        // Continue dragging
        int dx = touch->px - map_state.drag_start_x;
        int dy = touch->py - map_state.drag_start_y;
        
        if (abs(dx) > 2 || abs(dy) > 2) {
            pan_map(dx, dy);
            map_state.drag_start_x = touch->px;
            map_state.drag_start_y = touch->py;
        }
    }
}

void zoom_in(void) {
    if (map_state.zoom < 18) {
        map_state.zoom++;
        map_tiles_clear_cache();
    }
}

void zoom_out(void) {
    if (map_state.zoom > 1) {
        map_state.zoom--;
        map_tiles_clear_cache();
    }
}

void pan_map(int dx, int dy) {
    map_state.offset_x += dx;
    map_state.offset_y += dy;
    
    // Convert pixel offset to lat/lon offset
    double lat_per_pixel = 180.0 / (TILE_SIZE * pow(2, map_state.zoom));
    double lon_per_pixel = 360.0 / (TILE_SIZE * pow(2, map_state.zoom));
    
    map_state.lat -= dy * lat_per_pixel;
    map_state.lon += dx * lon_per_pixel;
    
    // Clamp latitude
    if (map_state.lat > 85.0511) map_state.lat = 85.0511;
    if (map_state.lat < -85.0511) map_state.lat = -85.0511;
    
    // Wrap longitude
    while (map_state.lon > 180.0) map_state.lon -= 360.0;
    while (map_state.lon < -180.0) map_state.lon += 360.0;
    
    // Reset offsets after converting to lat/lon
    map_state.offset_x = 0;
    map_state.offset_y = 0;
}

void render_map(void) {
    // Start rendering frame
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    // Render top screen
    C2D_TargetClear(top_screen, C2D_Color32f(0.7f, 0.8f, 1.0f, 1.0f)); // Sky blue background
    C2D_SceneBegin(top_screen);
    
    // Render map tiles for top screen
    map_tiles_render(map_state.lat, map_state.lon, map_state.zoom, 
                    map_state.offset_x, map_state.offset_y, 
                    SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP, true);
    
    // End frame rendering
    C3D_FrameEnd(0);
    
    // Show debug info on bottom screen using console (this should not affect top screen)
    printf("\x1b[0;0H3DS Google Maps v1.0");
    printf("\x1b[2;0HPosition: %.6f, %.6f", map_state.lat, map_state.lon);
    printf("\x1b[3;0HZoom: %d", map_state.zoom);
    printf("\x1b[4;0HOffset: %d, %d", map_state.offset_x, map_state.offset_y);
    printf("\x1b[6;0HControls:");
    printf("\x1b[7;0H  D-Pad: Pan map");
    printf("\x1b[8;0H  X/Y: Zoom in/out");
    printf("\x1b[9;0H  SELECT: Search address");
    printf("\x1b[10;0H  R: Menu");
    printf("\x1b[11;0H  B: Connect GPS");
    printf("\x1b[12;0H  A: Toggle GPS tracking");
    printf("\x1b[13;0H  Touch: Drag to pan");
    printf("\x1b[14;0H  START: Exit");
    
    printf("\x1b[16;0HMap Tiles:");
    printf("\x1b[17;0H  Colored boxes = downloaded tiles");
    printf("\x1b[18;0H  Patterns show tile coordinates");
    printf("\x1b[19;0H  Red crosshair = map center");
    
    if (map_state.dragging) {
        printf("\x1b[21;0HDragging...");
    } else {
        printf("\x1b[21;0H           ");
    }
    
    if (!is_connected()) {
        printf("\x1b[23;0HNo WiFi connection!");
    } else {
        printf("\x1b[23;0HWiFi connected     ");
    }
    
    // Add debug info about tile loading
    static int frame_count = 0;
    frame_count++;
    printf("\x1b[24;0HFrame: %d", frame_count);
}

static void open_menu(void) {
    while (aptMainLoop() && !g_should_exit) {
        consoleClear();
        printf("=== Menu ===\n\n");
        printf("A: Toggle SD tile cache\n");
        printf("B or R: Back\n");
        printf("START: Exit\n\n");

        printf("SD tile cache: %s\n", network_get_disk_cache_enabled() ? "ON" : "OFF");
        printf("(OFF = faster, but no saving/loading tiles)\n");

        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {
            g_should_exit = true;
            return;
        }
        if (kDown & KEY_B) {
            return;
        }
        if (kDown & KEY_R) {
            return;
        }
        if (kDown & KEY_A) {
            bool enabled = !network_get_disk_cache_enabled();
            network_set_disk_cache_enabled(enabled);
        }

        svcSleepThread(60000000LL); // ~60ms
    }
}

static bool url_encode_component(const char* input, char* output, size_t output_size) {
    if (!input || !output || output_size == 0) return false;

    size_t out = 0;
    for (size_t i = 0; input[i] != '\0'; i++) {
        unsigned char c = (unsigned char)input[i];
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';

        if (unreserved) {
            if (out + 1 >= output_size) return false;
            output[out++] = (char)c;
        } else if (c == ' ') {
            if (out + 3 >= output_size) return false;
            output[out++] = '%';
            output[out++] = '2';
            output[out++] = '0';
        } else {
            static const char* hex = "0123456789ABCDEF";
            if (out + 3 >= output_size) return false;
            output[out++] = '%';
            output[out++] = hex[(c >> 4) & 0xF];
            output[out++] = hex[c & 0xF];
        }
    }

    if (out >= output_size) return false;
    output[out] = '\0';
    return true;
}

static const char* skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static int parse_geocode_results_json(const char* json, u32 json_size, GeocodeResult* out_results, int max_results) {
    if (!json || json_size == 0 || !out_results || max_results <= 0) return 0;

    const char* end = json + json_size;
    const char* p = json;

    const char* results_key = strstr(p, "\"results\"");
    if (!results_key) return 0;
    p = results_key;

    // Move to start of array
    const char* array_start = strchr(p, '[');
    if (!array_start) return 0;
    p = array_start + 1;

    int count = 0;
    while (p < end && count < max_results) {
        const char* lat_key = strstr(p, "\"lat\"");
        if (!lat_key) break;

        const char* lon_key = strstr(lat_key, "\"lon\"");
        if (!lon_key) break;

        const char* name_key = strstr(lon_key, "\"name\"");
        if (!name_key) break;

        // Parse lat
        const char* q = strchr(lat_key, ':');
        if (!q) break;
        q++;
        q = skip_ws(q, end);
        if (q >= end) break;
        if (*q == '"') q++;
        char* lat_end = NULL;
        double lat = strtod(q, &lat_end);
        if (!lat_end || lat_end == q) break;

        // Parse lon
        q = strchr(lon_key, ':');
        if (!q) break;
        q++;
        q = skip_ws(q, end);
        if (q >= end) break;
        if (*q == '"') q++;
        char* lon_end = NULL;
        double lon = strtod(q, &lon_end);
        if (!lon_end || lon_end == q) break;

        if (lat > 90.0 || lat < -90.0) break;
        if (lon > 180.0 || lon < -180.0) break;

        // Parse name (expects proxy to sanitize quotes/backslashes)
        q = strchr(name_key, ':');
        if (!q) break;
        q++;
        q = skip_ws(q, end);
        if (q >= end) break;
        if (*q != '"') break;
        q++;

        size_t name_len = 0;
        while (q < end && *q != '"') {
            if (name_len + 1 < sizeof(out_results[count].name)) {
                out_results[count].name[name_len++] = *q;
            }
            q++;
        }
        out_results[count].name[name_len] = 0;

        out_results[count].lat = lat;
        out_results[count].lon = lon;
        count++;

        p = q;
        if (p < end) p++;
    }

    return count;
}

static void open_address_search(void) {
    if (!is_connected()) {
        printf("No WiFi; can't geocode.\n");
        return;
    }

    if (g_should_exit) return;

    char query[128] = {0};
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(query) - 1);
    swkbdSetHintText(&swkbd, "Enter address");
    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);

    SwkbdButton btn = swkbdInputText(&swkbd, query, sizeof(query));
    if (btn != SWKBD_BUTTON_RIGHT) {
        return; // cancelled
    }

    if (g_should_exit) return;

    if (query[0] == '\0') {
        printf("Empty search.\n");
        return;
    }

    char encoded[256];
    if (!url_encode_component(query, encoded, sizeof(encoded))) {
        printf("Search too long.\n");
        return;
    }

    char url[512];
    snprintf(url, sizeof(url), "http://193.122.144.54:8080/geocode?q=%s", encoded);

    printf("Geocoding: %s\n", query);

    u8* buf = NULL;
    u32 size = 0;
    if (!download_url(url, &buf, &size) || !buf || size == 0) {
        printf("Geocode request failed.\n");
        if (buf) free(buf);
        return;
    }

    // Ensure null-terminated for strstr/strchr
    u8* tmp = (u8*)realloc(buf, size + 1);
    if (tmp) buf = tmp;
    buf[size] = 0;

    GeocodeResult results[5];
    memset(results, 0, sizeof(results));
    int count = parse_geocode_results_json((const char*)buf, size, results, 5);
    free(buf);

    if (count <= 0) {
        printf("No results.\n");
        return;
    }

    int selected = 0;
    if (count > 1) {
        // Simple picker on the bottom screen.
        while (aptMainLoop()) {
            consoleClear();
            printf("Search results:\n");
            printf("Up/Down = select  A = go  B = cancel\n\n");

            for (int i = 0; i < count; i++) {
                printf("%c %d) %s\n", (i == selected) ? '>' : ' ', i + 1, results[i].name[0] ? results[i].name : "(result)");
            }

            hidScanInput();
            u32 kDown = hidKeysDown();
            if (kDown & KEY_B) {
                consoleClear();
                printf("Search cancelled.\n");
                return;
            }
            if (kDown & KEY_START) {
                g_should_exit = true;
                return;
            }
            if (kDown & KEY_DUP) {
                if (selected > 0) selected--;
            }
            if (kDown & KEY_DDOWN) {
                if (selected < count - 1) selected++;
            }
            if (kDown & KEY_A) {
                break;
            }

            svcSleepThread(60000000LL); // ~60ms
        }
    }

    // Jump map
    map_state.lat = results[selected].lat;
    map_state.lon = results[selected].lon;
    map_state.offset_x = 0;
    map_state.offset_y = 0;
    map_state.dragging = false;
    map_state.gps_tracking = false; // manual search takes control
    map_tiles_clear_cache();

    printf("Jumped to: %.6f, %.6f\n", map_state.lat, map_state.lon);
}
