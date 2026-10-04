/* HyperScanEmu - Sunplus S+core 7 interpreter
 * Port of MAME's score7_cpu_device (BSD-3-Clause, Sandro Ronco), see score7.c. */
#ifndef HS_SCORE7_H
#define HS_SCORE7_H

#include "emu.h"

typedef struct score7 {
	uint32_t pc, ppc, op;
	uint32_t gpr[32], cr[32], sr[3], ce[2];   /* ce[0] = CEH, ce[1] = CEL */
	uint64_t pending;                         /* interrupt requests, bit = vector */
	uint64_t cycles;                          /* total cycles at the current instruction start */
	int cpi;                                  /* cycles charged per instruction */
} score7_t;

extern score7_t g_cpu;

void score7_reset(void);
void score7_step(void);           /* one instruction (or interrupt entry + one instruction) */
/* Assert an interrupt line. As in MAME, the request reaches the core through a synchronizing
 * event, i.e. after the instruction in progress; score7_flush_inputs() delivers it. */
void score7_set_input(int line);
void score7_flush_inputs(void);

#endif
