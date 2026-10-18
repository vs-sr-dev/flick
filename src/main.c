/* Flick - command line and frontend */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "emu.h"
#include "machine.h"
#include "bus.h"
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
#include "toolbar.h"
#include "os.h"
#include "dump.h"
#include "config.h"
#ifdef HS_HAVE_SDL
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "controls.h"
#endif

#ifndef HS_VERSION                  /* set by CMake from project(VERSION) */
#define HS_VERSION "unknown"
#endif

int g_verbose;
static int s_error_box;              /* errors also in a message box: a window and no console */

/* an error for the user: on stderr, and in a message box when nobody would see stderr */
static void report(const char *fmt, ...)
{
	char m[1500];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(m, sizeof(m), fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s\n", m);
#ifdef HS_HAVE_SDL
	if (s_error_box) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Flick", m, NULL);
#endif
}

void hs_fatal(const char *fmt, ...)
{
	char m[1200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(m, sizeof(m), fmt, ap);
	va_end(ap);
	report("fatal: %s (cycle %llu)", m, (unsigned long long)g_cpu.cycles);
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
		"Flick " HS_VERSION " - Mattel HyperScan emulator\n"
		"usage: flick [options] [disc.cue|disc.iso]\n"
		"  with no options the window asks for the BIOS (on the first start) and a disc;\n"
		"  flick.ini next to the executable keeps the BIOS, volume, scale and timing\n"
		"  --bios FILE        HyperScan flash image (1 MB; default: the one in flick.ini)\n"
		"  --cd FILE          disc image: .cue (one BINARY file) or .iso\n"
		"  --card FILE        RFID card image (120 bytes) on the reader from the start\n"
		"  --quickload FILE   run a HYPER.EXE directly, as MAME's quickload (homebrew)\n"
		"  --card-at T:ID     place catalogue card ID (e.g. xmen001) on the reader at T s\n"
		"  --cards-dir DIR    where cards placed from the F1 panel keep their saves\n"
		"                     (default: the cards folder next to the executable)\n"
		"  --headless         no window, no audio\n"
		"  --seconds S        stop after S emulated seconds\n"
		"  --frames N         stop after N video frames\n"
		"  --screenshot FILE  save the last frame as PNG on exit\n"
		"  --wav FILE         record the sound output as WAV\n"
		"  --trace FILE[:N[:K[:FROM]]]\n"
		"                     trace of the first N instructions (default 50M), same format\n"
		"                     as the MAME fork's HS_TRACE; K > 0: sparse, a checkpoint every\n"
		"                     K instructions, instruction lines only from FROM on\n"
		"  --cpi N            cycles per instruction (overrides the preset)\n"
		"  --input SPEC       scripted presses \"T:button[:dur][@pad],...\" (seconds);\n"
		"                     buttons: green red yellow blue start select lshoulder\n"
		"                     rshoulder ltrigger rtrigger up down left right\n"
		"  --timing fast|mame fast (default): CPI 1, idle loops skipped; mame: as\n"
		"                     MAME (CPI 6), forced by --trace. F3 toggles; the window\n"
		"                     starts with the last preset chosen with F3\n"
		"  --cpi-at T:N       switch to N cycles per instruction at T seconds\n"
		"  --io-cycles N      extra cycles per I/O access (default 0, as MAME)\n"
		"  --field-vblank     PPU vblank per field in interlaced modes (MAME: per frame)\n"
		"  --mark PC          -v: count passes through PC (hex) per second\n"
		"  --log-uart         print the BIOS debug UART on stderr\n"
		"  -v                 verbose log\n"
		"  --version          print the version and exit\n");
	exit(2);
}

/* timing presets: "mame" reproduces MAME (flat CPI 6, a PPU vblank per frame);
 * "fast" = CPI 1, card pulses decoded by ratio (MAME's thresholds are tuned to CPI 6), idle
 * loops skipped; the PPU vblank stays per frame in interlaced modes, as in MAME. That gives
 * X-Men fights at 30 fps, the rate of captures from real consoles (2026-10-05); with a
 * vblank per field (--field-vblank) they would reach 60. "fast" also plays the BIOS boot
 * jingle (DAC FIFO set up in the BIOS's order, I2C clocked from the system clock). */
static void set_timing(int fast)
{
	g_cpu.cpi = fast ? 1 : 6;
	g_field_vblank = 0;
	g_card_scaled = fast;
	g_cpu.idle_skip = fast;
	g_spu_bios_dac = fast;
	g_i2c_sysclk = fast;
}

static cdimg *s_cd;                  /* the disc in the tray, NULL: none */

/* --quickload FILE: a HYPER.EXE loaded as MAME's quickload does it (homebrew such as hs-lba):
 * the file at 0xa00901fc, the CPU at its entry point 0xa0091000, the BIOS never runs.
 * Applied again after every reset. */
static char s_quickload[1024];

static int quickload_apply(void)
{
	if (!s_quickload[0]) return 1;
	FILE *f = os_fopen(s_quickload, "rb");
	if (!f) return 0;
	const size_t n = fread(&g_sdram[0x901fc], 1, SDRAM_SIZE - 0x901fc, f);
	fclose(f);
	g_cpu.pc = 0xa0091000;
	return n > 0;
}

/* --cpi-at T:N: switch the CPI at emulated time T (timing experiments) */
static uint64_t s_cpi_at_cycle = UINT64_MAX;
static int s_cpi_at;

/* --card-at T:ID: place a catalogue card on the reader at T seconds, as F1 does */
static uint64_t s_card_at_cycle = UINT64_MAX;
static const card_info *s_card_at;

static void timed_updates(void)
{
	input_script_update((double)g_cpu.cycles / CPU_CLOCK);
	if (g_cpu.cycles >= s_card_at_cycle)
	{
		cards_insert(s_card_at);
		s_card_at_cycle = UINT64_MAX;
	}
	if (g_cpu.cycles >= s_cpi_at_cycle)
	{
		g_cpu.cpi = s_cpi_at;
		s_cpi_at_cycle = UINT64_MAX;
	}
}

/* --wav FILE: the mixed output as a 16-bit stereo WAV (header rewritten on close) */
static FILE *s_wav;
static uint32_t s_wav_frames;

static void wav_header(void)
{
	const uint32_t data = s_wav_frames * 4;
	uint8_t h[44] = { 'R','I','F','F', 0,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
		0,0,0,0, 0,0,0,0, 4,0, 16,0, 'd','a','t','a', 0,0,0,0 };
	const uint32_t v[4][2] = { { 4, 36 + data }, { 24, SPU_OUT_RATE }, { 28, SPU_OUT_RATE * 4 }, { 40, data } };
	for (int i = 0; i < 4; i++)
		for (int b = 0; b < 4; b++) h[v[i][0] + b] = (uint8_t)(v[i][1] >> (8 * b));
	fseek(s_wav, 0, SEEK_SET);
	fwrite(h, 1, sizeof(h), s_wav);
	fseek(s_wav, 0, SEEK_END);
}

/* drain the mix into the WAV file, if one is open */
static void wav_write(const int16_t *buf, uint32_t n)
{
	if (!s_wav) return;
	fwrite(buf, 4, n, s_wav);          /* little-endian host */
	s_wav_frames += n;
}

static int run_headless(uint64_t end_cycle, uint64_t frames)
{
	static int16_t abuf[8192 * 2];
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
		if (s_wav)
		{
			spu_sync();
			for (uint32_t n; (n = spu_take(abuf, 8192)) > 0; )
				wav_write(abuf, n);
		}
		timed_updates();
		if (end_cycle && g_cpu.cycles >= end_cycle) break;
		if (frames && g_frame_count >= frames) break;
	}
	return 0;
}

