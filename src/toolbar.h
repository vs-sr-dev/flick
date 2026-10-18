/* Flick - toolbar along the top of the window (640x480 ARGB overlay) */
#ifndef HS_TOOLBAR_H
#define HS_TOOLBAR_H

#include <stdint.h>

enum { TB_NONE, TB_DISC, TB_CARDS, TB_CARD_OFF, TB_PAUSE, TB_RESET, TB_TIMING,
       TB_SHOT, TB_SCALE, TB_FULLSCREEN, TB_MUTE, TB_VOLUME, TB_CONTROLS };

/* what the buttons show */
typedef struct {
	int paused, fast, fullscreen, scale, volume, muted, card_on;
} tb_state;

/* pointer moved, in overlay coordinates; while the volume is dragged, returns TB_VOLUME
 * with the new level (0-100) in *value */
int tb_mouse(int x, int y, double now, int *value);
void tb_show(double now);            /* show it as if the pointer had moved (startup) */
void tb_leave(void);                 /* pointer left the window */
/* left button down: the command under the pointer (TB_NONE if not on a button);
 * TB_VOLUME sets *value */
int tb_press(int x, int y, double now, int *value);
void tb_release(void);
/* wheel over the volume: TB_VOLUME with *value, else TB_NONE */
int tb_wheel(int x, int y, int dy, int volume, int *value);
int tb_visible(const tb_state *st, double now);
/* draw over the overlay; returns 1 if anything was drawn */
int tb_render(uint32_t *ov, const tb_state *st, double now);

#endif
