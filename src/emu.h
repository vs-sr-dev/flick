/* Flick - common definitions */
#ifndef HS_EMU_H
#define HS_EMU_H

#include <stdint.h>
#include <stdio.h>

/* Emulated time in attoseconds, as MAME's attotime (exact integer arithmetic; 64 bits
 * would overflow after 18 s). GCC/Clang extension. */
typedef unsigned __int128 atto_t;
#define ATTO_PER_SEC   ((atto_t)1000000000000000000ULL)
#define ATTO_NEVER     (~(atto_t)0)

/* MAME's HZ_TO_ATTOSECONDS for an integer clock: truncating division. */
static inline atto_t hz_to_atto(uint32_t hz) { return (atto_t)(1000000000000000000ULL / hz); }

#define XTAL_27M       27000000u
#define CPU_CLOCK      (XTAL_27M * 4)      /* 108 MHz */

void hs_fatal(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
void hs_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
extern int g_verbose;

#endif
