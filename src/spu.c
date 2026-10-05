// license:BSD-3-Clause
// copyright-holders:Sandro Ronco, ROG
/* SPG290 SPU - port of the HS-LBA fork's spg290_spu.cpp (BSD-3-Clause), which in turn
 * builds on MAME's driver by Sandro Ronco; IMA ADPCM as MAME's imaadpcm.cpp (Andrew
 * Gardner, Aaron Giles).
 *
 * Software channel, two modes:
 *  1. Direct DAC: the CPU writes signed 16-bit PCM to WAVE_IN_L/R (0x1040/44). Homebrew.
 *  2. DAC FIFO DMA: a double-buffered FIFO in RAM (P_DAC_FIFOBA 0x1080, rate
 *     27 MHz / (P_DAC_SAMPLE_CLK + 1), P_DAC_INT_STATUS 0x1088: [1:0] size, [2] stereo,
 *     [14] enable, [15] IRQ). An IRQ (FIQ, vector 63) at each half-buffer. Retail games.
 * Hardware channels 0-23: 4-bit IMA ADPCM / 8- or 16-bit PCM with end codes; an ended
 * channel raises the FIQ.
 *
 * The BIOS plays its boot jingle through the FIFO too, but sets it up the other way round:
 * enable (0x1088 = 0x4007) first, then the base address and the sample clock, and its
 * control flags (0x1034 = 0x10cb) lack bit 9, which the fork takes as the software-channel
 * enable. In SPG2xx's layout (VolSel in bits 7:6, as used below) bit 9 is NoInt and
 * SoftCh is bit 12, set by the BIOS and by the games alike. With g_spu_bios_dac the FIFO
 * plays when bit 12 is set and starts once a clock is written while enabled; without it,
 * as MAME, the jingle is silent. The BIOS decodes the jingle (KVAG ADPCM) into the FIFO
 * only while it waits for the vblank, which also needs the I2C timing in i2c.c.
 *
 * Timing as in MAME: the FIFO half-buffer is a timer (cycle-exact against the fork); the
 * hardware channels are mixed when the stream is updated, i.e. at register writes and at
 * a 50 Hz tick here (MAME: its sound manager), so their FIQ timing is approximate. */
#include <math.h>
#include <string.h>
#include "spu.h"
#include "devices.h"
#include "sched.h"
#include "score7.h"
#include "bus.h"

#define SPU_CLOCK   XTAL_27M
#define OUT_RATE    44100
#define RING_SIZE   32768          /* frames, FIFO DMA ring between the timer and the mixer */

/* ---- IMA ADPCM (MAME imaadpcm.cpp) ---- */
typedef struct { int32_t signal, step; } ima_state;
static const int8_t s_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static int s_diff_lookup[89 * 16];

