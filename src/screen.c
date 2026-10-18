// license:BSD-3-Clause
// copyright-holders:Sandro Ronco, Aaron Giles
/* Raster timing and TV encoder.
 * The timing follows MAME's screen_device (configure, vblank begin/end timers, vpos)
 * because the PPU vblank IRQ has to land on the same instruction as in MAME; the TV
 * registers and the frame composition (TVE frame buffer, fade) follow the spg29x driver. */
#include <stdio.h>
#include <string.h>
#include "devices.h"
#include "sched.h"
#include "bus.h"

static int s_width, s_height, s_vis_w, s_vis_h;
static atto_t s_frame_period, s_scantime, s_pixeltime, s_vblank_period;
static atto_t s_vblank_start_time, s_vblank_end_time;
static emu_timer *s_vbl_begin, *s_vbl_end, *s_field;
int g_field_vblank;

static uint16_t s_tve_control;
static uint8_t s_tve_fade_offset;
static uint8_t s_tve_buffer_ctrl = 3;
static uint32_t s_tv_start_addr[3];

uint32_t g_frame[480][640];
uint64_t g_frame_count;
void (*g_on_frame)(void);

/* MAME: HZ_TO_ATTOSECONDS(27_MHz_XTAL), a truncated double division */
#define PIXEL_ATTO ((atto_t)37037037037ULL)

static atto_t time_until_pos(int vpos, int hpos)
{
	vpos += s_height - s_vis_h;           /* visarea.bottom() + 1 == vis_h */
	vpos %= s_height;
	atto_t target = (atto_t)vpos * s_scantime + (atto_t)hpos * s_pixeltime;
	atto_t cur = sched_time() - s_vblank_start_time;
	if (target <= cur + s_pixeltime / 2)
		target += s_frame_period;
	while (target <= cur)
		target += s_frame_period;
	return target - cur;
}

static atto_t time_until_vblank_start(void) { return time_until_pos(s_vis_h, 0); }

static atto_t time_until_vblank_end(void)
{
	atto_t now = sched_time();
	atto_t target = s_vblank_end_time;
	if (!(now < s_vblank_end_time))
		target += s_frame_period;
	return target - now;
}

int screen_vpos(void)
{
	atto_t delta = sched_time() - s_vblank_start_time;
	delta += s_pixeltime / 2;
	int vpos = (int)(delta / s_scantime);
	return (s_vis_h + vpos) % s_height;
}

/* when screen_vpos() next changes: the start of the next line, half a pixel early as above */
atto_t screen_vpos_change_time(void)
{
	const atto_t delta = sched_time() - s_vblank_start_time + s_pixeltime / 2;
	return s_vblank_start_time + (delta / s_scantime + 1) * s_scantime - s_pixeltime / 2;
}

int screen_width(void)  { return s_vis_w; }
int screen_height(void) { return s_vis_h; }

/* what the frame composition needs, for debug dumps (fixed layout, same build) */
#define SCREEN_STATE(X) X(s_vis_w) X(s_vis_h) X(s_tve_control) X(s_tve_fade_offset) \
	X(s_tve_buffer_ctrl) X(s_tv_start_addr)

void screen_dump(FILE *f)
{
#define W(v) fwrite(&v, sizeof(v), 1, f);
	SCREEN_STATE(W)
#undef W
}

int screen_undump(FILE *f)
{
	int ok = 1;
#define R(v) ok &= fread(&v, sizeof(v), 1, f) == 1;
	SCREEN_STATE(R)
#undef R
	return ok;
}

/* HVGA (640x240): the picture starts 8 lines into the layers. Ben 10's split screen
 * leaves its tilemaps' first row as non-tile data and lays out two identical 120-line
 * halves (border, 104 lines of scene, dark band) from line 8 on, with its raster IRQ at
 * line 120; from line 0, as MAME shows it, the junk row is on screen and player 2's dark
 * band falls off the bottom. Spider-Man's only HVGA frames are black. */
#define HVGA_TOP 8

void screen_compose(void)
{
	static uint32_t tall[240 + HVGA_TOP][640];
	if (s_vis_w == 640 && s_vis_h == 240)
	{
		ppu_render(tall, s_vis_w, s_vis_h + HVGA_TOP);
		memcpy(g_frame, tall[HVGA_TOP], sizeof(g_frame[0]) * s_vis_h);
	}
	else
		ppu_render(g_frame, s_vis_w, s_vis_h);

	// TVE frame buffer
	if (s_tve_buffer_ctrl < 3)
	{
		const int interlaced = s_tve_control & 1;
		for (int y = 0; y < s_vis_h; y += (interlaced ? 1 : 2))
			for (int x = 0; x < s_vis_w; x++)
			{
				const uint32_t a = s_tv_start_addr[s_tve_buffer_ctrl] + (y * s_vis_w + x) * 2;
				uint8_t *p = bus_ptr(a & ~1u);
				const uint16_t rgb = p ? (uint16_t)(p[0] | p[1] << 8) : 0;
				const uint32_t r = (rgb >> 11) & 0x1f, g = (rgb >> 5) & 0x3f, b = rgb & 0x1f;
				const uint32_t pix = ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
				g_frame[y][x] = pix;
				if (!interlaced && y + 1 < s_vis_h)
					g_frame[y + 1][x] = pix;
			}
	}

	if (s_tve_fade_offset)
	{
		const int f = 255 - s_tve_fade_offset;
		for (int y = 0; y < s_vis_h; y++)
			for (int x = 0; x < s_vis_w; x++)
			{
				const uint32_t p = g_frame[y][x];
				const uint32_t r = ((p >> 16) & 0xff) * f / 255, g = ((p >> 8) & 0xff) * f / 255, b = (p & 0xff) * f / 255;
				g_frame[y][x] = (r << 16) | (g << 8) | b;
			}
	}
}

