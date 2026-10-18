/* Flick - controls and their panel (see controls.h) */
#ifdef HS_HAVE_SDL
#include <stdio.h>
#include <string.h>
#include "controls.h"
#include "config.h"
#include "input.h"
#include "font.h"

#define OW 640
#define OH 480
#define SCALE 2
#define LH (FONT_H * SCALE)
#define RH 20                        /* row height */
#define Y0 (24 + 2 * LH + 14)        /* first row */
#define COL_K 240                    /* keyboard column */
#define COL_P 432                    /* controller column */
#define CELL_W 180
#define NONE (-1)
#define PAD_LT 100                   /* the triggers are axes for SDL */
#define PAD_RT 101

/* the panel's rows, then "Reset to defaults" */
static const int s_rows[] = { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_GREEN, BTN_RED,
	BTN_YELLOW, BTN_BLUE, BTN_START, BTN_SELECT, BTN_LSHOULDER, BTN_RSHOULDER,
	BTN_LTRIGGER, BTN_RTRIGGER };
#define NROWS ((int)(sizeof(s_rows) / sizeof(s_rows[0])))

static const char *const s_label[BTN_COUNT] = {
	[BTN_UP] = "Up", [BTN_DOWN] = "Down", [BTN_LEFT] = "Left", [BTN_RIGHT] = "Right",
	[BTN_GREEN] = "Green", [BTN_RED] = "Red", [BTN_YELLOW] = "Yellow", [BTN_BLUE] = "Blue",
	[BTN_START] = "Start", [BTN_SELECT] = "Select", [BTN_LSHOULDER] = "L shoulder",
	[BTN_RSHOULDER] = "R shoulder", [BTN_LTRIGGER] = "L trigger", [BTN_RTRIGGER] = "R trigger",
};

static const SDL_Scancode s_key_default[BTN_COUNT] = {
	[BTN_UP] = SDL_SCANCODE_UP, [BTN_DOWN] = SDL_SCANCODE_DOWN,
	[BTN_LEFT] = SDL_SCANCODE_LEFT, [BTN_RIGHT] = SDL_SCANCODE_RIGHT,
	[BTN_GREEN] = SDL_SCANCODE_Z, [BTN_RED] = SDL_SCANCODE_X,
	[BTN_YELLOW] = SDL_SCANCODE_C, [BTN_BLUE] = SDL_SCANCODE_V,
	[BTN_START] = SDL_SCANCODE_RETURN, [BTN_SELECT] = SDL_SCANCODE_BACKSPACE,
	[BTN_LSHOULDER] = SDL_SCANCODE_A, [BTN_RSHOULDER] = SDL_SCANCODE_S,
	[BTN_LTRIGGER] = SDL_SCANCODE_Q, [BTN_RTRIGGER] = SDL_SCANCODE_W,
};

/* colours as on the Xbox pad: A green, B red, X blue, Y yellow */
static const int s_pad_default[BTN_COUNT] = {
	[BTN_UP] = SDL_CONTROLLER_BUTTON_DPAD_UP, [BTN_DOWN] = SDL_CONTROLLER_BUTTON_DPAD_DOWN,
	[BTN_LEFT] = SDL_CONTROLLER_BUTTON_DPAD_LEFT, [BTN_RIGHT] = SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
	[BTN_GREEN] = SDL_CONTROLLER_BUTTON_A, [BTN_RED] = SDL_CONTROLLER_BUTTON_B,
	[BTN_YELLOW] = SDL_CONTROLLER_BUTTON_Y, [BTN_BLUE] = SDL_CONTROLLER_BUTTON_X,
	[BTN_START] = SDL_CONTROLLER_BUTTON_START, [BTN_SELECT] = SDL_CONTROLLER_BUTTON_BACK,
	[BTN_LSHOULDER] = SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	[BTN_RSHOULDER] = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	[BTN_LTRIGGER] = PAD_LT, [BTN_RTRIGGER] = PAD_RT,
};

static int s_key[BTN_COUNT], s_pad[BTN_COUNT];

static int s_open, s_row, s_col, s_listen;
static char s_msg[80];
static double s_msg_until, s_now;

/* ---- names ---- */

static const char *pad_name(int p)
{
	static const char *const names[] = { "A", "B", "X", "Y", "View", "Guide", "Menu",
		"L stick", "R stick", "LB", "RB", "D-pad up", "D-pad down", "D-pad left",
		"D-pad right", "Share", "Paddle 1", "Paddle 2", "Paddle 3", "Paddle 4", "Touchpad" };
	static char buf[16];
	if (p == PAD_LT) return "LT";
	if (p == PAD_RT) return "RT";
	if (p >= 0 && p < (int)(sizeof(names) / sizeof(names[0]))) return names[p];
	snprintf(buf, sizeof(buf), "Button %d", p);
	return buf;
}

