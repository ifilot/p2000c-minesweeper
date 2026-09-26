/* SPDX-License-Identifier: GPL-3.0-only */
/* game.c -- game state, keys, and the course of a game.
 *
 * After every change the board is brought up to date through the screen
 * module and the panel through the panel module; the status line is always
 * written last, so it doubles as a display-complete marker for the tests.
 *
 * A key is sometimes seen twice: the terminal board's Z80 both draws the
 * picture and scans the keyboard, so a key held only briefly while it is
 * busy can come through again as an auto-repeat. A key equal to the one
 * before is therefore dropped when it arrived while that key's update was
 * still being sent, or within REPEAT_TICKS after it.
 */
#include "video.h"
#include "field.h"
#include "game.h"
#include "screen.h"
#include "panel.h"
#include "screens.h"
#include "saver.h"
#include "clock.h"
#include "scores.h"

const unsigned char level_cols[LEVELS] = { 9, 16, 30 };
const unsigned char level_rows[LEVELS] = { 9, 16, 16 };
const unsigned char level_mines[LEVELS] = { 10, 40, 99 };
const char *const level_name[LEVELS] = { "Beginner", "Gevorderd", "Expert" };

unsigned char level;
unsigned char state;
unsigned char cur_r, cur_c;

static unsigned char started;               /* the clock runs from the first open */
static unsigned char last_key;              /* the key handled last */
static unsigned int last_done;              /* clock ticks when its update was sent */
static unsigned char queued;                /* a key was already waiting then */

/* Cursor keys. The P2000C keyboard's cursor quadrant emits the WordStar
 * diamond (^S ^D ^E ^X, as P2EDIT and SuperCalc expect); the graphical
 * emulator sends the terminal's own cursor-control bytes instead, so both
 * sets are accepted. */
#define KEY_LEFT   0x13                     /* ^S */
#define KEY_RIGHT  0x04                     /* ^D */
#define KEY_UP     0x05                     /* ^E */
#define KEY_DOWN   0x18                     /* ^X */
#define KEY_LEFT2  0x15
#define KEY_RIGHT2 0x06
#define KEY_UP2    0x1A
#define KEY_DOWN2  0x0A
#define KEY_TAB    0x09
#define KEY_BS     0x08
#define KEY_DEL    0x7F
#define KEY_CR     0x0D
#define KEY_ESC    0x1B
#define BEL        0x07
#define FAST_STEPS 5                        /* Shift+WASD */
#define REPEAT_TICKS 9                      /* 0.15 s at 60 Hz */

/* Idle hook while waiting for a key: keeps the clock display current. */
static void tick_clock(void)
{
    if (started && state == PLAYING && clock_update())
        show_clock();
}

static void new_game(void)
{
    field_init(level_cols[level], level_rows[level], level_mines[level]);
    screen_level(level);
    state = PLAYING;
    started = 0;
    clock_reset();
    cur_r = rows / 2;
    cur_c = cols / 2;
}

/* The status line from the game state; written last. */
static void announce(void)
{
    if (state == WON)
        show_status("Gewonnen!");
    else if (state == LOST)
        show_status("Verloren!");
    else
        show_status("Ruim het veld");
}

/* Rebuilds the whole game screen from the framebuffer and the game state,
 * after the help page or the screen saver (both leave graphics mode). */
void redraw_game_screen(void)
{
    video_graphics();
    draw_panel();
    screen_flush();
    restore_note();
    announce();
}

static void show_new_game(void)
{
    new_game();
    screen_compose();
    show_note("");
    redraw_game_screen();
}

/* Board and panel after a change, the status line last. */
static void refresh(void)
{
    screen_sync();
    show_mines();
    show_cursor_name();
    announce();
}

static void game_over(unsigned char result)
{
    unsigned int seconds;
    if (result == OPEN_BOOM) {
        state = LOST;
        clock_freeze();
        show_note("Mijn geraakt!");
        return;
    }
    if (!field_cleared())
        return;
    state = WON;
    clock_freeze();
    field_flag_all();
    show_note("");
    if (!clock_available)
        return;
    seconds = clock_seconds();
    if (seconds == 0)
        seconds = 1;
    if (best_time[level] == 0 || seconds < best_time[level]) {
        best_time[level] = seconds;
        scores_save();
        show_note("Nieuw record!");
    }
}

/* RETURN or space: open a closed cell, or on an open number whose flags
 * are all placed open its other neighbours. */
static void open_cursor(void)
{
    unsigned char result, was_laid = laid;
    show_status("Bezig...");                /* replaces the marker before any flushing */
    if (cells[CELL(cur_r, cur_c)] & F_OPEN)
        result = field_chord(cur_r, cur_c);
    else
        result = field_open(cur_r, cur_c);
    if (!was_laid && laid) {
        clock_reset();
        started = 1;
    }
    if (result == OPEN_NONE) {
        conout(BEL);
        announce();
        return;
    }
    game_over(result);
    refresh();
}

static void mark_cursor(void)
{
    show_status("Bezig...");
    if (!field_mark(cur_r, cur_c)) {
        conout(BEL);
        announce();
        return;
    }
    refresh();
}

static void move_to(unsigned char r, unsigned char c)
{
    if (state != PLAYING || (r == cur_r && c == cur_c))
        return;
    cur_r = r;
    cur_c = c;
    screen_sync();
    show_cursor_name();
}

#define UP    0
#define DOWN  1
#define LEFT  2
#define RIGHT 3

/* Moves the cursor, stopping at the edge. (Unsigned arithmetic only: a
 * signed-char version was miscompiled, moving up as if down.) */
