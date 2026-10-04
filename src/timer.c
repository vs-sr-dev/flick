// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/* SPG290 timers - port of MAME's spg290_timer.cpp and the driver's timers_clk_sel_w.
 *
 * MAME ticks each timer with a scheduler event per counter clock (1 MHz as the BIOS sets
 * it up). Here the counter is advanced arithmetically when the CPU touches the timer, and
 * the only event is the overflow that raises the IRQ. Ticks happen at exactly the same
 * times as MAME's periodic timer (start + k * period, period = 1e18 / clock attoseconds),
 * a tick counts as past for an instruction when MAME would have fired it before that
 * instruction, so the IRQs land on the same instructions. */
#include "devices.h"
#include "sched.h"
#include "score7.h"

typedef struct {
	int enabled;
	uint32_t control, control2, preload, counter, ccp, upcount;
	uint32_t clock;
	atto_t t0, period;       /* tick k (k >= 1) is at t0 + k * period */
	uint64_t next_k;         /* first tick not yet applied */
	emu_timer *ovf;          /* event at the next IRQ-raising overflow */
} spg_timer;

static spg_timer s_t[6];
static uint16_t s_clk_sel;
static atto_t s_aps;

/* latest time whose ticks have happened, seen from the current context: in a scheduler
 * callback its own time; for the CPU, every event MAME fires before the instruction at
 * cycle c, i.e. those at T < (c + 1) * aps */
static atto_t horizon(void)
{
	const atto_t now = sched_time();
	return sched_in_callback() ? now : now + s_aps - 1;
}

static void advance(spg_timer *t, atto_t h)
{
	if (!t->enabled || t->t0 + (atto_t)t->next_k * t->period > h)
		return;
	uint64_t n = (uint64_t)((h - t->t0) / t->period) - t->next_k + 1;   /* ticks due */
	t->next_k += n;

	if (!(t->control >> 31))
		return;
	if ((t->control2 >> 30) & 3)
		hs_fatal("unemulated timer mode %u", (t->control2 >> 30) & 3);

	while (n)
	{
		const uint64_t to_top = 0xffff - (t->counter & 0xffff);    /* increments before 0xffff */
		if (n <= to_top)
		{
			t->counter += (uint32_t)n;
			return;
		}
		n -= to_top + 1;                                        /* the tick at 0xffff reloads */
		t->counter = t->preload & 0xffff;
		if ((t->control >> 27) & 1)
		{
			t->control |= 0x04000000;
			score7_set_input(IRQ_TIMER);
		}
		else if (n)                                             /* no IRQ: skip whole periods */
		{
			const uint64_t p = 0x10000 - (t->preload & 0xffff);
			n %= p;
		}
	}
}

static void reschedule(spg_timer *t)
{
	if (!t->enabled || !(t->control >> 31) || !((t->control >> 27) & 1) || ((t->control2 >> 30) & 3))
	{
		timer_disable(t->ovf);
		return;
	}
	const uint64_t k = t->next_k + (0xffff - (t->counter & 0xffff));
	timer_adjust(t->ovf, t->t0 + (atto_t)k * t->period - sched_time(), 0, 0);
}

static void sync(spg_timer *t)
{
	advance(t, horizon());
}

static void ovf_event(void *ctx, int param)
{
	spg_timer *t = ctx;
	(void)param;
	sync(t);
	reschedule(t);
}

static void restart(spg_timer *t)
{
	/* device_clock_changed: adjust(from_hz(clock), 0, from_hz(clock)) from now */
	t->t0 = sched_time();
	t->period = hz_to_atto(t->clock);
	t->next_k = 1;
}

void timers_init(void)
{
	s_aps = hz_to_atto(CPU_CLOCK);
	for (int i = 0; i < 6; i++)
	{
		spg_timer *t = &s_t[i];
		*t = (spg_timer){ 0 };
		t->clock = XTAL_27M;
		t->ovf = timer_alloc(ovf_event, t, "timer");
	}
	s_clk_sel = 0;
}

void timer_control_w(int n, uint32_t data)
{
	spg_timer *t = &s_t[n];
	sync(t);
	int enabled = data & 1;
	if (t->enabled != enabled)
	{
		t->enabled = enabled;
		if (enabled) restart(t);
	}
	if (data & 2)
		t->counter = t->preload & 0xffff;
	reschedule(t);
}

uint32_t timer_read(int n, uint32_t off)
{
	spg_timer *t = &s_t[n];
	sync(t);                     /* the IRQ flag in control may have been set by now */
	switch (off)
	{
	case 0x00: return t->control;
	case 0x04: return t->control2;
	case 0x08: return t->preload;
	case 0x0c: return t->ccp;
	case 0x10: return t->upcount;
	}
	hs_log("timer%d: unknown read %x\n", n, off);
	return 0;
}

void timer_write(int n, uint32_t off, uint32_t data, uint32_t mask)
{
	spg_timer *t = &s_t[n];
	sync(t);
	switch (off)
	{
	case 0x00:
		COMBINE(t->control, data, mask);
		if (mask & 0xff000000)
			t->control &= ~(data & 0x04000000);  // IRQ ack (line clears are not modelled: the core latches asserts)
		break;
	case 0x04: COMBINE(t->control2, data, mask); break;
	case 0x08: COMBINE(t->preload, data, mask); break;
	case 0x0c: COMBINE(t->ccp, data, mask); break;
	case 0x10: COMBINE(t->upcount, data, mask); break;
	default: hs_log("timer%d: unknown write %x = %x\n", n, off, data);
	}
	reschedule(t);
}

uint32_t timers_clk_sel_r(void)
{
	return s_clk_sel;
}

void timers_clk_sel_w(uint32_t data, uint32_t mask)
{
	COMBINE(s_clk_sel, data, mask);

	/* 27_MHz_XTAL / ((data & 0xff) + 1), then XTAL::value() = u32(clock + 1e-3) */
	const uint32_t clock = (uint32_t)(27000000.0 / ((data & 0xff) + 1) + 1e-3);
	uint32_t m = 0x100;
	for (int i = 0; i < 6; i++, m <<= 1)
	{
		spg_timer *t = &s_t[i];
		const uint32_t c = (data & m) ? 32768 : clock;
		if (c != t->clock)              /* device_t::set_unscaled_clock: no change, no notify */
		{
			sync(t);
			t->clock = c;
			if (t->enabled) restart(t);
			reschedule(t);
		}
	}
}
