#ifndef TERRAIN_MOCK_IMAGE_H
#define TERRAIN_MOCK_IMAGE_H
unsigned char *stbi_load_from_memory(const unsigned char *data,int len,
                                     int *width,int *height,int *components,int desired);
void stbi_image_free(void *data);
#endif

int stbi_info_from_memory(const unsigned char *,int,int *,int *,int *);
