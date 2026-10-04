/* HyperScanEmu - SPG290 memory map (29-bit, as MAME's spg290_mem + spg290_bios_mem)
 *   0x00000000-0x07ffffff  SDRAM 16 MB, mirrored
 *   0x08000000-0x09ffffff  I/O blocks (machine.c dispatches; unmapped reads 0, as MAME)
 *   0x0a000000-0x0a003fff  internal SRAM 16 KB
 *   0x0b000000-0x0b007fff  internal ROM 32 KB (never executed by the HyperScan BIOS)
 *   0x10000000-0x1fffffff  flash 1 MB, mirrored
 * Everything else in 0x08000000-0x0fffffff goes through the I/O path (and the trace). */
#ifndef HS_BUS_H
#define HS_BUS_H

#include <string.h>
#include "emu.h"

#define SDRAM_SIZE  0x1000000
#define SRAM_SIZE   0x4000
#define IROM_SIZE   0x8000
#define FLASH_SIZE  0x100000

extern uint8_t g_sdram[SDRAM_SIZE];
extern uint8_t g_sram[SRAM_SIZE];
extern uint8_t g_irom[IROM_SIZE];
extern uint8_t g_flash[FLASH_SIZE];

/* I/O dword access with MAME's mem_mask (byte lanes); a is masked and dword aligned */
uint32_t io_read32(uint32_t a, uint32_t mem_mask);
void io_write32(uint32_t a, uint32_t data, uint32_t mem_mask);

/* slow path: I/O, unmapped, trace logging */
uint32_t bus_read_slow(uint32_t a, int size);
void bus_write_slow(uint32_t a, uint32_t v, int size);

/* host pointer for plain memory, NULL for I/O / unmapped */
static inline uint8_t *bus_ptr(uint32_t a)
{
	a &= 0x1fffffff;
	if (a < 0x08000000) return &g_sdram[a & (SDRAM_SIZE - 1)];
	if (a >= 0x10000000) return &g_flash[a & (FLASH_SIZE - 1)];
	if (a >= 0x0a000000 && a < 0x0a000000 + SRAM_SIZE) return &g_sram[a & (SRAM_SIZE - 1)];
	if (a >= 0x0b000000 && a < 0x0b000000 + IROM_SIZE) return &g_irom[a & (IROM_SIZE - 1)];
	return NULL;
}

static inline int bus_writable(uint32_t a)
{
	a &= 0x1fffffff;
	return a < 0x08000000 || (a >= 0x0a000000 && a < 0x0a000000 + SRAM_SIZE);
}

static inline uint32_t bus_fetch32(uint32_t a)
{
	uint8_t *p = bus_ptr(a);
	uint32_t v;
	if (!p) return io_read32(a & 0x1ffffffc, 0xffffffff);
	memcpy(&v, p, 4);
	return v;
}

static inline uint8_t bus_read8(uint32_t a)
{
	uint8_t *p = bus_ptr(a);
	return p ? *p : (uint8_t)bus_read_slow(a, 1);
}

static inline uint16_t bus_read16(uint32_t a)
{
	uint8_t *p = bus_ptr(a);
	uint16_t v;
	if (!p) return (uint16_t)bus_read_slow(a, 2);
	memcpy(&v, p, 2);
	return v;
}

static inline uint32_t bus_read32(uint32_t a)
{
	uint8_t *p = bus_ptr(a);
	uint32_t v;
	if (!p) return bus_read_slow(a, 4);
	memcpy(&v, p, 4);
	return v;
}

static inline void bus_write8(uint32_t a, uint8_t v)
{
	if (bus_writable(a)) *bus_ptr(a) = v;
	else bus_write_slow(a, v, 1);
}

static inline void bus_write16(uint32_t a, uint16_t v)
{
	if (bus_writable(a)) memcpy(bus_ptr(a), &v, 2);
	else bus_write_slow(a, v, 2);
}

static inline void bus_write32(uint32_t a, uint32_t v)
{
	if (bus_writable(a)) memcpy(bus_ptr(a), &v, 4);
	else bus_write_slow(a, v, 4);
}

#endif
