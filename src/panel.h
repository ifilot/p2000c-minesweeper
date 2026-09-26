/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.h -- the status panel on the 64x21 text plane.
 *
 * Beginner and gevorderd: a column right of the board. Expert: the board
 * spans the screen, so the panel takes two rows above it and the key help
 * three rows below it. */
#ifndef PANEL_H
#define PANEL_H

extern void panel_level(void);                     /* layout for the current level */
extern void draw_panel(void);                      /* static texts and all values */
extern void show_mines(void);                      /* mines minus flags */
extern void show_clock(void);                      /* elapsed game time, if a clock exists */
extern void show_cursor_name(void);                /* "Veld  C12" */
extern void show_note(const char *note);           /* remembered for restore_note() */
extern void restore_note(void);
extern void show_status(const char *status);       /* written last after every update */
extern void show_goto(const char *typed);          /* "Ga naar: C1_" on the status line */

#endif
