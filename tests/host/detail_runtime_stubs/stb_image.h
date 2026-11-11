#ifndef DETAIL_HOST_STBI_H
#define DETAIL_HOST_STBI_H
int stbi_info_from_memory(const unsigned char *data,int length,int *w,int *h,int *channels);
unsigned char *stbi_load_from_memory(const unsigned char *data,int length,
                                    int *w,int *h,int *channels,int requested);
void stbi_image_free(void *p);
#endif
