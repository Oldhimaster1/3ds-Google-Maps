#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include "map_tiles.h"
#include "network.h"
#include "gps.h"
#include "logging.h"
#include "stb_image.h"
#include "qrcode.h"

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
    /* GPS tab */
    SETTINGS_HIT_TAB_GPS,
    SETTINGS_HIT_GPS_MODE_CLIENT,
    SETTINGS_HIT_GPS_MODE_SERVER,
    SETTINGS_HIT_GPS_START_STOP
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
static RecentPlace g_favorites[MAX_FAVORITES];
static int g_favorite_count = 0;
static char g_last_place_name[96] = {0};
static u64 g_last_place_ms = 0;
static char g_status_message[96] = {0};
static u64 g_status_message_ms = 0;
static bool g_debug_overlay = false;
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

typedef struct {
    bool active;
    C2D_Image image;
    int tex_w;
    int tex_h;
    int img_w;
    int img_h;
    double center_lat;
    double center_lon;
    float yaw_deg;
    float pitch_deg;
    u64 last_ms;
} PanoState;

static PanoState g_pano = {0};

// Smooth camera state.
static double g_target_lat = 0.0;
static double g_target_lon = 0.0;
static int g_target_zoom = 0;
static u64 g_last_zoom_step_ms = 0;

typedef struct {
    double lat;
    double lon;
} RoutePoint;

#define MAX_ROUTE_POINTS 256
static RoutePoint g_route[MAX_ROUTE_POINTS];
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

static const char* skip_ws(const char* p, const char* end);
static bool extract_json_string_field(const char* start, const char* end, const char* key, char* out, size_t out_size);
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
static void open_panorama_at_center(void);
static void render_panorama(void);
static void update_panorama_input(void);
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

