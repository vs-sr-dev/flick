/* Flick - controls: the keyboard and controller bindings of the HyperScan buttons, and the
 * panel that changes them (drawn into the 640x480 ARGB overlay, in the card panel's style).
 * SDL frontend only. Bindings live in g_config (flick.ini), defaults left out. */
#ifndef HS_CONTROLS_H
#define HS_CONTROLS_H

#include <stdint.h>
#include <SDL.h>

void ctl_init(void);                 /* bindings from g_config */

/* the button (BTN_*) a key plays, -1 if none */
int ctl_key_button(SDL_Scancode sc);
/* the HyperScan buttons held on a controller, as 1 << BTN_* */
uint32_t ctl_pad_buttons(SDL_GameController *c);

int ctl_open(void);
void ctl_toggle(void);
/* with the panel open: a key, a controller button or trigger, a click (overlay
 * coordinates); keys and clicks return 1 when the panel has just closed */
int ctl_key(const SDL_KeyboardEvent *k);
int ctl_pad_button(int button);
void ctl_pad_axis(int axis, int value);
int ctl_click(int x, int y);
/* draw into ov (already cleared); returns 1 if open */
int ctl_render(uint32_t *ov, double now);

#endif
