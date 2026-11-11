#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "map_tiles.h"
#include "gps_follow_gate.h"
#include "search_input.h"
#include "network.h"
#include "gps.h"
#include "logging.h"
#include "stb_image.h"
#include "qrcode.h"
#include "region.h"
#include "explore.h"
#include "pano_runtime.h"
#include "pano_gyro.h"
#include "street_look_input.h"
#include "street_sources.h"
#include "street_fly_logic.h"
#include "ui_accessibility.h"
#include "pano_motion.h"
#include "guess_ui.h"
#include "street_nav.h"
#include <jansson.h>
#include <zlib.h>

#define SCREEN_WIDTH_TOP    400
#define SCREEN_HEIGHT_TOP   240
#define SCREEN_WIDTH_BOTTOM 320
#define SCREEN_HEIGHT_BOTTOM 240

#define TOUCH_BUTTON_GRID_TOP 96
#define TOUCH_BUTTON_COLS 4
#define TOUCH_BUTTON_ROWS 2
#define TOUCH_BUTTON_WIDTH (SCREEN_WIDTH_BOTTOM / TOUCH_BUTTON_COLS)
#define TOUCH_BUTTON_HEIGHT ((SCREEN_HEIGHT_BOTTOM - TOUCH_BUTTON_GRID_TOP) / TOUCH_BUTTON_ROWS)

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

typedef struct {
    double lat;
    double lon;
    char name[96];
} RecentPlace;

typedef enum {
    TOUCH_ACTION_NONE = 0,
    TOUCH_ACTION_SEARCH,
    TOUCH_ACTION_FAVORITES,
    TOUCH_ACTION_RECENTS,
    TOUCH_ACTION_ROUTE,
    TOUCH_ACTION_IDENTIFY,
    TOUCH_ACTION_GPS_CONNECT,
    TOUCH_ACTION_GPS_TRACK,
    TOUCH_ACTION_MENU
} TouchAction;

typedef enum {
    APP_SCREEN_MAIN = 0,
    APP_SCREEN_SETTINGS
} AppScreenMode;

typedef enum {
    SETTINGS_TAB_VIEW = 0,
    SETTINGS_TAB_TILES,
    SETTINGS_TAB_DATA,
    SETTINGS_TAB_GPS,
    SETTINGS_TAB_EXPLORE,
    SETTINGS_TAB_TEXT,
    SETTINGS_TAB_COUNT
} SettingsTab;

typedef enum {
    SETTINGS_HIT_NONE = 0,
    SETTINGS_HIT_CLOSE,
    SETTINGS_HIT_TAB_VIEW,
    SETTINGS_HIT_TAB_TILES,
    SETTINGS_HIT_TAB_DATA,
    SETTINGS_HIT_TILE_STREET,
    SETTINGS_HIT_TILE_SATELLITE,
    SETTINGS_HIT_NIGHT_MODE,
    SETTINGS_HIT_DEBUG_OVERLAY,
    SETTINGS_HIT_PREFETCH_RING_DEC,
    SETTINGS_HIT_PREFETCH_RING_INC,
    SETTINGS_HIT_PREFETCH_RATE_DEC,
    SETTINGS_HIT_PREFETCH_RATE_INC,
    SETTINGS_HIT_PROXY_TOGGLE,
    SETTINGS_HIT_CACHE_TOGGLE,
    SETTINGS_HIT_CLEAR_CACHE,
    SETTINGS_HIT_DOWNLOAD_REGION,
    /* GPS tab */
    SETTINGS_HIT_TAB_GPS,
    SETTINGS_HIT_GPS_MODE_CLIENT,
    SETTINGS_HIT_GPS_MODE_SERVER,
    SETTINGS_HIT_GPS_START_STOP,
    SETTINGS_HIT_TAB_EXPLORE,
    SETTINGS_HIT_GLOBE,
    SETTINGS_HIT_TERRAIN,
    SETTINGS_HIT_STREET_EXPLORER,
    SETTINGS_HIT_STREETGUESS,
    SETTINGS_HIT_STREET_COVERAGE,
    SETTINGS_HIT_TAB_TEXT,
    SETTINGS_HIT_TEXT_NORMAL,
    SETTINGS_HIT_TEXT_LARGE,
    SETTINGS_HIT_TEXT_XL,
    SETTINGS_HIT_TEXT_REGULAR,
    SETTINGS_HIT_TEXT_MEDIUM,
    SETTINGS_HIT_TEXT_BOLD,
    SETTINGS_HIT_TEXT_CONTRAST
} SettingsHitTarget;

#define MAX_RECENTS 10
#define MAX_FAVORITES 16
#define FAVORITES_FILE "sdmc:/3ds_google_maps/favorites.txt"

static MapState map_state;
static C3D_RenderTarget* top_screen;
static C3D_RenderTarget* bottom_screen;
static C2D_TextBuf g_ui_text_buf;
static PrintConsole g_modal_console;
static bool g_should_exit = false;
static u64 g_last_interaction_ms = 0;
static RecentPlace g_recents[MAX_RECENTS];
static int g_recent_count = 0;
/* Native keyboard resumes the last user-entered address instead of starting over. */
static char g_last_address_query[128];
static u64 g_last_geocoder_request_ms;
static RecentPlace g_favorites[MAX_FAVORITES];
static int g_favorite_count = 0;
static char g_last_place_name[96] = {0};
static u64 g_last_place_ms = 0;
static char g_status_message[96] = {0};
static u64 g_status_message_ms = 0;
static bool g_debug_overlay = false;
static bool g_show_coverage;
static const char *g_request_label;
static void (*g_bottom_modal)(void);
static u32 g_fps = 0;
static u64 g_fps_last_ms = 0;
static u32 g_fps_frames = 0;
static int g_prefetch_ring = 1;            // 0 disables prefetch
static int g_prefetch_every_n_frames = 1;  // 1 = every frame, 2 = every other frame, etc.

static bool g_night_mode = false;
static AppScreenMode g_screen_mode = APP_SCREEN_MAIN;
static SettingsTab g_settings_tab = SETTINGS_TAB_VIEW;
static SettingsHitTarget g_last_settings_hit = SETTINGS_HIT_NONE;
static u64 g_last_settings_hit_ms = 0;

static uint8_t g_qr_modules[QR_MAX_MODULES * QR_MAX_MODULES];
static int g_qr_size = 0;
static bool g_qr_valid = false;

static ExploreMode g_explore_mode;
static C3D_RenderTarget *g_explore_right;
static bool g_explore_gyro;
static bool g_explore_gyro_inverted = true;
static PanoGyroFilter g_explore_gyro_filter;
static PanoMotionFrame g_explore_motion;
static bool g_explore_accel;
static float g_explore_gyro_counts_per_dps;
static float g_explore_fov=55.0f;
static float g_explore_yaw, g_explore_pitch, g_explore_distance, g_explore_goal;
static u64 g_explore_last_ms, g_explore_dive_ms, g_map_fade_ms;
static bool g_globe_from_zoom0, g_provider_picker;
static int g_street_fly_ms=STREET_FLY_DEFAULT_MS;
static bool g_street_fly_dragging, g_street_entering;
static u64 g_street_enter_ms, g_street_reveal_ms;
static u64 g_street_swap_start_ms, g_street_swap_end_ms;
static double g_street_origin_lat, g_street_origin_lon;
static int g_street_origin_zoom, g_street_origin_x, g_street_origin_y;

// Smooth camera state.
static double g_target_lat = 0.0;
static double g_target_lon = 0.0;
static int g_target_zoom = 0;
static u64 g_last_zoom_step_ms = 0;

typedef RegionPoint RoutePoint;

#define MAX_ROUTE_POINTS 256
static RoutePoint g_route[MAX_ROUTE_POINTS];
static RoutePoint *g_route_geometry;
static unsigned g_route_geometry_count;
static int g_route_count = 0;
static bool g_route_active = false;
static char g_route_next_text[80] = {0};
static int g_route_next_dist_m = 0;
static TouchAction g_last_touch_action = TOUCH_ACTION_NONE;
static u64 g_last_touch_action_ms = 0;
static int g_console_ui_depth = 0;
static u64 g_last_dpad_pan_ms = 0;

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
/* The most recent successful lookup is reused for a few minutes. This both
 * speeds repeated searches and reduces load on the donated Nominatim API. */
static struct {
    char url[512];
    GeocodeResult results[5];
    int count;
    u64 when_ms;
} g_search_cache;

static bool json_coord(json_t *value, double limit, double *out);
static int parse_geocode_results_json(const char* json, u32 json_size, GeocodeResult* out_results, int max_results);
static bool parse_reverse_geocode_json(const char* json, u32 json_size, GeocodeResult* out_result);
static void open_address_search(void);
static void open_reverse_geocode_center(void);
static void open_menu(void);
static void close_menu(const char* status_message);
static void open_recents(void);
static void open_favorites(void);
static void open_cache_stats(void);
static void open_route_to_address(void);
static bool geocode_pick(const char* query, GeocodeResult* out);
static bool parse_route_json(const char* json, u32 json_size);
static void render_route_overlay(double center_lat, double center_lon, int zoom, int screen_width, int screen_height);
static void close_explore(void);
static void render_explore(void);
static void update_explore(void);
static void jump_to_location(double lat, double lon);
static bool favorites_load(void);
static bool favorites_save(void);
static void add_favorite(const char* name, double lat, double lon);
static void set_last_place_name(const char* name);
static void set_status_message(const char* message);
static void begin_bottom_console_ui(void);
static void end_bottom_console_ui(const char* status_message);
static TouchAction get_touch_action(const touchPosition* touch);
static bool handle_touch_button_action(TouchAction action);
static int circle_pad_axis_to_pan(int axis);
static void draw_ui_text(float x, float y, float scale_x, float scale_y, u32 color, u32 flags, const char* text);
static void render_bottom_panel(void);
static void render_settings_panel(void);
static void render_qr_top_screen(void);
static SettingsHitTarget get_settings_hit(const touchPosition* touch);
static void apply_settings_hit(SettingsHitTarget hit);
static bool point_in_rect(int px, int py, int x, int y, int w, int h);
static bool is_recent_settings_hit(SettingsHitTarget hit, u64 now_ms);
static void open_offline_download(void);
static void open_offline_regions(void);

static void ensure_app_data_dir(void) {
    mkdir("sdmc:/3ds_google_maps", 0777);
}
#define STREET_FLY_CFG "sdmc:/3ds_google_maps/street_fly.cfg"
static void street_fly_load(void) {
    FILE *file=fopen(STREET_FLY_CFG,"r");
    if (!file) return;
    int stored=STREET_FLY_DEFAULT_MS;
    if (fscanf(file,"%d",&stored)==1)
        g_street_fly_ms=street_fly_clamp_ms(stored);
    fclose(file);
}
static void street_fly_save(void) {
    ensure_app_data_dir();
    FILE *file=fopen(STREET_FLY_CFG,"w");
    if (!file) return;
    fprintf(file,"%d\n",g_street_fly_ms);
    fclose(file);
}

static void set_status_message(const char* message) {
    if (!message) message = "";
    strncpy(g_status_message, message, sizeof(g_status_message) - 1);
    g_status_message[sizeof(g_status_message) - 1] = 0;
    g_status_message_ms = osGetTime();
}

static int circle_pad_axis_to_pan(int axis) {
    const int deadzone = 20;
    const int max_speed = 10;

    if (axis > -deadzone && axis < deadzone) {
        return 0;
    }

    if (axis > 0) {
        axis -= deadzone;
    } else {
        axis += deadzone;
    }

    axis /= 10;
    if (axis > max_speed) return max_speed;
    if (axis < -max_speed) return -max_speed;
    return axis;
}

static bool point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < (x + w) && py >= y && py < (y + h);
}

static bool is_recent_settings_hit(SettingsHitTarget hit, u64 now_ms) {
    return g_last_settings_hit == hit && (now_ms - g_last_settings_hit_ms) < 180;
}

static void close_menu(const char* status_message) {
    g_screen_mode = APP_SCREEN_MAIN;
    g_last_settings_hit = SETTINGS_HIT_NONE;
    g_last_settings_hit_ms = 0;
    if (status_message && status_message[0]) {
        set_status_message(status_message);
    }
}

static void begin_bottom_console_ui(void) {
    if (g_console_ui_depth == 0) {
        consoleInit(GFX_TOP, &g_modal_console);
    }
    g_console_ui_depth++;
    consoleSelect(&g_modal_console);
    consoleClear();
}

static void end_bottom_console_ui(const char* status_message) {
    u16 fb_width = 0;
    u16 fb_height = 0;
    u8* fb = NULL;

    if (g_console_ui_depth <= 0) return;

    g_console_ui_depth--;
    if (g_console_ui_depth == 0) {
        consoleClear();

        gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
        gfxSetDoubleBuffering(GFX_TOP, true);

        fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_width, &fb_height);
        if (fb && fb_width > 0 && fb_height > 0) {
            memset(fb, 0, (size_t)fb_width * (size_t)fb_height * 3);
        }
        gfxScreenSwapBuffers(GFX_TOP, false);
        gspWaitForVBlank();

        fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_width, &fb_height);
        if (fb && fb_width > 0 && fb_height > 0) {
            memset(fb, 0, (size_t)fb_width * (size_t)fb_height * 3);
        }
        gfxScreenSwapBuffers(GFX_TOP, false);
        gspWaitForVBlank();

        if (status_message && status_message[0]) {
            set_status_message(status_message);
        }
    }
}


static bool parse_latlon(const char* s, double* out_lat, double* out_lon) {
    if (!s || !out_lat || !out_lon) return false;

    // Accept: "lat,lon" with optional spaces.
    while (*s == ' ' || *s == '\t') s++;
    char* end1 = NULL;
    double lat = strtod(s, &end1);
    if (!end1 || end1 == s) return false;

    while (*end1 == ' ' || *end1 == '\t') end1++;
    if (*end1 != ',') return false;
    end1++;
    while (*end1 == ' ' || *end1 == '\t') end1++;

    char* end2 = NULL;
    double lon = strtod(end1, &end2);
    if (!end2 || end2 == end1) return false;

    while (*end2 == ' ' || *end2 == '\t' || *end2 == '\r' || *end2 == '\n') end2++;
    if (*end2 != '\0') return false;

    if (!isfinite(lat) || lat < -90.0 || lat > 90.0) return false;
    if (!isfinite(lon) || lon < -180.0 || lon > 180.0) return false;

    *out_lat = lat;
    *out_lon = lon;
    return true;
}

static double approach_lon(double current, double target, double alpha) {
    double diff = target - current;
    if (diff > 180.0) diff -= 360.0;
    if (diff < -180.0) diff += 360.0;
    current += diff * alpha;
    while (current > 180.0) current -= 360.0;
    while (current < -180.0) current += 360.0;
    return current;
}

static void set_camera_target(double lat, double lon, int zoom, bool snap_now) {
    lat = fmax(-85.05112878, fmin(85.05112878, lat));
    g_target_lat = lat;
    g_target_lon = lon;
    g_target_zoom = zoom;

    if (snap_now) {
        map_state.lat = lat;
        map_state.lon = lon;
        map_state.zoom = zoom;
        map_tiles_view_changed();
        map_tiles_prefetch_reset();
    }
}

static void update_camera_smooth(void) {
    // Smoothly approach target (pan) and step zoom over time.
    const double alpha = map_state.dragging ? 0.35 : 0.18;
    map_state.lat += (g_target_lat - map_state.lat) * alpha;
    map_state.lon = approach_lon(map_state.lon, g_target_lon, alpha);

    // Clamp latitude / wrap longitude.
    if (map_state.lat > 85.0511) map_state.lat = 85.0511;
    if (map_state.lat < -85.0511) map_state.lat = -85.0511;
    while (map_state.lon > 180.0) map_state.lon -= 360.0;
    while (map_state.lon < -180.0) map_state.lon += 360.0;

    if (map_state.zoom != g_target_zoom) {
        u64 now_ms = osGetTime();
        // Step zoom every ~90ms (animated zoom).
        if (now_ms - g_last_zoom_step_ms >= 90) {
            g_last_zoom_step_ms = now_ms;
            if (map_state.zoom < g_target_zoom) map_state.zoom++;
            else if (map_state.zoom > g_target_zoom) map_state.zoom--;
            map_tiles_view_changed();
            map_tiles_prefetch_reset();
        }
    }
}

static void add_recent(const char* name, double lat, double lon) {
    if (!name || !name[0]) name = "(search result)";

    // De-dup by exact name + very small coord tolerance.
    for (int i = 0; i < g_recent_count; i++) {
        if (strncmp(g_recents[i].name, name, sizeof(g_recents[i].name)) == 0 &&
            fabs(g_recents[i].lat - lat) < 0.000001 &&
            fabs(g_recents[i].lon - lon) < 0.000001) {
            // Move to front.
            RecentPlace tmp = g_recents[i];
            for (int j = i; j > 0; j--) g_recents[j] = g_recents[j - 1];
            g_recents[0] = tmp;
            return;
        }
    }

    // Insert at front.
    for (int j = (g_recent_count < MAX_RECENTS ? g_recent_count : (MAX_RECENTS - 1)); j > 0; j--) {
        g_recents[j] = g_recents[j - 1];
    }
    memset(&g_recents[0], 0, sizeof(g_recents[0]));
    strncpy(g_recents[0].name, name, sizeof(g_recents[0].name) - 1);
    g_recents[0].lat = lat;
    g_recents[0].lon = lon;
    if (g_recent_count < MAX_RECENTS) g_recent_count++;
}

static void set_last_place_name(const char* name) {
    if (!name || !name[0]) {
        g_last_place_name[0] = '\0';
        g_last_place_ms = 0;
        return;
    }

    strncpy(g_last_place_name, name, sizeof(g_last_place_name) - 1);
    g_last_place_name[sizeof(g_last_place_name) - 1] = '\0';
    g_last_place_ms = osGetTime();
}

static bool favorites_save(void) {
    ensure_app_data_dir();
    FILE* f = fopen(FAVORITES_FILE, "wb");
    if (!f) return false;

    for (int i = 0; i < g_favorite_count; i++) {
        char clean_name[96];
        size_t out = 0;
        const char* src = g_favorites[i].name;
        while (*src && out + 1 < sizeof(clean_name)) {
            char c = *src++;
            if (c == '\r' || c == '\n' || c == '\t') c = ' ';
            clean_name[out++] = c;
        }
        clean_name[out] = '\0';
        fprintf(f, "%.8f\t%.8f\t%s\n", g_favorites[i].lat, g_favorites[i].lon, clean_name);
    }

    fclose(f);
    return true;
}

static bool favorites_load(void) {
    g_favorite_count = 0;
    FILE* f = fopen(FAVORITES_FILE, "rb");
    if (!f) return false;

    char line[256];
    while (fgets(line, sizeof(line), f) && g_favorite_count < MAX_FAVORITES) {
        char* tab1 = strchr(line, '\t');
        if (!tab1) continue;
        char* tab2 = strchr(tab1 + 1, '\t');
        if (!tab2) continue;

        *tab1 = '\0';
        *tab2 = '\0';

        char* name = tab2 + 1;
        char* nl = strpbrk(name, "\r\n");
        if (nl) *nl = '\0';
        if (!name[0]) continue;

        g_favorites[g_favorite_count].lat = strtod(line, NULL);
        g_favorites[g_favorite_count].lon = strtod(tab1 + 1, NULL);
        strncpy(g_favorites[g_favorite_count].name, name, sizeof(g_favorites[g_favorite_count].name) - 1);
        g_favorites[g_favorite_count].name[sizeof(g_favorites[g_favorite_count].name) - 1] = '\0';
        g_favorite_count++;
    }

    fclose(f);
    return g_favorite_count > 0;
}

static void add_favorite(const char* name, double lat, double lon) {
    if (!name || !name[0]) name = "(favorite)";

    for (int i = 0; i < g_favorite_count; i++) {
        if (strncmp(g_favorites[i].name, name, sizeof(g_favorites[i].name)) == 0 &&
            fabs(g_favorites[i].lat - lat) < 0.000001 &&
            fabs(g_favorites[i].lon - lon) < 0.000001) {
            RecentPlace tmp = g_favorites[i];
            for (int j = i; j > 0; j--) g_favorites[j] = g_favorites[j - 1];
            g_favorites[0] = tmp;
            favorites_save();
            return;
        }
    }

    for (int j = (g_favorite_count < MAX_FAVORITES ? g_favorite_count : (MAX_FAVORITES - 1)); j > 0; j--) {
        g_favorites[j] = g_favorites[j - 1];
    }
    memset(&g_favorites[0], 0, sizeof(g_favorites[0]));
    snprintf(g_favorites[0].name, sizeof(g_favorites[0].name), "%s", name);
    g_favorites[0].lat = lat;
    g_favorites[0].lon = lon;
    if (g_favorite_count < MAX_FAVORITES) g_favorite_count++;
    favorites_save();
}

static void jump_to_location(double lat, double lon) {
    map_state.offset_x = 0;
    map_state.offset_y = 0;
    map_state.dragging = false;
    map_state.gps_tracking = false;
    set_camera_target(lat, lon, g_target_zoom, true);
    g_last_interaction_ms = osGetTime();
}

#define SESSION_PATH "sdmc:/3ds_google_maps/session.cfg"

static void load_session(void) {
    FILE *f = fopen(SESSION_PATH, "r");
    if (!f) f = fopen(SESSION_PATH ".bak", "r");
    if (!f) return;
    unsigned version, source, night, cache, coverage, gps_mode;
    int zoom;
    double lat, lon;
    int fields = fscanf(f, "%u %lf %lf %d %u %u %u %u %u",
                        &version, &lat, &lon, &zoom, &source, &night, &cache, &coverage, &gps_mode);
    fclose(f);
    if (fields != 9 || version != 1 || !isfinite(lat) || fabs(lat) > 90 ||
        !isfinite(lon) || fabs(lon) > 180 || zoom < 0 || zoom > 18 ||
        source > 1 || night > 1 || cache > 1 || coverage > 1 || gps_mode > 1) return;
    network_set_tile_source((TileSource)source);
    network_set_disk_cache_enabled(cache);
    gps_set_source_mode((GpsSourceMode)gps_mode);
    g_night_mode = night;
    g_show_coverage = coverage;
    set_camera_target(lat, lon, zoom, true);
}

