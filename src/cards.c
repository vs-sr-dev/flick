/* HyperScanEmu - card catalogue and the cards folder (see cards.h).
 * Every card in the catalogue is rebuilt from its game ID and number (zero UID, bytes 8-9
 * game, 10-11 card; big-endian for Marvel Heroes), exactly as MAME's list images. When a
 * game writes to a card (progress, unlocks), the card is saved as <dir>/<id>.bin and that
 * file is used the next time the card is placed on the reader. */
#include <string.h>
#include <sys/stat.h>
#include "cards.h"
#include "card.h"
#ifdef _WIN32
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#else
#define mkdir_p(d) mkdir(d, 0755)
#endif

static char s_dir[1024] = "cards";
static const card_info *s_current;

static const struct { int game; const char *name; const char *vol; int gid, be; } s_games[CARD_GAMES] = {
	{ CARD_ANY, "all games", "", 0, 0 },
	{ CARD_IWL, "IWL", "IW", 1, 0 },
	{ CARD_BEN10, "Ben 10", "BT", 2, 0 },
	{ CARD_XMEN, "X-Men", "XM", 3, 0 },
	{ CARD_MARVEL, "Marvel Heroes", "MH", 4, 1 },
	{ CARD_SPIDERMAN, "Spider-Man", "SM", 5, 0 },
};

const char *cards_game_name(int game)
{
	return (game >= 0 && game < CARD_GAMES) ? s_games[game].name : "?";
}

int cards_detect_game(cdimg *cd)
{
	if (!cd) return CARD_ANY;
	uint8_t raw[2352] = { 0 };
	cdimg_read(cd, 16, raw, 0);                 /* primary volume descriptor */
	const uint8_t *pvd = raw + 16;
	if (memcmp(pvd + 1, "CD001", 5)) return CARD_ANY;
	for (int g = 1; g < CARD_GAMES; g++)
		if (!memcmp(pvd + 40, s_games[g].vol, 2) && pvd[42] == '_')
			return g;
	return CARD_ANY;
}

void cards_set_dir(const char *dir)
{
	snprintf(s_dir, sizeof(s_dir), "%s", dir);
}

void cards_insert(const card_info *c)
{
	uint8_t img[CARD_SIZE] = { 0 };
	char path[1200];
	snprintf(path, sizeof(path), "%s/%s.bin", s_dir, c->id);

	FILE *f = fopen(path, "rb");
	if (!f || fread(img, 1, CARD_SIZE, f) != CARD_SIZE)
	{
		const int gid = s_games[c->game].gid, be = s_games[c->game].be;
		memset(img, 0, sizeof(img));
		if (be) { img[8] = gid >> 8; img[9] = gid; img[10] = c->num >> 8; img[11] = c->num; }
		else    { img[8] = gid; img[9] = gid >> 8; img[10] = c->num; img[11] = c->num >> 8; }
	}
	if (f) fclose(f);

	mkdir_p(s_dir);                            /* so that a write can be saved */
	card_insert(img, path);
	s_current = c;
}

void cards_remove(void)
{
	card_remove();
	s_current = NULL;
}

const card_info *cards_current(void)
{
	return s_current;
}
