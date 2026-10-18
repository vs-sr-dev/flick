/* Flick - settings file (see config.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "os.h"

config g_config = { .volume = 100, .scale = 1, .fast = 1 };

static void config_path(char *out, size_t n)
{
	char dir[1024];
	os_config_dir(dir, sizeof(dir));
	snprintf(out, n, "%s/flick.ini", dir);
}

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	char *e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
	*e = 0;
	return s;
}

static int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

int config_load(void)
{
	char path[1100], line[1200];
	config_path(path, sizeof(path));
	FILE *f = os_fopen(path, "r");
	if (!f) return 0;
	while (fgets(line, sizeof(line), f))
	{
		char *s = trim(line), *eq = strchr(s, '=');
		if (*s == '#' || *s == ';' || !eq) continue;
		*eq = 0;
		const char *key = trim(s), *val = trim(eq + 1);
		if (!strcmp(key, "bios")) snprintf(g_config.bios, sizeof(g_config.bios), "%s", val);
		else if (!strcmp(key, "disc_folder")) snprintf(g_config.disc_dir, sizeof(g_config.disc_dir), "%s", val);
		else if (!strcmp(key, "volume")) g_config.volume = clamp(atoi(val), 0, 100);
		else if (!strcmp(key, "scale")) g_config.scale = clamp(atoi(val), 1, 8);
		else if (!strcmp(key, "timing")) g_config.fast = strcmp(val, "mame") != 0;
		else if (!strncmp(key, "key_", 4) && input_button_id(key + 4) >= 0)
			snprintf(g_config.key[input_button_id(key + 4)], sizeof(g_config.key[0]), "%s", val);
		else if (!strncmp(key, "pad_", 4) && input_button_id(key + 4) >= 0)
			snprintf(g_config.pad[input_button_id(key + 4)], sizeof(g_config.pad[0]), "%s", val);
	}
	fclose(f);
	return 1;
}

int config_save(void)
{
	char path[1100];
	config_path(path, sizeof(path));
	FILE *f = os_fopen(path, "w");
	if (!f) return 0;
	fprintf(f, "# Flick settings, rewritten on exit\n");
	fprintf(f, "bios = %s\n", g_config.bios);
	fprintf(f, "disc_folder = %s\n", g_config.disc_dir);
	fprintf(f, "volume = %d\n", g_config.volume);
	fprintf(f, "scale = %d\n", g_config.scale);
	fprintf(f, "timing = %s\n", g_config.fast ? "fast" : "mame");
	for (int b = 0; b < BTN_COUNT; b++)
		if (g_config.key[b][0]) fprintf(f, "key_%s = %s\n", input_button_name(b), g_config.key[b]);
	for (int b = 0; b < BTN_COUNT; b++)
		if (g_config.pad[b][0]) fprintf(f, "pad_%s = %s\n", input_button_name(b), g_config.pad[b]);
	return fclose(f) == 0;
}
