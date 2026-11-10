#include "explore.h"
#include "explore_shbin.h"
#include "earth_rgb_bin.h"
#include "canyon_rgb_bin.h"
#include "canyon_dem_bin.h"
#include "norfolk_rgb_bin.h"
#include "georgetown_rgb_bin.h"
#include <math.h>
#include <string.h>
#include <zlib.h>

const ExplorePlace explore_street_places[2] = {
    {"Norfolk Ave & Woodmont Ave", 38.987977, -77.095532},
    {"Old Georgetown Rd & Woodmont Ave", 38.986277, -77.096902}
};
const ExplorePlace explore_terrain_place = {"Grand Canyon, Arizona", 36.09, -112.13};

typedef struct { float x, y, z, u, v; } Vertex;
typedef struct { const u8 *data; u32 size; unsigned width, height; } Asset;
static DVLB_s *shader;
static shaderProgram_s program;
static int projection_uniform, view_uniform;
static Vertex *vertices;
static u16 *indices;
static int index_count;
static C3D_Tex texture;
static ExploreMode loaded;

static bool load_texture(C3D_Tex *tex, Asset asset, bool wrap) {
    if (!C3D_TexInit(tex, asset.width, asset.height, GPU_RGB565)) return false;
    unsigned long length = asset.width * asset.height * 2;
    if (uncompress(tex->data, &length, asset.data, asset.size) != Z_OK ||
        length != asset.width * asset.height * 2) {
        C3D_TexDelete(tex);
        return false;
    }
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(tex, wrap ? GPU_REPEAT : GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return true;
}

static Vertex make_vertex(ExploreMode mode, unsigned x, unsigned y, unsigned rows) {
    float u = x / 64.0f, v = y / (float)rows;
    if (mode == EXPLORE_TERRAIN) {
        unsigned sample = (y * 65 + x) * 2;
        unsigned metres = canyon_dem_bin[sample] | (canyon_dem_bin[sample + 1] << 8);
        /* Approximately 32 km across. Elevation is exaggerated 2.2x. */
        return (Vertex){(u - .5f) * 2.0f, (metres - 1700.0f) * .0001375f,
                        (v - .5f) * 1.81f, u, v};
    }
    float lon = (u * 2 - 1) * (float)M_PI;
    float lat = (.5f - v) * (float)M_PI;
    float radius = cosf(lat);
    float z = cosf(lon) * radius;
    return (Vertex){sinf(lon) * radius, sinf(lat), mode == EXPLORE_STREET ? -z : z,
                    u, v};
}

bool explore_load(ExploreMode mode, unsigned street_stop) {
    Asset asset;
    switch (mode) {
    case EXPLORE_GLOBE: asset = (Asset){earth_rgb_bin, earth_rgb_bin_size, 1024, 512}; break;
    case EXPLORE_TERRAIN: asset = (Asset){canyon_rgb_bin, canyon_rgb_bin_size, 256, 256}; break;
    case EXPLORE_STREET:
        if (street_stop >= 2) return false;
        asset = street_stop ? (Asset){georgetown_rgb_bin, georgetown_rgb_bin_size, 1024, 512} :
                              (Asset){norfolk_rgb_bin, norfolk_rgb_bin_size, 1024, 512};
        break;
    default: return false;
    }
    C3D_FrameSync();
    C3D_Tex next;
    if (!load_texture(&next, asset, mode != EXPLORE_TERRAIN)) return false;
    /* Adjacent street stops share geometry and shader. */
    if (loaded == mode) {
        C3D_TexDelete(&texture);
        texture = next;
        return true;
    }
    unsigned rows = mode == EXPLORE_TERRAIN ? 64 : 32;
    unsigned vertex_bytes = 65 * (rows + 1) * sizeof(Vertex);
    unsigned count = 64 * rows * 6;
    Vertex *mesh = linearAlloc(vertex_bytes);
    u16 *order = linearAlloc(count * sizeof(*order));
    DVLB_s *binary = DVLB_ParseFile((u32 *)explore_shbin, explore_shbin_size);
    if (!mesh || !order || !binary) {
        if (mesh) linearFree(mesh);
        if (order) linearFree(order);
        if (binary) DVLB_Free(binary);
        C3D_TexDelete(&next);
        return false;
    }
    for (unsigned y = 0; y <= rows; y++)
        for (unsigned x = 0; x <= 64; x++)
            mesh[y * 65 + x] = make_vertex(mode, x, y, rows);
    unsigned n = 0;
    for (unsigned y = 0; y < rows; y++) {
        for (unsigned x = 0; x < 64; x++) {
            u16 a = y * 65 + x, b = a + 1, c = a + 65, d = c + 1;
            order[n++] = a; order[n++] = c; order[n++] = b;
            order[n++] = b; order[n++] = c; order[n++] = d;
        }
    }
    GSPGPU_FlushDataCache(mesh, vertex_bytes);
    GSPGPU_FlushDataCache(order, count * sizeof(*order));
    explore_unload();
    shader = binary;
    shaderProgramInit(&program);
    shaderProgramSetVsh(&program, &shader->DVLE[0]);
    projection_uniform = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
    view_uniform = shaderInstanceGetUniformLocation(program.vertexShader, "view");
    texture = next;
    vertices = mesh;
    indices = order;
    index_count = count;
    loaded = mode;
    return true;
}


/* Swap the panorama only after the new 1024x512 Morton RGB565 image is complete.
 * The old scene remains visible if allocation fails. Call on the render thread
 * outside C3D_FrameBegin/C3D_FrameEnd. */
bool explore_load_street_pixels(const void *pixels) {
    if (!pixels || loaded != EXPLORE_STREET) return false;
    C3D_FrameSync();
    C3D_Tex next;
    if (!C3D_TexInit(&next, 1024, 512, GPU_RGB565)) return false;
    C3D_TexUpload(&next, pixels);
    C3D_TexFlush(&next); /* GPU must see the just-uploaded image */
    C3D_TexSetFilter(&next, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&next, GPU_REPEAT, GPU_CLAMP_TO_EDGE);
    C3D_FrameSync();
    C3D_TexDelete(&texture);
    texture = next;
    return true;
}

void explore_unload(void) {
    if (!loaded) return;
    C3D_FrameSync();
    C3D_TexDelete(&texture);
    linearFree(vertices);
    linearFree(indices);
    shaderProgramFree(&program);
    DVLB_Free(shader);
    loaded = EXPLORE_NONE;
}

void explore_draw(C3D_RenderTarget *target, float yaw, float pitch,
                  float distance, float fov, float eye) {
    C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0x081321FF, 0);
    if (!C3D_FrameDrawOn(target)) return;
    C3D_BindProgram(&program);
    C3D_AttrInfo *attrs = C3D_GetAttrInfo();
    AttrInfo_Init(attrs);
    AttrInfo_AddLoader(attrs, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrs, 1, GPU_FLOAT, 2);
    C3D_BufInfo *buffers = C3D_GetBufInfo();
    BufInfo_Init(buffers);
    BufInfo_Add(buffers, vertices, sizeof(*vertices), 2, 0x10);
    for (unsigned i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnvSrc(C3D_GetTexEnv(0), C3D_Both, GPU_TEXTURE0, 0, 0);
    C3D_TexBind(0, &texture);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);

    C3D_Mtx projection, view;
    float convergence = loaded == EXPLORE_GLOBE ? distance - .4f : distance;
    if (eye)
        Mtx_PerspStereoTilt(&projection, fov, C3D_AspectRatioTop, .025f, 16.0f,
                           eye, convergence, false);
    else Mtx_PerspTilt(&projection, fov, C3D_AspectRatioTop, .025f, 16.0f, false);
    C3D_FVec dir = FVec3_New(sinf(yaw) * cosf(pitch), sinf(pitch),
                             cosf(yaw) * cosf(pitch));
    C3D_FVec origin = FVec3_New(0, 0, 0);
    C3D_FVec up = FVec3_New(0, 1, 0);
    if (loaded == EXPLORE_STREET)
        Mtx_LookAt(&view, origin, FVec3_New(dir.x, dir.y, -dir.z), up, false);
    else
        Mtx_LookAt(&view, FVec3_New(dir.x * distance, dir.y * distance, dir.z * distance),
                   origin, up, false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, view_uniform, &view);
    C3D_DrawElements(GPU_TRIANGLES, index_count, C3D_UNSIGNED_SHORT, indices);
}