static void vblank_end(void *ctx, int param)
{
	(void)ctx; (void)param;
	ppu_screen_vblank(0);
	g_frame_count++;
}

static void field_vblank(void *ctx, int param)
{
	(void)ctx; (void)param;
	ppu_screen_vblank(1);
	ppu_screen_vblank(0);
}

static void vblank_begin(void *ctx, int param)
{
	(void)ctx; (void)param;
	s_vblank_start_time = sched_time();
	s_vblank_end_time = s_vblank_start_time + s_vblank_period;

	screen_compose();                        /* MAME's frame_update, before the callbacks */
	if (g_on_frame) g_on_frame();

	ppu_screen_vblank(1);

	/* timing experiment (not in MAME): a PPU vblank per field in interlaced modes */
	if (g_field_vblank && (s_tve_control & 1))
		timer_adjust(s_field, s_frame_period / 2, 0, 0);

	timer_adjust(s_vbl_begin, time_until_vblank_start(), 0, 0);

	if (s_vblank_period == 0)
		vblank_end(NULL, 0);
	else
		timer_adjust(s_vbl_end, time_until_vblank_end(), 0, 0);
}

void screen_configure(int width, int height, int vis_w, int vis_h, atto_t frame_period)
{
	s_width = width;
	s_height = height;
	s_vis_w = vis_w;
	s_vis_h = vis_h;
	s_frame_period = frame_period;
	s_scantime = frame_period / height;
	s_pixeltime = frame_period / (height * width);
	s_vblank_period = s_scantime * (height - vis_h);

	atto_t delta = sched_time() - s_vblank_start_time;
	if (delta >= s_frame_period)
		vblank_begin(NULL, 0);
	else
		timer_adjust(s_vbl_begin, time_until_vblank_start(), 0, 0);
}

void screen_init(void)
{
	s_vbl_begin = timer_alloc(vblank_begin, NULL, "vblank_begin");
	s_vbl_end = timer_alloc(vblank_end, NULL, "vblank_end");
	s_field = timer_alloc(field_vblank, NULL, "field_vblank");
	s_vblank_start_time = 0;
	/* set_raw(27_MHz_XTAL, 858, 0, 640, 525, 0, 480) */
	screen_configure(858, 525, 640, 480, PIXEL_ATTO * 858 * 525);
	s_vblank_start_time = 0;
	s_vblank_end_time = s_vblank_period;

	s_tve_control = 0;
	s_tve_fade_offset = 0;
	s_tve_buffer_ctrl = 3;
	memset(s_tv_start_addr, 0, sizeof(s_tv_start_addr));
	memset(g_frame, 0, sizeof(g_frame));
}

/* 0x08030000 block */
uint32_t tv_read(uint32_t off)
{
	if (off == 0x00) return s_tve_control;
	return 0;                               /* unmapped in MAME */
}

void tv_write(uint32_t off, uint32_t data, uint32_t mask)
{
	if (off == 0x0c)
	{
		s_tve_fade_offset = data & 0xff;
		return;
	}
	if (off != 0x00)
		return;

	COMBINE(s_tve_control, data, mask);
	int vw = 640, vh = 480;
	switch (s_tve_control & 0xc)
	{
	case 0x0: vw = 320; vh = 240; break;   // QVGA
	case 0x4: vw = 640; vh = 480; break;   // VGA
	case 0x8: vw = 640; vh = 240; break;   // HVGA
	default: hs_log("tv: unknown mode %04x\n", s_tve_control);
	}
	const int interlaced = s_tve_control & 1;
	if (s_tve_control & 2)
		screen_configure(864, 625, vw, vh, PIXEL_ATTO * 864 * 625 * (interlaced ? 2 : 1));   // PAL
	else
		screen_configure(858, 525, vw, vh, PIXEL_ATTO * 858 * 525 * (interlaced ? 2 : 1));   // NTSC
}

void tv_buffer_ctrl_w(uint32_t data)        { s_tve_buffer_ctrl = data & 3; }
uint32_t tv_start_addr_r(int n)             { return s_tv_start_addr[n]; }
void tv_start_addr_w(int n, uint32_t data, uint32_t mask) { COMBINE(s_tv_start_addr[n], data, mask); }