static void save_session(void) {
    ensure_app_data_dir();
    FILE *f = fopen(SESSION_PATH ".tmp", "w");
    if (!f) return;
    bool written = fprintf(f, "1 %.9f %.9f %d %u %u %u %u %u\n",
                           g_target_lat, g_target_lon, g_target_zoom,
                           (unsigned)network_get_tile_source(), (unsigned)g_night_mode,
                           (unsigned)network_get_disk_cache_enabled(), (unsigned)g_show_coverage,
                           (unsigned)gps_get_source_mode()) > 0;
    int closed = fclose(f);
    if (!written || closed != 0) return;
    if (access(SESSION_PATH, F_OK) == 0) {
        unlink(SESSION_PATH ".bak");
        if (rename(SESSION_PATH, SESSION_PATH ".bak") != 0) return;
    }
    if (rename(SESSION_PATH ".tmp", SESSION_PATH) == 0) unlink(SESSION_PATH ".bak");
}

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    log_init();
    cfguInit();
    sslcInit(0);
    httpcInit(0);
    
    // Initialize graphics
    init_graphics();
    
    // Initialize network
    if (!network_init()) {
        consoleInit(GFX_BOTTOM, NULL);
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

    set_camera_target(map_state.lat, map_state.lon, map_state.zoom, true);
    
    // Initialize map tiles system
    if (!map_tiles_init()) {
        consoleInit(GFX_BOTTOM, NULL);
        printf("Failed to initialize map tiles!\n");
        printf("Press START to exit.\n");
        while (aptMainLoop()) {
            hidScanInput();
            u32 kDown = hidKeysDown();
            if (kDown & KEY_START) break;
        }
        goto cleanup;
    }

    ensure_app_data_dir();
    street_fly_load();
    ui_access_load(UI_ACCESS_CFG);
    favorites_load();
    load_session();
    set_status_message("Ready");

    g_last_interaction_ms = osGetTime();
    g_fps_last_ms = g_last_interaction_ms;
    g_last_zoom_step_ms = g_last_interaction_ms;
    
    // Main loop
    static int frame_counter = 0;
    while (aptMainLoop() && !g_should_exit) {
        frame_counter++;
        g_fps_frames++;

        u64 now_ms = osGetTime();
        if (now_ms - g_fps_last_ms >= 1000) {
            g_fps = g_fps_frames;
            g_fps_frames = 0;
            g_fps_last_ms = now_ms;
        }

        // Update GPS if connected
        gps_update();

        /* Entirely separate game state/rendering path; no normal map camera
         * or provider preference is modified by StreetGuess. */
        if (guess_ui_active()) {
            guess_ui_update();
            if (g_should_exit) break;
            map_tiles_process_downloads();
            if (guess_ui_active()) guess_ui_draw(top_screen,bottom_screen);
            else render_map();
            continue;
        }

        if (g_explore_mode) {
            update_explore();
            if (g_should_exit) break;
            map_tiles_process_downloads();
            if (g_explore_mode) render_explore();
            else render_map();
            continue;
        }

        /* The phone may send many nearly identical fixes while stationary.
         * Update the target only when motion exceeds accuracy/zoom jitter.
         * This avoids churning the visible tile queue during GPS follow. */
        if (map_state.gps_tracking) {
            GpsFix fix;
            if (gps_get_fix(&fix) &&
                gps_should_move_camera(g_target_lat,g_target_lon,
                                       fix.lat,fix.lon,fix.accuracy_m,
                                       fix.age_ms,g_target_zoom))
                set_camera_target(fix.lat,fix.lon,g_target_zoom,false);
        }

        update_input();
        if (g_should_exit) break;
        if (guess_ui_active()) {
            /* Do not flash the underlying map on the frame StreetGuess opens. */
            map_tiles_process_downloads();
            guess_ui_draw(top_screen,bottom_screen);
            continue;
        }

        update_camera_smooth();

        // Upload any tiles that the background download thread has finished.
        map_tiles_process_downloads();

        // Prefetch a 1-tile ring around the view after ~0.5s of user idle.
        // This is throttled (one tile per frame) to keep things responsive.
        if (!map_state.gps_tracking && (network_get_tile_source() != TILE_SOURCE_STREET ||
                                         network_get_proxy_enabled())) {
            if (now_ms - g_last_interaction_ms > 500) {
                if (g_prefetch_ring > 0 && g_prefetch_every_n_frames > 0) {
                    if ((frame_counter % g_prefetch_every_n_frames) == 0) {
                        map_tiles_prefetch_step(map_state.lat, map_state.lon, map_state.zoom,
                                                SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP,
                                                g_prefetch_ring);
                    }
                }
            }
        }

        render_map();
    }

    save_session();

cleanup:
    if (guess_ui_active()) guess_ui_leave();
    if (g_explore_mode) close_explore();
    free(g_route_geometry);
    // Cleanup
    gps_cleanup();
    map_tiles_cleanup();
    network_cleanup();
    cleanup_graphics();
    log_close();
    httpcExit();
    sslcExit();
    cfguExit();
    gfxExit();
    
    return 0;
}

void init_graphics(void) {
    gfxSetScreenFormat(GFX_TOP, GSP_BGR8_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_BGR8_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    gfxSetDoubleBuffering(GFX_BOTTOM, true);

    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_ui_text_buf = C2D_TextBufNew(4096);
    
    // Main map uses hardware rendering on both screens. Console screens are still used for modal text menus.
    top_screen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bottom_screen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
}

void cleanup_graphics(void) {
    if (g_ui_text_buf) {
        C2D_TextBufDelete(g_ui_text_buf);
        g_ui_text_buf = NULL;
    }
    C2D_Fini();
    C3D_Fini();
}

void update_input(void) {
    hidScanInput();
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();
    touchPosition touch;

    if (kDown || kHeld) {
        g_last_interaction_ms = osGetTime();
    }

    hidTouchRead(&touch);
    
    // Exit on START
    if (kDown & KEY_START) {
        g_should_exit = true;
        return;
    }

    if (g_screen_mode == APP_SCREEN_SETTINGS) {
        if (g_street_fly_dragging && !(kHeld & KEY_TOUCH)) {
            street_fly_save();
            g_street_fly_dragging=false;
        }
        if (g_settings_tab == SETTINGS_TAB_VIEW && (kDown & KEY_SELECT)) {
            open_cache_stats();
            return;
        }
        if (kDown & KEY_B) {
            close_menu("Closed settings");
            return;
        }
        if (kDown & KEY_DLEFT) {
            if (g_settings_tab > 0) {
                g_settings_tab = (SettingsTab)(g_settings_tab - 1);
            }
            return;
        }
        if (kDown & KEY_DRIGHT) {
            if (g_settings_tab + 1 < SETTINGS_TAB_COUNT) {
                g_settings_tab = (SettingsTab)(g_settings_tab + 1);
            }
            return;
        }
        /* Drag the Street View transition slider without writing SD every frame. */
        if (g_settings_tab==SETTINGS_TAB_EXPLORE) {
            if ((kHeld & KEY_TOUCH) && touch.px>=130 && touch.px<=302 &&
                touch.py>=194 && touch.py<225) {
                int x=touch.px;
                if (x<150) x=150;
                if (x>294) x=294;
                g_street_fly_ms=street_fly_clamp_ms((x-150)*1600/144);
                g_street_fly_dragging=true;
                return;
            }
        }
        if ((kDown & KEY_TOUCH) && touch.px >= 0 && touch.py >= 0) {
            apply_settings_hit(get_settings_hit(&touch));
        }
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

    if (kDown & KEY_L) {
        open_reverse_geocode_center();
        return;
    }

    // GPS controls
    if (kDown & KEY_B) {
        if (!gps_is_connected()) {
            set_status_message("Connecting to GPS...");
            if (gps_init()) {
                set_status_message("GPS connected");
            } else {
                set_status_message("GPS connection failed");
            }
        } else {
            set_status_message("GPS already connected");
        }
    }
    if (kDown & KEY_A) {
        if (gps_is_connected()) {
            map_state.gps_tracking = !map_state.gps_tracking;
            set_status_message(map_state.gps_tracking ? "GPS tracking ON" : "GPS tracking OFF");
        } else {
            set_status_message("Connect GPS first");
        }
    }
    
    // Pan controls with D-Pad (disable if GPS tracking)
    if (!map_state.gps_tracking) {
        int pan_speed = 5;
        u64 now_ms = osGetTime();
        bool allow_dpad_pan = (kDown & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) ||
                      (now_ms - g_last_dpad_pan_ms >= 60);

        if (allow_dpad_pan) {
            bool did_pan = false;

            if (kHeld & KEY_DUP) {
                pan_map(0, -pan_speed);
                did_pan = true;
            }
            if (kHeld & KEY_DDOWN) {
                pan_map(0, pan_speed);
                did_pan = true;
            }
            if (kHeld & KEY_DLEFT) {
                pan_map(-pan_speed, 0);
                did_pan = true;
            }
            if (kHeld & KEY_DRIGHT) {
                pan_map(pan_speed, 0);
                did_pan = true;
            }

            if (did_pan) {
                g_last_dpad_pan_ms = now_ms;
            }
        }

        {
            circlePosition cp;
            int circle_dx;
            int circle_dy;

            hidCircleRead(&cp);
            circle_dx = circle_pad_axis_to_pan(cp.dx);
            circle_dy = circle_pad_axis_to_pan(cp.dy);

            if (circle_dx != 0 || circle_dy != 0) {
                pan_map(circle_dx, -circle_dy);
                g_last_interaction_ms = now_ms;
            }
        }
    }
    
    // Touch input: bottom button grid triggers actions, upper area still drag-pans.
    {
        if (touch.px > 0 && touch.py > 0) {
            g_last_interaction_ms = osGetTime();

            TouchAction action = get_touch_action(&touch);
            if (action != TOUCH_ACTION_NONE) {
                map_state.dragging = false;
                if (kDown & KEY_TOUCH) {
                    handle_touch_button_action(action);
                }
            } else if (!map_state.gps_tracking) {
                handle_touch_input(&touch);
            } else if (map_state.dragging) {
                map_state.dragging = false;
            }
        } else {
            // End dragging when touch is released
            if (map_state.dragging) {
                g_last_interaction_ms = osGetTime();
                map_state.dragging = false;
            }
        }
    }
}

static TouchAction get_touch_action(const touchPosition* touch) {
    int col;
    int row;

    if (!touch) return TOUCH_ACTION_NONE;
    if (touch->py < TOUCH_BUTTON_GRID_TOP) return TOUCH_ACTION_NONE;
    if (touch->px < 0 || touch->px >= SCREEN_WIDTH_BOTTOM) return TOUCH_ACTION_NONE;
    if (touch->py >= SCREEN_HEIGHT_BOTTOM) return TOUCH_ACTION_NONE;

    col = touch->px / TOUCH_BUTTON_WIDTH;
    row = (touch->py - TOUCH_BUTTON_GRID_TOP) / TOUCH_BUTTON_HEIGHT;

    if (row == 0) {
        switch (col) {
            case 0: return TOUCH_ACTION_SEARCH;
            case 1: return TOUCH_ACTION_FAVORITES;
            case 2: return TOUCH_ACTION_RECENTS;
            case 3: return TOUCH_ACTION_ROUTE;
            default: return TOUCH_ACTION_NONE;
        }
    }

    if (row == 1) {
        switch (col) {
            case 0: return TOUCH_ACTION_IDENTIFY;
            case 1: return TOUCH_ACTION_GPS_CONNECT;
            case 2: return TOUCH_ACTION_GPS_TRACK;
            case 3: return TOUCH_ACTION_MENU;
            default: return TOUCH_ACTION_NONE;
        }
    }

    return TOUCH_ACTION_NONE;
}

static bool handle_touch_button_action(TouchAction action) {
    g_last_touch_action = action;
    g_last_touch_action_ms = osGetTime();

    switch (action) {
        case TOUCH_ACTION_SEARCH:
            open_address_search();
            return true;
        case TOUCH_ACTION_FAVORITES:
            open_favorites();
            return true;
        case TOUCH_ACTION_RECENTS:
            open_recents();
            return true;
        case TOUCH_ACTION_ROUTE:
            open_route_to_address();
            return true;
        case TOUCH_ACTION_IDENTIFY:
            open_reverse_geocode_center();
            return true;
        case TOUCH_ACTION_GPS_CONNECT:
            if (!gps_is_connected()) {
                set_status_message("Connecting to GPS...");
                if (gps_init()) {
                    set_status_message("GPS connected");
                } else {
                    set_status_message("GPS connection failed");
                }
            } else {
                set_status_message("GPS already connected");
            }
            return true;
        case TOUCH_ACTION_GPS_TRACK:
            if (gps_is_connected()) {
                map_state.gps_tracking = !map_state.gps_tracking;
                set_status_message(map_state.gps_tracking ? "GPS tracking ON" : "GPS tracking OFF");
            } else {
                set_status_message("Connect GPS first");
            }
            return true;
        case TOUCH_ACTION_MENU:
            open_menu();
            return true;
        default:
            return false;
    }
}

static void close_explore(void) {
    g_provider_picker=false;
    g_street_entering=false;
    g_street_reveal_ms=0;
    g_street_swap_start_ms=g_street_swap_end_ms=0;
    g_globe_from_zoom0=false;
    pano_runtime_stop();
    if (g_explore_gyro) HIDUSER_DisableGyroscope();
    if (g_explore_accel) HIDUSER_DisableAccelerometer();
    g_explore_gyro = g_explore_accel = false;
    g_explore_mode = EXPLORE_NONE;
    g_explore_dive_ms = 0;
    explore_unload();
    if (g_explore_right) {
        C3D_RenderTargetDelete(g_explore_right);
        g_explore_right = NULL;
    }
    gfxSet3D(false);
    C2D_Prepare();
}

static void open_explore(ExploreMode mode) {
    /* Start the real imagery request BEFORE creating the scene or flying in. */
    if (mode == EXPLORE_STREET) {
        g_street_origin_lat=map_state.lat;
        g_street_origin_lon=map_state.lon;
        g_street_origin_zoom=map_state.zoom;
        g_street_origin_x=map_state.offset_x;
        g_street_origin_y=map_state.offset_y;
        g_street_enter_ms=osGetTime();
        g_street_reveal_ms=0;
        g_street_swap_start_ms=g_street_swap_end_ms=0;
        g_street_entering=g_street_fly_ms>0;
        /* Retain cached overhead tiles but discard queued speculation. */
        map_tiles_view_changed();
        pano_runtime_start(g_street_origin_lat,g_street_origin_lon);
    }
    if (!explore_load(mode, 0)) {
        if (mode == EXPLORE_STREET) pano_runtime_stop();
        g_street_entering=false;
        set_status_message("Could not load Explore scene");
        return;
    }
    g_explore_mode = mode;
    g_provider_picker=false;
    g_explore_yaw = mode == EXPLORE_GLOBE ? C3D_AngleFromDegrees((float)map_state.lon) :
                    mode == EXPLORE_TERRAIN ? .58f : 0;
    g_explore_pitch = mode == EXPLORE_GLOBE ?
        C3D_AngleFromDegrees(fmaxf(-89, fminf(89, map_state.lat))) :
        mode == EXPLORE_TERRAIN ? .74f : 0;
    g_explore_distance = g_explore_goal = mode == EXPLORE_TERRAIN ? 3.6f : 3.9f;
    g_explore_last_ms = osGetTime();
    if (mode == EXPLORE_TERRAIN)
        g_explore_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    close_menu(NULL);
    if (mode == EXPLORE_STREET) {
        g_explore_gyro_inverted = true;
        g_explore_fov=55.0f;
        pano_motion_reset(&g_explore_motion);
    }
}

static void explore_show_map(void) {
    double lat, lon;
    int zoom;
    if (g_explore_mode == EXPLORE_GLOBE) {
        lat = g_explore_pitch * (180.0 / M_PI);
        lon = g_explore_yaw * (180.0 / M_PI);
        zoom = g_globe_from_zoom0 ? 0 : 2;
    } else if (g_explore_mode == EXPLORE_TERRAIN) {
        lat=explore_terrain_place.lat;
        lon=explore_terrain_place.lon;
        zoom=11;
    } else {
        const PanoLiveImage *live=pano_runtime_current();
        lat=live ? live->lat : g_street_origin_lat;
        lon=live ? live->lon : g_street_origin_lon;
        zoom=live ? 17 : g_street_origin_zoom;
    }
    close_explore();
    g_globe_from_zoom0=false;
    map_state.gps_tracking = false;
    set_camera_target(lat, lon, zoom, true);
    g_map_fade_ms = osGetTime();
}

static void choose_street_provider(StreetProvider p) {
    g_provider_picker=false;
    const PanoLiveImage *live=pano_runtime_current();
    double lat=live ? live->lat : g_street_origin_lat;
    double lon=live ? live->lon : g_street_origin_lon;
    if (!pano_runtime_switch_provider(p,lat,lon))
        set_status_message("Could not start selected source");
}

static float explore_axis(int value) {
    return abs(value) < 13 ? 0 : fmaxf(-1, fminf(1, value / 145.0f));
}

static void update_explore(void) {
    /* Fade a decoded NEXT image in only after briefly dimming the previous
     * view. This avoids an extra 1 MiB GPU texture for crossfading. */
    u64 poll_now=osGetTime();
    if (g_explore_mode==EXPLORE_STREET &&
        pano_runtime_next_ready() && pano_runtime_has_live()) {
        if (!g_street_swap_start_ms) g_street_swap_start_ms=poll_now;
        else if (poll_now-g_street_swap_start_ms>=90) {
            pano_runtime_poll();
            g_street_swap_end_ms=poll_now;
            g_street_swap_start_ms=0;
        }
    } else {
        g_street_swap_start_ms=0;
        pano_runtime_poll();
    }
    hidScanInput();
    u32 keys = hidKeysDown(), held = hidKeysHeld();
    if ((keys & KEY_B) && !(keys & KEY_START) && g_provider_picker) {
        g_provider_picker=false;
        return;
    }
    if (keys & (KEY_B | KEY_START)) {
        if ((keys & KEY_B) && !(keys & KEY_START) &&
            g_explore_mode==EXPLORE_STREET)
            explore_show_map();
        else close_explore();
        if (keys & KEY_START) g_should_exit = true;
        return;
    }
    u64 now = osGetTime();
    if (g_explore_mode==EXPLORE_STREET && g_street_entering &&
        g_street_fly_ms>0) {
        float fly=street_fly_progress(now,g_street_enter_ms,g_street_fly_ms);
        if (fly>=.75f && pano_runtime_has_live() && !g_street_reveal_ms)
            g_street_reveal_ms=now;
        int reveal_ms=g_street_fly_ms/4;
        if (reveal_ms<100) reveal_ms=100;
        if (g_street_reveal_ms && now-g_street_reveal_ms>=(u64)reveal_ms)
            g_street_entering=false;
    }
    float dt = fminf(.05f, (now - g_explore_last_ms) * .001f);
    g_explore_last_ms = now;
    if (g_explore_dive_ms) {
        float t = (now - g_explore_dive_ms) / 430.0f;
        if (t >= 1) explore_show_map();
        else g_explore_distance = g_explore_goal + (1.12f - g_explore_goal) * t * t;
        return;
    }
    circlePosition stick;
    hidCircleRead(&stick);
    float x=0, y=0;
    if (g_explore_mode == EXPLORE_STREET) {
        /* D-left/right rotate the panorama; L/R follow linked roads. */
        street_look_axes(stick.dx, stick.dy, &x, &y);
        x=fmaxf(-1,fminf(1,x+!!(held&KEY_DRIGHT)-!!(held&KEY_DLEFT)));
    } else {
        x = explore_axis(stick.dx);
        y = explore_axis(stick.dy);
        x = fmaxf(-1, fminf(1, x + !!(held & KEY_DRIGHT) - !!(held & KEY_DLEFT)));
        y = fmaxf(-1, fminf(1, y + !!(held & KEY_DUP) - !!(held & KEY_DDOWN)));
    }
    float turn = g_explore_mode == EXPLORE_GLOBE && g_explore_goal < 2 ? .72f : 1.7f;
    g_explore_yaw += x * turn * dt;
    g_explore_pitch += y * turn * dt;
    if (g_explore_mode == EXPLORE_STREET) {
        const PanoLiveImage *active_photo = pano_runtime_current();
        if (active_photo && !active_photo->is_pano) {
            /* Ordinary street photos are flat, not a navigable sphere. */
            g_explore_yaw = g_explore_pitch = 0;
        }
        if ((keys & (KEY_L | KEY_R)) &&
            !((held & KEY_L) && (held & KEY_DDOWN))) {
            int dir = (keys & KEY_L) ? -1 : 1;
            if (pano_runtime_has_live()) {
                if (!pano_runtime_step_at(dir,g_explore_yaw))
                    set_status_message(pano_runtime_status());
                else if (!pano_runtime_busy())
                    g_street_swap_end_ms=osGetTime();
            } else if (!pano_runtime_busy()) {
                set_status_message("No live panorama: press Down to retry");
            }
        }
        if (keys & KEY_SELECT) {
            pano_gyro_reset(&g_explore_gyro_filter);
            pano_motion_reset(&g_explore_motion);
            set_status_message("Gyro recalibrating: hold console still");
        }
        if (keys & KEY_DUP)
            g_explore_fov=fmaxf(28.0f,g_explore_fov-5.0f);
        if (keys & KEY_DDOWN) {
            if (held & KEY_L) {
                double lat=active_photo ? active_photo->lat : g_street_origin_lat;
                double lon=active_photo ? active_photo->lon : g_street_origin_lon;
                if (!pano_runtime_start(lat,lon))
                    set_status_message(pano_runtime_status());
            } else g_explore_fov=fminf(85.0f,g_explore_fov+5.0f);
        }
        if (keys & KEY_Y) {
            StreetProvider next=(StreetProvider)((pano_runtime_provider()+1)%STREET_PROVIDER_COUNT);
            choose_street_provider(next);
        }
        if (keys & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);
            if (g_provider_picker) {
                if (touch.px>=12 && touch.px<308 && touch.py>=60 && touch.py<180) {
                    int index=(touch.py-60)/20;
                    if (index>=0 && index<STREET_PROVIDER_COUNT)
                        choose_street_provider((StreetProvider)index);
                } else g_provider_picker=false;
            } else if (touch.px>=12 && touch.px<308 && touch.py>=57 && touch.py<88) {
                g_provider_picker=true;
            } else if (touch.px>=164 && touch.px<308 && touch.py>=105 && touch.py<171) {
                int col=(touch.px-164)/48, row=(touch.py-105)/22;
                int cell=row*3+col;
                const PanoLiveImage *live=pano_runtime_current();
                int link=street_nav_at_cell(live,g_explore_yaw,cell);
                if (link>=0) {
                    if (!pano_runtime_follow((unsigned)link))
                        set_status_message("Panorama loading; try again");
                    else if (!pano_runtime_busy())
                        g_street_swap_end_ms=osGetTime();
                } else if (live && live->provider==STREET_PROVIDER_PANORAMAX &&
                           ((cell==1 && live->next_id[0]) ||
                            (cell==7 && live->prev_id[0]))) {
                    if (!pano_runtime_step_at(cell==1 ? 1:-1,g_explore_yaw))
                        set_status_message(pano_runtime_status());
                    else if (!pano_runtime_busy())
                        g_street_swap_end_ms=osGetTime();
                }
            }
        }
        if (keys & KEY_X) {
            if (g_explore_gyro) {
                HIDUSER_DisableGyroscope();
                if(g_explore_accel) HIDUSER_DisableAccelerometer();
                g_explore_gyro=g_explore_accel=false;
            } else {
                g_explore_gyro=R_SUCCEEDED(HIDUSER_EnableGyroscope());
                g_explore_accel=g_explore_gyro &&
                    R_SUCCEEDED(HIDUSER_EnableAccelerometer());
                g_explore_gyro_counts_per_dps=0;
                pano_gyro_reset(&g_explore_gyro_filter);
                pano_motion_reset(&g_explore_motion);
                if(g_explore_gyro &&
                   (R_FAILED(HIDUSER_GetGyroscopeRawToDpsCoefficient(
                         &g_explore_gyro_counts_per_dps)) ||
                    g_explore_gyro_counts_per_dps<=0)){
                    HIDUSER_DisableGyroscope();
                    if(g_explore_accel) HIDUSER_DisableAccelerometer();
                    g_explore_gyro=g_explore_accel=false;
                }
            }
        }
        if (g_explore_gyro && (!pano_runtime_current() ||
                              pano_runtime_current()->is_pano)) {
            angularRate gyro;hidGyroRead(&gyro);
            accelVector accel={0};
            if(g_explore_accel) hidAccelRead(&accel);
            float pitch_raw=0,yaw_raw=0;
            if(pano_motion_project(&g_explore_motion,gyro.x,gyro.y,gyro.z,
                    accel.x,accel.y,accel.z,&pitch_raw,&yaw_raw))
                pano_gyro_step(&g_explore_gyro_filter,pitch_raw,yaw_raw,
                    g_explore_gyro_counts_per_dps,dt,
                    g_explore_gyro_inverted,&g_explore_pitch,&g_explore_yaw);
        }
    } else {
        float minimum = g_explore_mode == EXPLORE_TERRAIN ? 1.65f : 1.45f;
        if (keys & KEY_X) {
            if (g_explore_mode == EXPLORE_GLOBE && g_explore_goal <= minimum) {
                g_explore_goal = g_explore_distance;
                g_explore_dive_ms = now;
            } else g_explore_goal = fmaxf(minimum, g_explore_goal * .73f);
        }
        if (keys & KEY_Y) g_explore_goal = fminf(5.2f, g_explore_goal / .73f);
        g_explore_distance += (g_explore_goal - g_explore_distance) * fminf(1, dt * 12);
    }
    g_explore_yaw = remainderf(g_explore_yaw, 2 * (float)M_PI);
    float low = g_explore_mode == EXPLORE_TERRAIN ? .32f : -1.5533f;
    float high = g_explore_mode == EXPLORE_TERRAIN ? 1.36f : 1.5533f;
    g_explore_pitch = fmaxf(low, fminf(high, g_explore_pitch));
    if (keys & KEY_A) {
        if (g_explore_mode == EXPLORE_GLOBE) {
            g_explore_goal = g_explore_distance;
            g_explore_dive_ms = now;
        } else explore_show_map();
    }
}

static void render_explore(void) {
    float slider = g_explore_right ? osGet3DSliderState() : 0;
    bool stereo = slider > .005f;
    if (gfxIs3D() != stereo) gfxSet3D(stereo);
    if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) return;
    float fov = C3D_AngleFromDegrees(g_explore_mode == EXPLORE_STREET ? g_explore_fov : 40);
    float eye = stereo ? slider * .065f : 0;
    u64 frame_now=osGetTime();
    bool flying=g_explore_mode==EXPLORE_STREET && g_street_entering &&
                g_street_fly_ms>0 && !g_street_reveal_ms;
    if (flying) {
        /* Simulate a camera diving toward the selected map point using only
         * already-cached tile textures. Never download tiles for the effect. */
        float p=street_fly_progress(frame_now,g_street_enter_ms,g_street_fly_ms);
        float descend=fminf(1.0f,p/.75f);
        float pending=(frame_now>g_street_enter_ms+(u64)g_street_fly_ms)?
            (frame_now-g_street_enter_ms-g_street_fly_ms)*.001f:0.0f;
        float breathe=pending>0?.015f*sinf(pending*4.5f):0.0f;
        C2D_Prepare();
        C2D_TargetClear(top_screen,C2D_Color32(13,20,29,255));
        C2D_SceneBegin(top_screen);
        map_tiles_draw_fly(g_street_origin_lat,g_street_origin_lon,
                           g_street_origin_zoom,g_street_origin_x,g_street_origin_y,
                           street_fly_scale(descend)*(1.0f+breathe));
        float pulse=4.0f*sinf(frame_now*.009f);
        C2D_DrawCircleSolid(200,120,.75f,19.0f+descend*8.0f+pulse,
                            C2D_Color32(66,205,234,75));
        C2D_DrawCircleSolid(200,120,.78f,3.0f,C2D_Color32(130,245,255,220));
        if(pending>0) {
            float t=fmodf(pending*.8f,1.0f);
            C2D_DrawRectSolid(90,213,.85f,220,4,C2D_Color32(15,35,49,230));
            C2D_DrawRectSolid(90+t*174,213,.86f,46,4,C2D_Color32(96,218,237,255));
        }
        C2D_DrawRectSolid(0,0,.9f,400,240,
            C2D_Color32f(0.015f,0.035f,0.055f,.12f+.34f*descend));
    } else {
        explore_draw(top_screen, g_explore_yaw, g_explore_pitch,
                     g_explore_distance, fov, -eye);
        if (stereo)
            explore_draw(g_explore_right, g_explore_yaw, g_explore_pitch,
                         g_explore_distance, fov, eye);
    }
    C2D_Prepare();
    if (g_explore_dive_ms) {
        C2D_SceneBegin(top_screen);
        float fade = fmaxf(0, fminf(1, ((float)(frame_now - g_explore_dive_ms) - 250) / 180));
        C2D_DrawRectSolid(0, 0, 1, 400, 240, C2D_Color32f(0, 0, 0, fade));
    } else if (g_explore_mode==EXPLORE_STREET && !flying) {
        float darkness=0.0f;
        if (g_street_entering && g_street_reveal_ms) {
            int reveal_ms=g_street_fly_ms/4;
            if (reveal_ms<100) reveal_ms=100;
            float t=(frame_now-g_street_reveal_ms)/(float)reveal_ms;
            darkness=fmaxf(0.0f,.95f*(1.0f-t));
        }
        if (g_street_swap_start_ms) {
            float t=(frame_now-g_street_swap_start_ms)/90.0f;
            darkness=fmaxf(darkness,.82f*fminf(1.0f,t));
        } else if (g_street_swap_end_ms) {
            float t=(frame_now-g_street_swap_end_ms)/150.0f;
            if (t>=1.0f) g_street_swap_end_ms=0;
            else darkness=fmaxf(darkness,.82f*fmaxf(0.0f,1.0f-t));
        }
        if (darkness>0.0f) {
            C2D_SceneBegin(top_screen);
            C2D_DrawRectSolid(0,0,1,400,240,
                C2D_Color32f(0.0f,0.0f,0.0f,darkness));
        }
    }
    C2D_TargetClear(bottom_screen, C2D_Color32(13, 18, 26, 255));
    C2D_SceneBegin(bottom_screen);
    C2D_TextBufClear(g_ui_text_buf);
    u32 white = C2D_Color32(238, 246, 253, 255), dim = C2D_Color32(164, 190, 211, 255);
    u32 accent = C2D_Color32(113, 215, 235, 255);
    const char *title = g_explore_mode == EXPLORE_GLOBE ? "World globe" :
                        g_explore_mode == EXPLORE_TERRAIN ? "Terrain in 3D" : "Street View Explorer";
    char coordinates[58];
    snprintf(coordinates, sizeof(coordinates), "%.2f, %.2f",
             g_explore_pitch * (180.0 / M_PI), g_explore_yaw * (180.0 / M_PI));
    const PanoLiveImage *live = g_explore_mode == EXPLORE_STREET ?
        pano_runtime_current() : NULL;
    char live_label[96];
    if (live) snprintf(live_label, sizeof(live_label), "%s %s",
                       street_provider_name(live->provider),
                       live->is_pano ? "360 panorama" : "street photo");
    const char *place = g_explore_mode == EXPLORE_STREET ?
        (live ? live_label : "Loading selected map location") :
        g_explore_mode == EXPLORE_TERRAIN ? explore_terrain_place.name : coordinates;
    C2D_DrawRectSolid(0, 0, .1f, 320, 55, C2D_Color32(25, 37, 53, 255));
    C2D_DrawRectSolid(0, 53, .2f, 320, 2, accent);
    draw_ui_text(12, 9, .58f, .58f, white, 0, title);
    draw_ui_text(12, 34, .40f, .40f, dim, 0, place);
    if (g_explore_mode == EXPLORE_STREET) {
        char source_label[64];
        snprintf(source_label,sizeof(source_label),"Y / Source: %s  [Select]",
                 street_provider_name(pano_runtime_provider()));
        C2D_DrawRectSolid(12,58,.15f,296,27,C2D_Color32(30,48,65,255));
        draw_ui_text(16,67,.38f,.38f,accent,0,source_label);
        draw_ui_text(14,91,.32f,.32f,dim,0,pano_runtime_status());
        /* Cached/offline map tiles only; any missing tile enters a throttled
         * low-priority queue, never synchronous HTTP on the render thread. */
        const double mlat=live?live->lat:map_state.lat;
        const double mlon=live?live->lon:map_state.lon;
        map_tiles_draw_minimap(mlat,mlon,15,12,106,144,66,
                               !pano_runtime_network_active());
        if (live) {
            for (unsigned i=0;i<live->link_count;i++) {
                const PanoLiveLink *l=&live->links[i];
                if (!l->has_location) continue;
                int cx,cy,nx,ny;
                lat_lon_to_pixel(mlat,mlon,15,&cx,&cy);
                lat_lon_to_pixel(l->lat,l->lon,15,&nx,&ny);
                int dx=nx-cx;
                int world=(1<<15)*256;
                if (dx>world/2)dx-=world;
                if (dx<-world/2)dx+=world;
                float px=84+dx*.34f,py=139+(ny-cy)*.34f;
                if (px>=15 && px<153 && py>=109 && py<169)
                    C2D_DrawCircleSolid(px,py,.65f,2.2f,accent);
            }
        }
        /* A short cyan pointing ray plus position dot shows camera direction. */
        float heading=live ? live->azimuth+g_explore_yaw*180.f/(float)M_PI : 0.f;
        float ang=heading*(float)(M_PI/180.0);
        C2D_DrawCircleSolid(84+13*sinf(ang),139-13*cosf(ang),.72f,3.2f,accent);
        C2D_DrawCircleSolid(84,139,.73f,4.f,white);
        static const char *arrows[]={"NW","N","NE","W","","E","SW","S","SE"};
        for(int cell=0;cell<9;cell++) {
            int row=cell/3,col=cell%3;
            float x=164+col*48, y=105+row*22;
            int link=street_nav_at_cell(live,g_explore_yaw,cell);
            bool enabled=link>=0 || (live && live->provider==STREET_PROVIDER_PANORAMAX &&
                ((cell==1 && live->next_id[0]) || (cell==7 && live->prev_id[0])));
            C2D_DrawRectSolid(x+2,y+1,.35f,44,20,
                C2D_Color32(enabled?36:30,enabled?108:49,enabled?126:63,255));
            if(cell!=4)draw_ui_text(x+14,y+4,.34f,.34f,
                enabled?white:dim,0,arrows[cell]);
        }
        draw_ui_text(14,174,.27f,.27f,dim,0,
            live ? live->credit : "Live panorama loading...");
        if (live && live->license[0])
            draw_ui_text(200,174,.26f,.26f,dim,0,live->license);
        if (g_provider_picker) {
            C2D_DrawRectSolid(10,57,.78f,300,127,C2D_Color32(20,33,48,255));
            for(int i=0;i<STREET_PROVIDER_COUNT;i++) {
                float y=60+i*20;
                if(i==pano_runtime_provider())
                    C2D_DrawRectSolid(14,y,.80f,292,19,C2D_Color32(36,91,106,255));
                draw_ui_text(19,y+4,.38f,.38f,white,0,
                             street_provider_name((StreetProvider)i));
            }
        }
    } else {
        draw_ui_text(14, 76, .44f, .44f, white, 0, "Circle Pad / D-Pad  Rotate");
        draw_ui_text(14, 101, .44f, .44f, white, 0, "X / Y  Zoom in / out");
        draw_ui_text(14, 127, .40f, .40f, accent, 0,
                     g_explore_mode == EXPLORE_GLOBE ? "Zoom in fully to enter the map" :
                     stereo ? "3D slider  Stereo active" : "Raise 3D slider on supported systems");
        draw_ui_text(14, 162, .36f, .36f, dim, 0,
                     g_explore_mode == EXPLORE_GLOBE ? "NASA / GSFC Blue Marble" : "USGS 3DEP  |  2.2x vertical exaggeration");
    }
    C2D_DrawRectSolid(0, 184, .1f, 320, 56, C2D_Color32(23, 32, 45, 255));
    if (g_explore_mode==EXPLORE_STREET) {
        draw_ui_text(12,190,.34f,.34f,dim,0,
            live && !live->is_pano ? "Flat photo  (gyro only for 360)" :
            g_explore_gyro && !g_explore_gyro_filter.ready ? "Gyro: HOLD STILL to calibrate" :
            g_explore_gyro ? "X Gyro ON  Select Recenter" : "X Gyro OFF  Select Recenter");
        char zoomlabel[80];
        snprintf(zoomlabel,sizeof(zoomlabel),"D-Up/Down Zoom  (%d deg)",(int)g_explore_fov);
        draw_ui_text(12,205,.39f,.39f,white,0,zoomlabel);
        draw_ui_text(12,224,.33f,.33f,dim,0,
            "L/R Move   L+Down Retry   A Map   B Back");
    } else draw_ui_text(14, 196, .44f, .44f, dim, 0, "A  Open map     B  Back");
    if (g_debug_overlay) {
        char fps[24];
        snprintf(fps, sizeof(fps), "%lu FPS", (unsigned long)g_fps);
        draw_ui_text(306, 196, .4f, .4f, accent, C2D_AlignRight, fps);
    }
    C3D_FrameEnd(0);
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
    if (g_target_zoom < 18) {
        g_target_zoom++;
        g_last_interaction_ms = osGetTime();
    }
}

