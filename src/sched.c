/* Flick - event scheduler (see sched.h) */
#include "sched.h"
#include "score7.h"

#define MAX_TIMERS 32

static emu_timer s_timers[MAX_TIMERS];
static int s_count;
static uint64_t s_seq;
static atto_t s_aps;                  /* attoseconds per CPU cycle */
static emu_timer *s_cb_timer;         /* timer whose callback is running */
static atto_t s_cb_time;
static int s_cb_modified;

uint64_t g_next_event_cycle = UINT64_MAX;

static void recompute(void)
{
	atto_t m = ATTO_NEVER;
	for (int i = 0; i < s_count; i++)
		if (s_timers[i].enabled && s_timers[i].expire < m)
			m = s_timers[i].expire;
	g_next_event_cycle = (m == ATTO_NEVER) ? UINT64_MAX : (uint64_t)(m / s_aps);
}

void sched_reset(void)
{
	s_count = 0;
	s_seq = 0;
	s_cb_timer = NULL;
	g_next_event_cycle = UINT64_MAX;
}

emu_timer *timer_alloc(timer_cb cb, void *ctx, const char *name)
{
	if (s_count == MAX_TIMERS) hs_fatal("too many timers");
	if (!s_aps) s_aps = hz_to_atto(CPU_CLOCK);
	emu_timer *t = &s_timers[s_count++];
	t->cb = cb; t->ctx = ctx; t->name = name;
	t->enabled = 0;
	return t;
}

int sched_in_callback(void)
{
	return s_cb_timer != NULL;
}

atto_t sched_time(void)
{
	if (s_cb_timer) return s_cb_time;
	return (atto_t)g_cpu.cycles * s_aps;
}

void sched_io_steady(atto_t t)
{
	if (s_cb_timer) return;                  /* only the CPU's own reads count */
	/* the first cycle c with c * aps >= t: a read at any earlier cycle sees the same value */
	const atto_t c = (t + s_aps - 1) / s_aps;
	g_cpu.io_steady++;
	if (c < g_cpu.steady_until) g_cpu.steady_until = c > UINT64_MAX ? UINT64_MAX : (uint64_t)c;
}

void timer_adjust(emu_timer *t, atto_t delay, int param, atto_t period)
{
	if (t == s_cb_timer) s_cb_modified = 1;
	t->param = param;
	t->enabled = 1;
	t->start = sched_time();
	t->expire = t->start + delay;
	t->period = period;
	t->seq = ++s_seq;
	recompute();
}

void timer_disable(emu_timer *t)
{
	if (t == s_cb_timer) s_cb_modified = 1;
	t->enabled = 0;
	recompute();
}

static emu_timer *earliest(void)
{
	emu_timer *best = NULL;
	for (int i = 0; i < s_count; i++)
	{
		emu_timer *t = &s_timers[i];
		if (t->enabled && (!best || t->expire < best->expire || (t->expire == best->expire && t->seq < best->seq)))
			best = t;
	}
	return best;
}

void sched_fire_due(void)
{
	for (;;)
	{
		emu_timer *t = earliest();
		if (!t || (uint64_t)(t->expire / s_aps) > g_cpu.cycles)
			break;
		/* as MAME's execute_timers: one-shots are disabled before the callback, periodic
		 * timers advance by exactly one period unless the callback re-adjusted them */
		if (t->period == 0) t->enabled = 0;
		s_cb_timer = t; s_cb_time = t->expire; s_cb_modified = 0;
		t->cb(t->ctx, t->param);
		s_cb_timer = NULL;
		if (!s_cb_modified && t->period != 0)
		{
			t->start = s_cb_time;
			t->expire += t->period;
			t->seq = ++s_seq;
		}
		recompute();
		score7_flush_inputs();
	}
}
