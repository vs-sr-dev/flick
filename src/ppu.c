// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/* SPG290 PPU - port of MAME's spg290_ppu.cpp, kept pixel-identical to it
 * (e.g. the bitmap blit never reaches the last visible row, blend results wrap to 8 bits).
 * TODO (as MAME): horizontal/vertical compression, character flip. */
#include <stdio.h>
#include <string.h>
#include "devices.h"
#include "score7.h"
#include "bus.h"

typedef struct {
	uint32_t control, attribute, posx, posy, nptr, blend;
	uint32_t buf_start[3];
} ppu_tx;

static uint32_t m_control, m_sprite_control, m_irq_control, m_irq_status, m_sprite_max;
static uint32_t m_sprite_buf_start, m_blend_mode, m_frame_buff[3], m_transrgb;
static uint32_t m_vcomp_value, m_vcomp_offset, m_vcomp_step, m_irq_timing_v, m_irq_timing_h, m_vblank_lines;
static ppu_tx m_txs[3];

/* 0x1000 char palette, 0x1800 sprite palette, 0x2000 hoffset, 0x3000 voffset, 0x4000 sprites.
 * The char palette is indexed with & 0x3ff in MAME although it holds 0x200 entries; the
 * upper half reads as zero here. */
static uint32_t m_char_palette_ram[0x400];
static uint32_t m_sprite_palette_ram[0x200];
static uint32_t m_hoffset_ram[0x200];
static uint32_t m_voffset_ram[0x200];
static uint32_t m_sprite_ram[0x400];

void ppu_init(void)
{
	m_control = m_sprite_control = m_irq_control = m_irq_status = m_sprite_max = 0;
	m_sprite_buf_start = m_blend_mode = m_transrgb = 0;
	memset(m_frame_buff, 0, sizeof(m_frame_buff));
	m_vcomp_value = m_vcomp_offset = m_vcomp_step = m_irq_timing_v = m_irq_timing_h = m_vblank_lines = 0;
	memset(m_txs, 0, sizeof(m_txs));
	memset(m_char_palette_ram, 0, sizeof(m_char_palette_ram));
	memset(m_sprite_palette_ram, 0, sizeof(m_sprite_palette_ram));
	memset(m_hoffset_ram, 0, sizeof(m_hoffset_ram));
	memset(m_voffset_ram, 0, sizeof(m_voffset_ram));
	memset(m_sprite_ram, 0, sizeof(m_sprite_ram));
}

/* the PPU's registers and memories, for debug dumps (fixed layout, same build) */
#define PPU_STATE(X) X(m_control) X(m_sprite_control) X(m_irq_control) X(m_irq_status) \
	X(m_sprite_max) X(m_sprite_buf_start) X(m_blend_mode) X(m_frame_buff) X(m_transrgb) \
	X(m_vcomp_value) X(m_vcomp_offset) X(m_vcomp_step) X(m_irq_timing_v) X(m_irq_timing_h) \
	X(m_vblank_lines) X(m_txs) X(m_char_palette_ram) X(m_sprite_palette_ram) X(m_hoffset_ram) \
	X(m_voffset_ram) X(m_sprite_ram)

void ppu_dump(FILE *f)
{
#define W(v) fwrite(&v, sizeof(v), 1, f);
	PPU_STATE(W)
#undef W
}

int ppu_undump(FILE *f)
{
	int ok = 1;
#define R(v) ok &= fread(&v, sizeof(v), 1, f) == 1;
	PPU_STATE(R)
#undef R
	return ok;
}

static uint32_t *ram_slot(uint32_t off)
{
	if (off >= 0x1000 && off < 0x1800) return &m_char_palette_ram[(off - 0x1000) >> 2];
	if (off >= 0x1800 && off < 0x2000) return &m_sprite_palette_ram[(off - 0x1800) >> 2];
	if (off >= 0x2000 && off < 0x2800) return &m_hoffset_ram[(off - 0x2000) >> 2];
	if (off >= 0x3000 && off < 0x3800) return &m_voffset_ram[(off - 0x3000) >> 2];
	if (off >= 0x4000 && off < 0x5000) return &m_sprite_ram[(off - 0x4000) >> 2];
	return NULL;
}