void zoom_out(void) {
    if (g_target_zoom > 0) {
        g_target_zoom--;
        g_last_interaction_ms = osGetTime();
    } else if (map_state.zoom==0) {
        /* Only a distinct, subsequent keydown after Z0 is reached enters the
         * globe. Holding Y or pressing during zoom animation does nothing. */
        open_explore(EXPLORE_GLOBE);
        if (g_explore_mode == EXPLORE_GLOBE) g_globe_from_zoom0=true;
    }
}

void pan_map(int dx, int dy) {
    double world = (double)(1 << map_state.zoom) * TILE_SIZE;
    double y = (1.0 - asinh(tan(g_target_lat * M_PI / 180.0)) / M_PI) * world / 2.0;
    y = fmax(0.0, fmin(world, y + dy));
    g_target_lat = atan(sinh(M_PI * (1.0 - 2.0 * y / world))) * 180.0 / M_PI;
    g_target_lon = fmod(g_target_lon + dx * 360.0 / world + 540.0, 360.0) - 180.0;
}

/* The nearby imagery list is reused from the last Street View discovery.
 * These are truthful photo markers, not fabricated road coverage lines.
 * No network requests, allocations or image decoding occur while drawing. */
static void draw_street_coverage(void) {
    if (!pano_runtime_coverage_enabled() || map_state.zoom < 12) return;
    unsigned count = pano_runtime_coverage_count();
    if (!count) return;
    int cx, cy;
    lat_lon_to_pixel(map_state.lat,map_state.lon,map_state.zoom,&cx,&cy);
    const int world = (1 << map_state.zoom) * TILE_SIZE;
    for (unsigned i=0;i<count;i++) {
        const PanoLiveImage *p = pano_runtime_coverage_at(i);
        if (!p) continue;
        int px,py;
        lat_lon_to_pixel(p->lat,p->lon,map_state.zoom,&px,&py);
        int dx=px-cx;
        if (dx>world/2) dx-=world;
        if (dx<-world/2) dx+=world;
        float x=(float)(200+dx+map_state.offset_x);
        float y=(float)(120+py-cy+map_state.offset_y);
        if (x<-6 || x>406 || y<-6 || y>246)continue;
        u32 color=p->provider==STREET_PROVIDER_MAPILLARY ?
            C2D_Color32(53,181,255,235) :
            p->provider==STREET_PROVIDER_KARTAVIEW ?
            C2D_Color32(255,196,81,235) : C2D_Color32(82,224,170,235);
        C2D_DrawCircleSolid(x,y,.86f,p->is_pano?4.0f:2.8f,
                            C2D_Color32(13,20,27,245));
        C2D_DrawCircleSolid(x,y,.87f,p->is_pano?2.9f:1.8f,color);
    }
}

static void draw_gps_fix(void) {
    GpsFix fix;
    if (!gps_get_fix(&fix)) return;
    int zoom = map_state.zoom, world = (1 << zoom) * TILE_SIZE;
    int cx, cy, px, py;
    lat_lon_to_pixel(map_state.lat, map_state.lon, zoom, &cx, &cy);
    lat_lon_to_pixel(fmax(-85.0511, fmin(85.0511, fix.lat)), fix.lon, zoom, &px, &py);
    int dx = (px - cx + world + world / 2) % world - world / 2;
    float x = SCREEN_WIDTH_TOP / 2 + dx, y = SCREEN_HEIGHT_TOP / 2 + py - cy;
    double meters_per_pixel = 40075016.686 * cos(fix.lat * M_PI / 180) / world;
    float radius = fix.accuracy_m < 0 || meters_per_pixel <= 0 ? 0 :
                   fmin(600, fix.accuracy_m / meters_per_pixel);
    if (x + radius < -6 || x - radius > 406 || y + radius < -6 || y - radius > 246) return;
    if (radius >= 4) C2D_DrawCircleSolid(x, y, .66f, radius, C2D_Color32(71, 169, 244, 54));
    C2D_DrawCircleSolid(x, y, .67f, 5, C2D_Color32(248, 252, 255, 255));
    C2D_DrawCircleSolid(x, y, .68f, 3.5f, C2D_Color32(50, 157, 241, 255));
}

void render_map(void) {
    // Start rendering frame
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    // Show QR code on top screen when GPS server is running in settings
    bool show_qr = (!g_bottom_modal && g_screen_mode == APP_SCREEN_SETTINGS &&
                    g_settings_tab == SETTINGS_TAB_GPS &&
                    g_qr_valid);

    // Render top screen
    if (show_qr) {
        C2D_TargetClear(top_screen, C2D_Color32f(1.0f, 1.0f, 1.0f, 1.0f));
        C2D_SceneBegin(top_screen);
        render_qr_top_screen();
    } else {
        if (g_night_mode) {
            C2D_TargetClear(top_screen, C2D_Color32f(0.05f, 0.06f, 0.08f, 1.0f));
        } else {
            C2D_TargetClear(top_screen, C2D_Color32f(0.7f, 0.8f, 1.0f, 1.0f));
        }
        C2D_SceneBegin(top_screen);

        map_tiles_set_coverage(g_show_coverage);
        map_tiles_set_gps_follow(map_state.gps_tracking);
        map_tiles_render(map_state.lat, map_state.lon, map_state.zoom, 
                        map_state.offset_x, map_state.offset_y, 
                        SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP, true);
        draw_street_coverage();

        render_route_overlay(map_state.lat, map_state.lon, map_state.zoom, SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP);
        draw_gps_fix();

        if (g_night_mode) {
            C2D_DrawRectSolid(0.0f, 0.0f, 0.95f, (float)SCREEN_WIDTH_TOP, (float)SCREEN_HEIGHT_TOP,
                              C2D_Color32f(0.0f, 0.0f, 0.0f, 0.35f));
        }
        if (g_map_fade_ms) {
            u64 elapsed = osGetTime() - g_map_fade_ms;
            if (elapsed < 240)
                C2D_DrawRectSolid(0, 0, 1, 400, 240, C2D_Color32f(0, 0, 0, 1 - elapsed / 240.0f));
            else g_map_fade_ms = 0;
        }
    }

    C2D_TargetClear(bottom_screen, C2D_Color32f(0.05f, 0.07f, 0.10f, 1.0f));
    C2D_SceneBegin(bottom_screen);
    C2D_TextBufClear(g_ui_text_buf);
    if (g_bottom_modal) {
        g_bottom_modal();
    } else if (g_screen_mode == APP_SCREEN_SETTINGS) {
        render_settings_panel();
    } else {
        render_bottom_panel();
    }

    // End frame rendering
    C3D_FrameEnd(0);
}

