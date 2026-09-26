/* SPDX-License-Identifier: GPL-3.0-only */
/* clock.c -- game clock.
 *
 * The P2000C BIOS keeps a documented system timer: "a 4 bytes long 60 Hz
 * system timer ... in DP bytes 29 to 2CH (29H is the lowest byte)", where DP
 * is the Driver Parameter Block whose address is the first word of the
 * interrupt vector table at FFD0H (System Reference and Service Manual,
 * BIOS sections 3.3-3.5). Only the low word is used: the difference between
 * successive reads is folded into a seconds count, which is exact as long
 * as clock_update() is called more often than every 18 minutes.
 * clock_probe() confirms once that the counter moves before it is trusted.
 */
#include "clock.h"

#define DPB_POINTER      (*(unsigned int *)0xFFD0)
#define DPB_CLOCK_OFFSET 0x29
#define TICKS            (*(volatile unsigned int *)(DPB_POINTER + DPB_CLOCK_OFFSET))
#define TICKS_PER_SECOND CLOCK_TICKS_PER_SECOND

unsigned char clock_available;

static unsigned int last_ticks;
static unsigned int tick_remainder;         /* ticks not yet worth a full second */
static unsigned int seconds;
static unsigned char frozen;

void clock_probe(void)
{
    unsigned int before = TICKS;
    volatile unsigned int spin;
    for (spin = 0; spin < 4000; spin++)      /* a few tens of milliseconds at 4 MHz */
        ;
    clock_available = TICKS != before;
}

unsigned int clock_ticks(void)
{
    return TICKS;
}

void clock_reset(void)
{
    last_ticks = TICKS;
    tick_remainder = 0;
    seconds = 0;
    frozen = 0;
}

unsigned char clock_update(void)
{
    unsigned int now, changed = 0;
    if (!clock_available || frozen)
        return 0;
    now = TICKS;
    tick_remainder += now - last_ticks;      /* modulo 65536 by unsigned arithmetic */
    last_ticks = now;
    while (tick_remainder >= TICKS_PER_SECOND) {
        tick_remainder -= TICKS_PER_SECOND;
        seconds++;
        changed = 1;
    }
    return (unsigned char)changed;
}

unsigned int clock_seconds(void)
{
    return seconds;
}

void clock_freeze(void)
{
    clock_update();
    frozen = 1;
}
