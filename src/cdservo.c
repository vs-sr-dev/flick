// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/* SPG290 CD servo (HLE) - port of MAME's spg290_cdservo.cpp.
 * Same behaviour, seek included (a seek is instant: TODO in MAME, measured 100-200 ms
 * on hardware); sectors arrive at 75 Hz x speed. */
#include <stdlib.h>
#include <string.h>
#include "cdservo.h"
#include "devices.h"
#include "sched.h"
#include "score7.h"
#include "bus.h"

#define LEADIN_LEN   7500
#define LEADOUT_LEN  6750

static cdimg *s_cd;
static emu_timer *s_timer;
static uint32_t m_addr, m_data, m_buf_start, m_buf_end, m_buf_ptr;
static uint8_t m_speed, m_seek_min, m_seek_sec, m_seek_frm;
static uint32_t m_seek_lba, m_sector_size, m_dsp_data;
static int m_frame_found;
static uint32_t m_control0, m_control1;
static uint16_t m_skip;
static uint32_t m_dsp_regs[0x10];
static uint32_t m_dsp_memory[0x10000];
static uint8_t *m_qsub;
static uint32_t m_tot_sectors;
static int m_cur_sector;

/* running_machine::rand(), fixed seed */
static uint32_t s_rand_seed = 0x9d14abd7;
static uint32_t machine_rand(void)
{
	s_rand_seed = 1664525 * s_rand_seed + 1013904223;
	return (s_rand_seed >> 16) | (s_rand_seed << 16);
}

static int bcd_2_dec(uint8_t v) { return (v >> 4) * 10 + (v & 0x0f); }
static uint8_t dec_2_bcd(int v) { return ((v / 10) << 4) | (v % 10); }

/* the driver's space_byte_w: a byte write through the CPU address space */
static void space_write_byte(uint32_t a, uint8_t v)
{
	if (bus_writable(a)) *bus_ptr(a) = v;
	else if (!bus_ptr(a)) io_write32(a & 0x1ffffffc, (uint32_t)v << ((a & 3) * 8), 0xffu << ((a & 3) * 8));
}

static void cd_update(void *ctx, int param)
{
	(void)ctx; (void)param;
	if (!(m_control1 & 0x04) && (uint32_t)m_cur_sector == m_seek_lba + LEADIN_LEN)
	{
		uint8_t cdbuf[2448] = { 0 };

		if ((m_control0 >> 15) & 1)        // CDDA
		{
			cdimg_read(s_cd, m_cur_sector - 150 - LEADIN_LEN, cdbuf, 1);

			for (int i = 0; i < 2352; i++)
			{
				space_write_byte(m_buf_ptr++, cdbuf[i]);
				if (m_buf_ptr > m_buf_end)
					m_buf_ptr = m_buf_start;
			}
		}
		else
		{
			cdimg_read(s_cd, m_cur_sector - 150 - LEADIN_LEN, cdbuf, 0);

			// FIXME (MAME): this is required for load iso images
			if (cdimg_track_type(s_cd, m_qsub[m_cur_sector * 12 + 1] - 1) == CDT_MODE1)
			{
				int lba = (bcd_2_dec(cdbuf[12]) * 60 + bcd_2_dec(cdbuf[13])) * 75 + bcd_2_dec(cdbuf[14]);
				uint32_t msf = cdimg_lba_to_msf(lba + 150);
				cdbuf[12] = (msf >> 16) & 0xff;
				cdbuf[13] = (msf >> 8) & 0xff;
				cdbuf[14] = (msf >> 0) & 0xff;
			}

			for (uint32_t i = 0; i < m_sector_size && i < sizeof(cdbuf); i++)
			{
				space_write_byte(m_buf_ptr++, cdbuf[i]);
				if (m_buf_ptr > m_buf_end)
					m_buf_ptr = m_buf_start;
			}
		}

		m_seek_lba++;
		m_frame_found = 1;
		score7_set_input(IRQ_CDSERVO);
	}

	if (((uint32_t)m_cur_sector < m_seek_lba + LEADIN_LEN) && ((uint32_t)m_cur_sector < m_tot_sectors))
		m_cur_sector++;
}

static void change_status(void)
{
	if (m_speed == 0 || !s_cd)
		timer_disable(s_timer);
	else
	{
		atto_t p = hz_to_atto(75 * m_speed);
		timer_adjust(s_timer, p, 0, p);
	}
}

static void add_qsub(int sector, uint8_t addrctrl, uint8_t track, uint8_t index, uint32_t rel_msf, uint32_t abs_msf)
{
	uint8_t *dest = m_qsub + sector * 12;
	dest[0] = ((addrctrl & 0x0f) << 4) | ((addrctrl & 0xf0) >> 4);
	dest[1] = track;
	dest[2] = index;
	dest[3] = rel_msf >> 16;
	dest[4] = rel_msf >> 8;
	dest[5] = rel_msf;
	dest[6] = 0;
	dest[7] = abs_msf >> 16;
	dest[8] = abs_msf >> 8;
	dest[9] = abs_msf;
	dest[10] = 0;
	dest[11] = 0;
}

