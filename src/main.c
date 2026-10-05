/* HyperScanEmu - command line and frontend */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "emu.h"
#include "machine.h"
#include "devices.h"
#include "score7.h"
#include "trace.h"
#include "png.h"
#include "cdimg.h"
#include "input.h"
#include "spu.h"
#include "card.h"
#include "cards.h"
#include "ui.h"
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
		"  --card FILE        RFID card image (120 bytes) on the reader from the start\n"
		"  --cards-dir DIR    where cards placed from the F1 panel keep their saves\n"
		"                     (default: cards)\n"
		"  --headless         no window, no audio\n"
		"  --seconds S        stop after S emulated seconds\n"
		"  --frames N         stop after N video frames\n"
		"  --screenshot FILE  save the last frame as PNG on exit\n"
		"  --trace FILE[:N[:K[:FROM]]]\n"
		"                     trace of the first N instructions (default 50M), same format\n"
		"                     as the MAME fork's HS_TRACE; K > 0: sparse, a checkpoint every\n"
		"                     K instructions, instruction lines only from FROM on\n"
		"  --cpi N            cycles per instruction (overrides the preset)\n"
		"  --input SPEC       scripted presses \"T:button[:dur][@pad],...\" (seconds);\n"
		"                     buttons: green red yellow blue start select lshoulder\n"
		"                     rshoulder ltrigger rtrigger up down left right\n"
		"  --timing fast|mame fast (default): CPI 1, idle loops skipped; mame: as\n"
		"                     MAME (CPI 6), forced by --trace. F3 toggles\n"
		"  --cpi-at T:N       switch to N cycles per instruction at T seconds\n"
		"  --io-cycles N      extra cycles per I/O access (default 0, as MAME)\n"
		"  --field-vblank     PPU vblank per field in interlaced modes (MAME: per frame)\n"
		"  --mark PC          -v: count passes through PC (hex) per second\n"
		"  --log-uart         print the BIOS debug UART on stderr\n"
		"  -v                 verbose log\n");
	exit(2);
}

/* timing presets: "mame" reproduces MAME (flat CPI 6, a PPU vblank per frame);
 * "fast" = CPI 1, card pulses decoded by ratio (MAME's thresholds are tuned to CPI 6), idle
 * loops skipped; the PPU vblank stays per frame in interlaced modes, as in MAME. That gives
 * X-Men fights at 30 fps, the rate of captures from real consoles (2026-10-05); with a
 * vblank per field (--field-vblank) they would reach 60. */
static void set_timing(int fast)
{
	g_cpu.cpi = fast ? 1 : 6;
	g_field_vblank = 0;
	g_card_scaled = fast;
	g_cpu.idle_skip = fast;
}

/* --cpi-at T:N: switch the CPI at emulated time T (timing experiments) */
static uint64_t s_cpi_at_cycle = UINT64_MAX;
static int s_cpi_at;

static void timed_updates(void)
{
	input_script_update((double)g_cpu.cycles / CPU_CLOCK);
	if (g_cpu.cycles >= s_cpi_at_cycle)
	{
		g_cpu.cpi = s_cpi_at;
		s_cpi_at_cycle = UINT64_MAX;
	}
}

