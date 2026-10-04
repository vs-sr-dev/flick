/* HyperScanEmu - HyperScan RFID card (port of MAME's hyperscan_card.cpp) */
#ifndef HS_CARD_H
#define HS_CARD_H

#include "emu.h"

#define CARD_SIZE 120

int card_load(const char *path);    /* 120-byte card image, writes stay in memory */
/* put a card on the reader; writes are saved to save_path (NULL: memory only) */
void card_insert(const uint8_t *image, const char *save_path);
void card_remove(void);
int card_loaded(void);
void card_reset(void);
void card_write(int state);         /* GPIO bit 1 */
int card_read(void);
void card_report(FILE *f);         /* pulse-width statistics */
extern int g_card_scaled;           /* decode pulses relative to the start pulse */

#endif