/* Preserve uniform glyph proportions; font size and weight preferences apply
 * to the entire application's lightweight UI without external fonts. */
static void draw_ui_text(float x, float y, float scale_x, float scale_y, u32 color, u32 flags, const char* text) {
    if (!g_ui_text_buf || !text || !text[0]) return;
    const UiAccessConfig *pref=ui_access_get();
    if(pref->high_contrast){
        unsigned r=color&255u,g=(color>>8)&255u,b=(color>>16)&255u;
        unsigned maximum=r>g?(r>b?r:b):(g>b?g:b);
        if(maximum>=120 && maximum<220 && ((color>>24)&255u)>195)
            color=C2D_Color32(221,237,247,(color>>24)&255u);
    }
    if (fabsf(scale_x-scale_y)<0.01f && scale_y<.61f) {
        scale_x*=ui_access_scale();
        scale_y*=ui_access_scale();
    }
    C2D_Text ui_text;
    C2D_TextParse(&ui_text,g_ui_text_buf,text);
    C2D_TextOptimize(&ui_text);
    C2D_DrawText(&ui_text,flags|C2D_WithColor,x,y,.98f,scale_x,scale_y,color);
    if(pref->weight>0){
        float spread=pref->weight==2?.80f:.40f;
        C2D_DrawText(&ui_text,flags|C2D_WithColor,x+spread,y,.985f,
                     scale_x,scale_y,color);
    }
}
/* Measure text instead of compressing it horizontally. UTF-8 is always
 * trimmed at complete codepoint boundaries, with an optional ellipsis. */
static void draw_ui_fit_line(float x,float y,float scale,u32 color,
                             const char *input,float max_width){
    if(!input||!input[0]||!g_ui_text_buf)return;
    char line[192];
    size_t total=strlen(input);
    if(total>=sizeof(line))total=sizeof(line)-1;
    size_t lo=0,hi=total;
    float real_scale=scale*ui_access_scale();
    while(lo<hi){
        size_t mid=(lo+hi+1)/2;
        while(mid>0 && mid<total && ((unsigned char)input[mid]&0xc0)==0x80)mid--;
        if(mid<=lo)mid=lo+1;
        if(mid>total)mid=total;
        while(mid<total && ((unsigned char)input[mid]&0xc0)==0x80)mid++;
        if(mid>=sizeof(line))mid=sizeof(line)-1;
        memcpy(line,input,mid);line[mid]=0;
        C2D_Text measure;
        C2D_TextParse(&measure,g_ui_text_buf,line);
        float width=measure.width*real_scale;
        if(width<=max_width)lo=mid;
        else hi=mid-1;
    }
    size_t take=lo;
    /* Bound the ellipsis and keep complete UTF-8 code points. */
    if(take>sizeof(line)-4)take=sizeof(line)-4;
    while(take>0 && ((unsigned char)input[take]&0xc0)==0x80)take--;
    if(take<strlen(input)){
        while(take>0 && ((unsigned char)input[take]&0xc0)==0x80)take--;
        while(take>0 && (input[take-1]==' '||input[take-1]==','))take--;
        while(take>0){
            memcpy(line,input,take);memcpy(line+take,"...",4);
            C2D_Text t;C2D_TextParse(&t,g_ui_text_buf,line);
            if(t.width*real_scale<=max_width)break;
            take--;
            while(take>0 && ((unsigned char)input[take]&0xc0)==0x80)take--;
        }
    }else{memcpy(line,input,take);line[take]=0;}
    if(!take)strcpy(line,"...");
    draw_ui_text(x,y,scale,scale,color,0,line);
}
static void draw_ui_address_two_lines(const char *name,u32 color){
    if(!name)return;
    char left[160],right[160];
    size_t total=strlen(name),cut=0;
    /* A comma nearest the middle makes addresses far easier to scan. */
    for(size_t i=0;i<total && i<75;i++)
        if(name[i]==',' && i>14 && i<65)cut=i+1;
    if(!cut){
        cut=total>42?42:total;
        while(cut<total && cut>16 && name[cut]!=' ')cut--;
        if(cut<=16)cut=total>42?42:total;
    }
    if(cut>=sizeof(left))cut=sizeof(left)-1;
    while(cut>0 && ((unsigned char)name[cut]&0xc0)==0x80)cut--;
    memcpy(left,name,cut);left[cut]=0;
    while(cut<total && (name[cut]==' '||name[cut]==','))cut++;
    snprintf(right,sizeof(right),"%s",name+cut);
    draw_ui_fit_line(17,67,.48f,color,left,283);
    if(right[0])draw_ui_fit_line(17,88,.45f,color,right,283);
}

static void render_qr_top_screen(void) {
    if (!g_qr_valid || g_qr_size == 0) return;

    int quiet = 4;
    int total_mods = g_qr_size + 2 * quiet;
    int px = 240 / total_mods;
    if (px < 2) px = 2;
    int total_px = total_mods * px;
    /* Integer pixel-aligned origins avoid half-pixel QR edges on versions
     * whose total module count is odd (e.g. V2 and V4). */
    int x0 = (400 - total_px) / 2;
    int y0 = (240 - total_px) / 2;

    /* Draw black modules (background is already white) */
    for (int r = 0; r < g_qr_size; r++) {
        for (int c = 0; c < g_qr_size; c++) {
            if (g_qr_modules[r * g_qr_size + c]) {
                float mx = x0 + (float)(c + quiet) * px;
                float my = y0 + (float)(r + quiet) * px;
                C2D_DrawRectSolid(mx, my, 0.5f, (float)px, (float)px,
                                  C2D_Color32f(0.0f, 0.0f, 0.0f, 1.0f));
            }
        }
    }

    /* Keep the entire top screen white except for QR modules. A header at
     * y=2 previously overlapped the four-module quiet zone on some sizes,
     * which could prevent phone cameras from scanning an otherwise valid QR.
     * Connection instructions and live status remain on the bottom panel. */
}

static SettingsHitTarget get_settings_hit(const touchPosition* touch) {
    if (!touch) return SETTINGS_HIT_NONE;

    /* Close button — top-right header area */
    if (point_in_rect(touch->px, touch->py, 246, 10, 62, 24)) return SETTINGS_HIT_CLOSE;

    static const SettingsHitTarget tabs[] = {
        SETTINGS_HIT_TAB_VIEW, SETTINGS_HIT_TAB_TILES, SETTINGS_HIT_TAB_DATA,
        SETTINGS_HIT_TAB_GPS, SETTINGS_HIT_TAB_EXPLORE, SETTINGS_HIT_TAB_TEXT
    };
    for (int i = 0; i < SETTINGS_TAB_COUNT; i++)
        if (point_in_rect(touch->px, touch->py, 5 + i * 52, 50, 50, 24)) return tabs[i];

    switch (g_settings_tab) {
        case SETTINGS_TAB_VIEW:
            if (point_in_rect(touch->px, touch->py, 172, 101, 56, 28)) return SETTINGS_HIT_TILE_STREET;
            if (point_in_rect(touch->px, touch->py, 238, 101, 58, 28)) return SETTINGS_HIT_TILE_SATELLITE;
            if (point_in_rect(touch->px, touch->py, 12, 150, 142, 68)) return SETTINGS_HIT_NIGHT_MODE;
            if (point_in_rect(touch->px, touch->py, 166, 150, 142, 68)) return SETTINGS_HIT_DEBUG_OVERLAY;
            break;
        case SETTINGS_TAB_TILES:
            if (point_in_rect(touch->px, touch->py, 214, 98, 28, 28)) return SETTINGS_HIT_PREFETCH_RING_DEC;
            if (point_in_rect(touch->px, touch->py, 272, 98, 28, 28)) return SETTINGS_HIT_PREFETCH_RING_INC;
            if (point_in_rect(touch->px, touch->py, 214, 162, 28, 28)) return SETTINGS_HIT_PREFETCH_RATE_DEC;
            if (point_in_rect(touch->px, touch->py, 272, 162, 28, 28)) return SETTINGS_HIT_PREFETCH_RATE_INC;
            break;
        case SETTINGS_TAB_DATA:
            if (point_in_rect(touch->px, touch->py, 12, 86, 142, 58)) return SETTINGS_HIT_PROXY_TOGGLE;
            if (point_in_rect(touch->px, touch->py, 166, 86, 142, 58)) return SETTINGS_HIT_CACHE_TOGGLE;
            if (point_in_rect(touch->px, touch->py, 12, 154, 142, 60)) return SETTINGS_HIT_CLEAR_CACHE;
            if (point_in_rect(touch->px, touch->py, 166, 154, 142, 60)) return SETTINGS_HIT_DOWNLOAD_REGION;
            break;
        case SETTINGS_TAB_EXPLORE:
            if (point_in_rect(touch->px, touch->py, 12, 80, 296, 24)) return SETTINGS_HIT_GLOBE;
            if (point_in_rect(touch->px, touch->py, 12, 107, 296, 24)) return SETTINGS_HIT_TERRAIN;
            if (point_in_rect(touch->px, touch->py, 12, 134, 296, 24)) return SETTINGS_HIT_STREET_EXPLORER;
            if (point_in_rect(touch->px, touch->py, 12, 161, 296, 24)) return SETTINGS_HIT_STREETGUESS;
            if (point_in_rect(touch->px, touch->py, 12, 225, 296, 15)) return SETTINGS_HIT_STREET_COVERAGE;
            break;
        case SETTINGS_TAB_TEXT:
            if (point_in_rect(touch->px,touch->py,12,103,296,34))
                return touch->px<110?SETTINGS_HIT_TEXT_NORMAL:
                       touch->px<209?SETTINGS_HIT_TEXT_LARGE:SETTINGS_HIT_TEXT_XL;
            if (point_in_rect(touch->px,touch->py,12,161,296,32))
                return touch->px<110?SETTINGS_HIT_TEXT_REGULAR:
                       touch->px<209?SETTINGS_HIT_TEXT_MEDIUM:SETTINGS_HIT_TEXT_BOLD;
            if (point_in_rect(touch->px,touch->py,12,203,296,30))
                return SETTINGS_HIT_TEXT_CONTRAST;
            break;
        case SETTINGS_TAB_GPS:
            /* [Client] / [Server] source mode buttons (same position as tile source) */
            if (point_in_rect(touch->px, touch->py, 172, 101, 56, 28)) return SETTINGS_HIT_GPS_MODE_CLIENT;
            if (point_in_rect(touch->px, touch->py, 238, 101, 58, 28)) return SETTINGS_HIT_GPS_MODE_SERVER;
            /* Info / start-stop card */
            if (point_in_rect(touch->px, touch->py, 12, 150, 296, 74)) return SETTINGS_HIT_GPS_START_STOP;
            break;
        default:
            break;
    }

    return SETTINGS_HIT_NONE;
}

static void apply_settings_hit(SettingsHitTarget hit) {
    if (hit == SETTINGS_HIT_NONE) return;

    g_last_settings_hit = hit;
    g_last_settings_hit_ms = osGetTime();

    switch (hit) {
        case SETTINGS_HIT_CLOSE:
            close_menu("Closed settings");
            break;
        case SETTINGS_HIT_TAB_VIEW:
            g_settings_tab = SETTINGS_TAB_VIEW;
            break;
        case SETTINGS_HIT_TAB_TILES:
            g_settings_tab = SETTINGS_TAB_TILES;
            break;
        case SETTINGS_HIT_TAB_DATA:
            g_settings_tab = SETTINGS_TAB_DATA;
            break;
        case SETTINGS_HIT_TAB_GPS:
            g_settings_tab = SETTINGS_TAB_GPS;
            break;
        case SETTINGS_HIT_TAB_EXPLORE:
            g_settings_tab = SETTINGS_TAB_EXPLORE;
            break;
        case SETTINGS_HIT_TAB_TEXT:
            g_settings_tab = SETTINGS_TAB_TEXT;
            break;
        case SETTINGS_HIT_TEXT_NORMAL:
        case SETTINGS_HIT_TEXT_LARGE:
        case SETTINGS_HIT_TEXT_XL:
            ui_access_set(hit-SETTINGS_HIT_TEXT_NORMAL,ui_access_get()->weight,
                          ui_access_get()->high_contrast);
            ui_access_save(UI_ACCESS_CFG);
            break;
        case SETTINGS_HIT_TEXT_REGULAR:
        case SETTINGS_HIT_TEXT_MEDIUM:
        case SETTINGS_HIT_TEXT_BOLD:
            ui_access_set(ui_access_get()->size,hit-SETTINGS_HIT_TEXT_REGULAR,
                          ui_access_get()->high_contrast);
            ui_access_save(UI_ACCESS_CFG);
            break;
        case SETTINGS_HIT_TEXT_CONTRAST:
            ui_access_set(ui_access_get()->size,ui_access_get()->weight,
                          !ui_access_get()->high_contrast);
            ui_access_save(UI_ACCESS_CFG);
            break;
        case SETTINGS_HIT_GLOBE:
            open_explore(EXPLORE_GLOBE);
            break;
        case SETTINGS_HIT_TERRAIN:
            open_explore(EXPLORE_TERRAIN);
            break;
        case SETTINGS_HIT_STREET_EXPLORER:
            open_explore(EXPLORE_STREET);
            break;
        case SETTINGS_HIT_STREETGUESS:
            close_menu(NULL);
            if (!guess_ui_enter())
                set_status_message("StreetGuess UI could not start");
            break;
        case SETTINGS_HIT_STREET_COVERAGE:
            pano_runtime_set_coverage(!pano_runtime_coverage_enabled());
            set_status_message(pano_runtime_coverage_enabled() ?
                "Street imagery markers ON" : "Street imagery markers OFF");
            break;
        case SETTINGS_HIT_TILE_STREET:
            if (network_get_tile_source() != TILE_SOURCE_STREET) {
                network_set_tile_source(TILE_SOURCE_STREET);
                map_tiles_clear_cache();
                map_tiles_prefetch_reset();
                set_status_message("Tile source: street");
            }
            break;
        case SETTINGS_HIT_TILE_SATELLITE:
            if (network_get_tile_source() != TILE_SOURCE_SATELLITE) {
                network_set_tile_source(TILE_SOURCE_SATELLITE);
                map_tiles_clear_cache();
                map_tiles_prefetch_reset();
                set_status_message("Tile source: satellite");
            }
            break;
        case SETTINGS_HIT_NIGHT_MODE:
            g_night_mode = !g_night_mode;
            set_status_message(g_night_mode ? "Night mode ON" : "Night mode OFF");
            break;
        case SETTINGS_HIT_DEBUG_OVERLAY:
            g_debug_overlay = !g_debug_overlay;
            set_status_message(g_debug_overlay ? "Debug overlay ON" : "Debug overlay OFF");
            break;
        case SETTINGS_HIT_PREFETCH_RING_DEC:
            if (g_prefetch_ring > 0) {
                g_prefetch_ring--;
                map_tiles_prefetch_reset();
                set_status_message("Prefetch ring updated");
            }
            break;
        case SETTINGS_HIT_PREFETCH_RING_INC:
            if (g_prefetch_ring < 2) {
                g_prefetch_ring++;
                map_tiles_prefetch_reset();
                set_status_message("Prefetch ring updated");
            }
            break;
        case SETTINGS_HIT_PREFETCH_RATE_DEC:
            if (g_prefetch_every_n_frames > 1) {
                g_prefetch_every_n_frames--;
                set_status_message("Prefetch cadence updated");
            }
            break;
        case SETTINGS_HIT_PREFETCH_RATE_INC:
            if (g_prefetch_every_n_frames < 10) {
                g_prefetch_every_n_frames++;
                set_status_message("Prefetch cadence updated");
            }
            break;
        case SETTINGS_HIT_PROXY_TOGGLE:
            if (!network_proxy_available()) {
                set_status_message("No proxy configured");
                break;
            }
            network_set_proxy_enabled(!network_get_proxy_enabled());
            map_tiles_clear_cache();
            map_tiles_prefetch_reset();
            set_status_message(network_get_proxy_enabled() ? "Proxy ON" : "Proxy OFF");
            break;
        case SETTINGS_HIT_CACHE_TOGGLE:
            network_set_disk_cache_enabled(!network_get_disk_cache_enabled());
            set_status_message(network_get_disk_cache_enabled() ? "SD cache ON" : "SD cache OFF");
            break;
        case SETTINGS_HIT_CLEAR_CACHE:
            if (network_clear_disk_tile_cache()) {
                set_status_message("SD cache cleared");
            } else {
                set_status_message("SD cache clear failed");
            }
            break;
        case SETTINGS_HIT_DOWNLOAD_REGION:
            open_offline_regions();
            break;

        /* ---- GPS tab -------------------------------------------------- */
        case SETTINGS_HIT_GPS_MODE_CLIENT:
            if (gps_is_connected()) { gps_cleanup(); }
            gps_set_source_mode(GPS_SOURCE_NMEA_CLIENT);
            g_qr_valid = false;
            set_status_message("GPS: NMEA TCP client mode");
            break;
        case SETTINGS_HIT_GPS_MODE_SERVER:
            log_write("[SETTINGS] Server button tapped\n");
            if (gps_is_connected()) {
                log_write("[SETTINGS] GPS was connected, cleaning up first\n");
                gps_cleanup();
            }
            gps_set_source_mode(GPS_SOURCE_HTTPS_SERVER);
            g_qr_valid = false;
            /* Auto-start the server so the QR code appears immediately */
            log_write("[SETTINGS] Calling gps_init() for HTTPS server...\n");
            if (gps_init()) {
                log_write("[SETTINGS] gps_init() succeeded\n");
                char svr_url[64];
                gps_get_server_url(svr_url, sizeof(svr_url));
                log_write("[SETTINGS] server URL = '%s'\n", svr_url);
                if (svr_url[0] && qr_encode(svr_url, strlen(svr_url),
                                             g_qr_modules, &g_qr_size)) {
                    g_qr_valid = true;
                    log_write("[SETTINGS] QR code generated, size=%d\n", g_qr_size);
                } else {
                    log_write("[SETTINGS] QR encode FAILED (url='%s', len=%d)\n", svr_url, (int)strlen(svr_url));
                }
                set_status_message(svr_url[0] ? svr_url : "GPS server started");
            } else {
                log_write("[SETTINGS] gps_init() FAILED\n");
                set_status_message("GPS server failed to start");
            }
            break;
        case SETTINGS_HIT_GPS_START_STOP:
            log_write("[SETTINGS] GPS start/stop card tapped, mode=%d connected=%d\n",
                      gps_get_source_mode(), gps_is_connected());
            if (gps_get_source_mode() == GPS_SOURCE_HTTPS_SERVER) {
                if (gps_is_connected()) {
                    log_write("[SETTINGS] Stopping GPS server\n");
                    gps_cleanup();
                    g_qr_valid = false;
                    set_status_message("GPS server stopped");
                } else {
                    log_write("[SETTINGS] Starting GPS server via start/stop card\n");
                    if (gps_init()) {
                        log_write("[SETTINGS] gps_init() succeeded (start/stop)\n");
                        char svr_url[64];
                        gps_get_server_url(svr_url, sizeof(svr_url));
                        log_write("[SETTINGS] server URL = '%s'\n", svr_url);
                        if (svr_url[0] && qr_encode(svr_url, strlen(svr_url),
                                                     g_qr_modules, &g_qr_size)) {
                            g_qr_valid = true;
                            log_write("[SETTINGS] QR generated, size=%d\n", g_qr_size);
                        } else {
                            log_write("[SETTINGS] QR encode FAILED\n");
                        }
                        set_status_message(svr_url[0] ? svr_url : "GPS server started");
                    } else {
                        log_write("[SETTINGS] gps_init() FAILED (start/stop)\n");
                        set_status_message("GPS server failed to start");
                    }
                }
            }
            break;
        default:
            break;
    }
}

