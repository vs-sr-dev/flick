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
#include "cdimg.h"
#include "input.h"
#include "spu.h"
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
		"  --cd FILE          disc image: .cue (one BINARY file) or .iso\n"
		"  --headless         no window, no audio\n"
		"  --seconds S        stop after S emulated seconds\n"
		"  --frames N         stop after N video frames\n"
		"  --screenshot FILE  save the last frame as PNG on exit\n"
		"  --trace FILE[:N[:K[:FROM]]]\n"
		"                     trace of the first N instructions (default 50M), same format\n"
		"                     as the MAME fork's HS_TRACE; K > 0: sparse, a checkpoint every\n"
		"                     K instructions, instruction lines only from FROM on\n"
		"  --cpi N            cycles per instruction (default 6, as MAME)\n"
		"  --input SPEC       scripted presses \"T:button[:dur][@pad],...\" (seconds);\n"
		"                     buttons: green red yellow blue start select lshoulder\n"
		"                     rshoulder ltrigger rtrigger up down left right\n"
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
		input_script_update((double)g_cpu.cycles / CPU_CLOCK);
		if (end_cycle && g_cpu.cycles >= end_cycle) break;
		if (frames && g_frame_count >= frames) break;
	}
	return 0;
}

#ifdef HS_HAVE_SDL
static const struct { SDL_Scancode sc; int button; } s_keymap[] = {
	{ SDL_SCANCODE_UP, BTN_UP }, { SDL_SCANCODE_DOWN, BTN_DOWN },
	{ SDL_SCANCODE_LEFT, BTN_LEFT }, { SDL_SCANCODE_RIGHT, BTN_RIGHT },
	{ SDL_SCANCODE_Z, BTN_GREEN }, { SDL_SCANCODE_X, BTN_RED },
	{ SDL_SCANCODE_C, BTN_YELLOW }, { SDL_SCANCODE_V, BTN_BLUE },
	{ SDL_SCANCODE_RETURN, BTN_START }, { SDL_SCANCODE_BACKSPACE, BTN_SELECT },
	{ SDL_SCANCODE_A, BTN_LSHOULDER }, { SDL_SCANCODE_S, BTN_RSHOULDER },
	{ SDL_SCANCODE_Q, BTN_LTRIGGER }, { SDL_SCANCODE_W, BTN_RTRIGGER },
};

static int run_window(uint64_t end_cycle, uint64_t frames)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0)
	{
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Window *win = SDL_CreateWindow("HyperScanEmu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		640, 480, SDL_WINDOW_RESIZABLE);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderSetLogicalSize(ren, 640, 480);
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);

	SDL_AudioSpec want = { 0 }, have;
	want.freq = SPU_OUT_RATE;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audio)
	{
		g_spu_output_enabled = 1;
		SDL_PauseAudioDevice(audio, 0);
	}
	else
		fprintf(stderr, "no audio: %s\n", SDL_GetError());
	static int16_t abuf[8192 * 2];

	const uint64_t slice = CPU_CLOCK / 60;
	uint64_t t0 = SDL_GetPerformanceCounter(), c0 = g_cpu.cycles;
	const double freq = (double)SDL_GetPerformanceFrequency();
	while (!g_quit && !g_trace_done)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE))
				g_quit = 1;
			if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat)
				for (int k = 0; k < (int)(sizeof(s_keymap) / sizeof(s_keymap[0])); k++)
					if (ev.key.keysym.scancode == s_keymap[k].sc)
						input_set(0, s_keymap[k].button, ev.type == SDL_KEYDOWN);
		}
		input_script_update((double)g_cpu.cycles / CPU_CLOCK);

		uint64_t target = g_cpu.cycles + slice;
		if (end_cycle && target > end_cycle) target = end_cycle;
		machine_run_until(target);

		/* audio: queue what the SPU mixed, keeping at most ~150 ms of latency */
		for (uint32_t n; (n = spu_take(abuf, 8192)) > 0; )
			if (audio && SDL_GetQueuedAudioSize(audio) < SPU_OUT_RATE * 4 * 15 / 100)
				SDL_QueueAudio(audio, abuf, n * 4);

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
	if (audio) SDL_CloseAudioDevice(audio);
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
#endif

int main(int argc, char **argv)
{
	const char *bios = NULL, *shot = NULL, *trace = NULL, *cdpath = NULL;
	int headless = 0, cpi = 6;
	double seconds = 0;
	uint64_t frames = 0;

	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), (char *)NULL))
		if (!strcmp(a, "--bios")) bios = NEXT();
		else if (!strcmp(a, "--cd")) cdpath = NEXT();
		else if (!strcmp(a, "--headless")) headless = 1;
		else if (!strcmp(a, "--seconds")) seconds = atof(NEXT());
		else if (!strcmp(a, "--frames")) frames = strtoull(NEXT(), NULL, 0);
		else if (!strcmp(a, "--screenshot")) shot = NEXT();
		else if (!strcmp(a, "--trace")) trace = NEXT();
		else if (!strcmp(a, "--cpi")) cpi = atoi(NEXT());
		else if (!strcmp(a, "--input")) { if (!input_script_parse(NEXT())) usage(); }
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
	cdimg *cd = NULL;
	if (cdpath && !(cd = cdimg_open(cdpath)))
	{
		fprintf(stderr, "cannot open disc image %s (-v for details)\n", cdpath);
		return 1;
	}
	machine_reset(cd);
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
