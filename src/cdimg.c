/* HyperScanEmu - CD images (see cdimg.h).
 * Supported: .iso (one MODE1/2048 track) and .cue sheets with a single BINARY file whose
 * tracks are MODE1/2048, MODE1/2352 or AUDIO, with INDEX 01 (INDEX 00, PREGAP and POSTGAP
 * are rejected for now: no HyperScan dump seen so far uses them). */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "cdimg.h"

uint32_t cdimg_lba_to_msf(uint32_t lba)
{
	uint8_t m = lba / (60 * 75);
	lba -= m * (60 * 75);
	uint8_t s = lba / 75, f = lba % 75;
	return ((m / 10) << 20) | ((m % 10) << 16) | ((s / 10) << 12) | ((s % 10) << 8) | ((f / 10) << 4) | (f % 10);
}

static uint64_t file_size(FILE *f)
{
	fseeko64(f, 0, SEEK_END);
	uint64_t n = ftello64(f);
	fseeko64(f, 0, SEEK_SET);
	return n;
}

static int ends_with(const char *s, const char *ext)
{
	size_t a = strlen(s), b = strlen(ext);
	if (a < b) return 0;
	for (size_t i = 0; i < b; i++)
		if (tolower((unsigned char)s[a - b + i]) != ext[i]) return 0;
	return 1;
}

static void finish_toc(cdimg *cd)
{
	/* cdrom_file constructor: logical offsets (pregaps are never in the file here) */
	uint32_t logofs = 0;
	for (int i = 0; i < cd->numtrks; i++)
	{
		cd_track *t = &cd->tracks[i];
		logofs += t->pregap;
		t->logframeofs = logofs;
		logofs += t->postgap + t->frames;
	}
	cd->tracks[cd->numtrks].logframeofs = logofs;
}

