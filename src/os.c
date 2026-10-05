/* HyperScanEmu - host services (see os.h) */
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

int os_pick_disc(struct SDL_Window *win, char *out, size_t n)
{
	HWND owner = NULL;
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
	ofn.lpstrFilter = "HyperScan discs (*.cue, *.iso)\0*.cue;*.iso\0All files\0*.*\0";
	ofn.lpstrFile = out;
	ofn.nMaxFile = (DWORD)n;
	ofn.lpstrTitle = "Open a HyperScan disc";
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

/* no native dialog here yet: discs are dropped on the window */
int os_pick_disc(struct SDL_Window *win, char *out, size_t n)
{
	(void)win; (void)out; (void)n;
	return 0;
}
#endif
