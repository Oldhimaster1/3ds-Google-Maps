#ifndef SIMPLE_PNG_H
#define SIMPLE_PNG_H

#include <3ds.h>

// Simple PNG decoder for 3DS
// Returns decoded RGBA pixel data, or NULL on failure
unsigned char* decode_png(const unsigned char* png_data, int png_size, int* width, int* height);

// Free decoded image data
void free_png_data(unsigned char* data);

#endif // SIMPLE_PNG_H