static void ima_tables(void)
{
	static const int8_t nbl2bit[16][4] = {
		{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
		{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
		{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
		{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
	};
	for (int step = -8; step <= 80; step++)
	{
		double v = floor(16.0 * pow(11.0 / 10.0, (double)step));
		int stepval = (int)(v < 32767. ? v : 32767.);
		if (step == -5 || step == -4)
			stepval++;
		for (int nib = 0; nib < 16; nib++)
			s_diff_lookup[(step + 8) * 16 + nib] = nbl2bit[nib][0] *
				(stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2] + stepval / 4 * nbl2bit[nib][3] + stepval / 8);
	}
}

static int16_t ima_clock(ima_state *s, uint8_t nibble)
{
	s->signal += s_diff_lookup[s->step * 16 + (nibble & 15)];
	if (s->signal > 32767) s->signal = 32767;
	else if (s->signal < -32768) s->signal = -32768;
	s->step += s_index_shift[nibble & 7];
	if (s->step > 88) s->step = 88;
	else if (s->step < 0) s->step = 0;
	return (int16_t)s->signal;
}

/* ---- state ---- */
typedef struct {
	uint32_t phase, phase_acc, waddr, loop_addr;
	uint16_t mode;
	uint8_t edd, ch_volume, pan;
	uint16_t wdd;
	int active;
	uint32_t cur_waddr;
	int sample_shift;
	uint16_t cur_word;
	int16_t last_sample;
	uint32_t sample_count, started_waddr;
	ima_state adpcm;
} spu_channel;

static emu_timer *s_dma_timer, *s_tick;
static int16_t m_wave_in_l, m_wave_in_r;
static uint32_t m_wave_addr, m_wave_addr_hi, m_soft_phase, m_soft_ctrl, m_wave_pos, m_dac_half;
static uint32_t m_dac_rate, m_dac_half_frames;
static int m_dac_stereo, m_dma_on;
static int16_t m_ring[RING_SIZE * 2];
static uint32_t m_ring_head, m_ring_tail, m_ring_frac;
static uint32_t m_ctrl_flags, m_main_vol, m_ch_enable;
static spu_channel m_channel[24];
static uint32_t m_regs[0x10000 / 4];

/* mixed output for the frontend */
#define OUT_SIZE 65536
static int16_t s_out[OUT_SIZE * 2];
static uint32_t s_out_head, s_out_tail;
static uint64_t s_samples_done;          /* stream position, samples since reset */
int g_spu_output_enabled;
int g_spu_bios_dac;

static uint16_t read_word(uint32_t a)
{
	uint8_t *p = bus_ptr(a & ~1u);
	return p ? (uint16_t)(p[0] | p[1] << 8) : (uint16_t)(io_read32(a & 0x1ffffffc, 0xffffffff) >> ((a & 2) * 8));
}

/* ---- stream ---- */

static int fetch_next_sample(int ch)
{
	spu_channel *c = &m_channel[ch];
	++c->sample_count;

	const int adpcm = (c->mode >> 15) & 1;
	const int m16 = (c->mode >> 14) & 1;

	if (adpcm)
	{
		// 4-bit IMA ADPCM, 4 nibbles per word MSB-first, end code 0xFFFF
		if (c->sample_shift == 0 && c->cur_word == 0xffff)
			return 0;
		const int shift = 12 - c->sample_shift;
		const uint8_t nibble = (c->cur_word >> shift) & 0x0f;
		c->last_sample = ima_clock(&c->adpcm, nibble);
		c->sample_shift += 4;
		if (c->sample_shift >= 16)
		{
			c->sample_shift = 0;
			c->cur_waddr++;
			c->cur_word = read_word(c->cur_waddr * 2);
		}
	}
	else if (m16)
	{
		// 16-bit PCM, end code 0xFFFF
		if (c->cur_word == 0xffff) return 0;
		c->last_sample = (int16_t)(c->cur_word ^ 0x8000);
		c->cur_waddr++;
		c->cur_word = read_word(c->cur_waddr * 2);
	}
	else
	{
		// 8-bit PCM, two per word, end code 0xFF
		const uint8_t byte = (c->sample_shift == 0) ? (c->cur_word & 0xff) : (c->cur_word >> 8) & 0xff;
		if (byte == 0xff) return 0;
		c->last_sample = (int16_t)(((int)byte - 0x80) << 8);
		c->sample_shift += 8;
		if (c->sample_shift >= 16)
		{
			c->sample_shift = 0;
			c->cur_waddr++;
			c->cur_word = read_word(c->cur_waddr * 2);
		}
	}
	return 1;
}

static void stop_channel(int ch)
{
	m_channel[ch].active = 0;
	m_ch_enable &= ~(1u << ch);
}

static void start_channel(int ch)
{
	spu_channel *c = &m_channel[ch];
	// a re-trigger with the same Waddr resumes (IWL re-enables ch0 every frame)
	if (c->started_waddr != c->waddr)
	{
		c->cur_waddr = c->waddr;
		c->sample_shift = 0;
		c->cur_word = read_word(c->cur_waddr * 2);
		c->adpcm.signal = c->adpcm.step = 0;
		c->last_sample = 0;
		c->sample_count = 0;
		c->started_waddr = c->waddr;
	}
	c->phase_acc = 0;
	c->active = 1;
}

static inline int16_t clip16(int32_t v)
{
	return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
}

static void stream_render(uint32_t n)
{
	const float master = (float)(m_main_vol & 0x7f) / 127.0f;
	const uint32_t softch = g_spu_bios_dac ? 0x1000 : 0x0200;
	const int fifo_dac_on = (m_ctrl_flags & softch) && (m_soft_ctrl & 0x4000) && m_dac_rate;
	uint32_t fifo_step = 0;
	float fifo_vol = 0.0f;
	if (fifo_dac_on)
	{
		static const float volsel[4] = { 1.0f / 32, 1.0f / 8, 1.0f / 2, 1.0f };
		fifo_step = (uint32_t)(((uint64_t)m_dac_rate << 16) / OUT_RATE);
		fifo_vol = volsel[(m_ctrl_flags >> 6) & 3];
	}

	for (uint32_t i = 0; i < n; i++)
	{
		int32_t mix_l = 0, mix_r = 0, dac_l = 0, dac_r = 0;

		if (fifo_dac_on)
		{
			// hold the last frame if the ring runs dry: a held sample does not click
			uint32_t pos = m_ring_tail + (m_ring_frac >> 16);
			if (pos >= m_ring_head)
				pos = m_ring_head ? m_ring_head - 1 : 0;
			const uint32_t slot = (pos % RING_SIZE) * 2;
			dac_l = (int32_t)(m_ring[slot + 0] * fifo_vol);
			dac_r = (int32_t)(m_ring[slot + 1] * fifo_vol);
			m_ring_frac += fifo_step;
		}
		else
		{
			mix_l += m_wave_in_l;
			mix_r += m_wave_in_r;
		}

		const uint32_t enable = m_ch_enable;
		for (int ch = 0; ch < 24; ch++)
		{
			if (!((enable >> ch) & 1))
				continue;
			spu_channel *c = &m_channel[ch];
			if (!c->active || !c->phase)
				continue;

			// sample rate = Phase * 281250 / 2^19 (54 MHz DAC clock)
			const uint32_t step_q16 = (uint32_t)(((uint64_t)c->phase * 281250ull * 65536ull) / (524288ull * 44100ull));
			c->phase_acc += step_q16;
			while (c->phase_acc >= 0x10000 && c->active)
			{
				c->phase_acc -= 0x10000;
				if (!fetch_next_sample(ch))
				{
					stop_channel(ch);
					c->started_waddr = 0xffffffff;  // natural end: the next ChEn=1 restarts
					score7_set_input(IRQ_SPU_FIQ);
					break;
				}
			}

			int pan_l, pan_r;
			if (c->pan < 64) { pan_l = 0x7f; pan_r = c->pan * 2; }
			else             { pan_l = (127 - c->pan) * 2; pan_r = 0x7f; }
			const int32_t s = (int32_t)c->last_sample * c->ch_volume * c->edd / (127 * 127);
			mix_l += (s * pan_l) >> 7;
			mix_r += (s * pan_r) >> 7;
		}

		if (g_spu_output_enabled && s_out_head - s_out_tail < OUT_SIZE)
		{
			const uint32_t slot = (s_out_head % OUT_SIZE) * 2;
			s_out[slot + 0] = clip16((int32_t)(mix_l * master) + dac_l);
			s_out[slot + 1] = clip16((int32_t)(mix_r * master) + dac_r);
			s_out_head++;
		}
	}

	if (fifo_dac_on)
	{
		m_ring_tail += m_ring_frac >> 16;
		m_ring_frac &= 0xffff;
		if (m_ring_tail > m_ring_head)
			m_ring_tail = m_ring_head;
	}
}

/* m_stream->update(): render up to the current emulated time */
static void stream_update(void)
{
	const uint64_t target = (uint64_t)(sched_time() * OUT_RATE / ATTO_PER_SEC);
	if (target > s_samples_done)
	{
		stream_render((uint32_t)(target - s_samples_done));
		s_samples_done = target;
	}
}

void spu_sync(void)
{
	stream_update();
}

uint32_t spu_take(int16_t *dst, uint32_t max_frames)
{
	uint32_t n = 0;
	while (n < max_frames && s_out_tail != s_out_head)
	{
		const uint32_t slot = (s_out_tail % OUT_SIZE) * 2;
		dst[n * 2 + 0] = s_out[slot + 0];
		dst[n * 2 + 1] = s_out[slot + 1];
		s_out_tail++;
		n++;
	}
	return n;
}

/* ---- DAC FIFO DMA ---- */

static void dac_geometry(void)
{
	const uint32_t bytes = 1024u << (m_soft_ctrl & 3);   // 1/2/4/8 KB
	m_dac_stereo = (m_soft_ctrl & 0x0004) != 0;
	m_dac_half_frames = (bytes / 2) / (m_dac_stereo ? 2 : 1) / 2;
	m_dac_rate = m_soft_phase ? SPU_CLOCK / (m_soft_phase + 1) : 0;
}

static void dac_fetch_half(uint32_t half)
{
	const uint32_t words = m_dac_half_frames * (m_dac_stereo ? 2 : 1);
	uint32_t addr = m_wave_addr + half * words * 2;

	for (uint32_t f = 0; f < m_dac_half_frames; f++)
	{
		const int16_t l = (int16_t)(read_word(addr) ^ 0x8000);
		addr += 2;
		int16_t r = l;
		if (m_dac_stereo)
		{
			r = (int16_t)(read_word(addr) ^ 0x8000);
			addr += 2;
		}
		const uint32_t slot = (m_ring_head % RING_SIZE) * 2;
		m_ring[slot + 0] = l;
		m_ring[slot + 1] = r;
		m_ring_head++;
	}
}

static void dma_tick(void *ctx, int param)
{
	(void)ctx; (void)param;
	if (!(m_soft_ctrl & 0x4000))
		return;

	stream_update();

	// the half we were feeding is consumed: the CPU refills it, we play the other one
	m_wave_pos += m_dac_half_frames;
	m_dac_half ^= 1;
	dac_fetch_half(m_dac_half);

	m_soft_ctrl |= 0x8000;      // IRQ pending
	score7_set_input(IRQ_SPU_FIQ);
}

static void dma_start(void)
{
	m_wave_pos = 0;
	m_ring_head = m_ring_tail = m_ring_frac = 0;
	m_dac_half = 0;
	memset(m_ring, 0, sizeof(m_ring));

	dac_geometry();
	if (!m_dac_rate || !m_dac_half_frames)
	{
		hs_log("spu: DAC DMA start ignored, phase=%04x ctrl=%04x\n", m_soft_phase, m_soft_ctrl);
		return;
	}

	// the consumer starts one half-buffer behind the producer (see the fork's notes)
	m_ring_head = m_dac_half_frames;

	stream_update();
	dac_fetch_half(0);

	/* attotime::from_ticks(frames, rate) */
	const atto_t period = (atto_t)m_dac_half_frames * hz_to_atto(m_dac_rate);
	timer_adjust(s_dma_timer, period, 0, period);
	m_dma_on = 1;
}

static void dma_stop(void)
{
	timer_disable(s_dma_timer);
	m_dma_on = 0;
}

static void tick(void *ctx, int param)
{
	(void)ctx; (void)param;
	stream_update();
}

void spu_init(void)
{
	static int tables;
	if (!tables) { ima_tables(); tables = 1; }
	s_dma_timer = timer_alloc(dma_tick, NULL, "spu_dma");
	s_tick = timer_alloc(tick, NULL, "spu_stream");
	m_wave_in_l = m_wave_in_r = 0;
	m_wave_addr = m_wave_addr_hi = m_soft_phase = m_soft_ctrl = m_wave_pos = m_dac_half = 0;
	m_dac_rate = m_dac_half_frames = 0;
	m_dac_stereo = m_dma_on = 0;
	m_ring_head = m_ring_tail = m_ring_frac = 0;
	m_ctrl_flags = m_main_vol = m_ch_enable = 0;
	memset(m_regs, 0, sizeof(m_regs));
	memset(m_ring, 0, sizeof(m_ring));
	for (int i = 0; i < 24; i++)
	{
		spu_channel *c = &m_channel[i];
		memset(c, 0, sizeof(*c));
		c->edd = 0x7f; c->pan = 0x40; c->wdd = 0x8000;
		c->started_waddr = 0xffffffff;
	}
	s_out_head = s_out_tail = 0;
	s_samples_done = 0;
	timer_adjust(s_tick, hz_to_atto(50), 0, hz_to_atto(50));
}

uint32_t spu_read(uint32_t off)
{
	switch (off)
	{
	case 0x1000: return m_ch_enable;
	case 0x1004: return m_main_vol;
	case 0x1034: return m_ctrl_flags;
	case 0x1040: return (uint16_t)m_wave_in_l;
	case 0x1044: return (uint16_t)m_wave_in_r;
	case 0x1064: return m_soft_phase;
	case 0x1080: return m_wave_addr;
	case 0x1084: return m_wave_addr_hi;
	case 0x1088: return m_soft_ctrl;
	}
	return m_regs[(off >> 2) & 0x3fff];
}

void spu_write(uint32_t off, uint32_t data, uint32_t mask)
{
	COMBINE(m_regs[(off >> 2) & 0x3fff], data, mask);

	switch (off)
	{
	case 0x1000:  // CH_ENABLE[15:0]
	case 0x1400:  // CH_ENABLE[23:16]
	{
		stream_update();
		const uint32_t old_en = m_ch_enable;
		const int base = (off == 0x1400) ? 16 : 0;
		const uint32_t m = 0xffffu << base;
		m_ch_enable = (m_ch_enable & ~m) | (((data & 0xffff) << base) & m);
		for (int b = 0; b < 16; b++)
		{
			const int ch = base + b;
			if (ch >= 24) break;
			const int was = (old_en >> ch) & 1, now = (m_ch_enable >> ch) & 1;
			if (was == now) continue;
			if (now) start_channel(ch);
			else stop_channel(ch);
		}
		break;
	}
	case 0x1004:  // MAIN_VOL
		stream_update();
		COMBINE(m_main_vol, data, mask);
		break;
	case 0x1034:  // P_DAC_MODE_CTRL2 / control flags
		COMBINE(m_ctrl_flags, data, mask);
		if (m_ctrl_flags & 1)
			m_ctrl_flags &= ~1u;
		break;
	case 0x1040:  // WAVE_IN_L
		stream_update();
		m_wave_in_l = (int16_t)(data & 0xffff);
		break;
	case 0x1044:  // WAVE_IN_R
		stream_update();
		m_wave_in_r = (int16_t)(data & 0xffff);
		break;
	case 0x1064:  // P_DAC_SAMPLE_CLK
		stream_update();
		COMBINE(m_soft_phase, data, mask);
		if (g_spu_bios_dac && (m_soft_ctrl & 0x4000) && !m_dma_on)
			dma_start();                                // enabled before the clock (BIOS)
		break;
	case 0x1080:  // P_DAC_FIFOBA_LOW
		stream_update();
		COMBINE(m_wave_addr, data, mask);
		break;
	case 0x1084:  // P_DAC_FIFOBA_HIGH
		COMBINE(m_wave_addr_hi, data, mask);
		break;
	case 0x1088:  // P_DAC_INT_STATUS
	{
		stream_update();
		const uint32_t old_ctrl = m_soft_ctrl;
		COMBINE(m_soft_ctrl, data, mask);
		if (data & 0x8000)
			m_soft_ctrl &= ~0x8000u;  // IRQ ack
		if (!(old_ctrl & 0x4000) && (m_soft_ctrl & 0x4000))
			dma_start();
		else if ((old_ctrl & 0x4000) && !(m_soft_ctrl & 0x4000))
			dma_stop();
		break;
	}
	default:
		if (off < 0x0600)                               // attribute SRAM, 24 x 0x40
		{
			spu_channel *c = &m_channel[off / 0x40];
			switch (off & 0x3c)
			{
			case 0x00: c->waddr = (c->waddr & 0x3f0000) | (data & 0xffff); break;              // Waddr_lo
			case 0x04:                                                                         // Mode, WaddrHi, LoopAddrHi
				c->mode = (uint16_t)data;
				c->waddr = (c->waddr & 0xffff) | ((data & 0x3f) << 16);
				c->loop_addr = (c->loop_addr & 0xffff) | (((data >> 6) & 0x3f) << 16);
				break;
			case 0x08: c->loop_addr = (c->loop_addr & 0x3f0000) | (data & 0xffff); break;      // LoopAddr_lo
			case 0x0c: c->ch_volume = data & 0x7f; c->pan = (data >> 8) & 0x7f; break;        // Pan, ChVolume
			case 0x14: c->edd = data & 0x7f; break;                                             // EnvData
			case 0x2c: c->wdd = (uint16_t)data; break;                                          // WDD
			}
		}
		else if (off >= 0x0800 && off < 0x0e00)        // phase SRAM
		{
			spu_channel *c = &m_channel[(off - 0x0800) / 0x40];
			if (((off - 0x0800) & 0x3c) == 0x00)      c->phase = data & 0x7ffff;
			else if (((off - 0x0800) & 0x3c) == 0x04) c->phase_acc = data & 0x7ffff;
		}
		break;
	}
}
