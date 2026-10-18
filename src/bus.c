/* Flick - memory map slow path (see bus.h) */
#include "bus.h"
#include "trace.h"
#include "score7.h"

uint8_t g_sdram[SDRAM_SIZE];
uint8_t g_sram[SRAM_SIZE];
uint8_t g_irom[IROM_SIZE];
uint8_t g_flash[FLASH_SIZE];

static inline uint32_t lane_mask(uint32_t a, int size)
{
	const int sh = (a & 3) * 8;
	return size == 4 ? 0xffffffffu : size == 2 ? 0xffffu << sh : 0xffu << sh;
}

uint32_t bus_read_slow(uint32_t a, int size)
{
	const uint32_t m = a & 0x1fffffff;
	const int sh = (m & 3) * 8;
	g_cpu.io_count++;
	uint32_t v = io_read32(m & ~3u, lane_mask(m, size));
	if (size == 2) v = (v >> sh) & 0xffff;
	else if (size == 1) v = (v >> sh) & 0xff;
	if (g_trace) trace_access('R', size, a, v);
	return v;
}

void bus_write_slow(uint32_t a, uint32_t v, int size)
{
	const uint32_t m = a & 0x1fffffff;
	if (bus_ptr(a)) return;                  /* flash / internal ROM: read-only */
	g_cpu.io_count++;
	if (g_trace) trace_access('W', size, a, v);
	io_write32(m & ~3u, v << ((m & 3) * 8), lane_mask(m, size));
}