uint32_t ppu_read(uint32_t off)
{
	if (off >= 0x100)
	{
		uint32_t *p = ram_slot(off);
		return p ? *p : 0;
	}
	const uint32_t offset = off >> 2;
	switch (off)
	{
	case 0x00: return m_control;
	case 0x04: return m_sprite_control;
	case 0x08: return m_sprite_max;
	case 0x0c: return m_blend_mode;
	case 0x10: return m_transrgb;
	case 0x20: return m_txs[0].posx;
	case 0x24: return m_txs[0].posy;
	case 0x28: return m_txs[0].attribute;
	case 0x2c: return m_txs[0].control;
	case 0x30: return m_txs[0].nptr;
	case 0x38: return m_txs[0].blend;
	case 0x3c: return m_txs[1].posx;
	case 0x40: return m_txs[1].posy;
	case 0x44: return m_txs[1].attribute;
	case 0x48: return m_txs[1].control;
	case 0x4c: return m_txs[1].nptr;
	case 0x54: return m_txs[1].blend;
	case 0x58: return m_txs[2].posx;
	case 0x5c: return m_txs[2].posy;
	case 0x60: return m_txs[2].attribute;
	case 0x64: return m_txs[2].control;
	case 0x68: return m_txs[2].nptr;
	case 0x70: return m_txs[2].blend;
	case 0x74: return m_vcomp_value;
	case 0x78: return m_vcomp_offset;
	case 0x7c: return m_vcomp_step;
	case 0x80: return m_irq_control;
	case 0x84: return m_irq_status;
	case 0x88: return m_irq_timing_v;
	case 0x8c: return m_irq_timing_h;
	case 0x90: return m_vblank_lines;
	case 0x94: return m_vblank_lines + screen_vpos();
	case 0xa0: case 0xa4: case 0xa8: return m_txs[0].buf_start[offset & 3];
	case 0xac: case 0xb0: case 0xb4: return m_txs[1].buf_start[(offset + 1) & 3];
	case 0xb8: case 0xbc: case 0xc0: return m_txs[2].buf_start[(offset + 2) & 3];
	case 0xc4: case 0xc8: case 0xcc: return m_frame_buff[(offset - 1) & 3];
	case 0xd0: return m_sprite_buf_start;
	}
	hs_log("ppu: unknown read %x\n", off);
	return 0;
}

void ppu_write(uint32_t off, uint32_t data, uint32_t mask)
{
	if (off >= 0x100)
	{
		uint32_t *p = ram_slot(off);
		if (p) COMBINE(*p, data, mask);
		return;
	}
	const uint32_t offset = off >> 2;
	switch (off)
	{
	case 0x00: COMBINE(m_control, data, mask); break;
	case 0x04: COMBINE(m_sprite_control, data, mask); break;
	case 0x08: COMBINE(m_sprite_max, data, mask); break;
	case 0x0c: COMBINE(m_blend_mode, data, mask); break;
	case 0x10: COMBINE(m_transrgb, data, mask); break;
	case 0x20: COMBINE(m_txs[0].posx, data, mask); break;
	case 0x24: COMBINE(m_txs[0].posy, data, mask); break;
	case 0x28: COMBINE(m_txs[0].attribute, data, mask); break;
	case 0x2c: COMBINE(m_txs[0].control, data, mask); break;
	case 0x30: COMBINE(m_txs[0].nptr, data, mask); break;
	case 0x38: COMBINE(m_txs[0].blend, data, mask); break;
	case 0x3c: COMBINE(m_txs[1].posx, data, mask); break;
	case 0x40: COMBINE(m_txs[1].posy, data, mask); break;
	case 0x44: COMBINE(m_txs[1].attribute, data, mask); break;
	case 0x48: COMBINE(m_txs[1].control, data, mask); break;
	case 0x4c: COMBINE(m_txs[1].nptr, data, mask); break;
	case 0x54: COMBINE(m_txs[1].blend, data, mask); break;
	case 0x58: COMBINE(m_txs[2].posx, data, mask); break;
	case 0x5c: COMBINE(m_txs[2].posy, data, mask); break;
	case 0x60: COMBINE(m_txs[2].attribute, data, mask); break;
	case 0x64: COMBINE(m_txs[2].control, data, mask); break;
	case 0x68: COMBINE(m_txs[2].nptr, data, mask); break;
	case 0x70: COMBINE(m_txs[2].blend, data, mask); break;
	case 0x74: COMBINE(m_vcomp_value, data, mask); break;
	case 0x78: COMBINE(m_vcomp_offset, data, mask); break;
	case 0x7c: COMBINE(m_vcomp_step, data, mask); break;
	case 0x80:
		COMBINE(m_irq_control, data, mask);
		m_irq_status &= m_irq_control;
		break;
	case 0x84:
		if (mask & 0xff)
			m_irq_status &= ~data;            /* line clears are not modelled (latched asserts) */
		break;
	case 0x88: COMBINE(m_irq_timing_v, data, mask); break;
	case 0x8c: COMBINE(m_irq_timing_h, data, mask); break;
	case 0x90: COMBINE(m_vblank_lines, data, mask); break;
	case 0xa0: case 0xa4: case 0xa8: COMBINE(m_txs[0].buf_start[offset & 3], data, mask); break;
	case 0xac: case 0xb0: case 0xb4: COMBINE(m_txs[1].buf_start[(offset + 1) & 3], data, mask); break;
	case 0xb8: case 0xbc: case 0xc0: COMBINE(m_txs[2].buf_start[(offset + 2) & 3], data, mask); break;
	case 0xc4: case 0xc8: case 0xcc: COMBINE(m_frame_buff[(offset - 1) & 3], data, mask); break;
	case 0xd0: COMBINE(m_sprite_buf_start, data, mask); break;
	default: hs_log("ppu: unknown write %x = %x\n", off, data);
	}
}