static void render_settings_panel(void) {
    static const char* tab_names[SETTINGS_TAB_COUNT] = { "View", "Tiles", "Data", "GPS", "Explore", "Text" };
    static const char* tab_hints[SETTINGS_TAB_COUNT] = {
        "Style and live overlays",
        "Tile loading behavior",
        "Network and storage",
        "GPS source and server",
        "Globe, terrain and street-level photos",
        "Readability, text size and weight"
    };
    u64 now_ms = osGetTime();
    char line[128];

    C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, (float)SCREEN_WIDTH_BOTTOM, (float)SCREEN_HEIGHT_BOTTOM,
                      C2D_Color32f(0.05f, 0.07f, 0.09f, 1.0f));
    C2D_DrawRectSolid(0.0f, 0.0f, 0.1f, (float)SCREEN_WIDTH_BOTTOM, 42.0f,
                      C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
    C2D_DrawRectSolid(0.0f, 42.0f, 0.1f, (float)SCREEN_WIDTH_BOTTOM, 38.0f,
                      C2D_Color32f(0.08f, 0.10f, 0.13f, 1.0f));

    draw_ui_text(14.0f, 10.0f, 0.66f, 0.66f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "Settings");
    draw_ui_text(14.0f, 30.0f, 0.34f, 0.34f, C2D_Color32f(0.72f, 0.81f, 0.89f, 1.0f), 0, tab_hints[g_settings_tab]);

    {
        bool pressed = is_recent_settings_hit(SETTINGS_HIT_CLOSE, now_ms);
        u32 fill = pressed ? C2D_Color32f(0.96f, 0.66f, 0.30f, 1.0f) : C2D_Color32f(0.20f, 0.25f, 0.31f, 1.0f);
        u32 text = pressed ? C2D_Color32f(0.16f, 0.09f, 0.03f, 1.0f) : C2D_Color32f(0.95f, 0.97f, 0.99f, 1.0f);
        C2D_DrawRectSolid(246.0f, 10.0f, 0.2f, 62.0f, 24.0f, fill);
        draw_ui_text(277.0f, 16.0f, 0.40f, 0.40f, text, C2D_AlignCenter, "Close");
    }

    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        float x = 5.0f + (float)i * 52.0f;
        bool selected = (i == g_settings_tab);
        bool pressed = (i == SETTINGS_TAB_VIEW  && is_recent_settings_hit(SETTINGS_HIT_TAB_VIEW,  now_ms)) ||
                       (i == SETTINGS_TAB_TILES && is_recent_settings_hit(SETTINGS_HIT_TAB_TILES, now_ms)) ||
                       (i == SETTINGS_TAB_DATA  && is_recent_settings_hit(SETTINGS_HIT_TAB_DATA,  now_ms)) ||
                       (i == SETTINGS_TAB_GPS   && is_recent_settings_hit(SETTINGS_HIT_TAB_GPS,   now_ms)) ||
                       (i == SETTINGS_TAB_EXPLORE && is_recent_settings_hit(SETTINGS_HIT_TAB_EXPLORE, now_ms)) ||
                       (i == SETTINGS_TAB_TEXT && is_recent_settings_hit(SETTINGS_HIT_TAB_TEXT, now_ms));
        u32 fill = selected ? C2D_Color32f(0.28f, 0.48f, 0.66f, 1.0f)
                            : (pressed ? C2D_Color32f(0.24f, 0.29f, 0.35f, 1.0f) : C2D_Color32f(0.15f, 0.18f, 0.22f, 1.0f));
        C2D_DrawRectSolid(x, 50.0f, 0.2f, 50.0f, 24.0f, fill);
        draw_ui_text(x + 25.0f, 56.0f, 0.35f, 0.35f,
                     selected ? C2D_Color32f(0.97f, 0.99f, 1.0f, 1.0f) : C2D_Color32f(0.80f, 0.86f, 0.92f, 1.0f),
                     C2D_AlignCenter,
                     tab_names[i]);
    }

    if (g_settings_tab == SETTINGS_TAB_VIEW) {
        bool street_active = network_get_tile_source() == TILE_SOURCE_STREET;
        bool sat_active = network_get_tile_source() == TILE_SOURCE_SATELLITE;
        bool street_pressed = is_recent_settings_hit(SETTINGS_HIT_TILE_STREET, now_ms);
        bool sat_pressed = is_recent_settings_hit(SETTINGS_HIT_TILE_SATELLITE, now_ms);
        bool night_pressed = is_recent_settings_hit(SETTINGS_HIT_NIGHT_MODE, now_ms);
        bool debug_pressed = is_recent_settings_hit(SETTINGS_HIT_DEBUG_OVERLAY, now_ms);

        C2D_DrawRectSolid(12.0f, 86.0f, 0.2f, 296.0f, 52.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        C2D_DrawRectSolid(12.0f, 86.0f, 0.3f, 296.0f, 6.0f, C2D_Color32f(0.52f, 0.75f, 0.92f, 1.0f));
        draw_ui_text(24.0f, 98.0f, 0.46f, 0.46f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "Map style");
        snprintf(line, sizeof(line), "%s via %s", network_get_tile_source_name(), network_get_effective_tile_backend_name());
        draw_ui_text(24.0f, 118.0f, 0.32f, 0.32f, C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f), 0, line);

        C2D_DrawRectSolid(172.0f, 101.0f, 0.4f, 56.0f, 28.0f,
                          street_active ? C2D_Color32f(0.27f, 0.56f, 0.42f, 1.0f) : (street_pressed ? C2D_Color32f(0.34f, 0.39f, 0.44f, 1.0f) : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f)));
        C2D_DrawRectSolid(238.0f, 101.0f, 0.4f, 58.0f, 28.0f,
                          sat_active ? C2D_Color32f(0.27f, 0.56f, 0.42f, 1.0f) : (sat_pressed ? C2D_Color32f(0.34f, 0.39f, 0.44f, 1.0f) : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f)));
        draw_ui_text(200.0f, 109.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "Street");
        draw_ui_text(267.0f, 109.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "Sat");

        C2D_DrawRectSolid(12.0f, 150.0f, 0.2f, 142.0f, 68.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        C2D_DrawRectSolid(166.0f, 150.0f, 0.2f, 142.0f, 68.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        draw_ui_text(24.0f, 164.0f, 0.44f, 0.44f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "Night mode");
        draw_ui_text(178.0f, 164.0f, 0.44f, 0.44f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "Debug overlay");
        C2D_DrawRectSolid(24.0f, 190.0f, 0.3f, 118.0f, 18.0f,
                          g_night_mode ? C2D_Color32f(0.28f, 0.58f, 0.43f, 1.0f) : (night_pressed ? C2D_Color32f(0.42f, 0.32f, 0.18f, 1.0f) : C2D_Color32f(0.22f, 0.26f, 0.30f, 1.0f)));
        C2D_DrawRectSolid(178.0f, 190.0f, 0.3f, 118.0f, 18.0f,
                          g_debug_overlay ? C2D_Color32f(0.28f, 0.58f, 0.43f, 1.0f) : (debug_pressed ? C2D_Color32f(0.42f, 0.32f, 0.18f, 1.0f) : C2D_Color32f(0.22f, 0.26f, 0.30f, 1.0f)));
        draw_ui_text(83.0f, 193.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, g_night_mode ? "Enabled" : "Disabled");
        draw_ui_text(237.0f, 193.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, g_debug_overlay ? "Enabled" : "Disabled");
    } else if (g_settings_tab == SETTINGS_TAB_TILES) {
        bool ring_dec_pressed = is_recent_settings_hit(SETTINGS_HIT_PREFETCH_RING_DEC, now_ms);
        bool ring_inc_pressed = is_recent_settings_hit(SETTINGS_HIT_PREFETCH_RING_INC, now_ms);
        bool rate_dec_pressed = is_recent_settings_hit(SETTINGS_HIT_PREFETCH_RATE_DEC, now_ms);
        bool rate_inc_pressed = is_recent_settings_hit(SETTINGS_HIT_PREFETCH_RATE_INC, now_ms);

        C2D_DrawRectSolid(12.0f, 86.0f, 0.2f, 296.0f, 56.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        C2D_DrawRectSolid(12.0f, 150.0f, 0.2f, 296.0f, 56.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));

        draw_ui_text(24.0f, 100.0f, 0.44f, 0.44f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "Prefetch ring");
        snprintf(line, sizeof(line), "%d tile%s beyond view", g_prefetch_ring, (g_prefetch_ring == 1) ? "" : "s");
        draw_ui_text(24.0f, 120.0f, 0.32f, 0.32f, C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f), 0, line);

        draw_ui_text(24.0f, 164.0f, 0.44f, 0.44f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "Prefetch cadence");
        snprintf(line, sizeof(line), "Every %d idle frame%s", g_prefetch_every_n_frames, (g_prefetch_every_n_frames == 1) ? "" : "s");
        draw_ui_text(24.0f, 184.0f, 0.32f, 0.32f, C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f), 0, line);

        C2D_DrawRectSolid(214.0f, 98.0f, 0.3f, 28.0f, 28.0f,
                          ring_dec_pressed ? C2D_Color32f(0.95f, 0.66f, 0.30f, 1.0f) : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f));
        C2D_DrawRectSolid(272.0f, 98.0f, 0.3f, 28.0f, 28.0f,
                          ring_inc_pressed ? C2D_Color32f(0.95f, 0.66f, 0.30f, 1.0f) : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f));
        C2D_DrawRectSolid(245.0f, 98.0f, 0.3f, 24.0f, 28.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        draw_ui_text(228.0f, 104.0f, 0.46f, 0.46f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "-");
        snprintf(line, sizeof(line), "%d", g_prefetch_ring);
        draw_ui_text(257.0f, 104.0f, 0.40f, 0.40f, C2D_Color32f(0.90f, 0.93f, 0.97f, 1.0f), C2D_AlignCenter, line);
        draw_ui_text(286.0f, 104.0f, 0.42f, 0.42f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "+");

        C2D_DrawRectSolid(214.0f, 162.0f, 0.3f, 28.0f, 28.0f,
                          rate_dec_pressed ? C2D_Color32f(0.95f, 0.66f, 0.30f, 1.0f) : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f));
        C2D_DrawRectSolid(272.0f, 162.0f, 0.3f, 28.0f, 28.0f,
                          rate_inc_pressed ? C2D_Color32f(0.95f, 0.66f, 0.30f, 1.0f) : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f));
        C2D_DrawRectSolid(245.0f, 162.0f, 0.3f, 24.0f, 28.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        draw_ui_text(228.0f, 168.0f, 0.46f, 0.46f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "-");
        snprintf(line, sizeof(line), "%d", g_prefetch_every_n_frames);
        draw_ui_text(257.0f, 168.0f, 0.40f, 0.40f, C2D_Color32f(0.90f, 0.93f, 0.97f, 1.0f), C2D_AlignCenter, line);
        draw_ui_text(286.0f, 168.0f, 0.42f, 0.42f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "+");

        snprintf(line, sizeof(line), "RAM %lu/%lu tiles", (unsigned long)map_tiles_get_cache_used(), (unsigned long)map_tiles_get_cache_capacity());
        draw_ui_text(12.0f, 216.0f, 0.34f, 0.34f, C2D_Color32f(0.74f, 0.82f, 0.90f, 1.0f), 0, line);
    } else if (g_settings_tab == SETTINGS_TAB_DATA) {
        bool proxy_pressed = is_recent_settings_hit(SETTINGS_HIT_PROXY_TOGGLE, now_ms);
        bool cache_pressed = is_recent_settings_hit(SETTINGS_HIT_CACHE_TOGGLE, now_ms);
        bool clear_pressed = is_recent_settings_hit(SETTINGS_HIT_CLEAR_CACHE, now_ms);
        bool dl_pressed    = is_recent_settings_hit(SETTINGS_HIT_DOWNLOAD_REGION, now_ms);
        bool proxy_enabled = network_get_proxy_enabled();
        bool cache_enabled = network_get_disk_cache_enabled();
        bool proxy_available = network_proxy_available();

        C2D_DrawRectSolid(12.0f, 86.0f, 0.2f, 142.0f, 58.0f,
                          proxy_available ? C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f) : C2D_Color32f(0.10f, 0.11f, 0.13f, 1.0f));
        C2D_DrawRectSolid(166.0f, 86.0f, 0.2f, 142.0f, 58.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));

        /* Clear cache card (left) */
        C2D_DrawRectSolid(12.0f, 154.0f, 0.2f, 142.0f, 60.0f,
                          clear_pressed ? C2D_Color32f(0.28f, 0.20f, 0.14f, 1.0f) : C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        /* Download region card (right) */
        C2D_DrawRectSolid(166.0f, 154.0f, 0.2f, 142.0f, 60.0f,
                          dl_pressed ? C2D_Color32f(0.14f, 0.22f, 0.30f, 1.0f) : C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));

        draw_ui_text(24.0f, 100.0f, 0.44f, 0.44f,
                     proxy_available ? C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f) : C2D_Color32f(0.56f, 0.61f, 0.67f, 1.0f),
                     0,
                     "Proxy");
        draw_ui_text(178.0f, 100.0f, 0.44f, 0.44f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "SD cache");
        draw_ui_text(24.0f, 120.0f, 0.32f, 0.32f,
                     proxy_available ? C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f) : C2D_Color32f(0.48f, 0.54f, 0.60f, 1.0f),
                     0,
                     proxy_available ? (proxy_enabled ? "Enabled for tile requests" : "Direct requests") : "Not configured");
        draw_ui_text(178.0f, 120.0f, 0.32f, 0.32f, C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f), 0,
                     cache_enabled ? "Tiles saved to SD" : "RAM cache only");

        C2D_DrawRectSolid(86.0f, 103.0f, 0.3f, 56.0f, 22.0f,
                          proxy_enabled ? C2D_Color32f(0.28f, 0.58f, 0.43f, 1.0f) : (proxy_pressed ? C2D_Color32f(0.95f, 0.66f, 0.30f, 1.0f) : C2D_Color32f(0.22f, 0.26f, 0.30f, 1.0f)));
        C2D_DrawRectSolid(240.0f, 103.0f, 0.3f, 56.0f, 22.0f,
                          cache_enabled ? C2D_Color32f(0.28f, 0.58f, 0.43f, 1.0f) : (cache_pressed ? C2D_Color32f(0.95f, 0.66f, 0.30f, 1.0f) : C2D_Color32f(0.22f, 0.26f, 0.30f, 1.0f)));
        draw_ui_text(114.0f, 108.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, proxy_enabled ? "ON" : "OFF");
        draw_ui_text(268.0f, 108.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, cache_enabled ? "ON" : "OFF");

        draw_ui_text(24.0f, 166.0f, 0.40f, 0.40f, C2D_Color32f(0.97f, 0.98f, 1.0f, 1.0f), 0, "Clear SD");
        draw_ui_text(24.0f, 184.0f, 0.40f, 0.40f, C2D_Color32f(0.97f, 0.98f, 1.0f, 1.0f), 0, "tile cache");
        draw_ui_text(178.0f, 166.0f, 0.40f, 0.40f, C2D_Color32f(0.60f, 0.88f, 0.98f, 1.0f), 0, "Offline");
        draw_ui_text(178.0f, 184.0f, 0.40f, 0.40f, C2D_Color32f(0.60f, 0.88f, 0.98f, 1.0f), 0, "regions");
        snprintf(line, sizeof(line), "backend %s", network_get_effective_tile_backend_name());
        draw_ui_text(12.0f, 222.0f, 0.30f, 0.30f, C2D_Color32f(0.56f, 0.62f, 0.70f, 1.0f), 0, line);
    } else if (g_settings_tab == SETTINGS_TAB_EXPLORE) {
        static const char *names[] = {"World globe", "3D terrain",
                                      "Street View Explorer", "Play StreetGuess"};
        for (int i = 0; i < 4; i++) {
            float y = 80 + i * 27;
            C2D_DrawRectSolid(12, y, .2f, 296, 24, C2D_Color32(30, 42, 58, 255));
            C2D_DrawRectSolid(12, y, .3f, 3, 24, C2D_Color32(113, 215, 235, 255));
            draw_ui_text(24, y + 4, .43f, .43f, C2D_Color32(238, 246, 253, 255), 0, names[i]);
        }
        char fly_label[45];
        if (g_street_fly_ms==0) snprintf(fly_label,sizeof(fly_label),"Fly-in: OFF");
        else snprintf(fly_label,sizeof(fly_label),"Fly-in: %d ms",g_street_fly_ms);
        draw_ui_text(20, 202, .34f, .34f, C2D_Color32(238, 246, 253, 255), 0, fly_label);
        C2D_DrawRectSolid(150,210,.5f,144,5,C2D_Color32(48,74,92,255));
        C2D_DrawRectSolid(150,210,.6f,144*g_street_fly_ms/(float)STREET_FLY_MAX_MS,5,
                          C2D_Color32(75,190,215,255));
        C2D_DrawCircleSolid(150+144*g_street_fly_ms/(float)STREET_FLY_MAX_MS,212.5f,.8f,6,
                            C2D_Color32(200,247,255,255));
        draw_ui_text(20, 225, .38f, .38f, C2D_Color32(164, 190, 211, 255), 0,
            pano_runtime_coverage_enabled() ?
                "Street imagery dots: ON (tap)" :
                "Street imagery dots: OFF (tap)");
    } else if (g_settings_tab == SETTINGS_TAB_TEXT) {
        const UiAccessConfig *a=ui_access_get();
        const char *sizes[]={"Normal","Large","Extra Large"};
        const char *weights[]={"Regular","Medium","Bold"};
        draw_ui_text(13,83,.45f,.45f,C2D_Color32(242,249,255,255),0,"Text size");
        for(int i=0;i<3;i++){
            float x=12+i*99;
            u32 bg=a->size==(unsigned)i?C2D_Color32(30,128,149,255):C2D_Color32(35,48,64,255);
            C2D_DrawRectSolid(x,103,.25f,96,34,bg);
            draw_ui_text(x+48,112,.40f,.40f,C2D_Color32(255,255,255,255),C2D_AlignCenter,sizes[i]);
        }
        draw_ui_text(13,143,.45f,.45f,C2D_Color32(242,249,255,255),0,"Text weight");
        for(int i=0;i<3;i++){
            float x=12+i*99;
            u32 bg=a->weight==(unsigned)i?C2D_Color32(30,128,149,255):C2D_Color32(35,48,64,255);
            C2D_DrawRectSolid(x,161,.25f,96,32,bg);
            draw_ui_text(x+48,169,.40f,.40f,C2D_Color32(255,255,255,255),C2D_AlignCenter,weights[i]);
        }
        C2D_DrawRectSolid(12,203,.2f,296,30,C2D_Color32(35,48,64,255));
        draw_ui_text(23,209,.43f,.43f,C2D_Color32(245,248,255,255),0,"High contrast");
        draw_ui_text(294,209,.43f,.43f,C2D_Color32(130,239,238,255),C2D_AlignRight,
                     a->high_contrast?"ON":"OFF");
    } else { /* SETTINGS_TAB_GPS */
        bool client_mode   = (gps_get_source_mode() == GPS_SOURCE_NMEA_CLIENT);
        bool server_mode   = (gps_get_source_mode() == GPS_SOURCE_HTTPS_SERVER);
        bool svr_running   = gps_is_connected(); /* in server mode = gps_server_is_running() */
        bool cli_pressed   = is_recent_settings_hit(SETTINGS_HIT_GPS_MODE_CLIENT, now_ms);
        bool svr_pressed   = is_recent_settings_hit(SETTINGS_HIT_GPS_MODE_SERVER, now_ms);
        bool ss_pressed    = is_recent_settings_hit(SETTINGS_HIT_GPS_START_STOP, now_ms);

        /* Lazy QR generation: produce the QR code whenever the server is
           running but we don't have a valid QR yet (covers start from main
           panel GPS button, mode switch, etc.) */
        if (server_mode && svr_running && !g_qr_valid) {
            char svr_url[64];
            gps_get_server_url(svr_url, sizeof(svr_url));
            if (svr_url[0] && qr_encode(svr_url, strlen(svr_url),
                                         g_qr_modules, &g_qr_size)) {
                g_qr_valid = true;
            }
        }
        if (!svr_running) g_qr_valid = false;

        /* Card 1: Source mode toggle */
        C2D_DrawRectSolid(12.0f, 86.0f, 0.2f, 296.0f, 56.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        draw_ui_text(24.0f, 100.0f, 0.44f, 0.44f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "GPS source");
        draw_ui_text(24.0f, 120.0f, 0.32f, 0.32f, C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f), 0,
                     client_mode ? "NMEA TCP client (GPS2IP)" : "HTTPS server (browser GPS)");

        /* [Client] button */
        C2D_DrawRectSolid(172.0f, 101.0f, 0.3f, 56.0f, 28.0f,
                          client_mode ? C2D_Color32f(0.27f, 0.56f, 0.42f, 1.0f)
                                      : (cli_pressed ? C2D_Color32f(0.34f, 0.39f, 0.44f, 1.0f)
                                                     : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f)));
        draw_ui_text(200.0f, 109.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "Client");

        /* [Server] button */
        C2D_DrawRectSolid(238.0f, 101.0f, 0.3f, 58.0f, 28.0f,
                          server_mode ? C2D_Color32f(0.27f, 0.56f, 0.42f, 1.0f)
                                      : (svr_pressed ? C2D_Color32f(0.34f, 0.39f, 0.44f, 1.0f)
                                                     : C2D_Color32f(0.20f, 0.24f, 0.30f, 1.0f)));
        draw_ui_text(267.0f, 109.0f, 0.34f, 0.34f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "Server");

        /* Card 2: Status / start-stop */
        u32 card2_col;
        if (server_mode && svr_running)
            card2_col = C2D_Color32f(0.09f, 0.17f, 0.13f, 1.0f); /* dark green tint */
        else if (ss_pressed && server_mode)
            card2_col = C2D_Color32f(0.20f, 0.28f, 0.22f, 1.0f);
        else
            card2_col = C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f);
        C2D_DrawRectSolid(12.0f, 150.0f, 0.2f, 296.0f, 70.0f, card2_col);

        if (client_mode) {
            draw_ui_text(24.0f, 164.0f, 0.44f, 0.44f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, "TCP NMEA client");
            snprintf(line, sizeof(line), "Host  %s  port %d", gps_get_nmea_host(), gps_get_nmea_port());
            draw_ui_text(24.0f, 184.0f, 0.32f, 0.32f, C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f), 0, line);
            draw_ui_text(24.0f, 204.0f, 0.30f, 0.30f,
                         gps_is_connected() ? C2D_Color32f(0.55f, 0.96f, 0.72f, 1.0f)
                                            : C2D_Color32f(0.60f, 0.68f, 0.76f, 1.0f),
                         0, gps_is_connected() ? "Connected" : "Not connected  \xe2\x80\x94 tap GPS button");
        } else {
            if (svr_running) {
                draw_ui_text(24.0f, 162.0f, 0.42f, 0.42f, C2D_Color32f(0.55f, 0.96f, 0.72f, 1.0f), 0, "Server running  [tap to stop]");
                char svr_url[64];
                gps_get_server_url(svr_url, sizeof(svr_url));
                draw_ui_text(24.0f, 180.0f, 0.34f, 0.34f, C2D_Color32f(0.60f, 0.88f, 0.98f, 1.0f), 0, svr_url);
                GpsFix fix;
                if(gps_get_fix(&fix) && gps_get_server_fix_count()>0)
                    snprintf(line,sizeof(line),"Fixes %u  Age %.1fs  Accuracy %.0fm",
                             gps_get_server_fix_count(),fix.age_ms/1000.0,
                             fix.accuracy_m>=0?fix.accuracy_m:0);
                else
                    snprintf(line,sizeof(line),"Waiting for phone GPS  (0 fixes)");
                draw_ui_text(24.0f, 200.0f, 0.36f, 0.36f, C2D_Color32f(0.80f, 0.88f, 0.92f, 1.0f), 0, line);
            } else {
                /* Show the actual IP address even before starting */
                u32 raw_ip = (u32)gethostid();
                unsigned char *ipb = (unsigned char *)&raw_ip;
                snprintf(line, sizeof(line), "HTTPS server  port 8443  (%u.%u.%u.%u)",
                         ipb[0], ipb[1], ipb[2], ipb[3]);
                draw_ui_text(24.0f, 164.0f, 0.42f, 0.42f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), 0, line);
                draw_ui_text(24.0f, 184.0f, 0.32f, 0.32f, C2D_Color32f(0.72f, 0.82f, 0.90f, 1.0f), 0, "Tap here or GPS button to start");
                snprintf(line, sizeof(line), "https://%u.%u.%u.%u:8443",
                         ipb[0], ipb[1], ipb[2], ipb[3]);
                draw_ui_text(24.0f, 204.0f, 0.30f, 0.30f, C2D_Color32f(0.60f, 0.88f, 0.98f, 1.0f), 0, line);
            }
        }
    }

    if(g_settings_tab==SETTINGS_TAB_GPS && gps_get_source_mode()==GPS_SOURCE_HTTPS_SERVER && gps_is_connected())
        draw_ui_fit_line(12,222,.35f,C2D_Color32(193,215,230,255),
                         "Scan TOP QR | Same WiFi | Allow phone location",296);
    else
        draw_ui_text(160.0f, 222.0f, 0.32f, 0.32f, C2D_Color32f(0.66f, 0.76f, 0.86f, 1.0f), C2D_AlignCenter,
                     g_settings_tab == SETTINGS_TAB_VIEW ? "SELECT: tile timings   B: close" :
                     "Touch cards to change settings   B to close");
}