/* the name flick.ini keeps: SDL's own */
static const char *pad_id(int p)
{
	if (p == PAD_LT) return "lefttrigger";
	if (p == PAD_RT) return "righttrigger";
	const char *s = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)p);
	return s ? s : "none";
}

static int parse_key(const char *s, int def)
{
	if (!*s) return def;
	if (!strcmp(s, "none")) return NONE;
	const SDL_Scancode sc = SDL_GetScancodeFromName(s);
	return sc == SDL_SCANCODE_UNKNOWN ? def : (int)sc;
}

static int parse_pad(const char *s, int def)
{
	if (!*s) return def;
	if (!strcmp(s, "none")) return NONE;
	if (!strcmp(s, "lefttrigger")) return PAD_LT;
	if (!strcmp(s, "righttrigger")) return PAD_RT;
	const SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(s);
	return b == SDL_CONTROLLER_BUTTON_INVALID ? def : (int)b;
}

/* the binding of button b into g_config: "" when it is the default */
static void store(int b)
{
	if (s_key[b] == (int)s_key_default[b]) g_config.key[b][0] = 0;
	else snprintf(g_config.key[b], sizeof(g_config.key[b]), "%s",
		s_key[b] == NONE ? "none" : SDL_GetScancodeName((SDL_Scancode)s_key[b]));
	if (s_pad[b] == s_pad_default[b]) g_config.pad[b][0] = 0;
	else snprintf(g_config.pad[b], sizeof(g_config.pad[b]), "%s", s_pad[b] == NONE ? "none" : pad_id(s_pad[b]));
}

void ctl_init(void)
{
	for (int b = 0; b < BTN_COUNT; b++)
	{
		s_key[b] = parse_key(g_config.key[b], s_key_default[b]);
		s_pad[b] = parse_pad(g_config.pad[b], s_pad_default[b]);
	}
}

/* ---- lookups for the frontend ---- */

int ctl_key_button(SDL_Scancode sc)
{
	for (int b = 0; b < BTN_COUNT; b++)
		if (s_key[b] == (int)sc) return b;
	return -1;
}

uint32_t ctl_pad_buttons(SDL_GameController *c)
{
	uint32_t btn = 0;
	for (int b = 0; b < BTN_COUNT; b++)
	{
		const int p = s_pad[b];
		int down = 0;
		if (p == PAD_LT) down = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
		else if (p == PAD_RT) down = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;
		else if (p != NONE) down = SDL_GameControllerGetButton(c, (SDL_GameControllerButton)p);
		if (down) btn |= 1u << b;
	}
	return btn;
}

/* ---- the panel ---- */

int ctl_open(void) { return s_open; }

void ctl_toggle(void)
{
	s_open = !s_open;
	s_listen = 0;
}

static void message(const char *m)
{
	snprintf(s_msg, sizeof(s_msg), "%s", m);
	s_msg_until = s_now + 2.5;
}

/* a binding already used by another button moves to it: the two swap */
static void bind(int *map, int b, int v)
{
	for (int o = 0; o < BTN_COUNT; o++)
		if (o != b && map[o] == v)
		{
			map[o] = map[b];
			store(o);
		}
	map[b] = v;
	store(b);
	s_listen = 0;
}

static void activate(void)
{
	if (s_row < NROWS)
	{
		s_listen = 1;
		return;
	}
	for (int b = 0; b < BTN_COUNT; b++)
	{
		s_key[b] = s_key_default[b];
		s_pad[b] = s_pad_default[b];
		store(b);
	}
	message("Default controls restored");
}

static void clear(void)
{
	if (s_row >= NROWS) return;
	const int b = s_rows[s_row];
	if (s_col == 0) s_key[b] = NONE;
	else s_pad[b] = NONE;
	store(b);
}

/* Flick's own keys stay out of the bindings */
static int reserved(SDL_Scancode sc)
{
	return sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_PAUSE || sc == SDL_SCANCODE_P ||
		(sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12);
}

int ctl_key(const SDL_KeyboardEvent *k)
{
	if (k->type != SDL_KEYDOWN) return 0;
	const SDL_Scancode sc = k->keysym.scancode;
	if (s_listen)
	{
		if (k->repeat) return 0;
		if (sc == SDL_SCANCODE_ESCAPE) s_listen = 0;
		else if (s_col == 1) message("Press a button on the controller (Esc: cancel)");
		else if (reserved(sc)) message("F1-F12, P, Pause and Esc are Flick's keys");
		else if (!*SDL_GetScancodeName(sc)) message("This key cannot be used");
		else bind(s_key, s_rows[s_row], sc);
		return 0;
	}
	switch (sc)
	{
	case SDL_SCANCODE_UP: s_row--; break;
	case SDL_SCANCODE_DOWN: s_row++; break;
	case SDL_SCANCODE_HOME: s_row = 0; break;
	case SDL_SCANCODE_END: s_row = NROWS; break;
	case SDL_SCANCODE_LEFT: s_col = 0; break;
	case SDL_SCANCODE_RIGHT: s_col = 1; break;
	case SDL_SCANCODE_TAB: s_col ^= 1; break;
	case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE:
		if (!k->repeat) activate();
		break;
	case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE: clear(); break;
	case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_F4:
		if (k->repeat) break;
		s_open = 0;
		return 1;
	default: break;
	}
	if (s_row < 0) s_row = 0;
	if (s_row > NROWS) s_row = NROWS;
	return 0;
}