void ppu_screen_vblank(int state)
{
	if (state && (m_irq_control & 0x01))       // VBlanking Start IRQ
	{
		m_irq_status |= 0x01;
		score7_set_input(IRQ_PPU_VBL);
	}
	else if (!state && (m_irq_control & 0x02)) // VBlanking End IRQ
	{
		m_irq_status |= 0x02;
		score7_set_input(IRQ_PPU_VBL);
	}
}


/* ------------------------------------------------------------------------ */
/*  rendering                                                                */
/* ------------------------------------------------------------------------ */

typedef struct { int min_x, max_x, min_y, max_y; } rect;

static uint32_t (*s_bmp)[640];
static rect s_clip;

/* m_space_read_cb: a dword read of the CPU space, address aligned down */
static uint32_t space_read(uint32_t a)
{
	uint8_t *p = bus_ptr(a & ~3u);
	uint32_t v;
	if (!p) return io_read32(a & 0x1ffffffc, 0xffffffff);
	memcpy(&v, p, 4);
	return v;
}

static inline int contains(int x, int y)
{
	return x >= s_clip.min_x && x <= s_clip.max_x && y >= s_clip.min_y && y <= s_clip.max_y;
}

static inline uint32_t rgb(int r, int g, int b)    /* rgb_t(u8, u8, u8) */
{
	return ((uint32_t)(uint8_t)r << 16) | ((uint32_t)(uint8_t)g << 8) | (uint8_t)b;
}

#define PAL5(x) ((((x) & 0x1f) << 3) | (((x) & 0x1f) >> 2))
#define PAL6(x) ((((x) & 0x3f) << 2) | (((x) & 0x3f) >> 4))

static inline uint32_t blend_colors(uint32_t c0, uint32_t c1, uint8_t level)
{
	const int r0 = (c0 >> 16) & 0xff, g0 = (c0 >> 8) & 0xff, b0 = c0 & 0xff;
	const int r1 = (c1 >> 16) & 0xff, g1 = (c1 >> 8) & 0xff, b1 = c1 & 0xff;
	if (m_blend_mode & 1)
		return rgb((r0 * level / 63) - (r1 * (63 - level) / 63),
		           (g0 * level / 63) - (g1 * (63 - level) / 63),
		           (b0 * level / 63) - (b1 * (63 - level) / 63));
	return rgb((r0 * level / 63) + (r1 * (63 - level) / 63),
	           (g0 * level / 63) + (g1 * (63 - level) / 63),
	           (b0 * level / 63) + (b1 * (63 - level) / 63));
}

