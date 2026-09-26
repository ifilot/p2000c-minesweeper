/* SPDX-License-Identifier: GPL-3.0-only */
/* clock.h -- game clock driven by the BIOS's documented 60 Hz system timer. */
#ifndef CLOCK_H
#define CLOCK_H

/* Checks once whether the tick counter advances; without it there is no clock. */
extern void clock_probe(void);
extern unsigned char clock_available;

/* Restarts the clock at 0:00 (frozen clocks keep their last value). */
extern void clock_reset(void);

/* Folds elapsed ticks into seconds; returns nonzero when the second changed.
 * Call it at least every few minutes while waiting, e.g. from an idle hook. */
extern unsigned char clock_update(void);

/* Raw low word of the tick counter (60 Hz); differences wrap after 18 minutes. */
extern unsigned int clock_ticks(void);
#define CLOCK_TICKS_PER_SECOND 60

/* Elapsed seconds since clock_reset(), or the frozen value. */
extern unsigned int clock_seconds(void);
extern void clock_freeze(void);

#endif
