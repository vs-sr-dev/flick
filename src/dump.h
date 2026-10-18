/* Flick - debug dumps: a frame's video state and RAM, to re-render it offline */
#ifndef HS_DUMP_H
#define HS_DUMP_H

#include <stddef.h>

/* write dir/ram.bin, sram.bin, video.bin, frame.png, info.txt; 1 on success */
int debug_dump(const char *dir);
/* load a dump and render its frame again into g_frame; 1 on success */
int debug_render(const char *dir);

#endif
