/* Flick - on-screen card panel and messages (640x480 ARGB overlay) */
#ifndef HS_UI_H
#define HS_UI_H

#include <stdint.h>

enum { UI_UP, UI_DOWN, UI_PGUP, UI_PGDN, UI_HOME, UI_END, UI_ENTER, UI_BACKSPACE,
       UI_CLOSE, UI_TAB, UI_REMOVE };

void ui_init(int game);              /* game: CARD_* filter for the panel */
int ui_panel_open(void);
void ui_panel_toggle(void);
void ui_key(int key);
void ui_text(const char *s);         /* typed characters (search) */
void ui_toast(const char *msg);
/* draw into ov (640x480); returns 1 if anything is visible. now: host seconds */
int ui_render(uint32_t *ov, double now);

#endif
