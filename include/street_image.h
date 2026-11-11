#ifndef STREET_IMAGE_H
#define STREET_IMAGE_H
#include <stdint.h>
/* Insert one decoded 512x512 JPEG tile into a complete 1024x512 RGB565
 * Morton-tiled image. Tile zero is the left hemisphere, tile one the right.
 * The vertical flip matches the original working panorama decoder. */
void street_google_write_half_rgb565(const unsigned char *rgb, unsigned half,
                                      uint16_t *target);
unsigned street_image_tiled_index(unsigned x, unsigned y);
#endif
