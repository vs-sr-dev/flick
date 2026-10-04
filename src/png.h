#ifndef HS_PNG_H
#define HS_PNG_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* pixels are 0x00RRGGBB, stride in pixels */
int png_write(const char *path, const uint32_t *pixels, int stride, int w, int h);

#endif