static void render_bottom_panel(void) {
    static const char* primary_labels[8] = {
        "Search", "Favorites", "Recents", "Route",
        "Identify", "GPS", "Track", "Menu"
    };
    static const char* secondary_labels[8] = {
        "SELECT", "X", "Y", "TOUCH",
        "L", "B", "A", "R"
    };
    static const TouchAction actions[8] = {
        TOUCH_ACTION_SEARCH, TOUCH_ACTION_FAVORITES, TOUCH_ACTION_RECENTS, TOUCH_ACTION_ROUTE,
        TOUCH_ACTION_IDENTIFY, TOUCH_ACTION_GPS_CONNECT, TOUCH_ACTION_GPS_TRACK, TOUCH_ACTION_MENU
    };

    char line[128];
    char small[64];
    u64 now_ms = osGetTime();

    C2D_DrawRectSolid(0.0f, 0.0f, 0.0f, (float)SCREEN_WIDTH_BOTTOM, (float)SCREEN_HEIGHT_BOTTOM,
                      C2D_Color32f(0.07f, 0.09f, 0.13f, 1.0f));
    C2D_DrawRectSolid(0.0f, 0.0f, 0.1f, (float)SCREEN_WIDTH_BOTTOM, 56.0f,
                      C2D_Color32f(0.10f, 0.13f, 0.18f, 1.0f));
    C2D_DrawRectSolid(0.0f, 56.0f, 0.1f, (float)SCREEN_WIDTH_BOTTOM, 40.0f,
                      C2D_Color32f(0.08f, 0.10f, 0.14f, 1.0f));

    draw_ui_text(12.0f, 10.0f, 0.62f, 0.62f, C2D_Color32f(0.95f, 0.97f, 0.99f, 1.0f), 0, "3DS Maps Control");

    snprintf(line, sizeof(line), "WiFi %s   %s %s   Z%d   FPS %lu",
             is_connected() ? "OK" : "OFF",
             network_get_effective_tile_backend_name(),
             network_get_tile_source_name(),
             map_state.zoom,
             (unsigned long)g_fps);
    draw_ui_text(12.0f, 32.0f, 0.42f, 0.42f, C2D_Color32f(0.73f, 0.82f, 0.92f, 1.0f), 0, line);

    if (g_request_label) {
        snprintf(line, sizeof(line), "%s   B: Cancel", g_request_label);
    } else if (g_debug_overlay) {
        TileTiming timing;
        if (map_tiles_get_timing(&timing))
            snprintf(line, sizeof(line), "SD %u  Net %u  Decode %u  GPU %u ms",
                     timing.sd_ms, timing.network_ms, timing.decode_ms, timing.upload_ms);
        else snprintf(line, sizeof(line), "Waiting for a tile timing sample");
    } else if (g_status_message[0] && (now_ms - g_status_message_ms) < 2200) {
        snprintf(line, sizeof(line), "Status: %.32s", g_status_message);
    } else if (gps_is_connected() || map_state.gps_tracking) {
        GpsFix fix;
        if (!gps_get_fix(&fix)) snprintf(line, sizeof(line), "Waiting for GPS fix");
        else if (fix.accuracy_m >= 0)
            snprintf(line, sizeof(line), "GPS %.1fs old   Accuracy %.0f m", fix.age_ms / 1000.0, fix.accuracy_m);
        else snprintf(line, sizeof(line), "GPS %.1fs old   Accuracy unavailable", fix.age_ms / 1000.0);
    } else if (g_route_active) {
        snprintf(line, sizeof(line), "Route: %.34s", g_route_next_text[0] ? g_route_next_text : "Active");
    } else if (g_last_place_name[0]) {
        snprintf(line, sizeof(line), "Place: %.34s", g_last_place_name);
    } else {
        snprintf(line, sizeof(line), "Lat %.4f  Lon %.4f", map_state.lat, map_state.lon);
    }
    draw_ui_text(12.0f, 62.0f, 0.42f, 0.42f, C2D_Color32f(0.90f, 0.92f, 0.95f, 1.0f), 0, line);

    snprintf(small, sizeof(small), g_show_coverage ? "Green: saved  Amber: no pack tile" : "Touch here to drag map");
    C2D_DrawRectSolid(10.0f, 72.0f, 0.2f, 300.0f, 16.0f, C2D_Color32f(0.16f, 0.21f, 0.27f, 1.0f));
    draw_ui_text(160.0f, 75.0f, 0.36f, 0.36f, C2D_Color32f(0.84f, 0.89f, 0.94f, 1.0f), C2D_AlignCenter, small);

    for (int row = 0; row < TOUCH_BUTTON_ROWS; row++) {
        for (int col = 0; col < TOUCH_BUTTON_COLS; col++) {
            int index = row * TOUCH_BUTTON_COLS + col;
            float x = (float)(col * TOUCH_BUTTON_WIDTH + 6);
            float y = (float)(TOUCH_BUTTON_GRID_TOP + row * TOUCH_BUTTON_HEIGHT + 6);
            float w = (float)TOUCH_BUTTON_WIDTH - 12.0f;
            float h = (float)TOUCH_BUTTON_HEIGHT - 12.0f;
            bool pressed = (g_last_touch_action == actions[index]) && ((now_ms - g_last_touch_action_ms) < 180);
            bool active = false;
            u32 fill;
            u32 accent;
            u32 text_color;

            if (actions[index] == TOUCH_ACTION_GPS_TRACK) {
                active = map_state.gps_tracking;
            } else if (actions[index] == TOUCH_ACTION_GPS_CONNECT) {
                active = gps_is_connected();
            } else if (actions[index] == TOUCH_ACTION_ROUTE) {
                active = g_route_active;
            }

            if (pressed) {
                fill = C2D_Color32f(0.92f, 0.55f, 0.22f, 1.0f);
                accent = C2D_Color32f(0.99f, 0.86f, 0.63f, 1.0f);
                text_color = C2D_Color32f(0.14f, 0.09f, 0.05f, 1.0f);
            } else if (active) {
                fill = C2D_Color32f(0.21f, 0.54f, 0.42f, 1.0f);
                accent = C2D_Color32f(0.71f, 0.93f, 0.83f, 1.0f);
                text_color = C2D_Color32f(0.95f, 0.99f, 0.97f, 1.0f);
            } else {
                fill = C2D_Color32f(0.17f, 0.22f, 0.29f, 1.0f);
                accent = C2D_Color32f(0.56f, 0.72f, 0.86f, 1.0f);
                text_color = C2D_Color32f(0.93f, 0.96f, 0.99f, 1.0f);
            }

            C2D_DrawRectSolid(x + 2.0f, y + 3.0f, 0.1f, w, h, C2D_Color32f(0.02f, 0.03f, 0.05f, 0.45f));
            C2D_DrawRectSolid(x, y, 0.2f, w, h, fill);
            C2D_DrawRectSolid(x, y, 0.3f, w, 6.0f, accent);

            draw_ui_text(x + (w * 0.5f), y + 18.0f, 0.48f, 0.48f, text_color, C2D_AlignCenter, primary_labels[index]);
            draw_ui_text(x + (w * 0.5f), y + 42.0f, 0.34f, 0.34f,
                         pressed ? C2D_Color32f(0.25f, 0.14f, 0.06f, 1.0f) : C2D_Color32f(0.73f, 0.82f, 0.92f, 1.0f),
                         C2D_AlignCenter,
                         secondary_labels[index]);
        }
    }
}

static void open_menu(void) {
    g_settings_tab = SETTINGS_TAB_VIEW;
    g_screen_mode = APP_SCREEN_SETTINGS;
    g_last_settings_hit = SETTINGS_HIT_NONE;
    g_last_settings_hit_ms = 0;
    set_status_message("Settings open");
}

static void open_favorites(void) {
    int selected = 0;
    begin_bottom_console_ui();
    while (aptMainLoop() && !g_should_exit) {
        consoleClear();
        printf("=== Favorites ===\n");
        printf("A = go  Y = add center\n");
        printf("X = delete  B or R = back\n");
        printf("START = exit\n\n");

        if (g_favorite_count <= 0) {
            printf("No favorites saved yet.\n");
            printf("Press Y to add the current center.\n");
        } else {
            for (int i = 0; i < g_favorite_count; i++) {
                printf("%c %d) %s\n", (i == selected) ? '>' : ' ', i + 1, g_favorites[i].name);
            }
        }

        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {
            g_should_exit = true;
            end_bottom_console_ui(NULL);
            return;
        }
        if (kDown & (KEY_B | KEY_R)) {
            end_bottom_console_ui("Closed favorites");
            return;
        }
        if (kDown & KEY_DUP) {
            if (selected > 0) selected--;
        }
        if (kDown & KEY_DDOWN) {
            if (selected < g_favorite_count - 1) selected++;
        }
        if ((kDown & KEY_A) && g_favorite_count > 0) {
            jump_to_location(g_favorites[selected].lat, g_favorites[selected].lon);
            end_bottom_console_ui("Jumped to favorite");
            return;
        }
        if (kDown & KEY_Y) {
            char name[96] = {0};
            SwkbdState swkbd;
            swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(name) - 1);
            swkbdSetHintText(&swkbd, "Favorite name");
            SwkbdButton btn = swkbdInputText(&swkbd, name, sizeof(name));
            if (btn == SWKBD_BUTTON_RIGHT) {
                if (name[0] == '\0') {
                    snprintf(name, sizeof(name), "%.4f, %.4f", map_state.lat, map_state.lon);
                }
                add_favorite(name, map_state.lat, map_state.lon);
                set_last_place_name(name);
            }
        }
        if ((kDown & KEY_X) && g_favorite_count > 0) {
            for (int i = selected; i + 1 < g_favorite_count; i++) {
                g_favorites[i] = g_favorites[i + 1];
            }
            if (g_favorite_count > 0) g_favorite_count--;
            if (selected >= g_favorite_count && selected > 0) selected--;
            favorites_save();
        }

        svcSleepThread(60000000LL);
    }
    end_bottom_console_ui(NULL);
}

typedef struct {
    const char *url;
    atomic_bool done, cancel;
    bool ok;
    u8 *bytes;
    u32 size;
} MapRequest;

static void request_worker(void *arg) {
    MapRequest *request = arg;
    void *curl = network_alloc_tile_curl();
    request->ok = network_request(curl, request->url, 1024 * 1024,
                                  &request->cancel, &request->bytes, &request->size);
    network_free_tile_curl(curl);
    atomic_store(&request->done, true);
}

static bool wait_for_request(const char *url, const char *label, u8 **buffer, u32 *size) {
    /* The public geocoder prohibits autocomplete and requires <=1 query/s.
     * Wait without freezing the map render or suppressing GPS updates. */
    if (strstr(url,"nominatim.openstreetmap.org/")) {
        while (g_last_geocoder_request_ms &&
               osGetTime()-g_last_geocoder_request_ms < 1100) {
            g_request_label="Waiting for search cooldown (B cancels)";
            hidScanInput();
            u32 keys=hidKeysDown();
            if(keys&KEY_START)g_should_exit=true;
            if(keys&(KEY_B|KEY_START)){
                g_request_label=NULL;
                set_status_message("Search cancelled");
                return false;
            }
            gps_update();
            map_tiles_process_downloads();
            render_map();
            gspWaitForVBlank();
        }
        g_request_label=NULL;
        g_last_geocoder_request_ms=osGetTime();
    }
    MapRequest request = { .url = url };
    Thread thread = threadCreate(request_worker, &request, 64 * 1024, 0x31, -2, false);
    if (!thread) return false;
    g_request_label = label;
    while (!atomic_load(&request.done) && aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_START) g_should_exit = true;
        if (keys & (KEY_B | KEY_START)) atomic_store(&request.cancel, true);
        if (atomic_load(&request.cancel)) g_request_label = "Cancelling...";
        gps_update();
        circlePosition pad;
        hidCircleRead(&pad);
        int dx = circle_pad_axis_to_pan(pad.dx), dy = circle_pad_axis_to_pan(pad.dy);
        if (dx || dy) {
            map_state.gps_tracking = false;
            pan_map(dx, -dy);
        }
        update_camera_smooth();
        map_tiles_process_downloads();
        render_map();
    }
    g_request_label = NULL;
    if (!atomic_load(&request.done)) atomic_store(&request.cancel, true);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    if (atomic_load(&request.cancel) || !request.ok) {
        free(request.bytes);
        set_status_message(atomic_load(&request.cancel) ? "Request cancelled" : "Network request failed");
        return false;
    }
    *buffer = request.bytes;
    *size = request.size;
    return true;
}

static void build_search_url(char *url, size_t size, const char *encoded, bool near) {
    if (network_get_proxy_enabled()) {
        snprintf(url, size, "%s/geocode?q=%s", network_get_proxy_base_url(), encoded);
        return;
    }
    int used = snprintf(url, size,
        "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=5&q=%s", encoded);
    if (near && used > 0 && (size_t)used < size) {
        double span = fmax(0.02, 360.0 / (1 << map_state.zoom));
        double west = fmax(-180, map_state.lon - span), east = fmin(180, map_state.lon + span);
        double north = fmin(90, map_state.lat + span), south = fmax(-90, map_state.lat - span);
        snprintf(url + used, size - used, "&viewbox=%.6f,%.6f,%.6f,%.6f", west, north, east, south);
    }
}

static const GeocodeResult *place_rows;
static int place_count, place_selection;
static bool place_identify;
static bool place_allow_worldwide;

static void render_place_panel(void) {
    u32 white = C2D_Color32(239, 247, 253, 255);
    u32 dim = C2D_Color32(165, 191, 213, 255);
    C2D_DrawRectSolid(0, 0, .1f, 320, 49, C2D_Color32(25, 37, 53, 255));
    C2D_DrawRectSolid(0, 47, .2f, 320, 2, C2D_Color32(113, 215, 235, 255));
    draw_ui_text(12, 9, .57f, .57f, white, 0, place_identify ? "Map center" : "Search results");
    char line[72];
    if (place_identify) {
        C2D_DrawRectSolid(9,57,.20f,302,67,C2D_Color32(31,74,91,255));
        draw_ui_address_two_lines(place_rows[0].name,white);
        snprintf(line, sizeof(line), "%.6f, %.6f", place_rows[0].lat, place_rows[0].lon);
        draw_ui_text(13, 146, .45f, .45f, dim, 0, line);
        draw_ui_text(13, 172, .43f, .43f, dim, 0, "Saved to Recents");
    } else {
        snprintf(line, sizeof(line), "%d of %d", place_selection + 1, place_count);
        draw_ui_text(12, 31, .39f, .39f, dim, 0, line);
        int first=place_selection>2?place_selection-2:0;
        for(int i=first;i<place_count && i<first+3;i++){
            int y=57+(i-first)*43;
            C2D_DrawRectSolid(9,y,.20f,302,39,i==place_selection?
                C2D_Color32(31,74,91,255):C2D_Color32(30,42,58,255));
            /* Keep the place and its city/region readable on separate
             * lines. Unlike the old renderer, neither line is squeezed. */
            const char *address=place_rows[i].name;
            const char *comma=strchr(address,',');
            if(comma && (size_t)(comma-address)<78 && comma[1]){
                char first[96];
                size_t cut=(size_t)(comma-address);
                memcpy(first,address,cut);first[cut]=0;
                while(*++comma==' ') {}
                draw_ui_fit_line(17,y+3,.40f,white,first,282);
                draw_ui_fit_line(17,y+21,.31f,dim,comma,282);
            }else draw_ui_fit_line(17,y+10,.44f,white,address,282);
        }
    }
    C2D_DrawRectSolid(0, 194, .2f, 320, 46, C2D_Color32(23, 32, 45, 255));
    draw_ui_text(12, 205, .41f, .41f, dim, 0,
        place_identify ? "B  Back" :
        place_allow_worldwide ? "Tap/A  Go   X  Worldwide   B  Cancel" :
                                "Tap/A  Go   B  Cancel");
}

static int show_places(const GeocodeResult *places, int count, bool identify) {
    if (!count) return -1;
    place_rows = places;
    place_count = count;
    place_selection = 0;
    place_identify = identify;
    int result = -1;
    g_bottom_modal = render_place_panel;
    while (aptMainLoop() && !g_should_exit) {
        hidScanInput();
        u32 keys = hidKeysDown();
        if (keys & KEY_START) g_should_exit = true;
        if (keys & (KEY_START | KEY_B)) break;
        if (!identify && (keys & KEY_A)) { result = place_selection; break; }
        if (!identify && place_allow_worldwide && (keys & KEY_X)) {
            result = -2; break;
        }
        if (!identify && (keys & KEY_TOUCH)) {
            touchPosition touch; hidTouchRead(&touch);
            int first=place_selection>2?place_selection-2:0;
            if (touch.px>=9 && touch.px<311 && touch.py>=57 && touch.py<186) {
                int index=first+(touch.py-57)/43;
                if(index<count){place_selection=index;result=index;break;}
            }
        }
        if (keys & KEY_DUP && place_selection > 0) place_selection--;
        if (keys & KEY_DDOWN && place_selection + 1 < count) place_selection++;
        gps_update();
        map_tiles_process_downloads();
        render_map();
    }
    g_bottom_modal = NULL;
    place_rows = NULL;
    return result;
}

static bool geocode_pick(const char* query, GeocodeResult* out) {
    if (!query || !query[0] || !out) return false;
    if (!is_connected()) return false;

    char encoded[256];
    if (!url_encode_component(query, encoded, sizeof(encoded))) {
        return false;
    }

    char url[512];
    build_search_url(url, sizeof(url), encoded, true);

    u8* buf = NULL;
    u32 size = 0;
    if (!wait_for_request(url, "Searching places", &buf, &size)) {
        if (buf) free(buf);
        return false;
    }

    GeocodeResult results[5];
    memset(results, 0, sizeof(results));
    int count = parse_geocode_results_json((const char*)buf, size, results, 5);
    free(buf);

    if (!count) {
        set_status_message("No search results");
        return false;
    }
    int selected = count > 1 ? show_places(results, count, false) : 0;
    if (selected < 0) {
        set_status_message("Search cancelled");
        return false;
    }

    *out = results[selected];
    return true;
}

#define ROUTE_GEOMETRY_LIMIT 16000

static bool parse_route_json(const char *json, u32 size) {
    json_t *root = json_loadb(json, size, JSON_REJECT_DUPLICATES, NULL);
    json_t *route = json_array_get(json_object_get(root, "routes"), 0);
    json_t *coordinates = json_object_get(json_object_get(route, "geometry"), "coordinates");
    const char *poly = json_string_value(json_object_get(root, "poly"));
    size_t count = json_array_size(coordinates);
    if (poly) {
        count = *poly ? 1 : 0;
        for (const char *p = poly; *p && count <= ROUTE_GEOMETRY_LIMIT; p++)
            if (*p == ';' && p[1]) count++;
    }
    if (count < 2 || count > ROUTE_GEOMETRY_LIMIT) {
        json_decref(root);
        return false;
    }
    RoutePoint *points = calloc(count, sizeof(*points));
    if (!points) { json_decref(root); return false; }
    bool ok = true;
    const char *p = poly;
    for (size_t i = 0; i < count && ok; i++) {
        if (poly) {
            char *end;
            points[i].lat = strtod(p, &end);
            if (end == p || *end != ',') { ok = false; break; }
            p = end + 1;
            points[i].lon = strtod(p, &end);
            if (end == p || (i + 1 < count ? *end != ';' : (*end && (*end != ';' || end[1])))) {
                ok = false;
                break;
            }
            p = *end ? end + 1 : end;
            ok = isfinite(points[i].lat) && fabs(points[i].lat) <= 90 &&
                 isfinite(points[i].lon) && fabs(points[i].lon) <= 180;
        } else {
            json_t *pair = json_array_get(coordinates, i);
            ok = json_coord(json_array_get(pair, 0), 180, &points[i].lon) &&
                 json_coord(json_array_get(pair, 1), 90, &points[i].lat);
        }
    }
    if (!ok) {
        free(points);
        json_decref(root);
        return false;
    }

    const char *next = json_string_value(json_object_get(root, "next"));
    if (!next) {
        json_t *leg = json_array_get(json_object_get(route, "legs"), 0);
        json_t *step = json_array_get(json_object_get(leg, "steps"), 0);
        next = json_string_value(json_object_get(step, "name"));
    }
    double distance = json_number_value(json_object_get(poly ? root : route, poly ? "distance_m" : "distance"));
    g_route_next_dist_m = (int)fmin(2147483647, fmax(0, distance));
    snprintf(g_route_next_text, sizeof(g_route_next_text), "%s", next && *next ? next : "Route");
    free(g_route_geometry);
    g_route_geometry = points;
    g_route_geometry_count = count;
    g_route_count = count < MAX_ROUTE_POINTS ? (int)count : MAX_ROUTE_POINTS;
    for (int i = 0; i < g_route_count; i++)
        g_route[i] = points[i * (count - 1) / (g_route_count - 1)];
    json_decref(root);
    return true;
}

