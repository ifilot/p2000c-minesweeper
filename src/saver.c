/* SPDX-License-Identifier: GPL-3.0-only */
/* saver.c -- CRT screen saver.
 *
 * Idle time is taken from the BIOS 60 Hz tick counter when clock_probe()
 * found one; otherwise it is estimated by counting keyboard polls, with
 * POLLS_PER_SECOND calibrated in the emulator for a bare polling loop (an
 * idle hook that does work per poll slows that estimate down, which is why
 * the tick counter is preferred).
 *
 * The saver itself lives in the 80x24 text mode with the cursor hidden, and
 * prints its caption with the quarter-bright attribute, so hardly any
 * phosphor is driven and never at one place for long.
 */
#include "video.h"
#include "saver.h"
#include "clock.h"

#define POLLS_PER_SECOND 20300              /* calibrated in the emulator: wait_key() polls per second at 4 MHz */
#define MOVE_SECONDS     3                  /* caption moves every few seconds */
#define ESC 27

static const char *const CAPTION[2] = { "M I J N E N V E G E R", "Philips P2000C" };

/* Quarter-bright attribute: intensity bits (6, 0) both zero; 40h is normal. */
static void set_brightness(unsigned char attribute)
{
    conout(ESC); conout('0'); conout(attribute);
}

static void caption_at(unsigned char row, unsigned char col)
{
    conout(12);                              /* form feed: clear screen, cursor home */
    con_at(ROWCOL(row, col));     con_puts(CAPTION[0]);
    con_at(ROWCOL(row + 1, col)); con_puts(CAPTION[1]);
}

/* Runs until a key is pressed; that key is consumed. */
static void screen_saver(void)
{
    unsigned char row = 5, col = 10;
    unsigned int polls;
    unsigned char seconds;

    video_text();
    conout(ESC); conout('c');
    set_brightness(0x00);
    for (;;) {
        caption_at(row, col);
        for (seconds = 0; seconds < MOVE_SECONDS; seconds++)
            for (polls = 0; polls < POLLS_PER_SECOND; polls++)
                if (conready()) {
                    conin();
                    set_brightness(0x40);
                    conout(12);
                    return;
                }
        row = (unsigned char)((row + 7) % 22);       /* a simple walk that covers the screen */
        col = (unsigned char)((col + 23) % 58);    /* the caption is 21 wide */
    }
}

#define IDLE_EVERY 512                       /* polls between idle() calls and clock checks, ~25 ms */
#define SAVER_TICKS ((unsigned int)SAVER_SECONDS * CLOCK_TICKS_PER_SECOND)   /* 18000 < 65536 */

unsigned char wait_key_idle(void (*redraw)(void), void (*idle)(void))
{
    unsigned int polls = 0, seconds = 0, start = clock_ticks();
    unsigned char expired;
    for (;;) {
        if (conready())
            return conin();
        expired = 0;
        if ((polls & (IDLE_EVERY - 1)) == 0) {
            if (idle)
                idle();
            if (clock_available && (unsigned int)(clock_ticks() - start) >= SAVER_TICKS)
                expired = 1;
        }
        if (++polls >= POLLS_PER_SECOND) {
            polls = 0;
            if (!clock_available && ++seconds >= SAVER_SECONDS)
                expired = 1;
        }
        if (expired) {
            screen_saver();
            redraw();
            start = clock_ticks();
            seconds = 0;
        }
    }
}

unsigned char wait_key(void (*redraw)(void))
{
    return wait_key_idle(redraw, 0);
}
