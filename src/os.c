/* Flick - host services (see os.h) */
#include <stdio.h>
#include <string.h>
#include "os.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <direct.h>
#ifdef HS_HAVE_SDL
#include <SDL.h>
#include <SDL_syswm.h>
#endif

void os_exe_dir(char *out, size_t n)
{
	char p[MAX_PATH];
	const DWORD l = GetModuleFileNameA(NULL, p, sizeof(p));
	char *slash = (l && l < sizeof(p)) ? strrchr(p, '\\') : NULL;
	if (!slash) { snprintf(out, n, "."); return; }
	*slash = 0;
	snprintf(out, n, "%s", p);
}

void os_mkdir(const char *path)
{
	_mkdir(path);
}

int os_pick_file(struct SDL_Window *win, int kind, const char *dir, char *out, size_t n)
{
	HWND owner = NULL;
	char initial[MAX_PATH];
#ifdef HS_HAVE_SDL
	SDL_SysWMinfo wm;
	SDL_VERSION(&wm.version);
	if (win && SDL_GetWindowWMInfo(win, &wm))
		owner = wm.info.win.window;
#else
	(void)win;
#endif
	OPENFILENAMEA ofn;
	memset(&ofn, 0, sizeof(ofn));
	out[0] = 0;
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	if (kind == OS_PICK_BIOS)
	{
		ofn.lpstrFilter = "HyperScan BIOS (*.bin)\0*.bin\0All files\0*.*\0";
		ofn.lpstrTitle = "Choose the HyperScan BIOS (hyperscan.bin)";
	}
	else
	{
		ofn.lpstrFilter = "HyperScan discs (*.cue, *.iso)\0*.cue;*.iso\0All files\0*.*\0";
		ofn.lpstrTitle = "Open a HyperScan disc";
	}
	if (dir && *dir)
	{
		snprintf(initial, sizeof(initial), "%s", dir);
		for (char *c = initial; *c; c++)
			if (*c == '/') *c = '\\';            /* the dialog wants native separators */
		ofn.lpstrInitialDir = initial;
	}
	ofn.lpstrFile = out;
	ofn.nMaxFile = (DWORD)n;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
	return GetOpenFileNameA(&ofn) != 0;
}

#else
#include <sys/stat.h>
#include <unistd.h>

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
