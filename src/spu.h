/* HyperScanEmu - SPG290 SPU (port of the HS-LBA fork's spg290_spu.cpp) */
#ifndef HS_SPU_H
#define HS_SPU_H

#include "emu.h"

#define IRQ_SPU_FIQ 63
#define SPU_OUT_RATE 44100

extern int g_spu_output_enabled;    /* keep mixed samples for the frontend */
extern int g_spu_bios_dac;          /* DAC FIFO as the BIOS boot jingle needs (off = as MAME) */

void spu_init(void);
uint32_t spu_read(uint32_t off);
void spu_write(uint32_t off, uint32_t data, uint32_t mask);
/* render the mix up to the current emulated time (frontend; changes when hardware-voice
 * FIQs are noticed, so not used in trace comparisons) */
void spu_sync(void);
/* pop up to max_frames stereo frames (interleaved L,R) of mixed output */
uint32_t spu_take(int16_t *dst, uint32_t max_frames);

#endif
