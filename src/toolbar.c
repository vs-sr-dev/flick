/* HyperScanEmu - toolbar along the top of the window, drawn into the 640x480 0xAARRGGBB
 * overlay in the card panel's style: 12x12 pixel-art icons at twice their size, a tooltip
 * under the button the pointer is on, a volume slider. It shows when the pointer moves
 * and hides 2.5 s later (never while paused or while the pointer is on it). Frontend
 * independent: the frontend passes pointer positions in overlay coordinates and carries
 * out the TB_* commands. */
#include <stdio.h>
#include <string.h>
#include "toolbar.h"
#include "font.h"

#define OW 640
#define OH 480
#define BAR_H 32                     /* background; a gold line under it */
#define BTN_Y 3
#define BTN_H 26
#define HIDE_AFTER 2.5

enum { IC_DISC, IC_CARD, IC_EJECT, IC_PAUSE, IC_PLAY, IC_RESET, IC_CAMERA, IC_FULL,
       IC_WINDOW, IC_SPEAKER, IC_MUTED, IC_COUNT };

static const char s_icon[IC_COUNT][12][13] = {
	[IC_DISC] = {
		"....####....", "..##....##..", ".#........#.", ".#........#.",
		"#....##....#", "#...#..#...#", "#...#..#...#", "#....##....#",
		".#........#.", ".#........#.", "..##....##..", "....####...." },
	[IC_CARD] = {
		"............", "############", "#..........#", "#.###......#",
		"#.#.#..###.#", "#.###......#", "#......###.#", "#..........#",
		"#.########.#", "#..........#", "############", "............" },
	[IC_EJECT] = {
		"............", ".....##.....", "....####....", "...######...",
		"..########..", ".##########.", "############", "............",
		"############", "############", "............", "............" },
	[IC_PAUSE] = {
		"............", "..###..###..", "..###..###..", "..###..###..",
		"..###..###..", "..###..###..", "..###..###..", "..###..###..",
		"..###..###..", "..###..###..", "..###..###..", "............" },
	[IC_PLAY] = {
		"..#.........", "..##........", "..###.......", "..####......",
		"..#####.....", "..######....", "..######....", "..#####.....",
		"..####......", "..###.......", "..##........", "..#........." },
	[IC_RESET] = {
		"....#####.#.", "..##.....##.", ".#......###.", ".#..........",
		"#...........", "#...........", "#...........", "#..........#",
		".#........#.", ".#........#.", "..##....##..", "....####...." },
	[IC_CAMERA] = {
		"............", "....####....", ".##########.", "############",
		"####....####", "###.####.###", "###.####.###", "####....####",
		"############", ".##########.", "............", "............" },
	[IC_FULL] = {
		"####....####", "#..........#", "#..........#", "#..........#",
		"............", "............", "............", "............",
		"#..........#", "#..........#", "#..........#", "####....####" },
	[IC_WINDOW] = {
		"...#....#...", "...#....#...", "...#....#...", "####....####",
		"............", "............", "............", "............",
		"####....####", "...#....#...", "...#....#...", "...#....#..." },
	[IC_SPEAKER] = {
		"............", ".....#......", "....##...#..", "...###....#.",
		"######..#..#", "######...#.#", "######...#.#", "######..#..#",
		"...###....#.", "....##...#..", ".....#......", "............" },
	[IC_MUTED] = {
		"............", ".....#......", "....##......", "...###......",
		"######.#...#", "######..#.#.", "######...#..", "######..#.#.",
		"...###.#...#", "....##......", ".....#......", "............" },
};

typedef struct { int cmd, x, w; } button;

/* left group from the left edge, right group from the right edge */
static const button s_buttons[] = {
	{ TB_DISC, 6, 28 }, { TB_CARDS, 36, 28 }, { TB_CARD_OFF, 66, 28 },
	{ TB_PAUSE, 106, 28 }, { TB_RESET, 136, 28 },
	{ TB_TIMING, 176, 56 },
	{ TB_SHOT, 418, 28 }, { TB_SCALE, 448, 32 }, { TB_FULLSCREEN, 482, 28 },
	{ TB_MUTE, 522, 28 }, { TB_VOLUME, 554, 78 },
};
#define NBUTTONS ((int)(sizeof(s_buttons) / sizeof(s_buttons[0])))

