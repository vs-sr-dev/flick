/* Flick - settings kept in flick.ini next to the executable
 *
 * One "key = value" per line, '#' or ';' starts a comment. The window frontend reads it at
 * start and writes it back on exit; only what the user chose in the window (or a BIOS
 * picked from the dialog) goes in, so command-line options never end up saved. */
#ifndef HS_CONFIG_H
#define HS_CONFIG_H

typedef struct
{
	char bios[1024];          /* HyperScan flash image, "" = ask */
	char disc_dir[1024];      /* folder of the last disc opened, for the dialog */
	int volume;               /* 0..100 */
	int scale;                /* window size, multiple of 640x480 */
	int fast;                 /* timing preset: 1 fast, 0 mame */
} config;

extern config g_config;

/* load flick.ini (defaults for what is missing); 0 if there is no file */
int config_load(void);
/* write flick.ini back; 0 if it cannot be written */
int config_save(void);

#endif