static void open_route_to_address(void) {
    if (!is_connected()) {
        set_status_message("No WiFi for routing");
        return;
    }

    char query[128] = {0};
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(query) - 1);
    swkbdSetHintText(&swkbd, "Route to address");
    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
    SwkbdButton btn = swkbdInputText(&swkbd, query, sizeof(query));
    if (btn != SWKBD_BUTTON_RIGHT) {
        set_status_message("Route cancelled");
        return;
    }
    if (query[0] == '\0') {
        set_status_message("Route cancelled");
        return;
    }

    GeocodeResult dest;
    memset(&dest, 0, sizeof(dest));
    if (!geocode_pick(query, &dest)) return;

    double from_lat = map_state.lat;
    double from_lon = map_state.lon;
    if (gps_is_connected() && gps_has_fix()) {
        (void)gps_get_position(&from_lat, &from_lon);
    }

    char url[512];
    if (network_get_proxy_enabled()) {
        snprintf(url, sizeof(url),
                 "%s/route?from=%.6f,%.6f&to=%.6f,%.6f", network_get_proxy_base_url(),
                 from_lat, from_lon, dest.lat, dest.lon);
    } else {
        snprintf(url, sizeof(url),
                 "https://router.project-osrm.org/route/v1/driving/%.6f,%.6f;%.6f,%.6f?overview=full&geometries=geojson&steps=true",
                 from_lon, from_lat, dest.lon, dest.lat);
    }

    u8 *buf = NULL;
    u32 size = 0;
    if (!wait_for_request(url, "Planning route", &buf, &size)) return;
    bool ok = parse_route_json((const char *)buf, size);
    free(buf);
    if (!ok) {
        set_status_message("Route parse failed");
        return;
    }

    g_route_active = true;
    add_recent(dest.name, dest.lat, dest.lon);
    set_status_message("Route ready");
}

static void render_route_overlay(double center_lat, double center_lon, int zoom, int screen_width, int screen_height) {
    if (!g_route_active || g_route_count < 2) return;

    int center_px, center_py;
    lat_lon_to_pixel(center_lat, center_lon, zoom, &center_px, &center_py);

    const int world = TILE_SIZE * (1 << zoom);
    const u32 color = C2D_Color32f(1.0f, 1.0f, 0.0f, 0.8f);
    for (int i = 1; i < g_route_count; i++) {
        int p0x, p0y, p1x, p1y;
        lat_lon_to_pixel(g_route[i - 1].lat, g_route[i - 1].lon, zoom, &p0x, &p0y);
        lat_lon_to_pixel(g_route[i].lat, g_route[i].lon, zoom, &p1x, &p1y);

        int dx0 = (p0x - center_px + world + world / 2) % world - world / 2;
        int dx1 = dx0 + (p1x - p0x + world + world / 2) % world - world / 2;
        float x0 = (float)(dx0 + screen_width / 2);
        float y0 = (float)(p0y - center_py + (screen_height / 2));
        float x1 = (float)(dx1 + screen_width / 2);
        float y1 = (float)(p1y - center_py + (screen_height / 2));

        // Skip segments fully off-screen (cheap check).
        if ((x0 < -50 && x1 < -50) || (x0 > screen_width + 50 && x1 > screen_width + 50) ||
            (y0 < -50 && y1 < -50) || (y0 > screen_height + 50 && y1 > screen_height + 50)) {
            continue;
        }

        // citro2d line primitive
        C2D_DrawLine(x0, y0, color, x1, y1, color, 2.0f, 0.6f);
    }
}

static void open_cache_stats(void) {
    begin_bottom_console_ui();
    while (aptMainLoop() && !g_should_exit) {
        consoleClear();
        printf("=== Cache stats ===\n\n");
        printf("RAM tiles: %lu/%lu\n",
               (unsigned long)map_tiles_get_cache_used(),
               (unsigned long)map_tiles_get_cache_capacity());
        printf("SD cache: %s\n", network_get_disk_cache_enabled() ? "ON" : "OFF");
     printf("Prefetch: ring=%d throttle=%d\n", g_prefetch_ring, g_prefetch_every_n_frames);
        printf("Night mode: %s\n", g_night_mode ? "ON" : "OFF");
        TileTiming timing;
        if (map_tiles_get_timing(&timing)) {
            printf("Last loaded tile: %s\n", timing.local ? "SD card" : "network");
            printf("SD access:  %u ms\nNetwork:    %u ms\nDecode:     %u ms\nGPU upload: %u ms\n",
                   timing.sd_ms, timing.network_ms, timing.decode_ms, timing.upload_ms);
        }
        printf("FPS: %lu\n", (unsigned long)g_fps);

        printf("\nX: Clear SD tile cache\n");
        printf("B or R: Back\n");
        printf("START: Exit\n");

        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {
            g_should_exit = true;
            end_bottom_console_ui(NULL);
            return;
        }
        if (kDown & KEY_X) {
            consoleClear();
            printf("Clearing SD tile cache...\n");
            bool ok = network_clear_disk_tile_cache();
            printf("%s\n", ok ? "Done." : "Failed.");
            svcSleepThread(700000000LL);
            continue;
        }
        if (kDown & (KEY_B | KEY_R)) {
            end_bottom_console_ui("Closed cache stats");
            return;
        }
        svcSleepThread(60000000LL); // ~60ms
    }
    end_bottom_console_ui(NULL);
}

/* -------- Offline region download ---------------------------------------- */

#define REGION_LIST_MAX (REGION_PACK_LIMIT * 2 + 2)

typedef struct {
    char path[REGION_PATH_LIMIT];
    char name[REGION_NAME_LIMIT + 1];
    TileSource source;
    bool partial, legacy;
    long size;
} RegionItem;

static char region_title[48], region_detail[64], region_status[96];
static const RegionItem *region_items;
static int region_selected, region_count;
static bool region_confirm;
static unsigned region_progress, region_total;
static char region_summary[48], region_hint[56];
static const char *region_actions[3];

static void draw_region_panel(void) {
    u32 white = C2D_Color32(238, 246, 253, 255);
    u32 dim = C2D_Color32(164, 190, 211, 255);
    u32 accent = C2D_Color32(113, 215, 235, 255);
    C2D_DrawRectSolid(0, 0, 0.1f, 320, 54, C2D_Color32(25, 37, 53, 255));
    C2D_DrawRectSolid(0, 52, 0.2f, 320, 2, accent);
    draw_ui_text(12, 7, .58f, .58f, white, 0, region_title);
    draw_ui_text(12, 32, .38f, .38f, dim, 0, region_detail);
    if (region_items && region_count) {
        int first = region_selected > 2 ? region_selected - 2 : 0;
        if (first + 3 > region_count) first = region_count > 3 ? region_count - 3 : 0;
        for (int row = 0; row < 3 && first + row < region_count; row++) {
            int i = first + row;
            float y = 61 + row * 31;
            const RegionItem *item = &region_items[i];
            C2D_DrawRectSolid(9, y, 0.2f, 302, 27, i == region_selected ?
                              C2D_Color32(31, 74, 91, 255) : C2D_Color32(30, 42, 58, 255));
            draw_ui_text(17, y + 4, .43f, .43f, white, 0, item->name);
            char type[48];
            const char *layer = item->partial ? "Paused" :
                                item->source == TILE_SOURCE_SATELLITE ? "Sat" : "Street";
            if (item->size < 1024 * 1024)
                snprintf(type, sizeof(type), "%s  %.1f KB", layer, item->size / 1024.0);
            else snprintf(type, sizeof(type), "%s  %.1f MB", layer, item->size / 1048576.0);
            draw_ui_text(302, y + 6, .35f, .35f, dim, C2D_AlignRight, type);
        }
    } else if (region_items) {
        draw_ui_text(15, 74, .48f, .48f, white, 0, "No saved regions yet");
        draw_ui_text(15, 101, .42f, .42f, dim, 0, "Press Y to make a pack for this area.");
        draw_ui_text(15, 123, .42f, .42f, dim, 0, "Press L to change the offline source.");
    } else {
        C2D_DrawRectSolid(10, 66, .2f, 300, 72, C2D_Color32(30, 42, 58, 255));
        draw_ui_text(20, 74, .51f, .51f, white, 0, region_summary);
        if (region_total) {
            C2D_DrawRectSolid(20, 108, .21f, 280, 9, C2D_Color32(52, 69, 89, 255));
            float fraction = fminf(1, (float)region_progress / region_total);
            if (fraction > 0) C2D_DrawRectSolid(20, 108, .22f, 280 * fraction, 9, accent);
            draw_ui_text(20, 124, .36f, .36f, dim, 0, region_hint);
        } else draw_ui_text(20, 106, .43f, .43f, dim, 0, region_hint);
    }
    draw_ui_text(12, 151, .40f, .40f, region_confirm ? accent : white, 0, region_status);
    C2D_DrawRectSolid(0, 178, 0.1f, 320, 62, C2D_Color32(23, 32, 45, 255));
    for (int i = 0; i < 3; i++)
        if (region_actions[i]) draw_ui_text(12, 183 + 17*i, .42f, .42f, dim, 0, region_actions[i]);
}

static u32 region_frame(void) {
    hidScanInput();
    u32 keys = hidKeysDown();
    if (keys & KEY_START) g_should_exit = true;
    gps_update();
    map_tiles_process_downloads();
    g_bottom_modal = draw_region_panel;
    render_map();
    g_bottom_modal = NULL;
    return keys;
}

static bool region_keyboard(char *out, size_t size, const char *hint, const char *initial) {
    SwkbdState keyboard;
    swkbdInit(&keyboard, SWKBD_TYPE_NORMAL, 2, size - 1);
    swkbdSetHintText(&keyboard, hint);
    if (initial && initial[0]) swkbdSetInitialText(&keyboard, initial);
    return swkbdInputText(&keyboard, out, size) == SWKBD_BUTTON_RIGHT && out[0];
}

static bool tile_image_valid(const u8 *bytes, size_t size, bool decode) {
    if (!bytes || size < 24 || size > 1024 * 1024) return false;
    if (memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) &&
        !(bytes[0] == 0xff && bytes[1] == 0xd8)) return false;
    int w = 0, h = 0, channels;
    if (!stbi_info_from_memory(bytes, size, &w, &h, &channels) || w != 256 || h != 256) return false;
    if (!decode) return true;
    unsigned char *pixels = stbi_load_from_memory(bytes, size, &w, &h, &channels, 4);
    if (!pixels) return false;
    stbi_image_free(pixels);
    return true;
}

typedef struct {
    RegionJob *job;
    const char *endpoint;
    LightLock append_lock;
    atomic_bool done, cancel, launch;
    atomic_uint saved, failed, next, workers_left, cooldown_workers;
    atomic_uint downloaded_bytes;
} RegionTransfer;

#define REGION_DOWNLOAD_WORKERS 2
#define REGION_DOWNLOAD_ATTEMPTS 3

typedef struct {
    RegionTransfer *transfer;
} RegionWorker;