static const uint32_t C_BG = 0xE8101822, C_GOLD = 0xFFFFD040, C_WHITE = 0xFFFFFFFF,
	C_DIM = 0xFF5A6474, C_HOVER = 0xFF2C5A96, C_TRACK = 0xFF3A4454;

static int s_mx = -1, s_my = -1, s_inside, s_drag;
static double s_last_move = -1e9;

static const button *at(int x, int y)
{
	if (y < BTN_Y || y >= BTN_Y + BTN_H) return NULL;
	for (int i = 0; i < NBUTTONS; i++)
		if (x >= s_buttons[i].x && x < s_buttons[i].x + s_buttons[i].w)
			return &s_buttons[i];
	return NULL;
}

static int slider_value(const button *b, int x)
{
	const int x0 = b->x + 4, w = b->w - 8;
	int v = (x - x0) * 100 / w;
	return v < 0 ? 0 : v > 100 ? 100 : v;
}

static int on_bar(void)
{
	return s_inside && s_my >= 0 && s_my < BAR_H + 2;
}

int tb_mouse(int x, int y, double now, int *value)
{
	s_mx = x; s_my = y;
	s_inside = 1;
	s_last_move = now;
	if (s_drag)
	{
		*value = slider_value(&s_buttons[NBUTTONS - 1], x);
		return TB_VOLUME;
	}
	return TB_NONE;
}

void tb_show(double now)
{
	s_last_move = now;
}

void tb_leave(void)
{
	s_inside = 0;
}

int tb_press(int x, int y, double now, int *value)
{
	tb_mouse(x, y, now, value);
	const button *b = at(x, y);
	if (!b) return TB_NONE;
	if (b->cmd == TB_VOLUME)
	{
		s_drag = 1;
		*value = slider_value(b, x);
	}
	return b->cmd;
}

void tb_release(void)
{
	s_drag = 0;
}

int tb_wheel(int x, int y, int dy, int volume, int *value)
{
	const button *b = at(x, y);
	if (!b || (b->cmd != TB_VOLUME && b->cmd != TB_MUTE)) return TB_NONE;
	int v = volume + dy * 5;
	*value = v < 0 ? 0 : v > 100 ? 100 : v;
	return TB_VOLUME;
}

int tb_visible(const tb_state *st, double now)
{
	return st->paused || s_drag || on_bar() || now - s_last_move < HIDE_AFTER;
}

static void icon(uint32_t *ov, const button *b, int ic, uint32_t color)
{
	const int x0 = b->x + (b->w - 24) / 2, y0 = BTN_Y + 1;
	for (int r = 0; r < 12; r++)
		for (int c = 0; c < 12; c++)
			if (s_icon[ic][r][c] == '#')
				fill_rect(ov, OW, OW, OH, x0 + 2 * c, y0 + 2 * r, 2, 2, color);
}

static void label(uint32_t *ov, const button *b, const char *s, uint32_t color)
{
	const int w = (int)strlen(s) * FONT_W * 2 - 2;
	font_draw(ov, OW, OW, OH, b->x + (b->w - w) / 2, BTN_Y + (BTN_H - 14) / 2, s, color, 2);
}

