/* Flick - controller input (see input.h)
 * Bits follow MAME's hyperscan_ctrl ports: IN.0 Blue 01, Start 02, Select 04, L shoulder 08,
 * R shoulder 10, L trigger 20, R trigger 40; IN.1 Yellow 20, Red 40, Green 80; IN.2/IN.3
 * the analog stick (Y, X), 0x7f at rest, reversed. */
#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "devices.h"

static const struct { const char *name; int port; uint8_t bit; } s_btn[BTN_COUNT] = {
	{ "blue", 0, 0x01 }, { "start", 0, 0x02 }, { "select", 0, 0x04 }, { "lshoulder", 0, 0x08 },
	{ "rshoulder", 0, 0x10 }, { "ltrigger", 0, 0x20 }, { "rtrigger", 0, 0x40 },
	{ "yellow", 1, 0x20 }, { "red", 1, 0x40 }, { "green", 1, 0x80 },
	{ "up", 2, 0xff }, { "down", 2, 0x00 }, { "left", 3, 0xff }, { "right", 3, 0x00 },
};

int input_button_id(const char *name)
{
	for (int i = 0; i < BTN_COUNT; i++)
		if (!strcmp(name, s_btn[i].name)) return i;
	return -1;
}

/* each source keeps its own state, merged into g_pad: releasing a key does not centre a
 * stick held on the controller */
static uint32_t s_keys[2], s_joy_btn[2];
static int s_joy_x[2], s_joy_y[2];

#define DEADZONE 8000

/* SDL axis to a port value: 0x7f at rest, reversed (up/left = 0xff), dead zone removed */
static int axis_value(int a)
{
	if (a > -DEADZONE && a < DEADZONE) return 0x7f;
	const int m = a < 0 ? -a : a;
	int d = (m - DEADZONE) * 128 / (32768 - DEADZONE);
	if (d > 128) d = 128;
	const int v = a < 0 ? 0x7f + d : 0x7f - d;
	return v < 0 ? 0 : v > 0xff ? 0xff : v;
}

/* a direction: full deflection (keys, d-pad), else the stick */
static uint8_t stick(uint32_t btn, int neg, int pos, int analog)
{
	if (btn & (1u << neg)) return 0xff;
	if (btn & (1u << pos)) return 0x00;
	return (uint8_t)analog;
}

static void merge(int pad)
{
	const uint32_t btn = s_keys[pad] | s_joy_btn[pad];
	uint8_t p0 = 0, p1 = 0;
	for (int b = 0; b < BTN_COUNT; b++)
		if ((btn >> b) & 1)
		{
			if (s_btn[b].port == 0) p0 |= s_btn[b].bit;
			else if (s_btn[b].port == 1) p1 |= s_btn[b].bit;
		}
	g_pad[pad][0] = p0;
	g_pad[pad][1] = p1;
	g_pad[pad][2] = stick(btn, BTN_UP, BTN_DOWN, axis_value(s_joy_y[pad]));
	g_pad[pad][3] = stick(btn, BTN_LEFT, BTN_RIGHT, axis_value(s_joy_x[pad]));
}

void input_set(int pad, int button, int down)
{
	if (down) s_keys[pad] |= 1u << button;
	else s_keys[pad] &= ~(1u << button);
	merge(pad);
}

void input_joy(int pad, uint32_t buttons, int x, int y)
{
	s_joy_btn[pad] = buttons;
	s_joy_x[pad] = x;
	s_joy_y[pad] = y;
	merge(pad);
}

void input_release_keys(void)
{
	for (int pad = 0; pad < 2; pad++)
	{
		s_keys[pad] = 0;
		merge(pad);
	}
}

void input_release_all(void)
{
	for (int pad = 0; pad < 2; pad++)
	{
		s_keys[pad] = s_joy_btn[pad] = 0;
		s_joy_x[pad] = s_joy_y[pad] = 0;
		merge(pad);
	}
}

typedef struct { double t; int pad, button, down; } in_event;
static in_event *s_ev;
static int s_nev, s_next;

static int cmp_ev(const void *a, const void *b)
{
	const in_event *x = a, *y = b;
	return x->t < y->t ? -1 : x->t > y->t;
}

int input_script_parse(const char *spec)
{
	char *copy = strdup(spec), *save = NULL;
	for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
	{
		char name[32] = "";
		double t = 0, dur = 0.2;
		int pad = 0;
		char *at = strchr(tok, '@');
		if (at) { pad = atoi(at + 1) & 1; *at = 0; }
		if (sscanf(tok, "%lf:%31[^:]:%lf", &t, name, &dur) < 2) { free(copy); return 0; }
		int b = input_button_id(name);
		if (b < 0) { free(copy); return 0; }
		s_ev = realloc(s_ev, (s_nev + 2) * sizeof(*s_ev));
		s_ev[s_nev++] = (in_event){ t, pad, b, 1 };
		s_ev[s_nev++] = (in_event){ t + dur, pad, b, 0 };
	}
	free(copy);
	qsort(s_ev, s_nev, sizeof(*s_ev), cmp_ev);
	return 1;
}

void input_script_update(double t)
{
	while (s_next < s_nev && s_ev[s_next].t <= t)
	{
		input_set(s_ev[s_next].pad, s_ev[s_next].button, s_ev[s_next].down);
		s_next++;
	}
}