static int run_headless(uint64_t end_cycle, uint64_t frames)
{
	clock_t h0 = clock();
	uint64_t c0 = g_cpu.cycles, f0 = g_frame_count, m0 = g_mark_count;
	while (!g_trace_done && !g_quit)
	{
		if (g_verbose && g_cpu.cycles - c0 >= CPU_CLOCK)      /* -v: speed per emulated second */
		{
			const double host = (double)(clock() - h0) / CLOCKS_PER_SEC;
			fprintf(stderr, "t=%.0f s  speed %.0f%%  %llu vblanks/s  mark %llu/s\n", (double)g_cpu.cycles / CPU_CLOCK,
				host > 0 ? 100.0 * (g_cpu.cycles - c0) / CPU_CLOCK / host : 0.0, (unsigned long long)(g_frame_count - f0),
				(unsigned long long)(g_mark_count - m0));
			h0 = clock(); c0 = g_cpu.cycles; f0 = g_frame_count; m0 = g_mark_count;
		}
		uint64_t step = g_cpu.cycles + CPU_CLOCK / 1000;
		if (end_cycle && step > end_cycle) step = end_cycle;
		machine_run_until(step);
		timed_updates();
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

	SDL_Texture *otex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
	SDL_SetTextureBlendMode(otex, SDL_BLENDMODE_BLEND);
	static uint32_t overlay[480 * 640];
	SDL_StopTextInput();
	ui_toast("F1: cards");

	const uint64_t slice = CPU_CLOCK / 60;
	uint64_t t0 = SDL_GetPerformanceCounter(), c0 = g_cpu.cycles;
	const double freq = (double)SDL_GetPerformanceFrequency();
	while (!g_quit && !g_trace_done)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			if (ev.type == SDL_QUIT)
				g_quit = 1;
			else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F1)
			{
				ui_panel_toggle();
				if (ui_panel_open())
				{
					for (int b = 0; b < BTN_COUNT; b++) input_set(0, b, 0);   /* no stuck buttons */
					SDL_StartTextInput();
				}
				else
					SDL_StopTextInput();
			}
			else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F2)
				ui_key(UI_REMOVE);
			else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_F3)
			{
				const int fast = g_cpu.cpi == 6;
				set_timing(fast);
				ui_toast(fast ? "Timing: fast (CPI 1)" : "Timing: MAME (CPI 6, frame vblank)");
			}
			else if (ui_panel_open())
			{
				if (ev.type == SDL_TEXTINPUT)
					ui_text(ev.text.text);
				else if (ev.type == SDL_KEYDOWN)
				{
					static const struct { SDL_Keycode k; int ui; } map[] = {
						{ SDLK_UP, UI_UP }, { SDLK_DOWN, UI_DOWN }, { SDLK_PAGEUP, UI_PGUP },
						{ SDLK_PAGEDOWN, UI_PGDN }, { SDLK_HOME, UI_HOME }, { SDLK_END, UI_END },
						{ SDLK_RETURN, UI_ENTER }, { SDLK_KP_ENTER, UI_ENTER },
						{ SDLK_BACKSPACE, UI_BACKSPACE }, { SDLK_ESCAPE, UI_CLOSE }, { SDLK_TAB, UI_TAB },
					};
					for (int k = 0; k < (int)(sizeof(map) / sizeof(map[0])); k++)
						if (ev.key.keysym.sym == map[k].k) ui_key(map[k].ui);
					if (!ui_panel_open()) SDL_StopTextInput();
				}
			}
			else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
				g_quit = 1;
			else if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && !ev.key.repeat)
				for (int k = 0; k < (int)(sizeof(s_keymap) / sizeof(s_keymap[0])); k++)
					if (ev.key.keysym.scancode == s_keymap[k].sc)
						input_set(0, s_keymap[k].button, ev.type == SDL_KEYDOWN);
		}
		timed_updates();

		if (ui_panel_open())
		{
			/* paused: keep the pacing baseline with the wall clock */
			t0 = SDL_GetPerformanceCounter();
			c0 = g_cpu.cycles;
		}
		else if (audio)
		{
			/* the audio device is the clock: emulate in 4 ms steps until 60 ms of sound
			 * are queued (at most 100 ms of emulation per pass, so input stays live
			 * when the host cannot keep up) */
			const uint32_t low = SPU_OUT_RATE * 4 * 60 / 1000;
			for (int i = 0; i < 25 && SDL_GetQueuedAudioSize(audio) < low; i++)
			{
				uint64_t target = g_cpu.cycles + CPU_CLOCK / 250;
				if (end_cycle && target > end_cycle) target = end_cycle;
				machine_run_until(target);
				spu_sync();
				for (uint32_t n; (n = spu_take(abuf, 8192)) > 0; )
					SDL_QueueAudio(audio, abuf, n * 4);
				if (end_cycle && g_cpu.cycles >= end_cycle) break;
			}
			if (SDL_GetQueuedAudioSize(audio) >= low)
				SDL_Delay(1);
		}
		else
		{
			uint64_t target = g_cpu.cycles + slice;
			if (end_cycle && target > end_cycle) target = end_cycle;
			machine_run_until(target);
		}

		const int w = screen_width(), h = screen_height();
		SDL_UpdateTexture(tex, NULL, g_frame, sizeof(g_frame[0]));
		SDL_Rect src = { 0, 0, w, h };
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, &src, NULL);
		if (ui_render(overlay, (double)SDL_GetTicks() / 1000.0))
		{
			SDL_UpdateTexture(otex, NULL, overlay, 640 * 4);
			SDL_RenderCopy(ren, otex, NULL, NULL);
		}
		SDL_RenderPresent(ren);

		/* without audio: real-time pacing on emulated time */
		if (!audio)
		{
			const double emu_s = (double)(g_cpu.cycles - c0) / CPU_CLOCK;
			const double host_s = (double)(SDL_GetPerformanceCounter() - t0) / freq;
			if (emu_s > host_s) SDL_Delay((uint32_t)((emu_s - host_s) * 1000));
		}

		if (end_cycle && g_cpu.cycles >= end_cycle) break;
		if (frames && g_frame_count >= frames) break;
	}
	if (audio) SDL_CloseAudioDevice(audio);
	SDL_DestroyTexture(otex);
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
#endif