#ifdef HS_HAVE_SDL
/* ---- console controls: toolbar, keys, files dropped on the window ---- */
static int s_paused, s_volume = 100, s_muted, s_scale = 1, s_fullscreen;
static int s_mouse_x = -1, s_mouse_y = -1;

static const char *base_name(const char *p)
{
	const char *b = p;
	for (const char *q = p; *q; q++)
		if (*q == '/' || *q == '\\') b = q + 1;
	return b;
}

/* the folder of a file path, without the separator */
static void dir_name(const char *p, char *out, size_t n)
{
	const size_t l = (size_t)(base_name(p) - p);
	snprintf(out, n, "%.*s", (int)(l ? l - 1 : 0), p);
}

static void window_title(SDL_Window *win, const char *path)
{
	char t[300];
	const int game = cards_detect_game(s_cd);
	if (game != CARD_ANY) snprintf(t, sizeof(t), "Flick - %s", cards_game_name(game));
	else if (path) snprintf(t, sizeof(t), "Flick - %s", base_name(path));
	else snprintf(t, sizeof(t), "Flick");
	SDL_SetWindowTitle(win, t);
}

/* power cycle: the disc, the card on the reader and the timing stay */
static void console_reset(void)
{
	const int cpi = g_cpu.cpi, idle = g_cpu.idle_skip, io = g_cpu.io_cycles;
	machine_reset(s_cd);
	quickload_apply();
	g_cpu.cpi = cpi;
	g_cpu.idle_skip = idle;
	g_cpu.io_cycles = io;
	ui_init(cards_detect_game(s_cd));
}