static u64 region_sd_free_bytes(void) {
    FS_Archive sd;
    u64 free_bytes = 0;
    if (R_SUCCEEDED(FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        FSUSER_GetFreeBytes(&free_bytes, sd);
        FSUSER_CloseArchive(sd);
    }
    return free_bytes;
}

static void region_format_eta(char *out, size_t size, const RegionProgressMetrics *metrics) {
    if (!metrics->eta_known) {
        snprintf(out, size, "--");
    } else if (metrics->eta_seconds < 60) {
        snprintf(out, size, "%llus", (unsigned long long)metrics->eta_seconds);
    } else if (metrics->eta_seconds < 3600) {
        snprintf(out, size, "%llum %02llus",
                 (unsigned long long)(metrics->eta_seconds / 60),
                 (unsigned long long)(metrics->eta_seconds % 60));
    } else {
        snprintf(out, size, "%lluh %02llum",
                 (unsigned long long)(metrics->eta_seconds / 3600),
                 (unsigned long long)((metrics->eta_seconds / 60) % 60));
    }
}

static bool region_retry_wait(RegionTransfer *transfer, unsigned delay_ms) {
    atomic_fetch_add(&transfer->cooldown_workers, 1);
    unsigned waited = 0;
    while (waited < delay_ms && !atomic_load(&transfer->cancel)) {
        unsigned slice = delay_ms - waited;
        if (slice > 50) slice = 50;
        svcSleepThread((s64)slice * 1000000LL);
        waited += slice;
    }
    atomic_fetch_sub(&transfer->cooldown_workers, 1);
    return !atomic_load(&transfer->cancel);
}

static void region_worker(void *arg) {
    RegionTransfer *transfer = ((RegionWorker *)arg)->transfer;
    RegionJob *job = transfer->job;
    while (!atomic_load(&transfer->launch) && !atomic_load(&transfer->cancel))
        svcSleepThread(1000000LL);
    void *curl = network_alloc_tile_curl();
    if (!curl && !atomic_load(&transfer->cancel)) {
        atomic_fetch_add(&transfer->failed, 1);
        atomic_store(&transfer->cancel, true);
    }
    while (curl && !atomic_load(&transfer->cancel)) {
        unsigned i = atomic_fetch_add(&transfer->next, 1);
        if (i >= job->count) break;
        TilePackEntry *tile = &job->entries[i];
        if (tile->size) continue;

        bool tile_saved = false;
        for (unsigned attempt = 0; attempt < REGION_DOWNLOAD_ATTEMPTS &&
             !atomic_load(&transfer->cancel); attempt++) {
            char url[384];
            u8 *bytes = NULL;
            u32 size = 0;
            NetworkRequestInfo info = {0};
            bool requested = network_region_url(transfer->endpoint, tile->x, tile->y,
                                                tile->zoom, url, sizeof(url)) &&
                network_request_ex(curl, url, 1024 * 1024, &transfer->cancel,
                                   &bytes, &size, &info);
            bool valid = requested && tile_image_valid(bytes, size, true);
            bool appended = false;
            if (valid) {
                LightLock_Lock(&transfer->append_lock);
                appended = region_append(job, i, bytes, size);
                LightLock_Unlock(&transfer->append_lock);
            }
            if (appended) {
                atomic_fetch_add(&transfer->downloaded_bytes, size);
                atomic_fetch_add(&transfer->saved, 1);
                tile_saved = true;
            }
            free(bytes);
            if (tile_saved || atomic_load(&transfer->cancel)) break;

            bool storage_failed = valid && !appended;
            unsigned delay = storage_failed ? 0 :
                region_retry_delay_ms(attempt, info.http_status, info.retry_after_ms,
                                      requested && !valid);
            if (!delay) break;
            if (!region_retry_wait(transfer, delay)) break;
        }
        if (!tile_saved && !atomic_load(&transfer->cancel)) {
            atomic_fetch_add(&transfer->failed, 1);
            atomic_store(&transfer->cancel, true);
            break;
        }
    }
    network_free_tile_curl(curl);
    if (atomic_fetch_sub(&transfer->workers_left, 1) == 1)
        atomic_store(&transfer->done, true);
}

static void run_region_transfer(const RegionItem *item, TilePackEntry *plan, unsigned count) {
    if (network_region_count() >= REGION_PACK_LIMIT) {
        set_status_message("Remove an installed pack before downloading");
        return;
    }
    char endpoint[300], url[384];
    if (!is_connected()) {
        set_status_message("Connect to Wi-Fi to download");
        return;
    }
    if (!network_region_endpoint(item->source, endpoint, sizeof(endpoint)) ||
        !network_region_url(endpoint, 0, 0, 1, url, sizeof(url))) {
        set_status_message("Press L to fix the offline tile source");
        return;
    }
    uint32_t fingerprint = crc32(0, (const unsigned char *)endpoint, strlen(endpoint));
    uint8_t signature[3] = { fingerprint, fingerprint >> 8, fingerprint >> 16 };
    if (plan && count) memcpy(plan[0].reserved, signature, sizeof(signature));
    RegionJob job;
    if (!region_open(&job, item->path, plan, count)) {
        set_status_message("Could not open partial pack");
        return;
    }
    if (memcmp(job.entries[0].reserved, signature, sizeof(signature))) {
        region_close(&job);
        set_status_message("Restore this pack's original tile source to resume");
        return;
    }
    RegionTransfer transfer = { .job = &job, .endpoint = endpoint };
    LightLock_Init(&transfer.append_lock);
    unsigned initial_saved = 0;
    for (unsigned i = 0; i < job.count; i++)
        if (job.entries[i].size) initial_saved++;
    atomic_init(&transfer.done, false);
    atomic_init(&transfer.cancel, false);
    atomic_init(&transfer.launch, false);
    atomic_init(&transfer.saved, initial_saved);
    atomic_init(&transfer.failed, 0);
    atomic_init(&transfer.next, 0);
    atomic_init(&transfer.workers_left, 0);
    atomic_init(&transfer.cooldown_workers, 0);
    atomic_init(&transfer.downloaded_bytes, 0);

    Thread threads[REGION_DOWNLOAD_WORKERS] = {0};
    RegionWorker workers[REGION_DOWNLOAD_WORKERS];
    unsigned worker_count = 0;
    for (unsigned i = 0; i < REGION_DOWNLOAD_WORKERS; i++) {
        workers[i].transfer = &transfer;
        atomic_fetch_add(&transfer.workers_left, 1);
        threads[i] = threadCreate(region_worker, &workers[i], 96 * 1024, 0x32, -2, false);
        if (!threads[i]) {
            atomic_fetch_sub(&transfer.workers_left, 1);
            continue;
        }
        worker_count++;
    }
    if (!worker_count) {
        region_close(&job);
        set_status_message("Could not start download");
        return;
    }
    u64 start_ms = osGetTime();
    atomic_store(&transfer.launch, true);
    region_items = NULL;
    snprintf(region_title, sizeof(region_title), "Downloading region");
    snprintf(region_detail, sizeof(region_detail), "%.24s", item->name);
    region_total = job.count;
    snprintf(region_summary, sizeof(region_summary), "Saving to SD card");
    region_actions[0] = "B  Pause and keep downloaded tiles";
    region_actions[1] = NULL;
    region_actions[2] = NULL;
    u64 free_bytes = region_sd_free_bytes();
    u64 free_space_checked_ms = start_ms;
    while (!atomic_load(&transfer.done) && aptMainLoop() && !g_should_exit) {
        u64 now_ms = osGetTime();
        if (now_ms - free_space_checked_ms >= 1000) {
            free_bytes = region_sd_free_bytes();
            free_space_checked_ms = now_ms;
        }
        region_progress = atomic_load(&transfer.saved);
        RegionProgressMetrics metrics = region_progress_metrics(initial_saved, region_progress,
                                                                job.count, now_ms - start_ms);
        char eta[24];
        region_format_eta(eta, sizeof(eta), &metrics);
        snprintf(region_summary, sizeof(region_summary), "%u/%u tiles  |  %.1f tiles/s",
                 region_progress, job.count, metrics.tiles_per_second);
        snprintf(region_hint, sizeof(region_hint), "%.2f MB downloaded  |  ETA %s",
                 atomic_load(&transfer.downloaded_bytes) / 1048576.0, eta);
        const char *state = atomic_load(&transfer.cancel) ? "  |  Pausing..." :
                            atomic_load(&transfer.cooldown_workers) ? "  |  Provider cooldown" : "";
        if (free_bytes)
            snprintf(region_status, sizeof(region_status), "SD free %.0f MB  |  %u connection%s%s",
                     free_bytes / 1048576.0, worker_count, worker_count == 1 ? "" : "s", state);
        else snprintf(region_status, sizeof(region_status), "%u connection%s%s",
                      worker_count, worker_count == 1 ? "" : "s", state);
        if (region_frame() & (KEY_B | KEY_START)) atomic_store(&transfer.cancel, true);
    }
    if (!atomic_load(&transfer.done)) atomic_store(&transfer.cancel, true);
    for (unsigned i = 0; i < REGION_DOWNLOAD_WORKERS; i++) if (threads[i]) {
        threadJoin(threads[i], U64_MAX);
        threadFree(threads[i]);
    }
    char final[REGION_PATH_LIMIT];
    size_t stem = strlen(item->path) - 5;
    snprintf(final, sizeof(final), "%.*s.tilepack", (int)stem, item->path);
    if (atomic_load(&transfer.saved) == job.count &&
        region_finish(&job, item->path, final) && network_install_region(final, item->source)) {
        map_tiles_view_changed();
        set_status_message("Offline pack ready");
    } else {
        set_status_message(atomic_load(&transfer.failed) ?
                           "Download interrupted; A to resume" : "Download paused or pack unavailable");
    }
    region_close(&job);
}

static void setup_offline_source(void) {
    char endpoint[220] = "";
    TileSource source = network_get_tile_source();
    char initial[sizeof(endpoint)] = "";
    if (!network_region_endpoint(source, initial, sizeof(initial))) initial[0] = '\0';
    if (!region_keyboard(endpoint, sizeof(endpoint), source == TILE_SOURCE_SATELLITE ?
                         "Satellite tile URL" : "Street tile URL", initial)) return;
    char sample[384];
    if ((strncmp(endpoint, "https://", 8) && strncmp(endpoint, "http://", 7)) ||
        strstr(endpoint, "tile.openstreetmap.org") || strpbrk(endpoint, " \r\n") ||
        !network_region_url(endpoint, 0, 0, 1, sample, sizeof(sample))) {
        set_status_message("Use a valid offline-capable tile URL");
        return;
    }
    ensure_app_data_dir();
    FILE *f = fopen(source == TILE_SOURCE_SATELLITE ?
                    "sdmc:/3ds_google_maps/offline-sat.cfg" : "sdmc:/3ds_google_maps/offline-street.cfg", "w");
    if (!f) {
        set_status_message("Could not save tile source");
        return;
    }
    bool wrote = fprintf(f, "%s\n", endpoint) > 0;
    bool closed = fclose(f) == 0;
    set_status_message(wrote && closed ? "Offline source saved" : "Could not save tile source");
}

static void open_offline_download(void) {
    if (network_region_count() >= REGION_PACK_LIMIT) {
        set_status_message("Remove a region before downloading");
        return;
    }
    int max_zoom = map_state.zoom > 16 ? 18 : map_state.zoom + 2;
    int min_zoom = max_zoom > 4 ? max_zoom - 4 : 1;
    TilePackEntry *plan = calloc(REGION_LIMIT, sizeof(*plan));
    if (!plan) return;
    bool route = false, confirmed = false, quick_name = false, changed = true;
    unsigned count = 0;
    region_items = NULL;
    snprintf(region_title, sizeof(region_title), "New offline pack");
    region_total = 0;
    u64 free_bytes = region_sd_free_bytes();
    region_actions[0] = "Left/Right  Min zoom    Up/Down  Max zoom";
    region_actions[1] = "X  Area / Route corridor";
    region_actions[2] = "A  Name    Select  Quick save    B  Back";
    while (aptMainLoop() && !g_should_exit) {
        if (changed) {
            RegionPoint center = { map_state.lat, map_state.lon };
            count = region_plan(plan, REGION_LIMIT, min_zoom, max_zoom,
                                route ? g_route_geometry : &center,
                                route ? g_route_geometry_count : 1, map_state.zoom<1?1:map_state.zoom, 420);
            snprintf(region_detail, sizeof(region_detail), "%s  |  Zoom %d-%d",
                     route ? "Route corridor" : "Current map area", min_zoom, max_zoom);
            if (count > REGION_LIMIT) {
                snprintf(region_summary, sizeof(region_summary), "Region exceeds %d tiles", REGION_LIMIT);
                snprintf(region_hint, sizeof(region_hint), "Lower the maximum zoom.");
            } else {
                snprintf(region_summary, sizeof(region_summary), "%u tiles", count);
                snprintf(region_hint, sizeof(region_hint), "Estimated size: %.1f to %.1f MB",
                         count * 15 / 1024.0, count * 100 / 1024.0);
            }
            if (free_bytes)
                snprintf(region_status, sizeof(region_status), "SD free: %.0f MB  |  %s tiles",
                         free_bytes / 1048576.0, network_get_tile_source_name());
            else snprintf(region_status, sizeof(region_status), "Layer: %s", network_get_tile_source_name());
            changed = false;
        }
        u32 keys = region_frame();
        if (keys & (KEY_B | KEY_START)) break;
        if ((keys & (KEY_A | KEY_SELECT)) && count && count <= REGION_LIMIT) {
            quick_name = (keys & KEY_SELECT) != 0;
            confirmed = true;
            break;
        }
        if (keys & KEY_DLEFT && min_zoom > 1) { min_zoom--; changed = true; }
        if (keys & KEY_DRIGHT && min_zoom < max_zoom) { min_zoom++; changed = true; }
        if (keys & KEY_DUP && max_zoom < 18) { max_zoom++; changed = true; }
        if (keys & KEY_DDOWN && max_zoom > min_zoom) { max_zoom--; changed = true; }
        if (keys & KEY_X) {
            if (g_route_active && g_route_count >= 2) { route = !route; changed = true; }
            else snprintf(region_status, sizeof(region_status), "Plan a route first to download its corridor.");
        }
    }
    RegionItem item = { .source = network_get_tile_source(), .partial = true };
    if (quick_name) snprintf(item.name, sizeof(item.name), "%s-%08lx",
                             route ? "Route" : "Area", (unsigned long)(osGetTime() / 1000 & 0xffffffffu));
    if (confirmed && (quick_name || region_keyboard(item.name, sizeof(item.name), "Name this offline region", NULL))) {
        for (char *p = item.name; *p; p++) {
            unsigned char c = *p;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_')) *p = '-';
        }
        ensure_app_data_dir();
        mkdir(REGION_DIR, 0777);
        const char *prefix = item.source == TILE_SOURCE_SATELLITE ? "sat" : "str";
        snprintf(item.path, sizeof(item.path), "%s/%s-%s.part", REGION_DIR, prefix, item.name);
        char final[REGION_PATH_LIMIT];
        snprintf(final, sizeof(final), "%s/%s-%s.tilepack", REGION_DIR, prefix, item.name);
        if (access(item.path, F_OK) == 0 || access(final, F_OK) == 0)
            set_status_message("Name already exists; choose it to resume");
        else run_region_transfer(&item, plan, count);
    }
    free(plan);
}

static int compare_regions(const void *left, const void *right) {
    const RegionItem *a = left, *b = right;
    if (a->legacy != b->legacy) return a->legacy ? -1 : 1;
    int order = strcmp(a->name, b->name);
    return order ? order : (int)a->source - (int)b->source;
}

static int list_regions(RegionItem *items) {
    int count = 0;
    for (int sat = 0; sat <= 1; sat++) {
        TileSource source = sat ? TILE_SOURCE_SATELLITE : TILE_SOURCE_STREET;
        OfflinePackInfo info;
        if (!network_get_offline_pack(source, &info)) continue;
        RegionItem *item = &items[count++];
        *item = (RegionItem){ .source = source, .legacy = true, .size = info.bytes };
        snprintf(item->name, sizeof(item->name), "%s", sat ? "Legacy satellite" : "Legacy street");
        snprintf(item->path, sizeof(item->path), "sdmc:/3ds_google_maps/tiles/%s.tilepack", sat ? "sat" : "street");
    }
    DIR *dir = opendir(REGION_DIR);
    if (!dir) return count;
    struct dirent *entry;
    while (count < REGION_LIST_MAX && (entry = readdir(dir))) {
        const char *name = entry->d_name;
        size_t len = strlen(name);
        bool partial = len > 9 && !strcmp(name + len - 5, ".part");
        size_t ext = partial ? 5 : 9;
        if (len <= 4 + ext || len > 4 + REGION_NAME_LIMIT + ext ||
            (!partial && strcmp(name + len - ext, ".tilepack"))) continue;
        bool sat = !strncmp(name, "sat-", 4);
        if (!sat && strncmp(name, "str-", 4)) continue;
        RegionItem *item = &items[count++];
        *item = (RegionItem){ .source = sat ? TILE_SOURCE_SATELLITE : TILE_SOURCE_STREET, .partial = partial };
        snprintf(item->name, sizeof(item->name), "%.*s", (int)(len - 4 - ext), name + 4);
        snprintf(item->path, sizeof(item->path), "%s/%.*s", REGION_DIR, (int)len, name);
        struct stat statbuf;
        if (stat(item->path, &statbuf) == 0) item->size = statbuf.st_size;
    }
    closedir(dir);
    qsort(items, count, sizeof(*items), compare_regions);
    return count;
}

typedef struct {
    TilePack *pack;
    atomic_bool done, cancel;
    atomic_uint checked, bad;
} RegionCheck;

static void check_region_worker(void *arg) {
    RegionCheck *check = arg;
    TilePack *pack = check->pack;
    for (unsigned i = 0; i < pack->tile_count && !atomic_load(&check->cancel); i++) {
        TilePackEntry *tile = &pack->index[i];
        size_t size = 0;
        u8 *bytes = tilepack_read_tile(pack, tile->zoom, tile->x, tile->y, &size);
        if (!tile_image_valid(bytes, size, true)) atomic_fetch_add(&check->bad, 1);
        free(bytes);
        atomic_fetch_add(&check->checked, 1);
    }
    atomic_store(&check->done, true);
}

static void check_region(const RegionItem *item) {
    TilePack pack;
    if (!tilepack_open(&pack, item->path)) {
        set_status_message("Pack index missing or damaged");
        return;
    }
    RegionCheck check = { .pack = &pack };
    Thread thread = threadCreate(check_region_worker, &check, 96 * 1024, 0x32, -2, false);
    if (!thread) {
        tilepack_close(&pack);
        set_status_message("Could not start pack check");
        return;
    }
    region_items = NULL;
    snprintf(region_title, sizeof(region_title), "Checking offline pack");
    snprintf(region_detail, sizeof(region_detail), "%.24s", item->name);
    region_total = pack.tile_count;
    snprintf(region_summary, sizeof(region_summary), "Reading tile images");
    region_actions[0] = "B  Stop check";
    region_actions[1] = region_actions[2] = NULL;
    while (!atomic_load(&check.done) && aptMainLoop() && !g_should_exit) {
        region_progress = atomic_load(&check.checked);
        snprintf(region_status, sizeof(region_status), "%u / %u checked  |  %u unreadable",
                 region_progress, region_total, atomic_load(&check.bad));
        if (region_frame() & (KEY_B | KEY_START)) atomic_store(&check.cancel, true);
    }
    atomic_store(&check.cancel, true);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    unsigned checked = atomic_load(&check.checked);
    char result[96];
    snprintf(result, sizeof(result), "%u checked, %u unreadable%s", checked, atomic_load(&check.bad),
             checked < pack.tile_count ? " (stopped)" : "");
    tilepack_close(&pack);
    set_status_message(result);
}

static void open_offline_regions(void) {
    RegionItem items[REGION_LIST_MAX];
    int selected = 0;
    bool confirm = false;
    int count = list_regions(items);
    set_status_message("Up/Down selects a pack");
    while (aptMainLoop() && !g_should_exit) {
        region_items = items;
        region_selected = selected;
        region_count = count;
        region_confirm = confirm;
        const RegionItem *item = count ? &items[selected] : NULL;
        snprintf(region_title, sizeof(region_title), "Offline regions");
        snprintf(region_detail, sizeof(region_detail), "%d saved or paused  |  Coverage %s",
                 count, g_show_coverage ? "ON" : "OFF");
        snprintf(region_status, sizeof(region_status), "%s", confirm ? "Press X again to remove this pack." :
                 osGetTime() - g_status_message_ms < 3500 ? g_status_message : "Up/Down selects a pack.");
        region_actions[0] = "Y  New pack    L  Tile source    R  Coverage";
        region_actions[1] = item && item->partial ? "A  Resume    X  Remove" : "A  Preview    Select  Check    X  Remove";
        region_actions[2] = g_show_coverage ? "B  Back    Green: saved   Amber: missing" : "B  Back";
        u32 keys = region_frame();
        if (keys & (KEY_B | KEY_START)) break;
        if (keys & KEY_DUP && selected > 0) { selected--; confirm = false; }
        else if (keys & KEY_DDOWN && selected + 1 < count) { selected++; confirm = false; }
        else if (keys & KEY_R) g_show_coverage = !g_show_coverage;
        else if (keys & KEY_L) setup_offline_source();
        else if (keys & KEY_Y) open_offline_download();
        else if (item && (keys & KEY_X)) {
            if (!confirm) { confirm = true; continue; }
            bool ok = item->legacy ? network_remove_offline_pack(item->source) : network_remove_region(item->path);
            map_tiles_clear_cache();
            set_status_message(ok ? "Pack removed" : "Could not remove pack");
        } else if (item && !confirm && (keys & KEY_SELECT) && !item->partial) check_region(item);
        else if (item && !confirm && (keys & KEY_A)) {
            if (item->partial) run_region_transfer(item, NULL, 0);
            else {
                TilePack pack;
                if (!tilepack_open(&pack, item->path)) {
                    set_status_message("Pack index missing or damaged");
                    continue;
                }
                TilePackEntry sample = pack.index[pack.tile_count / 2];
                tilepack_close(&pack);
                double north, west, south, east;
                tile_to_lat_lon(sample.x, sample.y, sample.zoom, &north, &west);
                tile_to_lat_lon(sample.x + 1, sample.y + 1, sample.zoom, &south, &east);
                network_set_tile_source(item->source);
                map_tiles_clear_cache();
                map_state.gps_tracking = false;
                g_show_coverage = true;
                set_camera_target((north + south) / 2, (west + east) / 2,
                                  sample.zoom < 1 ? 1 : sample.zoom > 18 ? 18 : sample.zoom, true);
                close_menu("Offline pack preview");
                break;
            }
        } else continue;
        confirm = false;
        count = list_regions(items);
        if (selected >= count) selected = count ? count - 1 : 0;
    }
    region_items = NULL;
    region_confirm = false;
}

static void open_recents(void) {
    if (g_recent_count <= 0) {
        begin_bottom_console_ui();
        consoleClear();
        printf("No recents yet.\n\n");
        printf("Run a SELECT address search first.\n\n");
        printf("Press B or R to return\n");
        while (aptMainLoop() && !g_should_exit) {
            hidScanInput();
            u32 kd = hidKeysDown();
            if (kd & KEY_START) {
                g_should_exit = true;
                end_bottom_console_ui(NULL);
                return;
            }
            if (kd & (KEY_B | KEY_R)) {
                end_bottom_console_ui("Closed recents");
                return;
            }
            svcSleepThread(60000000LL);
        }
        end_bottom_console_ui(NULL);
        return;
    }

    int selected = 0;
    begin_bottom_console_ui();
    while (aptMainLoop() && !g_should_exit) {
        consoleClear();
        printf("=== Recents ===\n");
        printf("Up/Down = select  A = go\n");
        printf("B or R = back  START = exit\n\n");

        for (int i = 0; i < g_recent_count; i++) {
            printf("%c %d) %s\n", (i == selected) ? '>' : ' ', i + 1, g_recents[i].name);
        }

        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & KEY_START) {
            g_should_exit = true;
            end_bottom_console_ui(NULL);
            return;
        }
        if (kDown & (KEY_B | KEY_R)) {
            end_bottom_console_ui("Closed recents");
            return;
        }
        if (kDown & KEY_DUP) {
            if (selected > 0) selected--;
        }
        if (kDown & KEY_DDOWN) {
            if (selected < g_recent_count - 1) selected++;
        }
        if (kDown & KEY_A) {
            map_state.offset_x = 0;
            map_state.offset_y = 0;
            map_state.dragging = false;
            map_state.gps_tracking = false;
            set_camera_target(g_recents[selected].lat, g_recents[selected].lon, g_target_zoom, true);
            g_last_interaction_ms = osGetTime();
            end_bottom_console_ui("Jumped to recent");
            return;
        }

        svcSleepThread(60000000LL);
    }
    end_bottom_console_ui(NULL);
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

static bool json_coord(json_t *value, double limit, double *out) {
    double number;
    if (json_is_number(value)) {
        number = json_number_value(value);
    } else if (json_is_string(value)) {
        const char *start = json_string_value(value);
        char *end;
        number = strtod(start, &end);
        if (end == start || *end) return false;
    } else {
        return false;
    }
    if (!isfinite(number) || fabs(number) > limit) return false;
    *out = number;
    return true;
}

static bool read_geocode(json_t *entry, GeocodeResult *result) {
    if (!json_coord(json_object_get(entry, "lat"), 90, &result->lat) ||
        !json_coord(json_object_get(entry, "lon"), 180, &result->lon)) return false;
    const char *name = json_string_value(json_object_get(entry, "display_name"));
    if (!name || !*name) name = json_string_value(json_object_get(entry, "name"));
    if (!name || !*name) return false;
    size_t n = strlen(name);
    if (n >= sizeof(result->name)) {
        n = sizeof(result->name) - 1;
        while (n && ((unsigned char)name[n] & 0xc0) == 0x80) n--;
    }
    for (size_t i = 0; i < n; i++) result->name[i] = (unsigned char)name[i] < 32 ? ' ' : name[i];
    result->name[n] = 0;
    return true;
}

static int parse_geocode_results_json(const char *json, u32 size, GeocodeResult *results, int max) {
    json_t *root = json_loadb(json, size, JSON_REJECT_DUPLICATES, NULL);
    json_t *list = json_is_array(root) ? root : json_object_get(root, "results");
    size_t i;
    json_t *entry;
    int count = 0;
    json_array_foreach(list, i, entry) {
        if (count >= max) break;
        if (read_geocode(entry, &results[count])) count++;
    }
    json_decref(root);
    return count;
}

static bool parse_reverse_geocode_json(const char *json, u32 size, GeocodeResult *result) {
    json_t *root = json_loadb(json, size, JSON_REJECT_DUPLICATES, NULL);
    bool ok = read_geocode(root, result);
    json_decref(root);
    return ok;
}

static void open_reverse_geocode_center(void) {
    if (!is_connected()) {
        set_status_message("No WiFi for identify");
        return;
    }

    char url[512];
    if (network_get_proxy_enabled()) {
        snprintf(url, sizeof(url), "%s/reverse?lat=%.6f&lon=%.6f", network_get_proxy_base_url(), map_state.lat, map_state.lon);
    } else {
        snprintf(url, sizeof(url),
                 "https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=18&lat=%.6f&lon=%.6f",
                 map_state.lat, map_state.lon);
    }

    u8 *buf = NULL;
    u32 size = 0;
    if (!wait_for_request(url, "Identifying map center", &buf, &size)) return;

    GeocodeResult result;
    memset(&result, 0, sizeof(result));
    bool ok = parse_reverse_geocode_json((const char*)buf, size, &result);
    free(buf);

    if (!ok) {
        set_status_message("No place found for center");
        return;
    }
    add_recent(result.name, result.lat, result.lon);
    set_last_place_name(result.name);
    show_places(&result, 1, true);
    set_status_message("Identify complete");
}

static void open_address_search(void) {
    if (g_should_exit) return;

    char query[128] = {0};
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(query) - 1);
    swkbdSetHintText(&swkbd, "Place, address, ZIP or lat,lon");
    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
    if (g_last_address_query[0])
        swkbdSetInitialText(&swkbd,g_last_address_query);

    SwkbdButton btn = swkbdInputText(&swkbd, query, sizeof(query));
    if (btn != SWKBD_BUTTON_RIGHT) {
        return; // cancelled
    }

    if (g_should_exit) return;

    /* UTF-8-safe trimming keeps names intact; input stays editable. */
    if (!search_trim_query(query)) {
        set_status_message("Type a place, address or coordinates");
        return;
    }
    snprintf(g_last_address_query,sizeof(g_last_address_query),"%s",query);

    // Coordinate entry shortcut: "lat,lon" jumps instantly without geocoding.
    double in_lat = 0.0, in_lon = 0.0;
    if (parse_latlon(query, &in_lat, &in_lon)) {
        map_state.offset_x = 0;
        map_state.offset_y = 0;
        map_state.dragging = false;
        map_state.gps_tracking = false;
        set_camera_target(in_lat, in_lon, g_target_zoom, true);
        add_recent(query, map_state.lat, map_state.lon);
        set_status_message("Jumped to coordinates");
        return;
    }

    if (!is_connected()) {
        set_status_message("No WiFi for search");
        return;
    }

    char encoded[256];
    if (!url_encode_component(query, encoded, sizeof(encoded))) {
        set_status_message("Search too long");
        return;
    }

    /* One-step search: near-map bias first, X on results expands worldwide.
     * Never send autocomplete or background queries to public Nominatim. */
    bool near = true;
    GeocodeResult results[5];
    int count=0,selected=-1;
    do {
        char url[512];
        build_search_url(url,sizeof(url),encoded,near);
        u64 now=osGetTime();
        if(g_search_cache.count>0 && strcmp(url,g_search_cache.url)==0 &&
           now>=g_search_cache.when_ms &&
           now-g_search_cache.when_ms<300000){
            count=g_search_cache.count;
            memcpy(results,g_search_cache.results,sizeof(results));
            set_status_message("Results loaded from local cache");
        }else{
            u8 *buf=NULL;
            u32 size=0;
            if (!wait_for_request(url,near?"Searching near map":"Searching worldwide",&buf,&size)) {
                free(buf);
                return;
            }
            memset(results,0,sizeof(results));
            count=parse_geocode_results_json((const char*)buf,size,results,5);
            free(buf);
            if(count>0){
                snprintf(g_search_cache.url,sizeof(g_search_cache.url),"%s",url);
                memcpy(g_search_cache.results,results,sizeof(results));
                g_search_cache.count=count;
                g_search_cache.when_ms=osGetTime();
            }
        }
        if(count<=0){
            if(near && !network_get_proxy_enabled()){
                /* One automatic worldwide fallback after a genuine empty
                 * local result. The global geocoder gate prevents hammering
                 * the public service and the query is never repeated as
                 * the user types. */
                near=false;
                selected=-2;
                set_status_message("No nearby match; searching worldwide");
                continue;
            }
            set_status_message("No matches; try a city, ZIP or shorter address");
            return;
        }
        place_allow_worldwide=near&&!network_get_proxy_enabled();
        selected=count>1 ? show_places(results,count,false) : 0;
        place_allow_worldwide=false;
        if(selected==-2 && near){
            near=false;
            /* An explicitly requested follow-up is rate-limited by
             * wait_for_request(), keeping the bottom screen responsive. */
        }
    }while(selected==-2 && !near);
    if(selected<0){
        set_status_message("Search cancelled");
        return;
    }

    // Jump map
    map_state.offset_x = 0;
    map_state.offset_y = 0;
    map_state.dragging = false;
    map_state.gps_tracking = false; // manual search takes control
    set_camera_target(results[selected].lat, results[selected].lon, g_target_zoom, true);

    add_recent(results[selected].name, map_state.lat, map_state.lon);
    set_status_message("Search jump complete");
}
