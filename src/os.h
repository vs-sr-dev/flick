/* Flick - the few host services the frontend needs (kept apart from windows.h) */
#ifndef HS_OS_H
#define HS_OS_H

#include <stddef.h>

struct SDL_Window;

/* folder of the executable, without a trailing separator ("." if unknown) */
void os_exe_dir(char *out, size_t n);
/* create a folder (no error if it exists) */
void os_mkdir(const char *path);
/* native "open a disc" dialog: 1 and the path in out, 0 if cancelled or unavailable */
int os_pick_disc(struct SDL_Window *win, char *out, size_t n);

#endif
