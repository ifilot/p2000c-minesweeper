/* SPDX-License-Identifier: GPL-3.0-only */
/* game.h -- game state shared by the modules, and the game flow. */
#ifndef GAME_H
#define GAME_H

#define LEVELS 3

/* state */
#define PLAYING 0
#define WON     1
#define LOST    2

/* play() results */
#define PLAY_QUIT 0                          /* back to CP/M */
#define PLAY_MENU 1                          /* back to the start screen */

extern const unsigned char level_cols[LEVELS], level_rows[LEVELS], level_mines[LEVELS];
extern const char *const level_name[LEVELS];

extern unsigned char level;                  /* 0 beginner, 1 gevorderd, 2 expert */
extern unsigned char state;
extern unsigned char cur_r, cur_c;           /* cursor */

/* Games at one level until the player leaves (ESC or Q). */
extern unsigned char play(unsigned char lvl);

/* Restores the game screen after a text-mode interlude (help, screen saver). */
extern void redraw_game_screen(void);

#endif
