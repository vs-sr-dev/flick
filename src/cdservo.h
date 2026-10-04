/* HyperScanEmu - SPG290 CD servo (port of MAME's spg290_cdservo.cpp) */
#ifndef HS_CDSERVO_H
#define HS_CDSERVO_H

#include "cdimg.h"

#define IRQ_CDSERVO 60

void cdservo_init(cdimg *cd);       /* cd may be NULL: no disc */
uint32_t cdservo_read(uint32_t off);
void cdservo_write(uint32_t off, uint32_t data, uint32_t mask);

#endif