static const char *tooltip(const button *b, const tb_state *st)
{
	static char buf[64];
	switch (b->cmd)
	{
	case TB_DISC: return "Open a disc (Ctrl+O, or drop it here)";
	case TB_CARDS: return "Cards (F1)";
	case TB_CARD_OFF: return st->card_on ? "Take the card off (F2)" : "No card on the reader";
	case TB_PAUSE: return st->paused ? "Resume (P)" : "Pause (P)";
	case TB_RESET: return "Reset (F5)";
	case TB_TIMING: return st->fast ? "Timing: fast. Click for MAME's (F3)" : "Timing: MAME's. Click for fast (F3)";
	case TB_SHOT: return "Screenshot (F12)";
	case TB_SCALE: return "Window size";
	case TB_FULLSCREEN: return st->fullscreen ? "Leave full screen (F11)" : "Full screen (F11, Alt+Enter)";
	case TB_MUTE: return st->muted ? "Sound off. Click to turn it on" : "Sound off";
	case TB_VOLUME: snprintf(buf, sizeof(buf), "Volume %d%%", st->volume); return buf;
	}
	return "";
}

int tb_render(uint32_t *ov, const tb_state *st, double now)
{
	if (!tb_visible(st, now)) return 0;

	fill_rect(ov, OW, OW, OH, 0, 0, OW, BAR_H, C_BG);
	fill_rect(ov, OW, OW, OH, 0, BAR_H, OW, 2, C_GOLD);
	const button *hover = (s_inside && !s_drag) ? at(s_mx, s_my) : NULL;

	for (int i = 0; i < NBUTTONS; i++)
	{
		const button *b = &s_buttons[i];
		if (b == hover && b->cmd != TB_VOLUME)
			fill_rect(ov, OW, OW, OH, b->x, BTN_Y, b->w, BTN_H, C_HOVER);
		switch (b->cmd)
		{
		case TB_DISC: icon(ov, b, IC_DISC, C_WHITE); break;
		case TB_CARDS: icon(ov, b, IC_CARD, C_WHITE); break;
		case TB_CARD_OFF: icon(ov, b, IC_EJECT, st->card_on ? C_WHITE : C_DIM); break;
		case TB_PAUSE: icon(ov, b, st->paused ? IC_PLAY : IC_PAUSE, st->paused ? C_GOLD : C_WHITE); break;
		case TB_RESET: icon(ov, b, IC_RESET, C_WHITE); break;
		case TB_TIMING: label(ov, b, st->fast ? "FAST" : "MAME", st->fast ? C_WHITE : C_GOLD); break;
		case TB_SHOT: icon(ov, b, IC_CAMERA, C_WHITE); break;
		case TB_SCALE:
		{
			char s[8];
			snprintf(s, sizeof(s), "%dx", st->scale);
			label(ov, b, st->fullscreen ? "--" : s, st->fullscreen ? C_DIM : C_WHITE);
			break;
		}
		case TB_FULLSCREEN: icon(ov, b, st->fullscreen ? IC_WINDOW : IC_FULL, C_WHITE); break;
		case TB_MUTE: icon(ov, b, st->muted ? IC_MUTED : IC_SPEAKER, st->muted ? C_DIM : C_WHITE); break;
		case TB_VOLUME:
		{
			const int x0 = b->x + 4, w = b->w - 8, y = BTN_Y + BTN_H / 2 - 2;
			const int k = w * st->volume / 100;
			fill_rect(ov, OW, OW, OH, x0, y, w, 4, C_TRACK);
			fill_rect(ov, OW, OW, OH, x0, y, k, 4, st->muted ? C_DIM : C_GOLD);
			fill_rect(ov, OW, OW, OH, x0 + k - 3, BTN_Y + 5, 6, BTN_H - 10, b == hover || s_drag ? C_GOLD : C_WHITE);
			break;
		}
		}
	}

	const button *tip = s_drag ? &s_buttons[NBUTTONS - 1] : hover;
	if (tip)
	{
		const char *s = tooltip(tip, st);
		const int w = (int)strlen(s) * FONT_W * 2 + 14;
		int x = tip->x;
		if (x + w > OW - 4) x = OW - 4 - w;
		fill_rect(ov, OW, OW, OH, x, BAR_H + 6, w, 26, C_BG);
		font_draw(ov, OW, OW, OH, x + 8, BAR_H + 12, s, C_WHITE, 2);
	}
	return 1;
}
