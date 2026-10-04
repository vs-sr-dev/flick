/* HyperScanEmu - command line and frontend */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"
#include "machine.h"
#include "devices.h"
#include "score7.h"
#include "trace.h"
#include "png.h"
#ifdef HS_HAVE_SDL
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

#define HS_VERSION "0.1.0-dev"

int g_verbose;

void hs_fatal(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "fatal: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, " (cycle %llu)\n", (unsigned long long)g_cpu.cycles);
	va_end(ap);
	if (g_trace) fflush(g_trace);
	exit(1);
}

void hs_log(const char *fmt, ...)
{
	if (!g_verbose) return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static void usage(void)
{
	fprintf(stderr,
		"HyperScanEmu " HS_VERSION " - Mattel HyperScan emulator\n"
		"usage: hyperscanemu --bios <hyperscan.bin> [options]\n"
		"  --bios FILE        HyperScan flash image (1 MB, required)\n"
		"  --headless         no window, no audio\n"
		"  --seconds S        stop after S emulated seconds\n"
		"  --frames N         stop after N video frames\n"
		"  --screenshot FILE  save the last frame as PNG on exit\n"
		"  --trace FILE[:N]   instruction trace of the first N instructions (default 50M),\n"
		"                     same format as the MAME fork's HS_TRACE\n"
		"  --cpi N            cycles per instruction (default 6, as MAME)\n"
		"  --log-uart         print the BIOS debug UART on stderr\n"
		"  -v                 verbose log\n");
	exit(2);
}

static int run_headless(uint64_t end_cycle, uint64_t frames)
{
	while (!g_trace_done && !g_quit)
	{
		uint64_t step = g_cpu.cycles + CPU_CLOCK / 1000;
		if (end_cycle && step > end_cycle) step = end_cycle;
		machine_run_until(step);
		if (end_cycle && g_cpu.cycles >= end_cycle) break;
		if (frames && g_frame_count >= frames) break;
	}
	return 0;
}

#ifdef HS_HAVE_SDL
static int run_window(uint64_t end_cycle, uint64_t frames)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Window *win = SDL_CreateWindow("HyperScanEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		640, 480, SDL_WINDOW_RESIZABLE);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderSetLogicalSize(ren, 640, 480);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);

	const uint64_t slice = CPU_CLOCK / 60;
	uint64_t t0 = SDL_GetPerformanceCounter(), c0 = g_cpu.cycles;
	const double freq = (double)SDL_GetPerformanceFrequency();
	while (!g_quit && !g_trace_done)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
			if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
				g_quit = 1;

		uint64_t target = g_cpu.cycles + slice;
		if (end_cycle && target > end_cycle) target = end_cycle;
		machine_run_until(target);

		const int w = screen_width(), h = screen_height();
		SDL_UpdateTexture(tex, NULL, g_frame, sizeof(g_frame[0]));
		SDL_Rect src = { 0, 0, w, h };
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, &src, NULL);
		SDL_RenderPresent(ren);

		/* real-time pacing on emulated time */
		const double emu_s = (double)(g_cpu.cycles - c0) / CPU_CLOCK;
		const double host_s = (double)(SDL_GetPerformanceCounter() - t0) / freq;
		if (emu_s > host_s) SDL_Delay((uint32_t)((emu_s - host_s) * 1000));

		if (end_cycle && g_cpu.cycles >= end_cycle) break;
		if (frames && g_frame_count >= frames) break;
	}
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
#endif

int main(int argc, char **argv)
{
	const char *bios = NULL, *shot = NULL, *trace = NULL;
	int headless = 0, cpi = 6;
	double seconds = 0;
	uint64_t frames = 0;

	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), (char *)NULL))
		if (!strcmp(a, "--bios")) bios = NEXT();
		else if (!strcmp(a, "--headless")) headless = 1;
		else if (!strcmp(a, "--seconds")) seconds = atof(NEXT());
		else if (!strcmp(a, "--frames")) frames = strtoull(NEXT(), NULL, 0);
		else if (!strcmp(a, "--screenshot")) shot = NEXT();
		else if (!strcmp(a, "--trace")) trace = NEXT();
		else if (!strcmp(a, "--cpi")) cpi = atoi(NEXT());
		else if (!strcmp(a, "--log-uart")) g_log_uart = 1;
		else if (!strcmp(a, "-v")) g_verbose = 1;
		else usage();
	}
	if (!bios || cpi < 1) usage();

	if (!machine_load_bios(bios))
	{
		fprintf(stderr, "cannot read BIOS %s\n", bios);
		return 1;
	}
	machine_reset();
	g_cpu.cpi = cpi;
	if (trace && !trace_open(trace))
	{
		fprintf(stderr, "cannot open trace %s\n", trace);
		return 1;
	}

	const uint64_t end_cycle = seconds > 0 ? machine_seconds_to_cycles(seconds) : 0;
	int rc;
#ifdef HS_HAVE_SDL
	if (!headless)
		rc = run_window(end_cycle, frames);
	else
#endif
	{
		if (!headless) fprintf(stderr, "built without SDL2: running headless\n");
		if (!end_cycle && !frames && !trace)
		{
			fprintf(stderr, "headless needs --seconds, --frames or --trace\n");
			return 2;
		}
		rc = run_headless(end_cycle, frames);
	}

	if (shot && !png_write(shot, &g_frame[0][0], 640, screen_width(), screen_height()))
		fprintf(stderr, "cannot write %s\n", shot);
	if (g_trace) fclose(g_trace);
	if (g_verbose)
		fprintf(stderr, "stopped at cycle %llu, frame %llu, pc %08x\n",
			(unsigned long long)g_cpu.cycles, (unsigned long long)g_frame_count, g_cpu.pc);
	return rc;
}
