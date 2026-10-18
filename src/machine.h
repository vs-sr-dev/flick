/* Flick - the HyperScan machine: memory map dispatch, reset, run loop */
#ifndef HS_MACHINE_H
#define HS_MACHINE_H

#include "emu.h"
#include "cdimg.h"

extern int g_log_uart;
extern volatile int g_quit;

int machine_load_bios(const char *path);    /* flash image, 1 MB */
/* the known good dump, as in MAME's hyprscan set (SHA-1 560cb747e7193e6781d4b8b0bd4d7b45d3d28690) */
#define BIOS_CRC32 0xce346a14u
uint32_t machine_bios_crc(void);             /* CRC-32 of the flash as loaded */
void machine_reset(cdimg *cd);     /* cd: NULL = no disc */
/* run until the CPU reaches end_cycle (or the trace limit / quit) */
void machine_run_until(uint64_t end_cycle);
uint64_t machine_seconds_to_cycles(double s);

#endif
