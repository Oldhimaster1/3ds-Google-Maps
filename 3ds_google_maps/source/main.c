#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "map_tiles.h"
#include "network.h"
#include "logging.h"
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

// Function prototypes
void init_graphics(void);
void cleanup_graphics(void);
void update_input(void);
void render_map(void);
void handle_touch_input(touchPosition* touch);
void zoom_in(void);
void zoom_out(void);
void pan_map(int dx, int dy);

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    cfguInit();
    
    // Initialize console for debug output
    consoleInit(GFX_BOTTOM, NULL);
    
    // Initialize logging to SD card
    log_init();
    
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
    printf("A: Toggle GPS tracking\n");
    printf("B: Connect to GPS\n");
    printf("Touch: Drag to pan\n");
    printf("START: Exit\n");
    printf("\nStarting at NYC (%.4f, %.4f)\n", DEFAULT_LAT, DEFAULT_LON);
    
    // Don't clear console - keep messages visible
    svcSleepThread(2000000000LL); // 2 seconds
    
    // Main loop
    static int frame_counter = 0;
    while (aptMainLoop()) {
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
        
        // Show status every 60 frames (about once per second)
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
        
        // Exit check
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        
        render_map();
    }

cleanup:
    // Cleanup
    gps_cleanup();
    map_tiles_cleanup();
    network_cleanup();
    cleanup_graphics();
    log_close();
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
        return;
    }
    
    // Zoom controls (X = zoom in, Y = zoom out)
    if (kDown & KEY_X) {
        zoom_in();
    }
    if (kDown & KEY_Y) {
        zoom_out();
    }
    
    // GPS controls
    if (kDown & KEY_B) {
        // Connect to GPS
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
        // Toggle GPS tracking
        if (gps_is_connected()) {
            map_state.gps_tracking = !map_state.gps_tracking;
            if (map_state.gps_tracking) {
                printf("GPS tracking ON\n");
            } else {
                printf("GPS tracking OFF\n");
            }
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
    
    // Convert pixel offset to lat/lon offset using proper Mercator projection
    double n = pow(2, map_state.zoom);
    double lon_per_pixel = 360.0 / (TILE_SIZE * n);
    
    // For latitude, we need to account for Mercator distortion
    double lat_rad = map_state.lat * M_PI / 180.0;
    double meters_per_pixel = 156543.03 * cos(lat_rad) / n;
    double lat_change = (dy * meters_per_pixel) / 111320.0; // meters per degree at equator
    
    map_state.lat -= lat_change;
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
    printf("\x1b[8;0H  L/R: Zoom in/out");
    printf("\x1b[9;0H  Touch: Drag to pan");
    printf("\x1b[10;0H  START: Exit");
    
    printf("\x1b[12;0HMap Tiles:");
    printf("\x1b[13;0H  Colored boxes = downloaded tiles");
    printf("\x1b[14;0H  Patterns show tile coordinates");
    printf("\x1b[15;0H  Red crosshair = map center");
    
    if (map_state.dragging) {
        printf("\x1b[17;0HDragging...");
    } else {
        printf("\x1b[17;0H           ");
    }
    
    if (!is_connected()) {
        printf("\x1b[19;0HNo WiFi connection!");
    } else {
        printf("\x1b[19;0HWiFi connected     ");
    }
    
    // Add debug info about tile loading
    static int frame_count = 0;
    frame_count++;
    printf("\x1b[21;0HFrame: %d", frame_count);
    printf("\x1b[22;0HGraphics working! Try L/R to zoom");
}
