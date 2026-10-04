// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/* SPG290 timers - port of MAME's spg290_timer.cpp and the driver's timers_clk_sel_w.
 * As in MAME, every timer tick is a scheduler event (up to 27 MHz in theory, 1 MHz as the
 * BIOS programs it); an analytic version can replace it once the trace-diff passes. */
#include "devices.h"
#include "sched.h"
#include "score7.h"

typedef struct {
	int enabled;
	uint32_t control, control2, preload, counter, ccp, upcount;
	uint32_t clock;
	emu_timer *tick;
} spg_timer;

static spg_timer s_t[6];
static uint16_t s_clk_sel;

static void tick(void *ctx, int param)
{
	spg_timer *t = ctx;
	(void)param;
	if (!(t->control >> 31))
		return;

	switch ((t->control2 >> 30) & 0x03)
	{
	case 0: // Timer Mode
		if (t->counter == 0xffff)
		{
			t->counter = t->preload & 0xffff;
			if ((t->control >> 27) & 1)
			{
				t->control |= 0x04000000;
				score7_set_input(IRQ_TIMER);
			}
		}
		else
			t->counter++;
		break;
	case 1: hs_fatal("unemulated timer Capture Mode");
	case 2: hs_fatal("unemulated timer Comparison Mode");
	case 3: hs_fatal("unemulated timer PWM Mode");
	}
}

static void clock_changed(spg_timer *t)
{
	if (t->enabled)
	{
		atto_t p = hz_to_atto(t->clock);    /* attotime::from_hz(u32) */
		timer_adjust(t->tick, p, 0, p);
	}
	else
		timer_disable(t->tick);
}

void timers_init(void)
{
	for (int i = 0; i < 6; i++)
	{
		spg_timer *t = &s_t[i];
		*t = (spg_timer){ 0 };
		t->clock = XTAL_27M;
		t->tick = timer_alloc(tick, t, "timer");
	}
	s_clk_sel = 0;
}

void timer_control_w(int n, uint32_t data)
{
	spg_timer *t = &s_t[n];
	int enabled = data & 1;
	if (t->enabled != enabled)
	{
		t->enabled = enabled;
		clock_changed(t);
	}
	if (data & 2)
		t->counter = t->preload & 0xffff;
}

uint32_t timer_read(int n, uint32_t off)
{
	spg_timer *t = &s_t[n];
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
		const uint32_t c = (data & m) ? 32768 : clock;
		if (c != s_t[i].clock)              /* device_t::set_unscaled_clock: no change, no notify */
		{
			s_t[i].clock = c;
			clock_changed(&s_t[i]);
		}
	}
}
