/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.h -- the text-mode screens: start and help. */
#ifndef SCREENS_H
#define SCREENS_H

extern void text_clear(void);               /* clear the text screen, hide the cursor */
extern unsigned char start_screen(void);    /* level 1-3, or 0 to quit */
extern void help_screen(void);              /* rules page from the game; restores the board */

#endif
