/* HyperScanEmu - controller input (see input.h)
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

void input_set(int pad, int button, int down)
{
	const int port = s_btn[button].port;
	if (port < 2)
	{
		if (down) g_pad[pad][port] |= s_btn[button].bit;
		else g_pad[pad][port] &= ~s_btn[button].bit;
	}
	else                                     /* stick: full deflection or centre */
		g_pad[pad][port] = down ? s_btn[button].bit : 0x7f;
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
