// stb_image.h - v2.28 - public domain image loader - https://github.com/nothings/stb
// Only PNG decode enabled for 3DS homebrew
#ifndef STB_IMAGE_H
#define STB_IMAGE_H

// Function declarations for stb_image
unsigned char *stbi_load_from_memory(const unsigned char *buffer, int len, int *x, int *y, int *channels_in_file, int desired_channels);
void stbi_image_free(void *retval_from_stbi_load);
const char *stbi_failure_reason(void);

#endif // STB_IMAGE_H