/* a new disc: the console restarts from the BIOS with it */
static void open_disc(SDL_Window *win, const char *path)
{
	char m[300];
	cdimg *cd = cdimg_open(path);
	if (!cd)
	{
		snprintf(m, sizeof(m), "Cannot open %s", base_name(path));
		ui_toast(m);
		return;
	}
	if (s_cd) cdimg_close(s_cd);
	s_cd = cd;
	console_reset();
	window_title(win, path);
	snprintf(m, sizeof(m), "Disc: %s", base_name(path));
	ui_toast(m);
	dir_name(path, g_config.disc_dir, sizeof(g_config.disc_dir));
}

static long file_size(const char *path)
{
	FILE *f = os_fopen(path, "rb");
	if (!f) return -1;
	fseek(f, 0, SEEK_END);
	const long n = ftell(f);
	fclose(f);
	return n;
}

/* first start, or the BIOS in flick.ini has gone: explain, then the file dialog until a
 * 1 MB image is chosen (0: cancelled). The choice goes in flick.ini. */
static int pick_bios(const char *missing)
{
	char m[1300], p[1024], dir[1024] = "";
	if (missing)
	{
		snprintf(m, sizeof(m), "Cannot read the HyperScan BIOS at\n%s\n\nPlease choose hyperscan.bin again.", missing);
		dir_name(missing, dir, sizeof(dir));
	}
	else
		snprintf(m, sizeof(m), "Flick needs the HyperScan BIOS: hyperscan.bin, the 1 MB image of the "
			"console's flash (the same file MAME uses).\n\nPlease choose it in the next window; Flick will remember it.");
	if (!os_can_pick())                          /* Linux without zenity or kdialog */
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Flick", "Flick needs the HyperScan BIOS "
			"(hyperscan.bin, the 1 MB flash image MAME uses).\n\nStart it once with\n"
			"    flick --bios /path/to/hyperscan.bin\nand it will remember it (or install zenity "
			"for a file dialog).", NULL);
		return 0;
	}
	for (;;)
	{
		SDL_ShowSimpleMessageBox(missing ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION, "Flick", m, NULL);
		if (!os_pick_file(NULL, OS_PICK_BIOS, dir, p, sizeof(p))) return 0;
		if (file_size(p) == FLASH_SIZE && machine_load_bios(p)) break;
		snprintf(m, sizeof(m), "%s is not a HyperScan BIOS: the flash image is exactly 1 MB "
			"(1048576 bytes).", base_name(p));
		missing = p;
		dir_name(p, dir, sizeof(dir));
	}
	snprintf(g_config.bios, sizeof(g_config.bios), "%s", p);
	return 1;
}

/* screenshots/hyperscan-YYYYMMDD-HHMMSS[-n].png in the data folder (os_data_dir) */
static void screenshot(void)
{
	char dir[1024], name[64], path[1200], m[1300];
	os_data_dir(dir, sizeof(dir));
	snprintf(path, sizeof(path), "%s/screenshots", dir);
	os_mkdir(path);
	const time_t t = time(NULL);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&t));
	for (int n = 1; ; n++)
	{
		if (n == 1) snprintf(name, sizeof(name), "hyperscan-%s.png", stamp);
		else snprintf(name, sizeof(name), "hyperscan-%s-%d.png", stamp, n);
		snprintf(path, sizeof(path), "%s/screenshots/%s", dir, name);
		FILE *f = os_fopen(path, "rb");
		if (!f) break;
		fclose(f);
	}
	if (png_write(path, &g_frame[0][0], 640, screen_width(), screen_height()))
		snprintf(m, sizeof(m), "Saved screenshots/%s", name);
	else
		snprintf(m, sizeof(m), "Cannot write %s", path);
	ui_toast(m);
}

/* Ctrl+F12: debug/hyperscan-YYYYMMDD-HHMMSS/ in the data folder, for --render-dump */
static void dump_now(void)
{
	char dir[1024], path[1200], m[1300];
	os_data_dir(dir, sizeof(dir));
	snprintf(path, sizeof(path), "%s/debug", dir);
	os_mkdir(path);
	const time_t t = time(NULL);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&t));
	snprintf(path, sizeof(path), "%s/debug/hyperscan-%s", dir, stamp);
	os_mkdir(path);
	if (debug_dump(path)) snprintf(m, sizeof(m), "Debug dump: debug/hyperscan-%s", stamp);
	else snprintf(m, sizeof(m), "Cannot write %s", path);
	ui_toast(m);
}

