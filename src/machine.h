/* HyperScanEmu - the HyperScan machine: memory map dispatch, reset, run loop */
#ifndef HS_MACHINE_H
#define HS_MACHINE_H

#include "emu.h"

extern int g_log_uart;
extern volatile int g_quit;

int machine_load_bios(const char *path);    /* flash image, 1 MB */
void machine_reset(void);
/* run until the CPU reaches end_cycle (or the trace limit / quit) */
void machine_run_until(uint64_t end_cycle);
uint64_t machine_seconds_to_cycles(double s);

#endif