static int adr_control(int track)
{
	return 0x10 | (s_cd->tracks[track].type != CDT_AUDIO ? 0x04 : 0);
}

static void generate_qsub(void)
{
	const int numtracks = s_cd->numtrks;
	const uint32_t total_sectors = s_cd->tracks[numtracks - 1].logframeofs + s_cd->tracks[numtracks - 1].frames + 150;

	m_tot_sectors = LEADIN_LEN + total_sectors + LEADOUT_LEN;
	m_qsub = calloc(m_tot_sectors, 12);

	int lba = 0;

	// 7500 sectors lead-in
	for (int s = 0; s < LEADIN_LEN; s += numtracks + 3)
	{
		if (lba < LEADIN_LEN) add_qsub(lba++, 0x14, 0, 0xa0, cdimg_lba_to_msf(s + 0), 1 << 16);                         // first track number
		if (lba < LEADIN_LEN) add_qsub(lba++, 0x14, 0, 0xa1, cdimg_lba_to_msf(s + 1), numtracks << 16);                 // last track number
		if (lba < LEADIN_LEN) add_qsub(lba++, 0x14, 0, 0xa2, cdimg_lba_to_msf(s + 2), cdimg_lba_to_msf(total_sectors)); // start time of lead-out

		for (int track = 0; track < numtracks; track++)
		{
			uint32_t track_start = s_cd->tracks[track].logframeofs + 150;
			if (lba < LEADIN_LEN)
				add_qsub(lba++, adr_control(track), 0, dec_2_bcd(track + 1), cdimg_lba_to_msf(s + 3 + track), cdimg_lba_to_msf(track_start));
		}
	}

	// data tracks
	const uint32_t pregap0 = s_cd->tracks[0].pregap;
	for (int track = 0; track < numtracks; track++)
	{
		const cd_track *t = &s_cd->tracks[track];
		uint32_t control = adr_control(track);
		uint32_t track_start = t->logframeofs;

		// pregap
		uint32_t pregap = t->pregap;

		// first track should have a 150 frames pregap
		if (track == 0 && pregap0 == 0)
			pregap = 150;
		else if (track != 0 && pregap0 == 0)
			track_start += 150;

		for (uint32_t s = 0; s < pregap; s++)
			add_qsub(lba++, control, dec_2_bcd(track + 1), 0, cdimg_lba_to_msf(s), cdimg_lba_to_msf(track_start + s));

		track_start += pregap;

		for (uint32_t s = 0; s < t->frames; s++)
			add_qsub(lba++, control, dec_2_bcd(track + 1), 1, cdimg_lba_to_msf(s), cdimg_lba_to_msf(track_start + s));

		track_start += t->frames;

		// postgap
		for (uint32_t s = 0; s < t->postgap; s++)
			add_qsub(lba++, control, dec_2_bcd(track + 1), 2, cdimg_lba_to_msf(s), cdimg_lba_to_msf(track_start + s));
	}

	// 6750 sectors lead-out
	for (int s = 0; s < LEADOUT_LEN; s++)
		add_qsub(lba++, 0x14, 0xaa, 1, cdimg_lba_to_msf(s), cdimg_lba_to_msf(total_sectors + s));
}

void cdservo_init(cdimg *cd)
{
	s_cd = cd;
	s_timer = timer_alloc(cd_update, NULL, "cdservo");
	m_addr = m_data = m_buf_start = m_buf_end = m_buf_ptr = 0;
	m_speed = 1;
	m_seek_min = m_seek_sec = m_seek_frm = 0;
	m_seek_lba = m_sector_size = m_dsp_data = 0;
	m_frame_found = 0;
	m_control0 = m_control1 = 0;
	m_skip = 0;
	memset(m_dsp_regs, 0, sizeof(m_dsp_regs));
	memset(m_dsp_memory, 0, sizeof(m_dsp_memory));
	m_tot_sectors = 0;
	m_cur_sector = 0;
	free(m_qsub);
	m_qsub = NULL;
	s_rand_seed = 0x9d14abd7;

	// generate Q subchannel
	if (s_cd)
		generate_qsub();

	change_status();
}

static void servo_cmd_r(void)
{
	if (m_addr == 0x079)
		m_data = 0xe3;          // servo status
	else if (m_addr == 0x07b)
		m_data = 0x06;          // unknown, should be in the range 4-7
	else if (m_addr == 0x07c)
		m_data = m_dsp_data >> 8;
	else if (m_addr == 0x07d)
		m_data = m_dsp_data;
	else if (m_addr == 0x082)
		m_data = 0x04;
	else if (m_addr == 0x083)
		m_data = machine_rand() & 0xff;
	else if (m_addr == 0x307)
		m_data = m_frame_found;
	else if (m_addr >= 0x340 && m_addr <= 0x349)    // read Q subchannel
		m_data = m_qsub ? m_qsub[m_cur_sector * 12 + (m_addr & 0x0f)] : 0;
	else if (m_addr == 0x34c)
		m_data = 0x40;          // Q subchannel ready
	else if (m_addr == 0x506)
		m_data = 0x04;          // DSP ready
	else if (m_addr >= 0x500 && m_addr < 0x50a)
		m_data = m_dsp_regs[m_addr & 0x0f];
	else if (m_addr == 0x41f)
		m_data = 0xff;          // if zero cause servo timeout
	else
		m_data = 0;
}

