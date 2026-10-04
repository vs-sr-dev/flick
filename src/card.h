/* HyperScanEmu - HyperScan RFID card (port of MAME's hyperscan_card.cpp) */
#ifndef HS_CARD_H
#define HS_CARD_H

#include "emu.h"

int card_load(const char *path);    /* 120-byte card image */
int card_loaded(void);
void card_reset(void);
void card_write(int state);         /* GPIO bit 1 */
int card_read(void);

#endif