int main(int argc, char **argv)
{
	const char *bios = NULL, *shot = NULL, *trace = NULL, *cdpath = NULL, *cardpath = NULL, *cardsdir = NULL;
	int headless = 0, cpi = 0, io_cycles = 0, fast = 1;
	double seconds = 0;
	uint64_t frames = 0;

	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), (char *)NULL))
		if (!strcmp(a, "--bios")) bios = NEXT();
		else if (!strcmp(a, "--cd")) cdpath = NEXT();
		else if (!strcmp(a, "--card")) cardpath = NEXT();
		else if (!strcmp(a, "--cards-dir")) cardsdir = NEXT();
		else if (!strcmp(a, "--headless")) headless = 1;
		else if (!strcmp(a, "--seconds")) seconds = atof(NEXT());
		else if (!strcmp(a, "--frames")) frames = strtoull(NEXT(), NULL, 0);
		else if (!strcmp(a, "--screenshot")) shot = NEXT();
		else if (!strcmp(a, "--trace")) trace = NEXT();
		else if (!strcmp(a, "--cpi")) cpi = atoi(NEXT());
		else if (!strcmp(a, "--timing"))
		{
			const char *t = NEXT();
			if (!strcmp(t, "mame")) fast = 0;
			else if (!strcmp(t, "fast")) fast = 1;
			else usage();
		}
		else if (!strcmp(a, "--cpi-at"))
		{
			double t; int n;
			if (sscanf(NEXT(), "%lf:%d", &t, &n) != 2 || n < 1) usage();
			s_cpi_at_cycle = machine_seconds_to_cycles(t);
			s_cpi_at = n;
		}
		else if (!strcmp(a, "--io-cycles")) io_cycles = atoi(NEXT());
		else if (!strcmp(a, "--field-vblank")) g_field_vblank = 1;
		else if (!strcmp(a, "--mark")) g_mark_pc = (uint32_t)strtoul(NEXT(), NULL, 16);
		else if (!strcmp(a, "--input")) { if (!input_script_parse(NEXT())) usage(); }
		else if (!strcmp(a, "--log-uart")) g_log_uart = 1;
		else if (!strcmp(a, "-v")) g_verbose = 1;
		else usage();
	}
	if (!bios || cpi < 0) usage();

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
	if (cardsdir) cards_set_dir(cardsdir);
	ui_init(cards_detect_game(cd));
	if (cardpath && !card_load(cardpath))
	{
		fprintf(stderr, "cannot read card image %s (120 bytes expected)\n", cardpath);
		return 1;
	}
	if (trace) fast = 0;                     /* traces compare with MAME */
	set_timing(fast);
	if (cpi) g_cpu.cpi = cpi;
	g_cpu.io_cycles = io_cycles;
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
	{
		card_report(stderr);
		fprintf(stderr, "idle loop iterations skipped: %llu\n", (unsigned long long)g_cpu.idle_skipped);
	}
	if (g_verbose)
		fprintf(stderr, "stopped at cycle %llu, frame %llu, pc %08x\n",
			(unsigned long long)g_cpu.cycles, (unsigned long long)g_frame_count, g_cpu.pc);
	return rc;
}