static void servo_cmd_w(void)
{
	if (m_addr == 0x013)    // skip n sectors
	{
		if (m_data & 0x01)
			m_cur_sector += (m_data & 0x10) ? -m_skip : m_skip;

		if (m_cur_sector < 0)                         m_cur_sector = 0;
		if ((uint32_t)m_cur_sector >= m_tot_sectors)  m_cur_sector = m_tot_sectors - 1;
	}
	else if (m_addr == 0x020)       // speed
	{
		if (m_speed != 1 << (m_data & 0x03))
		{
			m_speed = 1 << (m_data & 0x03);
			change_status();
		}
	}
	else if (m_addr == 0x030)       // Disc ID
	{
		// < 10 no disc, < 97 CD-RW, >= 97 CD-ROM
		if (m_data == 0x13)
			m_dsp_data = 8;
		else
			m_dsp_data = m_qsub ? 100 : 0;
	}
	else if (m_addr == 0x032)       // DSP version
		m_dsp_data = 0x0102;
	else if (m_addr == 0x19a)
		m_skip = (m_skip & 0x00ff) | (m_data << 8);
	else if (m_addr == 0x29a)
		m_skip = (m_skip & 0xff00) | m_data;
	else if (m_addr == 0x307)
		m_frame_found = 0;
	else if (m_addr == 0x505)       // DSP memory
	{
		if ((m_data & 0x0f) == 0x02)        // write: the CPU uploads the DSP code in 3-byte chunks
		{
			uint16_t addr = (m_dsp_regs[0] << 8) | m_dsp_regs[1];
			m_dsp_memory[addr] = (m_dsp_regs[2] << 16) | (m_dsp_regs[3] << 8) | m_dsp_regs[4];
		}
		else if ((m_data & 0x0f) == 0x03)   // read
		{
			uint16_t addr = (m_dsp_regs[0] << 8) | m_dsp_regs[1];
			m_dsp_regs[7] = (m_dsp_memory[addr] >> 16) & 0xff;
			m_dsp_regs[8] = (m_dsp_memory[addr] >> 8) & 0xff;
			m_dsp_regs[9] = (m_dsp_memory[addr] >> 0) & 0xff;
		}
	}
	else if (m_addr >= 0x500 && m_addr < 0x50a)
		m_dsp_regs[m_addr & 0x0f] = m_data;
}

uint32_t cdservo_read(uint32_t off)
{
	switch (off)
	{
	case 0x08: return m_data;       // CD servo DSP reply
	case 0x0c: return 0;            // CD servo ready
	case 0x40: return m_control0;
	case 0x44: return m_control1;
	case 0x48: return m_seek_min;
	case 0x4c: return m_seek_sec;
	case 0x50: return m_seek_frm;
	case 0x54: return 0;            // sector CRC error flags
	case 0x60: return m_buf_start;
	case 0x64: return m_buf_end;
	case 0x68: return m_buf_ptr;
	case 0x6c: return m_sector_size;
	}
	hs_log("cdservo: unknown read %x\n", off);
	return 0;
}

void cdservo_write(uint32_t off, uint32_t data, uint32_t mask)
{
	switch (off)
	{
	case 0x04: COMBINE(m_addr, data, mask); break;      // CD servo DSP command
	case 0x08: COMBINE(m_data, data, mask); break;      // CD servo DSP data
	case 0x0c:                                          // CD servo DSP exec
		if (mask & 0xff)
		{
			if (data & 1) servo_cmd_r();
			else servo_cmd_w();
		}
		break;
	case 0x40: COMBINE(m_control0, data, mask); break;
	case 0x44:
		COMBINE(m_control1, data, mask);
		m_seek_lba = (bcd_2_dec(m_seek_min) * 60 + bcd_2_dec(m_seek_sec)) * 75 + bcd_2_dec(m_seek_frm);
		m_cur_sector = LEADIN_LEN + m_seek_lba;  // TODO (MAME): seek time
		break;
	case 0x48: if (mask & 0xff) m_seek_min = data; break;
	case 0x4c: if (mask & 0xff) m_seek_sec = data; break;
	case 0x50: if (mask & 0xff) m_seek_frm = data; break;
	case 0x54: break;
	case 0x60: COMBINE(m_buf_start, data, mask); break;
	case 0x64: COMBINE(m_buf_end, data, mask); break;
	case 0x68: COMBINE(m_buf_ptr, data, mask); break;
	case 0x6c: COMBINE(m_sector_size, data, mask); break;
	default: hs_log("cdservo: unknown write %x = %x\n", off, data);
	}
}