static cdimg *open_cue(const char *path)
{
	FILE *cue = fopen(path, "r");
	if (!cue) return NULL;
	cdimg *cd = calloc(1, sizeof(*cd));
	char line[1024], bin[1024] = "";
	uint32_t idx1[CD_MAX_TRACKS];
	int ok = 1;
	while (ok && fgets(line, sizeof(line), cue))
	{
		char *p = line;
		while (isspace((unsigned char)*p)) p++;
		if (!strncmp(p, "FILE", 4))
		{
			if (bin[0]) { hs_log("cue: more than one FILE not supported\n"); ok = 0; break; }
			char *q1 = strchr(p, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
			if (!q2) { ok = 0; break; }
			*q2 = 0;
			/* path relative to the cue */
			const char *slash = strrchr(path, '/'), *bs = strrchr(path, '\\');
			if (bs > slash) slash = bs;
			if (slash) snprintf(bin, sizeof(bin), "%.*s%s", (int)(slash - path + 1), path, q1 + 1);
			else snprintf(bin, sizeof(bin), "%s", q1 + 1);
		}
		else if (!strncmp(p, "TRACK", 5))
		{
			if (cd->numtrks == CD_MAX_TRACKS) { ok = 0; break; }
			cd_track *t = &cd->tracks[cd->numtrks++];
			if (strstr(p, "MODE1/2048")) { t->type = CDT_MODE1; t->datasize = 2048; }
			else if (strstr(p, "MODE1/2352")) { t->type = CDT_MODE1_RAW; t->datasize = 2352; }
			else if (strstr(p, "AUDIO")) { t->type = CDT_AUDIO; t->datasize = 2352; }
			else { hs_log("cue: unsupported track type: %s", p); ok = 0; }
			idx1[cd->numtrks - 1] = 0;
		}
		else if (!strncmp(p, "INDEX", 5) && cd->numtrks)
		{
			int n, m, s, f;
			if (sscanf(p, "INDEX %d %d:%d:%d", &n, &m, &s, &f) != 4) { ok = 0; break; }
			if (n == 0) { hs_log("cue: INDEX 00 not supported\n"); ok = 0; break; }
			if (n == 1) idx1[cd->numtrks - 1] = (m * 60 + s) * 75 + f;
		}
		else if (!strncmp(p, "PREGAP", 6) || !strncmp(p, "POSTGAP", 7))
		{
			hs_log("cue: PREGAP/POSTGAP not supported\n");
			ok = 0;
		}
	}
	fclose(cue);
	if (!ok || !cd->numtrks || !bin[0] || !(cd->f = fopen(bin, "rb")))
	{
		free(cd);
		return NULL;
	}
	/* frames: up to the next track's INDEX 01, the last one up to the end of the file */
	const uint64_t len = file_size(cd->f);
	for (int i = 0; i < cd->numtrks; i++)
	{
		cd_track *t = &cd->tracks[i];
		t->offset = i ? cd->tracks[i - 1].offset + (uint64_t)cd->tracks[i - 1].frames * cd->tracks[i - 1].datasize : 0;
		if (i + 1 < cd->numtrks) t->frames = idx1[i + 1] - idx1[i];
		else t->frames = (uint32_t)((len - t->offset) / t->datasize);
	}
	finish_toc(cd);
	return cd;
}

cdimg *cdimg_open(const char *path)
{
	if (ends_with(path, ".cue"))
		return open_cue(path);
	if (ends_with(path, ".iso"))
	{
		cdimg *cd = calloc(1, sizeof(*cd));
		if (!(cd->f = fopen(path, "rb"))) { free(cd); return NULL; }
		cd->numtrks = 1;
		cd->tracks[0].type = CDT_MODE1;
		cd->tracks[0].datasize = 2048;
		cd->tracks[0].frames = (uint32_t)(file_size(cd->f) / 2048);
		finish_toc(cd);
		return cd;
	}
	return NULL;
}

void cdimg_close(cdimg *cd)
{
	if (!cd) return;
	fclose(cd->f);
	free(cd);
}

int cdimg_track_type(cdimg *cd, int track)
{
	if (track < 0 || track >= cd->numtrks) return -1;
	return cd->tracks[track].type;
}

static void read_partial(cdimg *cd, int tr, uint32_t lba, uint32_t chdsector, uint32_t startoffs, uint32_t length, uint8_t *dest)
{
	cd_track *t = &cd->tracks[tr];
	if (lba < t->logframeofs)                      /* pregap not in the file: blank */
	{
		memset(dest, 0, length);
		return;
	}
	fseeko64(cd->f, (int64_t)(t->offset + (uint64_t)chdsector * t->datasize + startoffs), SEEK_SET);
	size_t n = fread(dest, 1, length, cd->f);     /* a short read leaves the rest untouched, as MAME */
	(void)n;
}

void cdimg_read(cdimg *cd, uint32_t lba, uint8_t *buf, int audio)
{
	/* logical_to_chd_lba */
	int tr = 0;
	uint32_t chd = lba;
	for (int i = 0; i < cd->numtrks; i++)
		if (lba < cd->tracks[i + 1].logframeofs)
		{
			tr = i;
			chd = lba - cd->tracks[i].logframeofs;
			break;
		}

	const cd_track *t = &cd->tracks[tr];
	if (audio || t->type == CDT_AUDIO)
	{
		if (audio && t->type == CDT_AUDIO)
			read_partial(cd, tr, lba, chd, 0, 2352, buf);
		return;                                     /* conversion not supported in MAME */
	}
	if (t->type == CDT_MODE1)
	{
		/* promotion of a 2048-byte sector to mode 1 raw: sync, BCD MSF of the lba, mode 1;
		 * EDC/ECC are not generated */
		static const uint8_t sync[12] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
		const uint32_t msf = cdimg_lba_to_msf(lba);
		memcpy(buf, sync, 12);
		buf[12] = msf >> 16; buf[13] = msf >> 8; buf[14] = msf;
		buf[15] = 1;
		read_partial(cd, tr, lba, chd, 0, 2048, buf + 16);
	}
	else
		read_partial(cd, tr, lba, chd, 0, 2352, buf);
}