int ctl_pad_button(int button)
{
	if (s_listen)
	{
		if (s_col == 1) bind(s_pad, s_rows[s_row], button);
		else if (button == SDL_CONTROLLER_BUTTON_B) s_listen = 0;
		else message("Press a key on the keyboard (B: cancel)");
		return 0;
	}
	switch (button)
	{
	case SDL_CONTROLLER_BUTTON_DPAD_UP: if (s_row > 0) s_row--; break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN: if (s_row < NROWS) s_row++; break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT: s_col = 0; break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: s_col = 1; break;
	case SDL_CONTROLLER_BUTTON_A: activate(); break;
	case SDL_CONTROLLER_BUTTON_X: clear(); break;
	case SDL_CONTROLLER_BUTTON_B:
		s_open = 0;
		return 1;
	}
	return 0;
}

void ctl_pad_axis(int axis, int value)
{
	if (!s_listen || s_col != 1 || value <= 16000) return;
	if (axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) bind(s_pad, s_rows[s_row], PAD_LT);
	else if (axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) bind(s_pad, s_rows[s_row], PAD_RT);
}

int ctl_click(int x, int y)
{
	if (x < 16 || x >= OW - 16 || y < 14 || y >= OH - 14)
	{
		s_open = 0;                      /* a click outside the panel closes it */
		return 1;
	}
	const int r = (y - Y0 + 3) / RH;
	if (y < Y0 - 3 || r > NROWS) return 0;
	if (r == NROWS)
	{
		s_row = NROWS;
		activate();
		return 0;
	}
	for (int c = 0; c < 2; c++)
	{
		const int cx = (c ? COL_P : COL_K) - 6;
		if (x >= cx && x < cx + CELL_W)
		{
			s_row = r;
			s_col = c;
			s_listen = 1;
		}
	}
	return 0;
}

static void text(uint32_t *ov, int x, int y, const char *s, uint32_t color)
{
	font_draw(ov, OW, OW, OH, x, y, s, color, SCALE);
}

int ctl_render(uint32_t *ov, double now)
{
	s_now = now;
	if (!s_open) return 0;
	const uint32_t white = 0xFFFFFFFF, dim = 0xFF98A4B4, gold = 0xFFFFD040, sel = 0xFF2C5A96;
	fill_rect(ov, OW, OW, OH, 16, 14, OW - 32, OH - 28, 0xE8101822);
	fill_rect(ov, OW, OW, OH, 16, 14, OW - 32, 2, gold);
	text(ov, 28, 24, "Controls", white);
	text(ov, COL_K, 24 + LH + 8, "Keyboard", dim);
	text(ov, COL_P, 24 + LH + 8, "Controller", dim);

	for (int r = 0; r < NROWS; r++)
	{
		const int b = s_rows[r], y = Y0 + r * RH;
		text(ov, 28, y, s_label[b], white);
		for (int c = 0; c < 2; c++)
		{
			const int x = c ? COL_P : COL_K, v = c ? s_pad[b] : s_key[b];
			char name[16];
			snprintf(name, sizeof(name), "%s", v == NONE ? "-" : c ? pad_name(v) : SDL_GetScancodeName((SDL_Scancode)v));
			if (r == s_row && c == s_col)
				fill_rect(ov, OW, OW, OH, x - 6, y - 3, CELL_W, RH - 2, sel);
			if (r == s_row && c == s_col && s_listen)
				text(ov, x, y, c ? "press a button" : "press a key", gold);
			else
				text(ov, x, y, name, v == NONE ? dim : white);
		}
	}
	const int yd = Y0 + NROWS * RH + 4;
	if (s_row == NROWS) fill_rect(ov, OW, OW, OH, 22, yd - 3, OW - 44, RH - 2, sel);
	text(ov, 28, yd, "Reset to defaults", white);

	text(ov, 28, OH - 14 - 2 * LH - 4, "Enter or click: change   Del: clear", dim);
	if (now < s_msg_until) text(ov, 28, OH - 14 - LH - 2, s_msg, gold);
	else text(ov, 28, OH - 14 - LH - 2, "Keys in use swap places   Esc/F4: close", dim);
	return 1;
}
#endif