static void release_pad(void)
{
	input_release_all();
}

/* ---- game controllers: the first one is player 1, the second player 2 ---- */
static SDL_GameController *s_ctrl[2];
static int s_ctrl_hold[2];           /* ignore the buttons until released (panel closed by A) */
static int s_kbd_pad;                /* the player the keyboard plays */

/* the keyboard takes the first player without a controller (player 1 if both have one,
 * alongside the first pad); a change is appended to the message m */
static void keyboard_assign(char *m, size_t n)
{
	const int pad = !s_ctrl[0] ? 0 : !s_ctrl[1] ? 1 : 0;
	if (pad == s_kbd_pad) return;
	input_release_keys();
	s_kbd_pad = pad;
	const size_t l = strlen(m);
	snprintf(m + l, n - l, ", keyboard: player %d", pad + 1);
}

static void controller_added(int device)
{
	char m[200];
	for (int p = 0; p < 2; p++)
		if (!s_ctrl[p])
		{
			if (!(s_ctrl[p] = SDL_GameControllerOpen(device))) return;
			snprintf(m, sizeof(m), "Player %d: %s", p + 1, SDL_GameControllerName(s_ctrl[p]));
			keyboard_assign(m, sizeof(m));
			ui_toast(m);
			hs_log("%s\n", m);
			return;
		}
}

static void controller_removed(SDL_JoystickID id)
{
	char m[200];
	for (int p = 0; p < 2; p++)
		if (s_ctrl[p] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_ctrl[p])) == id)
		{
			SDL_GameControllerClose(s_ctrl[p]);
			s_ctrl[p] = NULL;
			input_joy(p, 0, 0, 0);
			snprintf(m, sizeof(m), "Player %d: controller disconnected", p + 1);
			keyboard_assign(m, sizeof(m));
			ui_toast(m);
		}
}

/* the HyperScan buttons from each controller (bindings in controls.c), the stick as is */
static void controllers_poll(void)
{
	for (int p = 0; p < 2; p++)
	{
		SDL_GameController *c = s_ctrl[p];
		if (!c) continue;
		uint32_t btn = ctl_pad_buttons(c);
		if (s_ctrl_hold[p] && btn) btn = 0;
		else s_ctrl_hold[p] = 0;
		input_joy(p, btn, SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX),
			SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
	}
}

/* the card panel from a controller: d-pad, shoulders to page, A to place, B to close */
static void controller_panel(int button)
{
	static const struct { int b, ui; } map[] = {
		{ SDL_CONTROLLER_BUTTON_DPAD_UP, UI_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, UI_DOWN },
		{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, UI_PGUP }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, UI_PGDN },
		{ SDL_CONTROLLER_BUTTON_A, UI_ENTER }, { SDL_CONTROLLER_BUTTON_B, UI_CLOSE },
	};
	for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
		if (button == map[i].b) ui_key(map[i].ui);
	if (!ui_panel_open())
	{
		SDL_StopTextInput();
		s_ctrl_hold[0] = s_ctrl_hold[1] = 1;
	}
}

static void toggle_panel(void)
{
	ui_panel_toggle();
	if (ui_panel_open())
	{
		release_pad();                       /* no stuck buttons */
		SDL_StartTextInput();
	}
	else
		SDL_StopTextInput();
}

/* a panel (cards or controls) is open: the console stops and the toolbar hides */
static int panel_open(void)
{
	return ui_panel_open() || ctl_open();
}

/* the largest whole multiple of 640x480 that fits the window's display */
static int max_scale(SDL_Window *win)
{
	SDL_Rect r;
	const int d = SDL_GetWindowDisplayIndex(win);
	if (d < 0 || SDL_GetDisplayUsableBounds(d, &r) != 0) return 1;
	int k = r.w / 640;
	if ((r.h - 40) / 480 < k) k = (r.h - 40) / 480;   /* room for the title bar */
	return k < 1 ? 1 : k;
}

/* window at k times 640x480 (at most what fits), centred on its display */
static void set_scale(SDL_Window *win, int k)
{
	const int d = SDL_GetWindowDisplayIndex(win);
	s_scale = k > max_scale(win) ? max_scale(win) : k;
	SDL_SetWindowSize(win, 640 * s_scale, 480 * s_scale);
	SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED_DISPLAY(d), SDL_WINDOWPOS_CENTERED_DISPLAY(d));
}