static void move_by(unsigned char dir, unsigned char steps)
{
    unsigned char r = cur_r, c = cur_c;
    switch (dir) {
    case UP:    r = r > steps ? r - steps : 0; break;
    case DOWN:  r = r + steps < rows ? r + steps : rows - 1; break;
    case LEFT:  c = c > steps ? c - steps : 0; break;
    case RIGHT: c = c + steps < cols ? c + steps : cols - 1; break;
    }
    move_to(r, c);
}

/* TAB: the next closed cell without a flag, in reading order, wrapping. */
static void next_closed(void)
{
    unsigned char r = cur_r, c = cur_c;
    unsigned int n;
    for (n = 0; n < ncells; n++) {
        if (++c == cols) {
            c = 0;
            if (++r == rows)
                r = 0;
        }
        if (!(cells[CELL(r, c)] & (F_OPEN | F_FLAG))) {
            move_to(r, c);
            return;
        }
    }
    conout(BEL);
}

/* G: "Ga naar: C12". A row letter, then the column number; the jump
 * happens on RETURN or space, or as soon as no further digit could make a
 * valid column. BS corrects, ESC cancels. */
static void go_to(void)
{
    char typed[4];
    unsigned char n = 0, key, column = 0, d;
    for (;;) {
        typed[n] = '\0';
        show_goto(typed);
        key = wait_key_idle(redraw_game_screen, tick_clock);
        if (key == KEY_ESC)
            break;
        if (key == KEY_BS || key == KEY_DEL || key == KEY_LEFT || key == KEY_LEFT2) {
            if (n) {
                n--;
                column /= 10;
            }
            continue;
        }
        if (key == KEY_CR || key == ' ') {
            if (column) {
                move_to(typed[0] - 'A', column - 1);
                break;
            }
            conout(BEL);
            continue;
        }
        if (key >= 'a' && key <= 'z')
            key -= 'a' - 'A';
        if (n == 0) {
            if (key >= 'A' && key < 'A' + rows)
                typed[n++] = key;
            else
                conout(BEL);
            continue;
        }
        d = key - '0';
        if (key < '0' || key > '9' || n == 3 || column * 10 + d == 0 || column * 10 + d > cols) {
            conout(BEL);
            continue;
        }
        typed[n++] = key;
        column = column * 10 + d;
        if (column * 10 > cols) {
            move_to(typed[0] - 'A', column - 1);
            break;
        }
    }
    show_cursor_name();
    announce();
}

/* A yes/no question on the status line; returns 1 for yes. */
static unsigned char confirm(const char *question)
{
    unsigned char key;
    show_status(question);
    key = conin();
    if (key == 'j' || key == 'J' || key == 'y' || key == 'Y')
        return 1;
    announce();
    return 0;
}

/* Giving up a game in progress needs a confirmation; a finished or
 * untouched one does not. */
static unsigned char may_leave(void)
{
    return state != PLAYING || !laid || confirm("Opgeven? J/N");
}

/* Is this key an unintended repeat of the one just handled? */
static unsigned char echo(unsigned char key)
{
    unsigned char was_queued = queued;
    queued = 0;                             /* only the first key read after an update */
    if (key != last_key)
        return 0;
    if (was_queued)
        return 1;
    return clock_available && (unsigned int)(clock_ticks() - last_done) < REPEAT_TICKS;
}

unsigned char play(unsigned char lvl)
{
    unsigned char key, handled = 0;

    level = lvl;
    last_key = 0;
    panel_level();
    show_new_game();

    for (;;) {
        if (handled) {                      /* the last key's update has been sent */
            last_done = clock_ticks();
            queued = conready();
            handled = 0;
        }
        key = wait_key_idle(redraw_game_screen, tick_clock);
        field_stir(((unsigned int)entropy() << 8) ^ clock_ticks());
        if (echo(key))
            continue;
        last_key = key;
        handled = 1;
        /* Shift+WASD: five cells at a time */
        switch (key) {
        case 'W': move_by(UP, FAST_STEPS);    continue;
        case 'A': move_by(LEFT, FAST_STEPS);  continue;
        case 'S': move_by(DOWN, FAST_STEPS);  continue;
        case 'D': move_by(RIGHT, FAST_STEPS); continue;
        }
        if (key >= 'A' && key <= 'Z')
            key += 'a' - 'A';
        switch (key) {
        case KEY_LEFT:  case KEY_LEFT2:  case 'a': move_by(LEFT, 1);  break;
        case KEY_RIGHT: case KEY_RIGHT2: case 'd': move_by(RIGHT, 1); break;
        case KEY_UP:    case KEY_UP2:    case 'w': move_by(UP, 1);    break;
        case KEY_DOWN:  case KEY_DOWN2:  case 's': move_by(DOWN, 1);  break;
        case KEY_TAB:
            if (state == PLAYING)
                next_closed();
            break;
        case 'g':
            if (state == PLAYING)
                go_to();
            break;
        case KEY_CR: case ' ':
            if (state == PLAYING)
                open_cursor();
            else
                conout(BEL);
            break;
        case 'f':
            if (state == PLAYING)
                mark_cursor();
            else
                conout(BEL);
            break;
        case 'h':
            help_screen();
            break;
        case 'n':
            if (may_leave())
                show_new_game();
            break;
        case KEY_ESC:
            if (may_leave())
                return PLAY_MENU;
            break;
        case 'q':
            if (confirm("Stoppen? J/N"))
                return PLAY_QUIT;
            break;
        }
    }
}
