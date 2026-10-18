/* Flick - card catalogue (MAME's hyperscan_card list) and the cards folder */
#ifndef HS_CARDS_H
#define HS_CARDS_H

#include "cdimg.h"

enum { CARD_ANY = 0, CARD_IWL, CARD_BEN10, CARD_XMEN, CARD_MARVEL, CARD_SPIDERMAN, CARD_GAMES };

typedef struct {
	int game, num;
	const char *id;      /* MAME software name, e.g. "xmen001": also the save file name */
	const char *name;    /* "Wolverine (Character)" */
} card_info;

extern const card_info g_card_list[];
extern const int g_card_count;

const char *cards_game_name(int game);
int cards_detect_game(cdimg *cd);           /* from the disc's ISO 9660 volume ID */
void cards_set_dir(const char *dir);        /* where card save files live (default "cards") */
/* put a catalogue card on the reader: its save file if present, else a fresh card */
void cards_insert(const card_info *c);
void cards_remove(void);
const card_info *cards_current(void);       /* NULL: no catalogue card on the reader */

#endif