static void command(int cmd, int value, SDL_Window *win, SDL_AudioDeviceID audio)
{
	switch (cmd)
	{
	case TB_DISC:
	{
		char p[1024];
		if (os_pick_file(win, OS_PICK_DISC, g_config.disc_dir, p, sizeof(p)))
			open_disc(win, p);
		else if (!os_can_pick())
			ui_toast("Drop a .cue or .iso file on the window");
		break;
	}
	case TB_CARDS:
		if (ctl_open()) ctl_toggle();
		toggle_panel();
		break;
	case TB_CONTROLS:
		if (ui_panel_open()) toggle_panel();
		ctl_toggle();
		if (ctl_open()) release_pad();
		break;
	case TB_CARD_OFF: ui_key(UI_REMOVE); break;
	case TB_PAUSE:
		s_paused = !s_paused;
		if (s_paused)
		{
			release_pad();
			if (audio) SDL_ClearQueuedAudio(audio);
		}
		break;
	case TB_RESET:
		console_reset();
		ui_toast("Reset");
		break;
	case TB_TIMING:
	{
		const int fast = g_cpu.cpi == 6;
		set_timing(fast);
		g_config.fast = fast;
		ui_toast(fast ? "Timing: fast (CPI 1)" : "Timing: MAME (CPI 6, frame vblank)");
		break;
	}
	case TB_SHOT: screenshot(); break;
	case TB_SCALE:
		if (s_fullscreen) break;
		set_scale(win, s_scale % max_scale(win) + 1);
		g_config.scale = s_scale;
		break;
	case TB_FULLSCREEN:
		s_fullscreen = !s_fullscreen;
		SDL_SetWindowFullscreen(win, s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		break;
	case TB_MUTE: s_muted = !s_muted; break;
	case TB_VOLUME:
		s_volume = g_config.volume = value;
		if (value > 0) s_muted = 0;
		break;
	}
}

static void key_event(const SDL_KeyboardEvent *k, SDL_Window *win, SDL_AudioDeviceID audio)
{
	const int down = k->type == SDL_KEYDOWN;
	const SDL_Keycode sym = k->keysym.sym;
	const int alt = (k->keysym.mod & KMOD_ALT) != 0, ctrl = (k->keysym.mod & KMOD_CTRL) != 0;

	if (ctl_open())                              /* the controls panel takes every key */
	{
		ctl_key(k);
		return;
	}
	if (down && !k->repeat)
	{
		int cmd = TB_NONE;
		if (sym == SDLK_F11 || (sym == SDLK_RETURN && alt)) cmd = TB_FULLSCREEN;
		else if (sym == SDLK_o && ctrl) cmd = TB_DISC;
		else if (sym == SDLK_F1) cmd = TB_CARDS;
		else if (sym == SDLK_F2) cmd = TB_CARD_OFF;
		else if (sym == SDLK_F3) cmd = TB_TIMING;
		else if (sym == SDLK_F4) cmd = TB_CONTROLS;
		else if (sym == SDLK_F5) cmd = TB_RESET;
		else if (sym == SDLK_F12 && ctrl) { dump_now(); return; }
		else if (sym == SDLK_F12) cmd = TB_SHOT;
		else if (sym == SDLK_PAUSE || (sym == SDLK_p && !panel_open())) cmd = TB_PAUSE;
		if (cmd != TB_NONE)
		{
			command(cmd, 0, win, audio);
			return;
		}
	}
	if (sym == SDLK_RETURN && alt) return;      /* not Start */

	if (ui_panel_open())
	{
		static const struct { SDL_Keycode k; int ui; } map[] = {
			{ SDLK_UP, UI_UP }, { SDLK_DOWN, UI_DOWN }, { SDLK_PAGEUP, UI_PGUP },
			{ SDLK_PAGEDOWN, UI_PGDN }, { SDLK_HOME, UI_HOME }, { SDLK_END, UI_END },
			{ SDLK_RETURN, UI_ENTER }, { SDLK_KP_ENTER, UI_ENTER },
			{ SDLK_BACKSPACE, UI_BACKSPACE }, { SDLK_ESCAPE, UI_CLOSE }, { SDLK_TAB, UI_TAB },
		};
		if (!down) return;
		for (int i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
			if (sym == map[i].k) ui_key(map[i].ui);
		if (!ui_panel_open()) SDL_StopTextInput();
		return;
	}
	if (down && sym == SDLK_ESCAPE)
	{
		if (s_fullscreen) command(TB_FULLSCREEN, 0, win, audio);
		else g_quit = 1;
		return;
	}
	if (k->repeat || s_paused) return;
	const int button = ctl_key_button(k->keysym.scancode);
	if (button >= 0) input_set(s_kbd_pad, button, down);
}

static tb_state toolbar_state(void)
{
	tb_state st;
	st.paused = s_paused;
	st.fast = g_cpu.cpi != 6;
	st.fullscreen = s_fullscreen;
	st.scale = s_scale;
	st.volume = s_volume;
	st.muted = s_muted;
	st.card_on = cards_current() != NULL || card_loaded();
	return st;
}

static int run_window(uint64_t end_cycle, uint64_t frames, const char *cdpath, int ask_disc)
{
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0)
	{
		report("SDL_Init: %s", SDL_GetError());
		return 1;
	}
	SDL_Window *win = SDL_CreateWindow("Flick", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		640, 480, SDL_WINDOW_RESIZABLE);
	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderSetLogicalSize(ren, 640, 480);     /* also maps mouse events to 640x480 */
	SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
	window_title(win, cdpath);
	s_volume = g_config.volume;
	ctl_init();
	if (g_config.scale > 1) set_scale(win, g_config.scale);
	if (ask_disc)                                /* started with no disc: offer one */
	{
		SDL_RenderClear(ren);
		SDL_RenderPresent(ren);
		command(TB_DISC, 0, win, 0);
	}

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
	tb_show((double)SDL_GetTicks() / 1000.0);
	int cursor = 1;

	const uint64_t slice = CPU_CLOCK / 60;
	uint64_t t0 = SDL_GetPerformanceCounter(), c0 = g_cpu.cycles;
	const double freq = (double)SDL_GetPerformanceFrequency();
	while (!g_quit && !g_trace_done)
	{
		SDL_Event ev;
		while (SDL_PollEvent(&ev))
		{
			const double now = (double)SDL_GetTicks() / 1000.0;
			const tb_state st = toolbar_state();
			int value = 0, cmd = TB_NONE;
			switch (ev.type)
			{
			case SDL_QUIT:
				g_quit = 1;
				break;
			case SDL_DROPFILE:
				open_disc(win, ev.drop.file);
				SDL_free(ev.drop.file);
				break;
			case SDL_WINDOWEVENT:
				if (ev.window.event == SDL_WINDOWEVENT_LEAVE) tb_leave();
				break;
			case SDL_MOUSEMOTION:
				s_mouse_x = ev.motion.x;
				s_mouse_y = ev.motion.y;
				if (tb_mouse(s_mouse_x, s_mouse_y, now, &value) == TB_VOLUME)
					command(TB_VOLUME, value, win, audio);
				break;
			case SDL_MOUSEBUTTONDOWN:
				if (ev.button.button != SDL_BUTTON_LEFT || ui_panel_open()) break;
				if (ctl_open())
				{
					ctl_click(ev.button.x, ev.button.y);
					break;
				}
				/* a click on a hidden toolbar only shows it */
				if (tb_visible(&st, now)) cmd = tb_press(ev.button.x, ev.button.y, now, &value);
				else tb_mouse(ev.button.x, ev.button.y, now, &value);
				if (cmd != TB_NONE) command(cmd, value, win, audio);
				break;
			case SDL_MOUSEBUTTONUP:
				if (ev.button.button == SDL_BUTTON_LEFT) tb_release();
				break;
			case SDL_MOUSEWHEEL:
				if (tb_visible(&st, now) && tb_wheel(s_mouse_x, s_mouse_y, ev.wheel.y, s_volume, &value) == TB_VOLUME)
					command(TB_VOLUME, value, win, audio);
				break;
			case SDL_CONTROLLERDEVICEADDED:
				controller_added(ev.cdevice.which);
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				controller_removed(ev.cdevice.which);
				break;
			case SDL_CONTROLLERBUTTONDOWN:
				if (ui_panel_open()) controller_panel(ev.cbutton.button);
				else if (ctl_open() && ctl_pad_button(ev.cbutton.button)) s_ctrl_hold[0] = s_ctrl_hold[1] = 1;
				break;
			case SDL_CONTROLLERAXISMOTION:
				if (ctl_open()) ctl_pad_axis(ev.caxis.axis, ev.caxis.value);
				break;
			case SDL_TEXTINPUT:
				if (ui_panel_open()) ui_text(ev.text.text);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				key_event(&ev.key, win, audio);
				break;
			}
		}
		timed_updates();
		if (!panel_open() && !s_paused) controllers_poll();

		if (panel_open() || s_paused)
		{
			/* stopped: keep the pacing baseline with the wall clock */
			t0 = SDL_GetPerformanceCounter();
			c0 = g_cpu.cycles;
		}
		else if (audio)
		{
			/* the audio device is the clock: emulate in 4 ms steps until 60 ms of sound
			 * are queued (at most 100 ms of emulation per pass, so input stays live
			 * when the host cannot keep up) */
			const uint32_t low = SPU_OUT_RATE * 4 * 60 / 1000;
			const int gain = s_muted ? 0 : s_volume;
			for (int i = 0; i < 25 && SDL_GetQueuedAudioSize(audio) < low; i++)
			{
				uint64_t target = g_cpu.cycles + CPU_CLOCK / 250;
				if (end_cycle && target > end_cycle) target = end_cycle;
				machine_run_until(target);
				spu_sync();
				for (uint32_t n; (n = spu_take(abuf, 8192)) > 0; )
				{
					wav_write(abuf, n);
					if (gain != 100)
						for (uint32_t s = 0; s < n * 2; s++) abuf[s] = (int16_t)(abuf[s] * gain / 100);
					SDL_QueueAudio(audio, abuf, n * 4);
				}
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

		const double now = (double)SDL_GetTicks() / 1000.0;
		const tb_state st = toolbar_state();
		const int w = screen_width(), h = screen_height();
		SDL_UpdateTexture(tex, NULL, g_frame, sizeof(g_frame[0]));
		SDL_Rect src = { 0, 0, w, h };
		SDL_RenderClear(ren);
		SDL_RenderCopy(ren, tex, &src, NULL);
		int drawn = ui_render(overlay, now);
		drawn |= ctl_render(overlay, now);
		if (!panel_open()) drawn |= tb_render(overlay, &st, now);
		if (drawn)
		{
			SDL_UpdateTexture(otex, NULL, overlay, 640 * 4);
			SDL_RenderCopy(ren, otex, NULL, NULL);
		}
		SDL_RenderPresent(ren);

		/* full screen: the pointer hides with the toolbar */
		const int want_cursor = !s_fullscreen || tb_visible(&st, now);
		if (want_cursor != cursor)
		{
			SDL_ShowCursor(want_cursor ? SDL_ENABLE : SDL_DISABLE);
			cursor = want_cursor;
		}

		/* without audio: real-time pacing on emulated time */
		if (!audio && !s_paused && !panel_open())
		{
			const double emu_s = (double)(g_cpu.cycles - c0) / CPU_CLOCK;
			const double host_s = (double)(SDL_GetPerformanceCounter() - t0) / freq;
			if (emu_s > host_s) SDL_Delay((uint32_t)((emu_s - host_s) * 1000));
		}

		if (end_cycle && g_cpu.cycles >= end_cycle) break;
		if (frames && g_frame_count >= frames) break;
	}
	if (audio) SDL_CloseAudioDevice(audio);
	for (int p = 0; p < 2; p++)
		if (s_ctrl[p]) SDL_GameControllerClose(s_ctrl[p]);
	SDL_DestroyTexture(otex);
	SDL_DestroyTexture(tex);
	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
	if (!config_save()) fprintf(stderr, "cannot write flick.ini next to the executable\n");
	return 0;
}
#endif

int main(int argc, char **argv)
{
	const char *bios = NULL, *shot = NULL, *wav = NULL, *render_dump = NULL, *dump_dir = NULL, *trace = NULL, *cdpath = NULL, *cardpath = NULL, *cardsdir = NULL;
	int headless = 0, cpi = 0, io_cycles = 0, fast = -1, field_vblank = 0;
	double seconds = 0;
	uint64_t frames = 0;
	const int console = os_console_attach();
	os_utf8_args(&argc, &argv);                 /* paths are UTF-8 from here on */

	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
#define NEXT() (i + 1 < argc ? argv[++i] : (usage(), (char *)NULL))
		if (!strcmp(a, "--bios")) bios = NEXT();
		else if (!strcmp(a, "--cd")) cdpath = NEXT();
		else if (!strcmp(a, "--card")) cardpath = NEXT();
		else if (!strcmp(a, "--quickload")) snprintf(s_quickload, sizeof(s_quickload), "%s", NEXT());
		else if (!strcmp(a, "--cards-dir")) cardsdir = NEXT();
		else if (!strcmp(a, "--card-at"))
		{
			char id[32];
			double t;
			if (sscanf(NEXT(), "%lf:%31s", &t, id) != 2) usage();
			for (int k = 0; k < g_card_count; k++)
				if (!strcmp(g_card_list[k].id, id)) s_card_at = &g_card_list[k];
			if (!s_card_at) { fprintf(stderr, "unknown card %s (e.g. xmen001)\n", id); return 1; }
			s_card_at_cycle = machine_seconds_to_cycles(t);
		}
		else if (!strcmp(a, "--headless")) headless = 1;
		else if (!strcmp(a, "--seconds")) seconds = atof(NEXT());
		else if (!strcmp(a, "--frames")) frames = strtoull(NEXT(), NULL, 0);
		else if (!strcmp(a, "--screenshot")) shot = NEXT();
		else if (!strcmp(a, "--wav")) wav = NEXT();
		else if (!strcmp(a, "--render-dump")) render_dump = NEXT();
		else if (!strcmp(a, "--debug-dump")) dump_dir = NEXT();
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
		else if (!strcmp(a, "--field-vblank")) field_vblank = 1;
		else if (!strcmp(a, "--mark")) g_mark_pc = (uint32_t)strtoul(NEXT(), NULL, 16);
		else if (!strcmp(a, "--input")) { if (!input_script_parse(NEXT())) usage(); }
		else if (!strcmp(a, "--log-uart")) g_log_uart = 1;
		else if (!strcmp(a, "-v")) g_verbose = 1;
		else if (!strcmp(a, "--version")) { printf("Flick %s\n", HS_VERSION); return 0; }
		else if (a[0] != '-' && !cdpath) cdpath = a;     /* "Open with", a disc dropped on the exe */
		else usage();
	}
	if (render_dump)                        /* re-render a Ctrl+F12 dump, no BIOS needed */
	{
		char out[1200];
		machine_reset(NULL);
		if (!debug_render(render_dump))
		{
			fprintf(stderr, "cannot read the dump in %s\n", render_dump);
			return 1;
		}
		snprintf(out, sizeof(out), "%s/render.png", render_dump);
		if (!png_write(shot ? shot : out, &g_frame[0][0], 640, screen_width(), screen_height()))
			fprintf(stderr, "cannot write %s\n", shot ? shot : out);
		return 0;
	}
	if (cpi < 0) usage();
	s_error_box = !headless && !console;

	/* the BIOS: --bios, else the one in flick.ini, else (window) ask for it */
	config_load();
	const int bios_given = bios != NULL;
	if (!bios && g_config.bios[0]) bios = g_config.bios;
	if (!bios || !machine_load_bios(bios))
	{
		int ok = 0;
#ifdef HS_HAVE_SDL
		if (!headless && !bios_given) ok = pick_bios(bios);
#endif
		if (!ok)
		{
			if (!bios) usage();
			report("cannot read BIOS %s%s", bios, bios_given ? "" : " (from flick.ini)");
			return 1;
		}
	}
	if (fast < 0) fast = headless ? 1 : g_config.fast;   /* scripts get the same default every time */
	if (cdpath && !(s_cd = cdimg_open(cdpath)))
	{
		report("cannot open disc image %s (-v for details)", cdpath);
		return 1;
	}
	machine_reset(s_cd);
	if (!quickload_apply())
	{
		report("cannot read %s", s_quickload);
		return 1;
	}
	if (cardsdir)
		cards_set_dir(cardsdir);
	else
	{
		char dir[1024], cards[1100];
		os_data_dir(dir, sizeof(dir));
		snprintf(cards, sizeof(cards), "%s/cards", dir);
		cards_set_dir(cards);
	}
	ui_init(cards_detect_game(s_cd));
	if (cardpath && !card_load(cardpath))
	{
		report("cannot read card image %s (120 bytes expected)", cardpath);
		return 1;
	}
	if (trace) fast = 0;                     /* traces compare with MAME */
	set_timing(fast);
	if (field_vblank) g_field_vblank = 1;
	if (cpi) g_cpu.cpi = cpi;
	g_cpu.io_cycles = io_cycles;
	if (trace && !trace_open(trace))
	{
		report("cannot open trace %s", trace);
		return 1;
	}

	if (wav)
	{
		if (!(s_wav = os_fopen(wav, "wb")))
		{
			report("cannot write %s", wav);
			return 1;
		}
		wav_header();
		g_spu_output_enabled = 1;
	}

	const uint64_t end_cycle = seconds > 0 ? machine_seconds_to_cycles(seconds) : 0;
	int rc;
#ifdef HS_HAVE_SDL
	if (!headless)
		rc = run_window(end_cycle, frames, cdpath, !cdpath && !bios_given);
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
	if (dump_dir)
	{
		os_mkdir(dump_dir);
		if (!debug_dump(dump_dir)) fprintf(stderr, "cannot write the dump in %s\n", dump_dir);
	}
	if (s_wav)
	{
		wav_header();
		fclose(s_wav);
	}
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
