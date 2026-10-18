/* Flick - host services (see os.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "os.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <direct.h>
#ifdef HS_HAVE_SDL
#include <SDL.h>
#include <SDL_syswm.h>
#endif

#define WPATH 4096

/* UTF-8 to UTF-16; 0 if s is not valid UTF-8 or does not fit */
static int widen(const char *s, wchar_t *w, int n)
{
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, w, n) > 0;
}

static void narrow(const wchar_t *w, char *s, size_t n)
{
	if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, s, (int)n, NULL, NULL) && n) s[0] = 0;
}

void os_utf8_args(int *argc, char ***argv)
{
	int n;
	wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n);
	if (!w) return;
	char **a = calloc((size_t)n + 1, sizeof(char *));
	for (int i = 0; a && i < n; i++)
	{
		const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL);
		if (!(a[i] = malloc(len > 0 ? len : 1))) return;
		narrow(w[i], a[i], len > 0 ? len : 1);
	}
	LocalFree(w);
	if (!a) return;
	*argc = n;
	*argv = a;                               /* kept for the whole run */
}

FILE *os_fopen(const char *path, const char *mode)
{
	wchar_t wp[WPATH], wm[16];
	/* not UTF-8 (a .cue written in the ANSI code page naming its .bin): as it is */
	if (!widen(path, wp, WPATH) || !widen(mode, wm, 16)) return fopen(path, mode);
	return _wfopen(wp, wm);
}

void os_exe_dir(char *out, size_t n)
{
	wchar_t p[WPATH];
	const DWORD l = GetModuleFileNameW(NULL, p, WPATH);
	wchar_t *slash = (l && l < WPATH) ? wcsrchr(p, L'\\') : NULL;
	if (!slash) { snprintf(out, n, "."); return; }
	*slash = 0;
	narrow(p, out, n);
}

static int std_handle_ok(DWORD which)
{
	const HANDLE h = GetStdHandle(which);
	return h && h != INVALID_HANDLE_VALUE;
}

int os_console_attach(void)
{
	const int out = std_handle_ok(STD_OUTPUT_HANDLE), err = std_handle_ok(STD_ERROR_HANDLE);
	if (out && err) return 1;                /* a console program, or redirected */
	if (!AttachConsole(ATTACH_PARENT_PROCESS)) return err;
	if (!out) freopen("CONOUT$", "w", stdout);
	if (!err) freopen("CONOUT$", "w", stderr);
	return 1;
}

void os_mkdir(const char *path)
{
	wchar_t wp[WPATH];
	if (widen(path, wp, WPATH)) _wmkdir(wp);
	else _mkdir(path);
}

int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n)
{
	HWND owner = NULL;
	wchar_t initial[WPATH], file[WPATH] = L"";
#ifdef HS_HAVE_SDL
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if (win && SDL_GetWindowWMInfo(win, &wm))
		owner = wm.info.win.window;
#else
	(void)win;
#endif
	OPENFILENAMEW ofn;
	memset(&ofn, 0, sizeof(ofn));
	out[0] = 0;
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	if (kind == OS_PICK_BIOS)
	{
		ofn.lpstrFilter = L"HyperScan BIOS (*.bin)\0*.bin\0All files\0*.*\0";
		ofn.lpstrTitle = L"Choose the HyperScan BIOS (hyperscan.bin)";
	}
	else
	{
		ofn.lpstrFilter = L"HyperScan discs (*.cue, *.iso)\0*.cue;*.iso\0All files\0*.*\0";
		ofn.lpstrTitle = L"Open a HyperScan disc";
	}
	if (dir && *dir && widen(dir, initial, WPATH))
	{
		for (wchar_t *c = initial; *c; c++)
			if (*c == L'/') *c = L'\\';          /* the dialog wants native separators */
		ofn.lpstrInitialDir = initial;
	}
	ofn.lpstrFile = file;
	ofn.nMaxFile = WPATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
	if (!GetOpenFileNameW(&ofn)) return 0;
	narrow(file, out, n);
	return out[0] != 0;
}

#else
#include <sys/stat.h>
#include <unistd.h>

void os_utf8_args(int *argc, char ***argv)
{
	(void)argc; (void)argv;                  /* already UTF-8 */
}

FILE *os_fopen(const char *path, const char *mode)
{
	return fopen(path, mode);
}

void os_exe_dir(char *out, size_t n)
{
	char p[4096];
	const ssize_t l = readlink("/proc/self/exe", p, sizeof(p) - 1);
	char *slash = NULL;
	if (l > 0) { p[l] = 0; slash = strrchr(p, '/'); }
	if (!slash) { snprintf(out, n, "."); return; }
	*slash = 0;
	snprintf(out, n, "%s", p);
}

int os_console_attach(void)
{
	return 1;
}

void os_mkdir(const char *path)
{
	mkdir(path, 0755);
}

/* no native dialog here yet: discs are dropped on the window, the BIOS given with --bios */
int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n)
{
	(void)win; (void)kind; (void)dir; (void)out; (void)n;
	return 0;
}
#endif
