/* Flick - debug dumps (see dump.h). The video state is the PPU's and the TV
 * encoder's registers and memories in a fixed layout, valid for the same build: a dump is
 * a way to bring a glitch home from a play session, not a save state. */
#include <stdio.h>
#include <string.h>
#include "dump.h"
#include "bus.h"
#include "devices.h"
#include "score7.h"
#include "png.h"
#include "os.h"

#define VIDEO_MAGIC "HSVIDEO1"

static int write_file(const char *dir, const char *name, const void *p, size_t n)
{
	char path[1200];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = os_fopen(path, "wb");
	if (!f) return 0;
	const int ok = fwrite(p, 1, n, f) == n;
	return fclose(f) == 0 && ok;
}

static int read_file(const char *dir, const char *name, void *p, size_t n)
{
	char path[1200];
	snprintf(path, sizeof(path), "%s/%s", dir, name);
	FILE *f = os_fopen(path, "rb");
	if (!f) return 0;
	const int ok = fread(p, 1, n, f) == n;
	fclose(f);
	return ok;
}

int debug_dump(const char *dir)
{
	char path[1200];
	int ok = write_file(dir, "ram.bin", g_sdram, SDRAM_SIZE)
		&& write_file(dir, "sram.bin", g_sram, SRAM_SIZE);

	snprintf(path, sizeof(path), "%s/video.bin", dir);
	FILE *f = os_fopen(path, "wb");
	if (!f) return 0;
	fwrite(VIDEO_MAGIC, 1, 8, f);
	screen_dump(f);
	ppu_dump(f);
	ok &= fclose(f) == 0;

	snprintf(path, sizeof(path), "%s/frame.png", dir);
	ok &= png_write(path, &g_frame[0][0], 640, screen_width(), screen_height());

	snprintf(path, sizeof(path), "%s/info.txt", dir);
	if ((f = os_fopen(path, "w")))
	{
		fprintf(f, "cycle %llu\nframe %llu\npc %08x\nscreen %dx%d\n",
			(unsigned long long)g_cpu.cycles, (unsigned long long)g_frame_count, g_cpu.pc,
			screen_width(), screen_height());
		fclose(f);
	}
	return ok;
}

int debug_render(const char *dir)
{
	char path[1200], magic[8];
	if (!read_file(dir, "ram.bin", g_sdram, SDRAM_SIZE) || !read_file(dir, "sram.bin", g_sram, SRAM_SIZE))
		return 0;
	snprintf(path, sizeof(path), "%s/video.bin", dir);
	FILE *f = os_fopen(path, "rb");
	if (!f) return 0;
	const int ok = fread(magic, 1, 8, f) == 8 && !memcmp(magic, VIDEO_MAGIC, 8)
		&& screen_undump(f) && ppu_undump(f);
	fclose(f);
	if (ok) screen_compose();
	return ok;
}
