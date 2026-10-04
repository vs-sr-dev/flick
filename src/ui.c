/* HyperScanEmu - on-screen card panel and messages, drawn into a 640x480 0xAARRGGBB
 * overlay. Frontend independent: the frontend maps its keys to UI_* and composites. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "cards.h"
#include "font.h"

#define OW 640
#define OH 480
#define SCALE 2
#define CW (FONT_W * SCALE)
#define LH (FONT_H * SCALE)
#define ROWS 19

static int s_open, s_game, s_sel, s_top;
static char s_filter[32];
static int s_list[1024], s_n;
static char s_toast[200];
static double s_toast_until, s_now;

static int match(const card_info *c)
{
	if (s_game != CARD_ANY && c->game != s_game) return 0;
	if (!s_filter[0]) return 1;
	char hay[160];
	snprintf(hay, sizeof(hay), "%03d %s", c->num, c->name);
	for (char *p = hay; *p; p++) *p = (char)tolower((unsigned char)*p);
	return strstr(hay, s_filter) != NULL;
}

static void refilter(void)
{
	s_n = 0;
	for (int i = 0; i < g_card_count && s_n < 1024; i++)
		if (match(&g_card_list[i])) s_list[s_n++] = i;
	if (s_sel >= s_n) s_sel = s_n ? s_n - 1 : 0;
	if (s_sel < 0) s_sel = 0;
}

void ui_init(int game)
{
	s_game = game;
	s_open = 0;
	s_filter[0] = 0;
	s_sel = s_top = 0;
	refilter();
}

int ui_panel_open(void) { return s_open; }

void ui_panel_toggle(void)
{
	s_open = !s_open;
	if (s_open)
	{
		refilter();
		/* start on the card that is on the reader, if it is in the list */
		const card_info *cur = cards_current();
		for (int i = 0; cur && i < s_n; i++)
			if (&g_card_list[s_list[i]] == cur) s_sel = i;
	}
}

void ui_toast(const char *msg)
{
	snprintf(s_toast, sizeof(s_toast), "%s", msg);
	s_toast_until = s_now + 2.5;
}

static void announce(void)
{
	char m[160];
	const card_info *c = cards_current();
	if (c) snprintf(m, sizeof(m), "Card on the reader: %03d %s", c->num, c->name);
	else snprintf(m, sizeof(m), "Reader empty");
	ui_toast(m);
}

void ui_key(int key)
{
	if (key == UI_REMOVE)
	{
		cards_remove();
		announce();
		return;
	}
	if (!s_open) return;
	switch (key)
	{
	case UI_UP:    s_sel--; break;
	case UI_DOWN:  s_sel++; break;
	case UI_PGUP:  s_sel -= ROWS; break;
	case UI_PGDN:  s_sel += ROWS; break;
	case UI_HOME:  s_sel = 0; break;
	case UI_END:   s_sel = s_n - 1; break;
	case UI_CLOSE: s_open = 0; break;
	case UI_TAB:   s_game = (s_game + 1) % CARD_GAMES; refilter(); break;
	case UI_BACKSPACE:
		if (s_filter[0]) { s_filter[strlen(s_filter) - 1] = 0; refilter(); }
		break;
	case UI_ENTER:
		if (s_n)
		{
			cards_insert(&g_card_list[s_list[s_sel]]);
			s_open = 0;
			announce();
		}
		break;
	}
	if (s_sel >= s_n) s_sel = s_n - 1;
	if (s_sel < 0) s_sel = 0;
}

void ui_text(const char *s)
{
	if (!s_open) return;
	for (; *s; s++)
	{
		const size_t l = strlen(s_filter);
		if ((unsigned char)*s >= 32 && (unsigned char)*s < 127 && l + 1 < sizeof(s_filter))
		{
			s_filter[l] = (char)tolower((unsigned char)*s);
			s_filter[l + 1] = 0;
		}
	}
	s_sel = 0;
	refilter();
}

static void text(uint32_t *ov, int x, int y, const char *s, uint32_t color)
{
	font_draw(ov, OW, OW, OH, x, y, s, color, SCALE);
}

int ui_render(uint32_t *ov, double now)
{
	s_now = now;
	int drawn = 0;
	memset(ov, 0, OW * OH * 4);

	if (s_open)
	{
		drawn = 1;
		const uint32_t white = 0xFFFFFFFF, dim = 0xFF98A4B4, gold = 0xFFFFD040;
		fill_rect(ov, OW, OW, OH, 16, 14, OW - 32, OH - 28, 0xE8101822);
		fill_rect(ov, OW, OW, OH, 16, 14, OW - 32, 2, gold);

		char title[128];
		snprintf(title, sizeof(title), "Cards: %s (%d)", cards_game_name(s_game), s_n);
		text(ov, 28, 24, title, white);
		char filt[64];
		snprintf(filt, sizeof(filt), "Find: %s_", s_filter);
		text(ov, 28, 24 + LH + 2, filt, s_filter[0] ? gold : dim);

		if (s_sel < s_top) s_top = s_sel;
		if (s_sel >= s_top + ROWS) s_top = s_sel - ROWS + 1;
		const card_info *cur = cards_current();
		const int y0 = 24 + 2 * LH + 12;
		for (int r = 0; r < ROWS && s_top + r < s_n; r++)
		{
			const card_info *c = &g_card_list[s_list[s_top + r]];
			const int y = y0 + r * LH;
			if (s_top + r == s_sel)
				fill_rect(ov, OW, OW, OH, 22, y - 3, OW - 44, LH, 0xFF2C5A96);
			char line[160];
			snprintf(line, sizeof(line), "%c %03d %s", c == cur ? '*' : ' ', c->num, c->name);
			if (strlen(line) > 49) { line[47] = '.'; line[48] = '.'; line[49] = 0; }
			text(ov, 28, y, line, c == cur ? gold : white);
		}
		if (!s_n) text(ov, 28, y0, "  no card matches", dim);

		text(ov, 28, OH - 14 - 2 * LH - 4, "Enter: place on reader   F2: take off", dim);
		text(ov, 28, OH - 14 - LH - 2, "Tab: game   type: find   Esc/F1: close", dim);
	}
	else if (now < s_toast_until && s_toast[0])
	{
		drawn = 1;
		fill_rect(ov, OW, OW, OH, 0, OH - LH - 12, OW, LH + 12, 0xC0101822);
		text(ov, 10, OH - LH - 4, s_toast, 0xFFFFFFFF);
	}
	return drawn;
}