static inline void argb1555(uint16_t posy, uint16_t posx, uint16_t argb, uint8_t blend)
{
	if (!(argb & 0x8000) && contains(posx, posy))
	{
		uint32_t color = rgb(PAL5(argb >> 10), PAL5(argb >> 5), PAL5(argb >> 0));
		if (blend)
			color = blend_colors(s_bmp[posy][posx], color, blend);
		s_bmp[posy][posx] = color;
	}
}

static inline void rgb565(uint16_t posy, uint16_t posx, uint16_t c, uint8_t blend)
{
	if ((!(m_transrgb & 0x10000) || (m_transrgb & 0xffff) != c) && contains(posx, posy))
	{
		uint32_t color = rgb(PAL5(c >> 11), PAL6(c >> 5), PAL5(c >> 0));
		if (blend)
			color = blend_colors(s_bmp[posy][posx], color, blend);
		s_bmp[posy][posx] = color;
	}
}

static void blit_sprite(uint32_t control, uint32_t attribute, uint32_t buf_start)
{
	uint16_t sprite_num   = control & 0xffff;
	uint16_t sprite_x     = (control >> 16) & 0x3ff;
	uint16_t sprite_y     = (attribute >> 16) & 0x3ff;
	uint8_t  sprite_hsize = 8 << ((attribute >> 4) & 0x03);
	uint8_t  sprite_vsize = 8 << ((attribute >> 6) & 0x03);
	uint8_t  bit_pixel    = ((attribute & 3) + 1) << 1;
	uint8_t  sprite_flip  = (attribute >> 2) & 0x03;
	uint16_t pen_bank     = ((attribute >> 8) & 0x1f) * 0x10;
	uint8_t  blend        = (attribute & 0x8000) ? 0x3f - ((attribute >> 26) & 0x3f) : 0;
	uint8_t  pixel_word   = 32 / bit_pixel;
	uint8_t  pixel_byte   = 8 / bit_pixel;
	uint8_t  word_line    = sprite_hsize / pixel_word;
	uint16_t pen_mask     = (1 << bit_pixel) - 1;

	if (sprite_num == 0)
		return;

	uint32_t sprite_base = buf_start + (sprite_num * (sprite_hsize * sprite_vsize * bit_pixel / 8));

	if (sprite_x > s_clip.max_x)  sprite_x = (sprite_x & 0x3ff) - 0x400;
	if (sprite_y > s_clip.max_y)  sprite_y = (sprite_y & 0x3ff) - 0x400;

	for (int y = 0; y < sprite_vsize; y++)
		for (int x = 0; x < word_line; x++)
		{
			uint32_t data = space_read(sprite_base + (y * word_line + x) * 4);

			for (int i = 0; i < 4; i++)
			{
				for (int b = 0; b < pixel_byte; b++)
				{
					int bitpos = i * pixel_byte + b;
					uint16_t pen = (data >> (8 - (b + 1) * bit_pixel)) & pen_mask;
					uint16_t posx = s_clip.min_x;
					if (sprite_flip & 0x01)
						posx += sprite_x + (sprite_hsize - (x * pixel_word + bitpos));
					else
						posx += sprite_x + x * pixel_word + bitpos;

					uint16_t posy = s_clip.min_y;
					if (sprite_flip & 0x02)
						posy += sprite_y + (sprite_vsize - y);
					else
						posy += sprite_y + y;

					argb1555(posy, posx, m_sprite_palette_ram[(pen_bank + pen) & 0x1ff], blend);
				}

				data >>= 8;
			}
		}
}

static void blit_bitmap(uint32_t control, int posy, int posx, uint32_t nptr, uint32_t buf_start, uint8_t blend)
{
	int max_x = s_clip.max_x + posx;
	int max_y = s_clip.max_y + posy;
	for (int y = 0; y < max_y; y++)
	{
		int line = (control & 0x04) ? 0 : y;
		uint32_t tx_start = space_read(nptr + line * 4) * 2;

		for (int x = 0; x < max_x; x += 2)
		{
			uint16_t px = s_clip.min_x + x;
			uint16_t py = s_clip.min_y + y;
			uint32_t data = space_read(buf_start + tx_start + x * 2);

			for (int b = 0; b < 2; b++)
			{
				if (control & 0x1000)
					rgb565(py - posy, px + b - posx, data & 0xffff, blend);
				else
					argb1555(py - posy, px + b - posx, data & 0xffff, blend);

				data >>= 16;
			}
		}
	}
}

