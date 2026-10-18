/* Flick - event scheduler
 *
 * Mirrors MAME's device_scheduler closely enough to land every event on the same
 * instruction: the CPU's local time is cycles * aps (aps = 10^18 / 108 MHz, truncated,
 * as MAME accumulates it), and an event due at time T is seen before the first
 * instruction that starts at cycle >= floor(T / aps). Timers keep MAME's semantics:
 * adjust() starts from "now", periodic timers expire at start + k * period exactly,
 * equal expiry times fire in insertion order. */
#ifndef HS_SCHED_H
#define HS_SCHED_H

#include "emu.h"

typedef void (*timer_cb)(void *ctx, int param);

typedef struct emu_timer {
	atto_t expire, period, start;
	int enabled, param;
	uint64_t seq;               /* insertion order, for ties */
	timer_cb cb;
	void *ctx;
	const char *name;
} emu_timer;

void sched_reset(void);         /* drop every timer (before the devices allocate theirs) */
emu_timer *timer_alloc(timer_cb cb, void *ctx, const char *name);
/* delay from now; period 0 = one-shot */
void timer_adjust(emu_timer *t, atto_t delay, int param, atto_t period);
void timer_disable(emu_timer *t);

atto_t sched_time(void);        /* MAME's machine().time() for the current context */
int sched_in_callback(void);    /* 1 inside a timer callback, 0 in CPU context */
extern uint64_t g_next_event_cycle;   /* CPU runs while cycles < this */
void sched_fire_due(void);      /* fire every timer due at the CPU's current cycle */

#endif
