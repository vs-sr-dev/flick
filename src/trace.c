/* HyperScanEmu - instruction trace (see trace.h) */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "trace.h"
#include "score7.h"

FILE *g_trace;
int g_trace_done;

#define NREGS (32 + 32 + 3 + 2)

static uint64_t s_n, s_limit = 50000000, s_ckpt, s_from;
static uint32_t s_snap[NREGS];
static char s_acc[1024];
static size_t s_acc_len;

int trace_open(const char *spec)
{
	char path[1024];
	snprintf(path, sizeof(path), "%s", spec);
	char *c = strchr(path + 2, ':');      /* keep "D:/..." intact */
	if (c)
	{
		char *e;
		s_limit = strtoull(c + 1, &e, 0);
		if (*e == ':') s_ckpt = strtoull(e + 1, &e, 0);
		if (s_ckpt) s_from = ~0ULL;
		if (*e == ':') s_from = strtoull(e + 1, &e, 0);
		*c = 0;
	}
	g_trace = fopen(path, "w");
	if (!g_trace) return 0;
	setvbuf(g_trace, NULL, _IOFBF, 1 << 22);
	fprintf(g_trace, "# hstrace 1\n");
	return 1;
}

void trace_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(g_trace, fmt, ap);
	va_end(ap);
}

static void gather(uint32_t *r)
{
	memcpy(r, g_cpu.gpr, 32 * 4);
	memcpy(r + 32, g_cpu.cr, 32 * 4);
	memcpy(r + 64, g_cpu.sr, 3 * 4);
	memcpy(r + 67, g_cpu.ce, 2 * 4);
}

static void regs(const uint32_t *r)
{
	for (int i = 0; i < 32; i++) fprintf(g_trace, " r%d=%08x", i, r[i]);
	for (int i = 0; i < 32; i++) fprintf(g_trace, " cr%d=%08x", i, r[32 + i]);
	for (int i = 0; i < 3; i++) fprintf(g_trace, " sr%d=%08x", i, r[64 + i]);
	fprintf(g_trace, " ceh=%08x cel=%08x", r[67], r[68]);
}

void trace_begin(void)
{
	if (s_n == 0 || s_n >= s_from) gather(s_snap);
	if (s_n == 0)
	{
		fprintf(g_trace, "S pc=%08x", g_cpu.pc);
		regs(s_snap);
		fprintf(g_trace, "\n");
	}
}

void trace_access(char rw, int size, uint32_t addr, uint32_t v)
{
	if (s_acc_len + 40 < sizeof(s_acc))
		s_acc_len += snprintf(s_acc + s_acc_len, sizeof(s_acc) - s_acc_len, " %c%d %08x %0*x\n", rw, size, addr, size * 2, v);
}

void trace_end(uint32_t pc, uint32_t op, int op16)
{
	static const char *const ce_name[2] = { "ceh", "cel" };
	uint32_t now[NREGS];
	gather(now);
	if (s_n >= s_from)
	{
		if (op16) fprintf(g_trace, "%08x %04x", pc, op & 0xffff); else fprintf(g_trace, "%08x %08x", pc, op);
		for (int i = 0; i < NREGS; i++)
			if (now[i] != s_snap[i])
			{
				if (i < 32) fprintf(g_trace, " r%d=%08x", i, now[i]);
				else if (i < 64) fprintf(g_trace, " cr%d=%08x", i - 32, now[i]);
				else if (i < 67) fprintf(g_trace, " sr%d=%08x", i - 64, now[i]);
				else fprintf(g_trace, " %s=%08x", ce_name[i - 67], now[i]);
			}
		fprintf(g_trace, "\n");
	}
	if (s_acc_len) { fwrite(s_acc, 1, s_acc_len, g_trace); s_acc_len = 0; }
	++s_n;
	if (s_ckpt && s_n % s_ckpt == 0)        /* sparse checkpoint: FNV-1a of all registers */
	{
		uint32_t h = 2166136261u;
		for (int i = 0; i < NREGS; i++)
			for (int k = 0; k < 4; k++) { h ^= (now[i] >> (k * 8)) & 0xff; h *= 16777619u; }
		fprintf(g_trace, "C n=%llu pc=%08x h=%08x\n", (unsigned long long)s_n, g_cpu.pc, h);
	}
	if (s_n < s_limit) return;
	fprintf(g_trace, "E n=%llu c=%llu pc=%08x", (unsigned long long)s_n, (unsigned long long)g_cpu.cycles, pc);
	regs(now);
	fprintf(g_trace, "\n");
	fclose(g_trace);
	g_trace = NULL;
	g_trace_done = 1;
}