static void ensure_app_data_dir(void) {
    mkdir("sdmc:/3ds_google_maps", 0777);
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

static void pano_free(void) {
    if (g_pano.image.tex) {
        C3D_TexDelete(g_pano.image.tex);
        free(g_pano.image.tex);
        g_pano.image.tex = NULL;
    }
    if (g_pano.image.subtex) {
        free((void*)g_pano.image.subtex);
        g_pano.image.subtex = NULL;
    }
    memset(&g_pano, 0, sizeof(g_pano));
}

static unsigned char* rgba_to_tiled_rgba8(const unsigned char* rgba, int w, int h, int* out_tex_w, int* out_tex_h) {
    if (!rgba || w <= 0 || h <= 0) return NULL;

    int tex_w = (w + 7) & ~7;
    int tex_h = (h + 7) & ~7;
    if (out_tex_w) *out_tex_w = tex_w;
    if (out_tex_h) *out_tex_h = tex_h;

    // Pad to multiples of 8 so our 8x8 Morton tiling works.
    unsigned char* padded = (unsigned char*)calloc((size_t)tex_w * (size_t)tex_h * 4, 1);
    if (!padded) return NULL;

    for (int y = 0; y < h; y++) {
        memcpy(padded + (size_t)y * (size_t)tex_w * 4, rgba + (size_t)y * (size_t)w * 4, (size_t)w * 4);
    }

    unsigned char* tiled = (unsigned char*)malloc((size_t)tex_w * (size_t)tex_h * 4);
    if (!tiled) {
        free(padded);
        return NULL;
    }

    for (int y = 0; y < tex_h; y++) {
        for (int x = 0; x < tex_w; x++) {
            int src_idx = (y * tex_w + x) * 4;

            int tile_x = x >> 3;
            int tile_y = y >> 3;
            int in_tile_x = x & 7;
            int in_tile_y = y & 7;

            int morton =
                ((in_tile_x & 1) << 0) | ((in_tile_y & 1) << 1) |
                ((in_tile_x & 2) << 1) | ((in_tile_y & 2) << 2) |
                ((in_tile_x & 4) << 2) | ((in_tile_y & 4) << 3);

            int tiles_per_row = tex_w >> 3;
            int tile_offset = (tile_y * tiles_per_row + tile_x) * 64;
            int dst_idx = (tile_offset + morton) * 4;

            tiled[dst_idx + 0] = padded[src_idx + 3];
            tiled[dst_idx + 1] = padded[src_idx + 2];
            tiled[dst_idx + 2] = padded[src_idx + 1];
            tiled[dst_idx + 3] = padded[src_idx + 0];
        }
    }

    free(padded);
    return tiled;
}

static bool pano_load_from_proxy(double lat, double lon) {
    if (!is_connected()) {
        printf("No WiFi; can't load pano.\n");
        return false;
    }

    char url[512];
    snprintf(url, sizeof(url), PROXY_BASE_URL "/pano?lat=%.6f&lon=%.6f", lat, lon);

    u8* buf = NULL;
    u32 size = 0;
    if (!download_url(url, &buf, &size) || !buf || size < 16) {
        if (buf) free(buf);
        return false;
    }

    int w = 0, h = 0, comp = 0;
    unsigned char* rgba = stbi_load_from_memory((const unsigned char*)buf, (int)size, &w, &h, &comp, 4);
    free(buf);
    if (!rgba || w <= 0 || h <= 0) {
        if (rgba) stbi_image_free(rgba);
        printf("Pano decode failed.\n");
        return false;
    }

    int tex_w = 0, tex_h = 0;
    unsigned char* tiled = rgba_to_tiled_rgba8(rgba, w, h, &tex_w, &tex_h);
    stbi_image_free(rgba);
    if (!tiled) {
        printf("Pano tiling failed.\n");
        return false;
    }

    C3D_Tex* tex = (C3D_Tex*)malloc(sizeof(C3D_Tex));
    if (!tex) {
        free(tiled);
        return false;
    }

    if (!C3D_TexInit(tex, tex_w, tex_h, GPU_RGBA8)) {
        free(tex);
        free(tiled);
        return false;
    }
    C3D_TexUpload(tex, tiled);
    free(tiled);

    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    Tex3DS_SubTexture* st = (Tex3DS_SubTexture*)malloc(sizeof(Tex3DS_SubTexture));
    if (!st) {
        C3D_TexDelete(tex);
        free(tex);
        return false;
    }

    st->width = tex_w;
    st->height = tex_h;
    st->left = 0.0f;
    st->top = 1.0f;
    st->right = (float)w / (float)tex_w;
    st->bottom = 1.0f - ((float)h / (float)tex_h);

    pano_free();
    g_pano.image.tex = tex;
    g_pano.image.subtex = st;
    g_pano.tex_w = tex_w;
    g_pano.tex_h = tex_h;
    g_pano.img_w = w;
    g_pano.img_h = h;
    g_pano.center_lat = lat;
    g_pano.center_lon = lon;
    g_pano.yaw_deg = 0.0f;
    g_pano.pitch_deg = 0.0f;
    g_pano.last_ms = osGetTime();
    g_pano.active = true;
    return true;
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

    if (lat < -90.0 || lat > 90.0) return false;
    if (lon < -180.0 || lon > 180.0) return false;

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
    g_target_lat = lat;
    g_target_lon = lon;
    g_target_zoom = zoom;

    if (snap_now) {
        map_state.lat = lat;
        map_state.lon = lon;
        map_state.zoom = zoom;
        map_tiles_clear_cache();
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
            map_tiles_clear_cache();
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

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    log_init();
    cfguInit();
    sslcInit(0);
    httpcInit(0);
    acInit();
    
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
    favorites_load();
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

        if (g_pano.active) {
            update_panorama_input();
            if (g_should_exit) break;
            render_panorama();
            continue;
        }

        // If GPS tracking is on and we have a fix, update position
        if (map_state.gps_tracking && gps_has_fix()) {
            double gps_lat, gps_lon;
            if (gps_get_position(&gps_lat, &gps_lon)) {
                set_camera_target(gps_lat, gps_lon, g_target_zoom, false);
            }
        }

        update_input();
        if (g_should_exit) break;

        update_camera_smooth();

        // Upload any tiles that the background download thread has finished.
        map_tiles_process_downloads();

        // Prefetch a 1-tile ring around the view after ~0.5s of user idle.
        // This is throttled (one tile per frame) to keep things responsive.
        if (!map_state.gps_tracking) {
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

cleanup:
    // Cleanup
    gps_cleanup();
    map_tiles_cleanup();
    network_cleanup();
    cleanup_graphics();
    log_close();
    httpcExit();
    sslcExit();
    acExit();
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

static void open_panorama_at_center(void) {
    if (!network_has_proxy()) {
        set_status_message("Panorama needs proxy");
        return;
    }

    begin_bottom_console_ui();
    consoleClear();
    printf("Loading panorama at map center...\n");
    printf("Center: %.6f, %.6f\n\n", map_state.lat, map_state.lon);
    printf("B = cancel   START = exit\n");

    // Enable gyro (fails harmlessly on some hardware).
    HIDUSER_EnableGyroscope();

    if (!pano_load_from_proxy(map_state.lat, map_state.lon)) {
        consoleClear();
        printf("No pano found (or proxy error).\n\n");
        printf("Tip: ensure proxy has MAPILLARY_ACCESS_TOKEN set.\n\n");
        printf("Press B to return\n");
        while (aptMainLoop() && !g_should_exit) {
            hidScanInput();
            u32 kd = hidKeysDown();
            if (kd & KEY_START) {
                g_should_exit = true;
                end_bottom_console_ui(NULL);
                return;
            }
            if (kd & KEY_B) {
                end_bottom_console_ui("Panorama unavailable");
                return;
            }
            svcSleepThread(60000000LL);
        }
        end_bottom_console_ui(NULL);
        return;
    }

    end_bottom_console_ui("Panorama loaded");
    set_status_message("Panorama loaded");
}

static void update_panorama_input(void) {
    hidScanInput();
    u32 kDown = hidKeysDown();
    u32 kHeld = hidKeysHeld();

    if (kDown || kHeld) {
        g_last_interaction_ms = osGetTime();
    }

    if (kDown & KEY_START) {
        g_should_exit = true;
        return;
    }
    if (kDown & KEY_B) {
        pano_free();
        HIDUSER_DisableGyroscope();
        set_status_message("Closed panorama");
        return;
    }

    // Manual tweak (Circle Pad) for systems without gyro / comfort.
    circlePosition cp;
    hidCircleRead(&cp);
    g_pano.yaw_deg += (float)cp.dx * 0.05f;
    g_pano.pitch_deg -= (float)cp.dy * 0.05f;

    // Gyro look-around: integrate angular rate.
    u64 now = osGetTime();
    float dt = (float)(now - g_pano.last_ms) / 1000.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.2f) dt = 0.2f;
    g_pano.last_ms = now;

    angularRate gyro;
    hidGyroRead(&gyro);
    // Empirical scaling; adjust if needed.
    const float sens = 0.0035f;
    g_pano.yaw_deg += (float)gyro.z * dt * (1.0f / sens);
    g_pano.pitch_deg += (float)gyro.x * dt * (1.0f / sens);

    // Clamp pitch and wrap yaw.
    if (g_pano.pitch_deg > 85.0f) g_pano.pitch_deg = 85.0f;
    if (g_pano.pitch_deg < -85.0f) g_pano.pitch_deg = -85.0f;
    while (g_pano.yaw_deg >= 360.0f) g_pano.yaw_deg -= 360.0f;
    while (g_pano.yaw_deg < 0.0f) g_pano.yaw_deg += 360.0f;
}

static void render_panorama(void) {
    if (!g_pano.active || !g_pano.image.tex || !g_pano.image.subtex) {
        return;
    }

    // Choose an equirectangular view window.
    const float hfov_deg = 90.0f;
    const float vfov_deg = 60.0f;

    float center_u = g_pano.yaw_deg / 360.0f;
    float center_v = 0.5f - (g_pano.pitch_deg / 180.0f);
    float half_u = (hfov_deg / 360.0f) * 0.5f;
    float half_v = (vfov_deg / 180.0f) * 0.5f;

    float u0 = center_u - half_u;
    float u1 = center_u + half_u;
    float v0 = center_v - half_v;
    float v1 = center_v + half_v;

    if (v0 < 0.0f) v0 = 0.0f;
    if (v1 > 1.0f) v1 = 1.0f;

    // Start rendering frame
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top_screen, C2D_Color32f(0.0f, 0.0f, 0.0f, 1.0f));
    C2D_SceneBegin(top_screen);

    // We need to handle horizontal wrap manually.
    Tex3DS_SubTexture* st = (Tex3DS_SubTexture*)g_pano.image.subtex;

    // Convert v into the project's flipped texture convention.
    float top = 1.0f - v0;
    float bottom = 1.0f - v1;
    if (top < bottom) {
        float tmp = top;
        top = bottom;
        bottom = tmp;
    }

    if (u0 >= 0.0f && u1 <= 1.0f) {
        st->left = u0;
        st->right = u1;
        st->top = top;
        st->bottom = bottom;
        C2D_DrawImageAt(g_pano.image, 0.0f, 0.0f, 0.0f, NULL,
                        (float)SCREEN_WIDTH_TOP, (float)SCREEN_HEIGHT_TOP);
    } else {
        // Wrap: draw right segment then left segment.
        float u0w = u0;
        float u1w = u1;
        while (u0w < 0.0f) { u0w += 1.0f; u1w += 1.0f; }
        while (u0w > 1.0f) { u0w -= 1.0f; u1w -= 1.0f; }

        // Segment A: [u0w, 1]
        float a0 = u0w;
        float a1 = 1.0f;
        // Segment B: [0, u1w-1]
        float b0 = 0.0f;
        float b1 = u1w - 1.0f;
        if (b1 < 0.0f) b1 = u1w;

        float a_frac = (a1 - a0) / (u1 - u0);
        if (a_frac < 0.0f) a_frac = 0.0f;
        if (a_frac > 1.0f) a_frac = 1.0f;

        float a_w = (float)SCREEN_WIDTH_TOP * a_frac;
        float b_w = (float)SCREEN_WIDTH_TOP - a_w;

        st->left = a0;
        st->right = a1;
        st->top = top;
        st->bottom = bottom;
        C2D_DrawImageAt(g_pano.image, 0.0f, 0.0f, 0.0f, NULL, a_w, (float)SCREEN_HEIGHT_TOP);

        st->left = b0;
        st->right = b1;
        st->top = top;
        st->bottom = bottom;
        C2D_DrawImageAt(g_pano.image, a_w, 0.0f, 0.0f, NULL, b_w, (float)SCREEN_HEIGHT_TOP);
    }

    C2D_TargetClear(bottom_screen, C2D_Color32f(0.03f, 0.04f, 0.06f, 1.0f));
    C2D_SceneBegin(bottom_screen);
    C2D_TextBufClear(g_ui_text_buf);
    draw_ui_text(12.0f, 12.0f, 0.62f, 0.62f, C2D_Color32f(0.96f, 0.97f, 0.99f, 1.0f), 0, "Panorama");

    {
        char line[96];
        snprintf(line, sizeof(line), "Center %.5f, %.5f", g_pano.center_lat, g_pano.center_lon);
        draw_ui_text(12.0f, 38.0f, 0.38f, 0.38f, C2D_Color32f(0.77f, 0.84f, 0.91f, 1.0f), 0, line);
        snprintf(line, sizeof(line), "Yaw %3.0f  Pitch %3.0f", g_pano.yaw_deg, g_pano.pitch_deg);
        draw_ui_text(12.0f, 60.0f, 0.46f, 0.46f, C2D_Color32f(0.92f, 0.94f, 0.97f, 1.0f), 0, line);
    }

    C2D_DrawRectSolid(12.0f, 96.0f, 0.2f, 296.0f, 56.0f, C2D_Color32f(0.16f, 0.22f, 0.30f, 1.0f));
    C2D_DrawRectSolid(12.0f, 96.0f, 0.3f, 296.0f, 6.0f, C2D_Color32f(0.62f, 0.78f, 0.94f, 1.0f));
    draw_ui_text(160.0f, 118.0f, 0.50f, 0.50f, C2D_Color32f(0.96f, 0.98f, 1.0f, 1.0f), C2D_AlignCenter, "B: Back");
    draw_ui_text(160.0f, 180.0f, 0.38f, 0.38f, C2D_Color32f(0.73f, 0.82f, 0.92f, 1.0f), C2D_AlignCenter, "START: Exit");

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
    if (g_target_zoom > 1) {
        g_target_zoom--;
        g_last_interaction_ms = osGetTime();
    }
}

void pan_map(int dx, int dy) {
    map_state.offset_x += dx;
    map_state.offset_y += dy;
    
    // Convert pixel offset to lat/lon offset
    double lat_per_pixel = 180.0 / (TILE_SIZE * pow(2, map_state.zoom));
    double lon_per_pixel = 360.0 / (TILE_SIZE * pow(2, map_state.zoom));
    
    g_target_lat -= dy * lat_per_pixel;
    g_target_lon += dx * lon_per_pixel;
    
    // Clamp latitude
    if (g_target_lat > 85.0511) g_target_lat = 85.0511;
    if (g_target_lat < -85.0511) g_target_lat = -85.0511;
    
    // Wrap longitude
    while (g_target_lon > 180.0) g_target_lon -= 360.0;
    while (g_target_lon < -180.0) g_target_lon += 360.0;
    
    // Reset offsets after converting to lat/lon
    map_state.offset_x = 0;
    map_state.offset_y = 0;
}

void render_map(void) {
    // Start rendering frame
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    
    // Show QR code on top screen when GPS server is running in settings
    bool show_qr = (g_screen_mode == APP_SCREEN_SETTINGS &&
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

        map_tiles_render(map_state.lat, map_state.lon, map_state.zoom, 
                        map_state.offset_x, map_state.offset_y, 
                        SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP, true);

        render_route_overlay(map_state.lat, map_state.lon, map_state.zoom, SCREEN_WIDTH_TOP, SCREEN_HEIGHT_TOP);

        if (g_night_mode) {
            C2D_DrawRectSolid(0.0f, 0.0f, 0.95f, (float)SCREEN_WIDTH_TOP, (float)SCREEN_HEIGHT_TOP,
                              C2D_Color32f(0.0f, 0.0f, 0.0f, 0.35f));
        }
    }

    C2D_TargetClear(bottom_screen, C2D_Color32f(0.05f, 0.07f, 0.10f, 1.0f));
    C2D_SceneBegin(bottom_screen);
    C2D_TextBufClear(g_ui_text_buf);
    if (g_screen_mode == APP_SCREEN_SETTINGS) {
        render_settings_panel();
    } else {
        render_bottom_panel();
    }

    // End frame rendering
    C3D_FrameEnd(0);
}

static void draw_ui_text(float x, float y, float scale_x, float scale_y, u32 color, u32 flags, const char* text) {
    C2D_Text ui_text;

    if (!g_ui_text_buf || !text || !text[0]) return;

    C2D_TextParse(&ui_text, g_ui_text_buf, text);
    C2D_TextOptimize(&ui_text);
    C2D_DrawText(&ui_text, flags | C2D_WithColor, x, y, 0.5f, scale_x, scale_y, color);
}

static void render_qr_top_screen(void) {
    if (!g_qr_valid || g_qr_size == 0) return;

    int quiet = 4;
    int total_mods = g_qr_size + 2 * quiet;
    int px = 240 / total_mods;
    if (px < 2) px = 2;
    int total_px = total_mods * px;
    float x0 = (400.0f - (float)total_px) / 2.0f;
    float y0 = (240.0f - (float)total_px) / 2.0f;

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

    /* Label */
    C2D_TextBufClear(g_ui_text_buf);
    draw_ui_text(200.0f, 2.0f, 0.38f, 0.38f,
                 C2D_Color32f(0.25f, 0.25f, 0.25f, 1.0f),
                 C2D_AlignCenter, "Scan with phone to connect GPS");
}

static SettingsHitTarget get_settings_hit(const touchPosition* touch) {
    if (!touch) return SETTINGS_HIT_NONE;

    /* Close button — top-right header area */
    if (point_in_rect(touch->px, touch->py, 246, 10, 62, 24)) return SETTINGS_HIT_CLOSE;

    /* 4 tabs: x = 8 + i*76, w = 72, y = 50, h = 24 */
    if (point_in_rect(touch->px, touch->py,   8, 50, 72, 24)) return SETTINGS_HIT_TAB_VIEW;
    if (point_in_rect(touch->px, touch->py,  84, 50, 72, 24)) return SETTINGS_HIT_TAB_TILES;
    if (point_in_rect(touch->px, touch->py, 160, 50, 72, 24)) return SETTINGS_HIT_TAB_DATA;
    if (point_in_rect(touch->px, touch->py, 236, 50, 72, 24)) return SETTINGS_HIT_TAB_GPS;

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
            if (point_in_rect(touch->px, touch->py, 12, 154, 296, 60)) return SETTINGS_HIT_CLEAR_CACHE;
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
    static const char* tab_names[SETTINGS_TAB_COUNT] = { "View", "Tiles", "Data", "GPS" };
    static const char* tab_hints[SETTINGS_TAB_COUNT] = {
        "Style and live overlays",
        "Tile loading behavior",
        "Network and storage",
        "GPS source and server"
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

    /* 4 tabs: x = 8 + i*76, w = 72 */
    for (int i = 0; i < SETTINGS_TAB_COUNT; i++) {
        float x = 8.0f + (float)i * 76.0f;
        bool selected = (i == g_settings_tab);
        bool pressed = (i == SETTINGS_TAB_VIEW  && is_recent_settings_hit(SETTINGS_HIT_TAB_VIEW,  now_ms)) ||
                       (i == SETTINGS_TAB_TILES && is_recent_settings_hit(SETTINGS_HIT_TAB_TILES, now_ms)) ||
                       (i == SETTINGS_TAB_DATA  && is_recent_settings_hit(SETTINGS_HIT_TAB_DATA,  now_ms)) ||
                       (i == SETTINGS_TAB_GPS   && is_recent_settings_hit(SETTINGS_HIT_TAB_GPS,   now_ms));
        u32 fill = selected ? C2D_Color32f(0.28f, 0.48f, 0.66f, 1.0f)
                            : (pressed ? C2D_Color32f(0.24f, 0.29f, 0.35f, 1.0f) : C2D_Color32f(0.15f, 0.18f, 0.22f, 1.0f));
        C2D_DrawRectSolid(x, 50.0f, 0.2f, 72.0f, 24.0f, fill);
        draw_ui_text(x + 36.0f, 56.0f, 0.38f, 0.38f,
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
        bool proxy_enabled = network_get_proxy_enabled();
        bool cache_enabled = network_get_disk_cache_enabled();
        bool proxy_available = network_proxy_available();

        C2D_DrawRectSolid(12.0f, 86.0f, 0.2f, 142.0f, 58.0f,
                          proxy_available ? C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f) : C2D_Color32f(0.10f, 0.11f, 0.13f, 1.0f));
        C2D_DrawRectSolid(166.0f, 86.0f, 0.2f, 142.0f, 58.0f, C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));
        C2D_DrawRectSolid(12.0f, 154.0f, 0.2f, 296.0f, 60.0f,
                          clear_pressed ? C2D_Color32f(0.28f, 0.20f, 0.14f, 1.0f) : C2D_Color32f(0.12f, 0.15f, 0.19f, 1.0f));

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

        draw_ui_text(24.0f, 168.0f, 0.44f, 0.44f, C2D_Color32f(0.97f, 0.98f, 1.0f, 1.0f), 0, "Clear SD tile cache");
        snprintf(line, sizeof(line), "Last tile %lu KB   backend %s", (unsigned long)((network_get_last_tile_size_bytes() + 1023) / 1024), network_get_effective_tile_backend_name());
        draw_ui_text(24.0f, 188.0f, 0.32f, 0.32f, C2D_Color32f(0.74f, 0.82f, 0.90f, 1.0f), 0, line);
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
                snprintf(line, sizeof(line), "Fixes from phone: %u", gps_get_server_fix_count());
                draw_ui_text(24.0f, 200.0f, 0.32f, 0.32f, C2D_Color32f(0.80f, 0.88f, 0.92f, 1.0f), 0, line);
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

    draw_ui_text(160.0f, 222.0f, 0.32f, 0.32f, C2D_Color32f(0.66f, 0.76f, 0.86f, 1.0f), C2D_AlignCenter,
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

    if (g_debug_overlay) {
        u32 last_ms = network_get_last_tile_download_ms();
        u32 last_bytes = network_get_last_tile_size_bytes();
        unsigned long kib = (unsigned long)((last_bytes + 1023) / 1024);
        if (network_get_last_tile_was_cache_hit()) {
            snprintf(line, sizeof(line), "Tile %lu KiB  SD cache", kib);
        } else if (last_ms > 0 && last_bytes > 0) {
            unsigned long kib_per_s = (unsigned long)((last_bytes * 1000ULL) / (1024ULL * last_ms));
            snprintf(line, sizeof(line), "Tile %lu KiB  %lu ms  %lu KiB/s", kib, (unsigned long)last_ms, kib_per_s);
        } else {
            snprintf(line, sizeof(line), "Tile debug waiting for download");
        }
    } else if (g_status_message[0] && (now_ms - g_status_message_ms) < 2200) {
        snprintf(line, sizeof(line), "Status: %.32s", g_status_message);
    } else if (gps_get_source_mode() == GPS_SOURCE_HTTPS_SERVER && gps_is_connected()) {
        char svr_url[56];
        gps_get_server_url(svr_url, sizeof(svr_url));
        snprintf(line, sizeof(line), "GPS %s  fixes:%u", svr_url, gps_get_server_fix_count());
    } else if (g_route_active) {
        snprintf(line, sizeof(line), "Route: %.34s", g_route_next_text[0] ? g_route_next_text : "Active");
    } else if (g_last_place_name[0]) {
        snprintf(line, sizeof(line), "Place: %.34s", g_last_place_name);
    } else {
        snprintf(line, sizeof(line), "Lat %.4f  Lon %.4f", map_state.lat, map_state.lon);
    }
    draw_ui_text(12.0f, 62.0f, 0.42f, 0.42f, C2D_Color32f(0.90f, 0.92f, 0.95f, 1.0f), 0, line);

    snprintf(small, sizeof(small), g_debug_overlay ? "Touch here to drag map   Debug ON" : "Touch here to drag map");
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

static bool geocode_pick(const char* query, GeocodeResult* out) {
    if (!query || !query[0] || !out) return false;
    if (!is_connected()) return false;

    char encoded[256];
    if (!url_encode_component(query, encoded, sizeof(encoded))) {
        return false;
    }

    char url[512];
    if (network_get_proxy_enabled()) {
        snprintf(url, sizeof(url), PROXY_BASE_URL "/geocode?q=%s", encoded);
    } else {
        snprintf(url, sizeof(url),
                 "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=5&q=%s",
                 encoded);
    }

    u8* buf = NULL;
    u32 size = 0;
    if (!download_url(url, &buf, &size) || !buf || size == 0) {
        if (buf) free(buf);
        return false;
    }

    u8* tmp = (u8*)realloc(buf, size + 1);
    if (tmp) buf = tmp;
    buf[size] = 0;

    GeocodeResult results[5];
    memset(results, 0, sizeof(results));
    int count = parse_geocode_results_json((const char*)buf, size, results, 5);
    free(buf);

    if (count <= 0) return false;

    int selected = 0;
    if (count > 1) {
        begin_bottom_console_ui();
        while (aptMainLoop()) {
            consoleClear();
            printf("Search results:\n");
            printf("Up/Down = select  A = choose  B = cancel\n\n");

            for (int i = 0; i < count; i++) {
                printf("%c %d) %s\n", (i == selected) ? '>' : ' ', i + 1, results[i].name[0] ? results[i].name : "(result)");
            }

            hidScanInput();
            u32 kDown = hidKeysDown();
            if (kDown & KEY_B) {
                end_bottom_console_ui("Search cancelled");
                return false;
            }
            if (kDown & KEY_START) {
                g_should_exit = true;
                end_bottom_console_ui(NULL);
                return false;
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

            svcSleepThread(60000000LL);
        }
        end_bottom_console_ui(NULL);
    }

    *out = results[selected];
    return true;
}

static bool parse_route_json(const char* json, u32 json_size) {
    if (!json || json_size == 0) return false;
    const char* end = json + json_size;

    if (strstr(json, "\"poly\"")) {
        const char* next_key = strstr(json, "\"next\"");
        const char* p;
        const char* dist_key;
        const char* q;
        const char* poly_key;
        const char* r;
        size_t nlen = 0;

        if (!next_key) return false;
        p = strchr(next_key, ':');
        if (!p) return false;
        p++;
        p = skip_ws(p, end);
        if (p >= end || *p != '"') return false;
        p++;
        while (p < end && *p != '"') {
            if (nlen + 1 < sizeof(g_route_next_text)) g_route_next_text[nlen++] = *p;
            p++;
        }
        g_route_next_text[nlen] = 0;

        dist_key = strstr(json, "\"distance_m\"");
        if (!dist_key) return false;
        q = strchr(dist_key, ':');
        if (!q) return false;
        q++;
        q = skip_ws(q, end);
        g_route_next_dist_m = (int)strtol(q, NULL, 10);
        if (g_route_next_dist_m < 0) g_route_next_dist_m = 0;

        poly_key = strstr(json, "\"poly\"");
        if (!poly_key) return false;
        r = strchr(poly_key, ':');
        if (!r) return false;
        r++;
        r = skip_ws(r, end);
        if (r >= end || *r != '"') return false;
        r++;

        g_route_count = 0;
        while (r < end && *r != '"' && g_route_count < MAX_ROUTE_POINTS) {
            char* lat_end = NULL;
            char* lon_end = NULL;
            double lat = strtod(r, &lat_end);
            double lon;
            if (!lat_end || lat_end == r) break;
            r = lat_end;
            if (*r != ',') break;
            r++;
            lon = strtod(r, &lon_end);
            if (!lon_end || lon_end == r) break;
            r = lon_end;
            g_route[g_route_count].lat = lat;
            g_route[g_route_count].lon = lon;
            g_route_count++;
            if (*r == ';') {
                r++;
                continue;
            }
            if (*r == '"') break;
            if (*r != ';') break;
        }

        return g_route_count >= 2;
    }

    {
        char type[32] = {0};
        char modifier[32] = {0};
        char road_name[64] = {0};
        const char* dist_key = strstr(json, "\"distance\"");
        const char* coords_key = strstr(json, "\"coordinates\"");
        const char* r;

        if (dist_key) {
            const char* q = strchr(dist_key, ':');
            if (q) {
                q++;
                q = skip_ws(q, end);
                g_route_next_dist_m = (int)strtod(q, NULL);
                if (g_route_next_dist_m < 0) g_route_next_dist_m = 0;
            }
        }

        (void)extract_json_string_field(json, end, "\"type\"", type, sizeof(type));
        (void)extract_json_string_field(json, end, "\"modifier\"", modifier, sizeof(modifier));
        (void)extract_json_string_field(json, end, "\"name\"", road_name, sizeof(road_name));

        if (modifier[0]) {
            snprintf(g_route_next_text, sizeof(g_route_next_text), "%s %s", type[0] ? type : "Go", modifier);
        } else if (type[0]) {
            snprintf(g_route_next_text, sizeof(g_route_next_text), "%s", type);
        } else {
            snprintf(g_route_next_text, sizeof(g_route_next_text), "%s", road_name[0] ? road_name : "Route");
        }

        if (!coords_key) return false;
        r = strchr(coords_key, '[');
        if (!r) return false;
        r++;

        g_route_count = 0;
        while (r < end && g_route_count < MAX_ROUTE_POINTS) {
            char* lon_end = NULL;
            char* lat_end = NULL;
            double lon;
            double lat;

            while (r < end && *r != '[' && *r != ']') r++;
            if (r >= end || *r == ']') break;
            r++;

            lon = strtod(r, &lon_end);
            if (!lon_end || lon_end == r) break;
            r = lon_end;
            if (*r != ',') break;
            r++;

            lat = strtod(r, &lat_end);
            if (!lat_end || lat_end == r) break;
            r = lat_end;

            g_route[g_route_count].lat = lat;
            g_route[g_route_count].lon = lon;
            g_route_count++;

            while (r < end && *r != ']') r++;
            if (r < end) r++;
        }

        return g_route_count >= 2;
    }
}

static void open_route_to_address(void) {
    if (!is_connected()) {
        set_status_message("No WiFi for routing");
        return;
    }

    begin_bottom_console_ui();

    char query[128] = {0};
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(query) - 1);
    swkbdSetHintText(&swkbd, "Route to address");
    swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT);
    SwkbdButton btn = swkbdInputText(&swkbd, query, sizeof(query));
    if (btn != SWKBD_BUTTON_RIGHT) {
        end_bottom_console_ui("Route cancelled");
        return;
    }
    if (query[0] == '\0') {
        end_bottom_console_ui("Route cancelled");
        return;
    }

    GeocodeResult dest;
    memset(&dest, 0, sizeof(dest));
    if (!geocode_pick(query, &dest)) {
        consoleClear();
        printf("No destination selected.\n");
        svcSleepThread(800000000LL);
        end_bottom_console_ui("No route destination");
        return;
    }

    double from_lat = map_state.lat;
    double from_lon = map_state.lon;
    if (gps_is_connected() && gps_has_fix()) {
        (void)gps_get_position(&from_lat, &from_lon);
    }

    char url[512];
    if (network_get_proxy_enabled()) {
        snprintf(url, sizeof(url),
                 PROXY_BASE_URL "/route?from=%.6f,%.6f&to=%.6f,%.6f",
                 from_lat, from_lon, dest.lat, dest.lon);
    } else {
        snprintf(url, sizeof(url),
                 "https://router.project-osrm.org/route/v1/driving/%.6f,%.6f;%.6f,%.6f?overview=full&geometries=geojson&steps=true",
                 from_lon, from_lat, dest.lon, dest.lat);
    }

    consoleClear();
    printf("Routing...\n");

    u8* buf = NULL;
    u32 size = 0;
    if (!download_url(url, &buf, &size) || !buf || size == 0) {
        consoleClear();
        printf("Route request failed.\n");
        if (buf) free(buf);
        svcSleepThread(1000000000LL);
        end_bottom_console_ui("Route request failed");
        return;
    }

    u8* tmp = (u8*)realloc(buf, size + 1);
    if (tmp) buf = tmp;
    buf[size] = 0;

    bool ok = parse_route_json((const char*)buf, size);
    free(buf);

    if (!ok) {
        consoleClear();
        printf("Failed to parse route.\n");
        svcSleepThread(1000000000LL);
        end_bottom_console_ui("Route parse failed");
        return;
    }

    g_route_active = true;
    add_recent(dest.name, dest.lat, dest.lon);
    end_bottom_console_ui("Route ready");
}

static void render_route_overlay(double center_lat, double center_lon, int zoom, int screen_width, int screen_height) {
    if (!g_route_active || g_route_count < 2) return;

    int center_px, center_py;
    lat_lon_to_pixel(center_lat, center_lon, zoom, &center_px, &center_py);

    const u32 color = C2D_Color32f(1.0f, 1.0f, 0.0f, 0.8f);
    for (int i = 1; i < g_route_count; i++) {
        int p0x, p0y, p1x, p1y;
        lat_lon_to_pixel(g_route[i - 1].lat, g_route[i - 1].lon, zoom, &p0x, &p0y);
        lat_lon_to_pixel(g_route[i].lat, g_route[i].lon, zoom, &p1x, &p1y);

        float x0 = (float)(p0x - center_px + (screen_width / 2));
        float y0 = (float)(p0y - center_py + (screen_height / 2));
        float x1 = (float)(p1x - center_px + (screen_width / 2));
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
        printf("Last tile: %lu ms%s\n",
               (unsigned long)network_get_last_tile_download_ms(),
               network_get_last_tile_was_cache_hit() ? " (SD)" : "");
        printf("Last tile size: %lu KB\n", (unsigned long)((network_get_last_tile_size_bytes() + 1023) / 1024));
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

static const char* skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static bool extract_json_string_field(const char* start, const char* end, const char* key, char* out, size_t out_size) {
    const char* field;
    const char* p;
    size_t len = 0;

    if (!start || !end || !key || !out || out_size == 0) return false;

    field = strstr(start, key);
    if (!field || field >= end) return false;
    p = strchr(field, ':');
    if (!p || p >= end) return false;
    p++;
    p = skip_ws(p, end);
    if (p >= end || *p != '"') return false;
    p++;

    while (p < end && *p != '"') {
        if (*p == '\\' && (p + 1) < end) {
            p++;
        }
        if (len + 1 < out_size) {
            out[len++] = *p;
        }
        p++;
    }

    out[len] = 0;
    return len > 0;
}

static int parse_geocode_results_json(const char* json, u32 json_size, GeocodeResult* out_results, int max_results) {
    if (!json || json_size == 0 || !out_results || max_results <= 0) return 0;

    const char* end = json + json_size;
    const char* p = json;

    const char* results_key = strstr(p, "\"results\"");
    if (results_key) {
        p = results_key;
    }

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
        if (!name_key) name_key = strstr(lon_key, "\"display_name\"");
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

        if (!extract_json_string_field(name_key, end,
                                       (strstr(name_key, "\"display_name\"") == name_key) ? "\"display_name\"" : "\"name\"",
                                       out_results[count].name,
                                       sizeof(out_results[count].name))) {
            break;
        }

        out_results[count].lat = lat;
        out_results[count].lon = lon;
        count++;

        p = q;
        if (p < end) p++;
    }

    return count;
}

static bool parse_reverse_geocode_json(const char* json, u32 json_size, GeocodeResult* out_result) {
    if (!json || json_size == 0 || !out_result) return false;

    const char* end = json + json_size;
    const char* name_key = strstr(json, "\"name\"");
    if (!name_key) name_key = strstr(json, "\"display_name\"");
    const char* lat_key = strstr(json, "\"lat\"");
    const char* lon_key = strstr(json, "\"lon\"");
    if (!name_key || !lat_key || !lon_key) return false;

    if (!extract_json_string_field(json, end,
                                   strstr(json, "\"display_name\"") ? "\"display_name\"" : "\"name\"",
                                   out_result->name,
                                   sizeof(out_result->name))) {
        return false;
    }

    const char* p = strchr(lat_key, ':');
    if (!p) return false;
    p++;
    p = skip_ws(p, end);
    out_result->lat = strtod(p, NULL);

    p = strchr(lon_key, ':');
    if (!p) return false;
    p++;
    p = skip_ws(p, end);
    out_result->lon = strtod(p, NULL);

    return out_result->name[0] != '\0';
}

static void open_reverse_geocode_center(void) {
    if (!is_connected()) {
        set_status_message("No WiFi for identify");
        return;
    }

    begin_bottom_console_ui();

    char url[512];
    if (network_get_proxy_enabled()) {
        snprintf(url, sizeof(url), PROXY_BASE_URL "/reverse?lat=%.6f&lon=%.6f", map_state.lat, map_state.lon);
    } else {
        snprintf(url, sizeof(url),
                 "https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=18&lat=%.6f&lon=%.6f",
                 map_state.lat, map_state.lon);
    }

    consoleClear();
    printf("Identifying map center...\n");
    printf("%.6f, %.6f\n", map_state.lat, map_state.lon);

    u8* buf = NULL;
    u32 size = 0;
    if (!download_url(url, &buf, &size) || !buf || size == 0) {
        if (buf) free(buf);
        consoleClear();
        printf("Reverse geocode failed.\n");
        printf("Press B to return\n");
        while (aptMainLoop() && !g_should_exit) {
            hidScanInput();
            u32 kd = hidKeysDown();
            if (kd & KEY_START) {
                g_should_exit = true;
                end_bottom_console_ui(NULL);
                return;
            }
            if (kd & KEY_B) {
                end_bottom_console_ui("Identify failed");
                return;
            }
            svcSleepThread(60000000LL);
        }
        end_bottom_console_ui(NULL);
        return;
    }

    u8* tmp = (u8*)realloc(buf, size + 1);
    if (tmp) buf = tmp;
    buf[size] = 0;

    GeocodeResult result;
    memset(&result, 0, sizeof(result));
    bool ok = parse_reverse_geocode_json((const char*)buf, size, &result);
    free(buf);

    consoleClear();
    if (!ok) {
        printf("No place found for center.\n\n");
        printf("Press B to return\n");
    } else {
        add_recent(result.name, result.lat, result.lon);
        set_last_place_name(result.name);
        printf("Center place:\n\n");
        printf("%s\n\n", result.name);
        printf("%.6f, %.6f\n\n", result.lat, result.lon);
        printf("Saved to recents.\n");
        printf("Press B to return\n");
    }

    while (aptMainLoop() && !g_should_exit) {
        hidScanInput();
        u32 kd = hidKeysDown();
        if (kd & KEY_START) {
            g_should_exit = true;
            end_bottom_console_ui(NULL);
            return;
        }
        if (kd & KEY_B) {
            end_bottom_console_ui(ok ? "Identify complete" : "Identify done");
            return;
        }
        svcSleepThread(60000000LL);
    }
    end_bottom_console_ui(NULL);
}

static void open_address_search(void) {
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
        set_status_message("Empty search");
        return;
    }

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

    char url[512];
    if (network_get_proxy_enabled()) {
        snprintf(url, sizeof(url), PROXY_BASE_URL "/geocode?q=%s", encoded);
    } else {
        snprintf(url, sizeof(url),
                 "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=5&q=%s",
                 encoded);
    }

    u8* buf = NULL;
    u32 size = 0;
    if (!download_url(url, &buf, &size) || !buf || size == 0) {
        set_status_message("Geocode request failed");
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
        set_status_message("No search results");
        return;
    }

    int selected = 0;
    if (count > 1) {
        // Simple picker on the bottom screen.
        begin_bottom_console_ui();
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
                end_bottom_console_ui("Search cancelled");
                return;
            }
            if (kDown & KEY_START) {
                g_should_exit = true;
                end_bottom_console_ui(NULL);
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
        end_bottom_console_ui(NULL);
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
