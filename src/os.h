/* Flick - the few host services the frontend needs (kept apart from windows.h) */
#ifndef HS_OS_H
#define HS_OS_H

#include <stddef.h>
#include <stdio.h>

struct SDL_Window;

/* Paths are UTF-8 everywhere in Flick (SDL hands dropped files over in UTF-8); on Windows
 * these go through the wide-character API so that any file name works. */

/* replace argv with UTF-8 copies of the command line's arguments (Windows; else no-op) */
void os_utf8_args(int *argc, char ***argv);
/* fopen for a UTF-8 path */
FILE *os_fopen(const char *path, const char *mode);

/* folder of the executable, without a trailing separator ("." if unknown) */
void os_exe_dir(char *out, size_t n);
/* the GUI build started from a terminal: print to its console (output redirected to a
 * file or a pipe stays as it is); 0 if stderr goes nowhere (started by a double click) */
int os_console_attach(void);
/* create a folder (no error if it exists) */
void os_mkdir(const char *path);
enum { OS_PICK_DISC, OS_PICK_BIOS };
/* native "open a file" dialog for a disc or the BIOS, starting in folder dir (NULL or "":
 * the system's choice); 1 and the path in out, 0 if cancelled or unavailable */
int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n);

#endif
