/* Flick - the few host services the frontend needs (kept apart from windows.h) */
#ifndef HS_OS_H
#define HS_OS_H

#include <stddef.h>

struct SDL_Window;

/* folder of the executable, without a trailing separator ("." if unknown) */
void os_exe_dir(char *out, size_t n);
/* create a folder (no error if it exists) */
void os_mkdir(const char *path);
enum { OS_PICK_DISC, OS_PICK_BIOS };
/* native "open a file" dialog for a disc or the BIOS, starting in folder dir (NULL or "":
 * the system's choice); 1 and the path in out, 0 if cancelled or unavailable */
int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n);

#endif
