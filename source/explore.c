#include "explore.h"
#include "terrain_math.h"
#include "explore_shbin.h"
#include "earth_rgb_bin.h"
/* The Canyon demo assets are no longer silently substituted for live terrain. */
#include <math.h>
#include <string.h>
#include <zlib.h>

/* Legacy symbol retained for callers; the live terrain origin is now the map
 * position, not an unconditional Arizona fallback. */ 
const ExplorePlace explore_terrain_place = {"Selected map location", 0, 0};

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
    if (!asset.data) {
        /* Street View is blank until its actual photo; live terrain uses a
         * neutral shaded placeholder while waiting for elevation data. */
        if(!wrap) {
            u16 *pix=(u16*)tex->data;
            for(unsigned i=0;i<length/2;i++)pix[i]=0x5B68;
        } else memset(tex->data, 0, length);
    } else if (uncompress(tex->data, &length, asset.data, asset.size) != Z_OK ||
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
        /* Flat loading surface until the selected location's elevation arrives. */
        return (Vertex){(u-.5f)*2.0f, 0, (v-.5f)*2.0f, u, v};
    }
    float lon = (u * 2 - 1) * (float)M_PI;
    float lat = (.5f - v) * (float)M_PI;
    float radius = cosf(lat);
    float z = cosf(lon) * radius;
    return (Vertex){sinf(lon) * radius, sinf(lat), mode == EXPLORE_STREET ? -z : z,
                    u, v};
}

/* On flat imagery, draw a fixed photo plane rather than falsely stretching a
 * normal forward-facing photograph across the inside of a 360-degree sphere.
 * The street mesh buffers already have room for both configurations. */
static bool street_flat;

static void rebuild_street_mesh(bool flat) {
    if (!vertices || !indices || loaded != EXPLORE_STREET) return;
    street_flat = flat;
    if (flat) {
        vertices[0] = (Vertex){-1.65f,  .95f, -2, 0, 0};
        vertices[1] = (Vertex){ 1.65f,  .95f, -2, 1, 0};
        vertices[2] = (Vertex){-1.65f, -.95f, -2, 0, 1};
        vertices[3] = (Vertex){ 1.65f, -.95f, -2, 1, 1};
        indices[0]=0; indices[1]=2; indices[2]=1;
        indices[3]=1; indices[4]=2; indices[5]=3;
        index_count = 6;
    } else {
        const unsigned rows = 32;
        for (unsigned y=0;y<=rows;y++)
            for (unsigned x=0;x<=64;x++)
                vertices[y*65+x] = make_vertex(EXPLORE_STREET,x,y,rows);
        unsigned n=0;
        for (unsigned y=0;y<rows;y++)
            for (unsigned x=0;x<64;x++) {
                u16 p=y*65+x,q=p+1,z=p+65,k=z+1;
                indices[n++]=p; indices[n++]=z; indices[n++]=q;
                indices[n++]=q; indices[n++]=z; indices[n++]=k;
            }
        index_count = n;
    }
    GSPGPU_FlushDataCache(vertices, (flat ? 4 : 65*33)*sizeof(*vertices));
    GSPGPU_FlushDataCache(indices, index_count*sizeof(*indices));
}

