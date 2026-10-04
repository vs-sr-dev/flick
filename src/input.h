/* HyperScanEmu - controller input: buttons by name, scripted presses */
#ifndef HS_INPUT_H
#define HS_INPUT_H

#include "emu.h"

/* button ids */
enum { BTN_BLUE, BTN_START, BTN_SELECT, BTN_LSHOULDER, BTN_RSHOULDER, BTN_LTRIGGER, BTN_RTRIGGER,
       BTN_YELLOW, BTN_RED, BTN_GREEN, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_COUNT };

int input_button_id(const char *name);
void input_set(int pad, int button, int down);
/* "T:name[:dur][@pad],..." - press at T seconds for dur (default 0.2 s) */
int input_script_parse(const char *spec);
void input_script_update(double t);

#endif
