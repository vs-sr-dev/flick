/* Flick - controller input: buttons by name, scripted presses */
#ifndef HS_INPUT_H
#define HS_INPUT_H

#include "emu.h"

/* button ids */
enum { BTN_BLUE, BTN_START, BTN_SELECT, BTN_LSHOULDER, BTN_RSHOULDER, BTN_LTRIGGER, BTN_RTRIGGER,
       BTN_YELLOW, BTN_RED, BTN_GREEN, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_COUNT };

int input_button_id(const char *name);
const char *input_button_name(int id);   /* "green", ...; "" for a bad id */
/* keyboard and scripted presses */
void input_set(int pad, int button, int down);
/* a game controller on pad 0/1: buttons as 1 << BTN_* (the d-pad as BTN_UP..BTN_RIGHT),
 * stick axes -32768..32767 as SDL reports them (up and left negative) */
void input_joy(int pad, uint32_t buttons, int x, int y);
void input_release_keys(void);       /* the keyboard's presses, both pads */
void input_release_all(void);        /* every source, both pads */
/* "T:name[:dur][@pad],..." - press at T seconds for dur (default 0.2 s) */
int input_script_parse(const char *spec);
void input_script_update(double t);

#endif
