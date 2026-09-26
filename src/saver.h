/* SPDX-License-Identifier: GPL-3.0-only */
/* saver.h -- waiting for a key with a CRT screen saver.
 *
 * Every place that waits for the player calls wait_key() instead of conin().
 * After SAVER_SECONDS without a key the screen is switched to text mode
 * (which blanks the picture) and a small, quarter-bright caption wanders
 * over the black screen; any key ends the saver, is consumed, and the
 * caller's redraw function restores the screen before waiting continues. */
#ifndef SAVER_H
#define SAVER_H

#define SAVER_SECONDS 300                   /* five minutes of inactivity */

extern unsigned char wait_key(void (*redraw)(void));

/* Like wait_key(), but calls idle() about 40 times a second while waiting
 * (the game uses it to keep its clock display current). idle may be 0. */
extern unsigned char wait_key_idle(void (*redraw)(void), void (*idle)(void));

#endif
