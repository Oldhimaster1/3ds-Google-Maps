#include "simple_png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

// Custom read function for libpng memory buffer
static void png_read_from_memory(png_structp png_ptr, png_bytep data, png_size_t length) {
    typedef struct {
        const unsigned char* data;
        int size;
        int position;
    } png_memory_buffer;
    
    png_memory_buffer* buf = (png_memory_buffer*)png_get_io_ptr(png_ptr);
    if (buf->position + (int)length > buf->size) {
        png_error(png_ptr, "Read past end of buffer");
    }
    memcpy(data, buf->data + buf->position, length);
    buf->position += (int)length;
}

// libpng-based PNG decoder
unsigned char* decode_png(const unsigned char* png_data, int png_size, int* width, int* height) {
    if (!png_data || png_size < 8) {
        printf("PNG: Invalid input data\n");
        return NULL;
    }
    
    // Check PNG signature
    if (png_sig_cmp((png_const_bytep)png_data, 0, 8) != 0) {
        printf("PNG: Not a valid PNG file\n");
        return NULL;
    }
    
    // Create read structure
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        printf("PNG: Failed to create read struct\n");
        return NULL;
    }
    
    // Create info structure
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        printf("PNG: Failed to create info struct\n");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return NULL;
    }
    
    // Set up error handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        printf("PNG: Error during PNG processing\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }
    
    // Set up custom read function for memory buffer
    typedef struct {
        const unsigned char* data;
        int size;
        int position;
    } png_memory_buffer;
    
    png_memory_buffer buffer = {png_data, png_size, 0};
    
    // Custom read function (defined as static function)
    png_set_read_fn(png_ptr, &buffer, png_read_from_memory);
    
    // Read PNG info
    png_read_info(png_ptr, info_ptr);
    
    *width = png_get_image_width(png_ptr, info_ptr);
    *height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    
    printf("PNG: %dx%d, color_type=%d, bit_depth=%d\n", *width, *height, color_type, bit_depth);
    
    // Transform PNG to RGB format (no alpha) - direct output
    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }
    
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }
    
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }
    
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
        png_set_strip_alpha(png_ptr);  // Strip the alpha after transparency conversion
    }
    
    // Strip alpha channel if present - force RGB output
    if (color_type == PNG_COLOR_TYPE_RGBA || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_strip_alpha(png_ptr);
    }
    
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    // Expand to RGBA (4 bytes per pixel) for C3D compatibility
    if (color_type != PNG_COLOR_TYPE_RGBA && color_type != PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_add_alpha(png_ptr, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png_ptr, info_ptr);
    
    // Allocate pixel buffer for RGBA (4 bytes per pixel)
    unsigned char* pixels = malloc(*width * *height * 4);
    if (!pixels) {
        printf("PNG: Failed to allocate pixel buffer\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }
    
    // Allocate row pointers
    png_bytep* row_pointers = malloc(*height * sizeof(png_bytep));
    if (!row_pointers) {
        printf("PNG: Failed to allocate row pointers\n");
        free(pixels);
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }
    
    for (int y = 0; y < *height; y++) {
        row_pointers[y] = pixels + y * *width * 4;  // 4 bytes per pixel for RGBA
    }
    
    // Read the image
    png_read_image(png_ptr, row_pointers);
    png_read_end(png_ptr, NULL);
    
    // Cleanup row pointers and libpng
    free(row_pointers);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    
    // Allocate buffer for tiled output
    unsigned char* tiled = malloc(*width * *height * 4);
    if (!tiled) {
        printf("PNG: Failed to allocate tiled buffer\n");
        free(pixels);
        return NULL;
    }
    
    int w = *width;
    int h = *height;
    
    // 3DS texture tiling: 8x8 pixel tiles in Morton/Z-order
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            // Source pixel - no transformation
            int src_idx = (y * w + x) * 4;
            
            // Calculate tiled destination
            // Tiles are 8x8, arranged in Morton order within each tile
            int tile_x = x >> 3;  // x / 8
            int tile_y = y >> 3;  // y / 8
            int in_tile_x = x & 7;  // x % 8
            int in_tile_y = y & 7;  // y % 8
            
            // Morton index within 8x8 tile
            int morton = 
                ((in_tile_x & 1) << 0) | ((in_tile_y & 1) << 1) |
                ((in_tile_x & 2) << 1) | ((in_tile_y & 2) << 2) |
                ((in_tile_x & 4) << 2) | ((in_tile_y & 4) << 3);
            
            // Tile index in row-major order
            int tiles_per_row = w >> 3;
            int tile_offset = (tile_y * tiles_per_row + tile_x) * 64;
            int dst_idx = (tile_offset + morton) * 4;
            
            // Copy as RGBA (GPU_RGBA8 expects RGBA byte order)
            tiled[dst_idx + 0] = pixels[src_idx + 0];  // R
            tiled[dst_idx + 1] = pixels[src_idx + 1];  // G
            tiled[dst_idx + 2] = pixels[src_idx + 2];  // B
            tiled[dst_idx + 3] = pixels[src_idx + 3];  // A
        }
    }
    
    free(pixels);
    
    printf("PNG: Successfully decoded and tiled %dx%d image\n", *width, *height);
    return tiled;
}

void free_png_data(unsigned char* data) {
    if (data) {
        free(data);
    }
}