bool explore_load(ExploreMode mode, unsigned street_stop) {
    Asset asset;
    switch (mode) {
    case EXPLORE_GLOBE: asset = (Asset){earth_rgb_bin, earth_rgb_bin_size, 1024, 512}; break;
    case EXPLORE_TERRAIN: asset = (Asset){NULL, 0, 256, 256}; break;
    case EXPLORE_STREET:
        (void)street_stop;
        asset = (Asset){NULL, 0, 8, 8};
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
        if (mode == EXPLORE_STREET && street_flat) rebuild_street_mesh(false);
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
    street_flat = false;
    return true;
}


/* Swap the panorama only after the new 1024x512 Morton RGB565 image is complete.
 * The old scene remains visible if allocation fails. Call on the render thread
 * outside C3D_FrameBegin/C3D_FrameEnd. */
bool explore_load_street_pixels(const void *pixels, bool is_pano) {
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
    if (street_flat == is_pano) rebuild_street_mesh(!is_pano);
    return true;
}

/* Heightmap application is render-thread only. The download/decode worker
 * never touches GPU allocations or the live scene. This function updates
 * the existing 65x65 mesh and a bounded 256x256 color texture in place. */
static unsigned terrain_swizzle(unsigned x,unsigned y) {
    unsigned m=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|
               ((x&4)<<2)|((y&4)<<3);
    return ((y>>3)*32+(x>>3))*64+m;
}
static float terrain_height_at(const TerrainGrid *g,float x,float y){
    x=fmaxf(0.f,fminf(32.f,x));y=fmaxf(0.f,fminf(32.f,y));
    int ix=(int)x,iy=(int)y;
    int nx=ix<32?ix+1:ix,ny=iy<32?iy+1:iy;
    float fx=x-ix,fy=y-iy;
    float h00=g->height[iy*33+ix],h10=g->height[iy*33+nx];
    float h01=g->height[ny*33+ix],h11=g->height[ny*33+nx];
    return (h00+(h10-h00)*fx)*(1.f-fy)+
           (h01+(h11-h01)*fx)*fy;
}
bool explore_apply_terrain(const TerrainGrid *grid,float span_m,float exaggeration) {
    if(loaded!=EXPLORE_TERRAIN||!vertices||!grid||!texture.data ||
       !isfinite(span_m)||span_m<100||!isfinite(exaggeration)||
       exaggeration<0||exaggeration>8)return false;
    C3D_FrameSync();
    float middle=(grid->min_height+grid->max_height)*.5f;
    for(int y=0;y<=64;y++)for(int x=0;x<=64;x++){
        unsigned idx=(unsigned)y*65+(unsigned)x;
        float h=terrain_height_at(grid,x*.5f,y*.5f);
        vertices[idx].y=(h-middle)*(2.f*exaggeration/span_m);
    }
    /* Compact elevation-derived texture: no unrelated Canyon imagery.
     * The colors are illustrative height shading, not satellite imagery. */
    u16 *pixels=(u16*)texture.data;
    for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){
        float h=terrain_height_at(grid,x*(32.f/255),y*(32.f/255));
        float dx=terrain_height_at(grid,fminf(32.f,x*(32.f/255)+.25f),
                                 y*(32.f/255))-
                 terrain_height_at(grid,fmaxf(0.f,x*(32.f/255)-.25f),
                                 y*(32.f/255));
        float dy=terrain_height_at(grid,x*(32.f/255),
                                 fminf(32.f,y*(32.f/255)+.25f))-
                 terrain_height_at(grid,x*(32.f/255),
                                 fmaxf(0.f,y*(32.f/255)-.25f));
        float shade=fmaxf(.57f,fminf(1.12f,.92f-(dx+dy)*.009f));
        int r=66,g=124,b=64;
        if(h<0){r=45;g=88;b=135;}
        else if(h>3200){r=222;g=226;b=225;}
        else if(h>1800){r=138;g=135;b=121;}
        else if(h>700){r=153;g=135;b=96;}
        r=(int)fminf(255,r*shade);
        g=(int)fminf(255,g*shade);
        b=(int)fminf(255,b*shade);
        pixels[terrain_swizzle(x,y)]=(u16)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
    }
    GSPGPU_FlushDataCache(vertices,65*65*sizeof(*vertices));
    C3D_TexFlush(&texture);
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
    if (loaded == EXPLORE_STREET) {
        C3D_FVec look = street_flat ? FVec3_New(0,0,-1) :
            FVec3_New(dir.x, dir.y, -dir.z);
        Mtx_LookAt(&view, origin, look, up, false);
    } else
        Mtx_LookAt(&view, FVec3_New(dir.x * distance, dir.y * distance, dir.z * distance),
                   origin, up, false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_uniform, &projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, view_uniform, &view);
    C3D_DrawElements(GPU_TRIANGLES, index_count, C3D_UNSIGNED_SHORT, indices);
}
