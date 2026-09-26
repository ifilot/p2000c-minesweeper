/* SPDX-License-Identifier: GPL-3.0-only */
/* Mijnenveger voor de Philips P2000C -- Minesweeper, in Dutch.
 *
 * Start screen (level 1-3, help, quit), then games until the player
 * leaves. Modules:
 *   game.c    state, keys, course of a game     screen.c  board picture and uploads
 *   panel.c   text panel                        screens.c start and help screens
 *   field.c   minefield rules                   scores.c  best times (MINES.DAT)
 *   saver.c   key waits with the CRT screen saver   clock.c   game clock (BIOS ticks)
 *   video.asm framebuffer primitives, ESC r uploads, BIOS console I/O, BDOS
 *
 * The board uses the terminal's 512x252 high-resolution mode with the 64x21
 * text plane for the panel; the other screens use the 80x24 text mode.
 */
#include "video.h"
#include "game.h"
#include "screens.h"
#include "scores.h"
#include "clock.h"

#define ESC 27

int main(void)
{
    unsigned char choice;

    conout(ESC); conout('c');                /* no blinking text cursor */
    clock_probe();
    scores_load();
    while ((choice = start_screen()) != 0) {
        choice = play(choice - 1);
        video_text();
        if (choice == PLAY_QUIT)
            break;
    }
    text_clear();
    conout(ESC); conout('C');                /* CP/M gets its cursor back */
    return 0;
}
