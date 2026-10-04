/* HyperScanEmu - instruction trace, same format as the MAME fork's HS_TRACE
 * (tools/mame/recon.patch, comment above struct hs_trace in score.cpp):
 *   S <all regs>                     state before the first instruction
 *   <pc> <op> [reg=val ...]          one per instruction, changed registers only
 *    R4 <addr> <val> / W1 ...        MMIO accesses made by the instruction above
 *   L <line> c=<cycles>              interrupt line asserted
 *   I <line> c=<cycles> / X <cause>  interrupt / exception taken (before the line of the
 *                                    instruction it happened in)
 *   E n=<insns> c=<cycles> <regs>    state after the last instruction */
#ifndef HS_TRACE_H
#define HS_TRACE_H

#include "emu.h"

extern FILE *g_trace;        /* NULL when tracing is off */
extern int g_trace_done;     /* set when the instruction limit is reached */

int trace_open(const char *spec);   /* "file[:N]" */
void trace_printf(const char *fmt, ...);
void trace_begin(void);
void trace_end(uint32_t pc, uint32_t op, int op16);
void trace_access(char rw, int size, uint32_t addr, uint32_t v);

#endif
