/* Flick - the HyperScan machine.
 * The I/O map mirrors MAME's spg29x driver (spg290_mem + spg290_bios_mem, BSD-3-Clause,
 * Sandro Ronco): unmapped reads return 0 and unmapped writes are dropped, as in MAME,
 * so that traces compare. Blocks MAME does not map (CKG, SFTCFG, MIU, UART, ...) stay
 * unmapped here too until a game needs them. */
#include <string.h>
#include "machine.h"
#include "bus.h"
#include "devices.h"
#include "sched.h"
#include "score7.h"
#include "trace.h"
#include "cdservo.h"
#include "spu.h"
#include "card.h"
#include "png.h"
#include "os.h"

int g_log_uart;
volatile int g_quit;

static uint16_t s_gpio_out;

int machine_load_bios(const char *path)
{
	FILE *f = os_fopen(path, "rb");
	if (!f) return 0;
	size_t n = fread(g_flash, 1, FLASH_SIZE, f);
	fclose(f);
	if (n != FLASH_SIZE) hs_log("warning: %s is %u bytes, expected %u\n", path, (unsigned)n, FLASH_SIZE);
	return 1;
}

uint32_t machine_bios_crc(void)
{
	return crc32_update(0, g_flash, FLASH_SIZE);
}

uint32_t io_read32(uint32_t a, uint32_t mask)
{
	(void)mask;
	const uint32_t blk = a & 0xffff0000, off = a & 0xffff;
	switch (blk)
	{
	case 0x08010000: return ppu_read(off);
	case 0x08030000: return tv_read(off);
	case 0x08050000: return spu_read(off);
	case 0x08060000: return cdservo_read(off);
	case 0x08070000:
		if (off < 0x0c) return tv_start_addr_r(off >> 2);
		if (off == 0x6c) return 0x01;        // MIU status: SDRAM is in self-refresh mode
		return 0;
	case 0x08130000: return i2c_read(off);
	case 0x08160000:
		if (off < 0x6000) return timer_read(off >> 12, off & 0xfff);
		return 0;
	case 0x08200000:
		if (off == 0x24) return s_gpio_out;
		if (off == 0x68) return card_read();             // RFID reader data
		if (off == 0x70) return card_loaded() ? 1 : 0;   // card detect (homebrew extension in the fork)
		return 0;
	case 0x08210000:
		if (off == 0xe4) return timers_clk_sel_r();
		return 0;
	}
	return 0;
}

void io_write32(uint32_t a, uint32_t data, uint32_t mask)
{
	const uint32_t blk = a & 0xffff0000, off = a & 0xffff;
	switch (blk)
	{
	case 0x08010000: ppu_write(off, data, mask); return;
	case 0x08030000: tv_write(off, data, mask); return;
	case 0x08050000: spu_write(off, data, mask); return;
	case 0x08060000: cdservo_write(off, data, mask); return;
	case 0x08070000:
		if (off < 0x0c) tv_start_addr_w(off >> 2, data, mask);
		return;
	case 0x08090000:
		if (off == 0x20) tv_buffer_ctrl_w(data);
		return;
	case 0x08130000: i2c_write(off, data, mask); return;
	case 0x08150000:                         // UART (unmapped in MAME): log TX
		if (off == 0x00 && g_log_uart) { fputc(data & 0xff, stderr); }
		return;
	case 0x08160000:
		if (off < 0x6000) timer_write(off >> 12, off & 0xfff, data, mask);
		return;
	case 0x08200000:
		if (off == 0x24)                                   // card GPIO, LEDs
		{
			COMBINE(s_gpio_out, data, mask);
			if (mask & 0xff)
				card_write((s_gpio_out >> 1) & 1);
		}
		return;
	case 0x08210000:
		if (off >= 0x6c && off <= 0x80) timer_control_w((off - 0x6c) >> 2, data);
		else if (off == 0xe4) timers_clk_sel_w(data, mask);
		return;
	}
}

void machine_reset(cdimg *cd)
{
	memset(g_sdram, 0, sizeof(g_sdram));
	memset(g_sram, 0, sizeof(g_sram));
	s_gpio_out = 0;

	sched_reset();
	score7_reset();
	timers_init();
	i2c_init();
	ppu_init();
	screen_init();
	cdservo_init(cd);
	spu_init();
	card_reset();

	/* the driver's machine_reset: disable JTAG */
	g_cpu.cr[29] = 0x20000000;
}

uint64_t machine_seconds_to_cycles(double s)
{
	atto_t t = (atto_t)(s * 1e6 + 0.5) * (ATTO_PER_SEC / 1000000);
	return (uint64_t)(t / hz_to_atto(CPU_CLOCK));
}

void machine_run_until(uint64_t end_cycle)
{
	while (g_cpu.cycles < end_cycle && !g_trace_done && !g_quit)
	{
		while (g_cpu.cycles < g_next_event_cycle && g_cpu.cycles < end_cycle && !g_trace_done)
			score7_step();
		sched_fire_due();
	}
}
