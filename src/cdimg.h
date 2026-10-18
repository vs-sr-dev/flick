/* Flick - CD images (.cue with one BINARY file, .iso)
 * Reproduces what MAME's cdrom_file returns for these layouts, so that the CD servo
 * delivers byte-identical sectors. */
#ifndef HS_CDIMG_H
#define HS_CDIMG_H

#include "emu.h"

#define CD_MAX_TRACKS 99

enum { CDT_MODE1 = 0, CDT_MODE1_RAW, CDT_AUDIO };

typedef struct {
	int type;
	uint32_t datasize;      /* bytes per sector in the file */
	uint32_t frames;        /* sectors in the file */
	uint32_t pregap, postgap;
	uint32_t logframeofs;   /* first logical sector */
	uint64_t offset;        /* byte offset in the file */
} cd_track;

typedef struct {
	FILE *f;
	int numtrks;
	cd_track tracks[CD_MAX_TRACKS + 1];   /* [numtrks] is the dummy end entry */
} cdimg;

cdimg *cdimg_open(const char *path);
void cdimg_close(cdimg *cd);
/* MAME's read_data(lba, buf, CD_TRACK_MODE1_RAW or CD_TRACK_AUDIO): up to 2352 bytes into
 * buf, which is left untouched wherever MAME leaves it untouched (unsupported conversion,
 * reads past the end of the file) */
void cdimg_read(cdimg *cd, uint32_t lba, uint8_t *buf, int audio);
int cdimg_track_type(cdimg *cd, int track);
uint32_t cdimg_lba_to_msf(uint32_t lba);

#endif