static void blit_character(uint32_t control, uint32_t attribute, int posy, int posx, uint32_t nptr, uint32_t buf_start, uint8_t blend)
{
	uint8_t  hsize      = 8 << ((attribute >> 4) & 0x03);
	uint8_t  vsize      = 8 << ((attribute >> 6) & 0x03);
	uint8_t  bit_pixel  = ((attribute & 3) + 1) << 1;
	uint16_t pen_bank   = ((attribute >> 8) & 0x1f) * 0x10;
	uint8_t  pixel_word = 32 / bit_pixel;
	uint8_t  chars_line = 1024 / hsize;
	uint16_t pen_mask   = (1 << bit_pixel) - 1;

	int max_x = (s_clip.max_x + posx) / vsize + 1;
	int max_y = (s_clip.max_y + posy) / hsize + 1;

	for (int y = 0; y <= max_y; y++)
	{
		for (int x = 0; x <= max_x; x++)
		{
			int line = (control & 0x04) ? 0 : y;
			uint32_t addr = line * chars_line + x;
			uint32_t char_id = space_read(nptr + addr * 2);

			if (addr & 1)
				char_id >>= 16;

			uint32_t char_addr = buf_start + (char_id & 0xffff) * hsize * vsize;

			for (int cy = 0; cy < vsize; cy++)
			{
				uint32_t line_addr = char_addr + cy * hsize;
				for (int cx = 0; cx < hsize; cx++)
				{
					uint32_t data = space_read(line_addr + (cx * bit_pixel) / 8);
					uint16_t px = s_clip.min_x + x * hsize + cx;
					uint16_t py = s_clip.min_y + y * vsize + cy;
					uint16_t pen = (data >> ((cx % pixel_word) * bit_pixel)) & pen_mask;
					uint16_t pix = m_char_palette_ram[(pen_bank + pen) & 0x3ff];

					if (control & 0x1000)
						rgb565(py - posy, px - posx, pix, blend);
					else
						argb1555(py - posy, px - posx, pix, blend);
				}
			}
		}
	}
}

void ppu_render(uint32_t (*bitmap)[640], int vis_w, int vis_h)
{
	s_bmp = bitmap;
	s_clip = (rect){ 0, vis_w - 1, 0, vis_h - 1 };

	for (int y = 0; y < vis_h; y++)
		memset(bitmap[y], 0, vis_w * 4);

	if (!(m_control & 0x1000))
		return;

	for (int depth = 0; depth < 4; depth++)
	{
		// draw the bitmap/text layers
		for (int l = 0; l < 3; l++)
		{
			if ((m_txs[l].control & 0x08) && ((m_txs[l].attribute >> 13) & 3) == (uint32_t)depth)
			{
				uint8_t blend = (m_txs[l].control & 0x0100) ? m_txs[l].blend : 0;

				// posx is 10-bit two's complement
				int posx = m_txs[l].posx & 0x3ff;
				if (posx & 0x200)
					posx = (posx & 0x3ff) - 0x400;

				// posy is 9-bit two's complement
				int posy = m_txs[l].posy & 0x1ff;
				if (posy & 0x100)
					posy = (posy & 0x1ff) - 0x200;

				if (m_txs[l].control & 0x01)
					blit_bitmap(m_txs[l].control, posy, posx, m_txs[l].nptr, m_txs[l].buf_start[0], blend);
				else
					blit_character(m_txs[l].control, m_txs[l].attribute, posy, posx, m_txs[l].nptr, m_txs[l].buf_start[0], blend);
			}
		}

		// draw the sprites
		if (m_sprite_control & 1)
		{
			for (int i = 0; i <= (int)(m_sprite_max & 0x1ff); i++)
			{
				if (((m_sprite_ram[i * 2 + 1] >> 13) & 3) == (uint32_t)depth)
					blit_sprite(m_sprite_ram[i * 2], m_sprite_ram[i * 2 + 1], m_sprite_buf_start);
			}
		}
	}
}
