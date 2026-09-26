/* SPDX-License-Identifier: GPL-3.0-only */
/* panel.c -- the status panel: title, level, mines left, clock, cursor,
 * status, note and key help.
 *
 * Every field is padded to its width so a shorter text overwrites a longer
 * one. Nothing is ever written to the last column of the last text row,
 * which would scroll the text plane.
 */
#include "video.h"
#include "field.h"
#include "game.h"
#include "panel.h"
#include "clock.h"

#define CLOCK_MAX 35999u                    /* 9:59:59 */

struct layout {
    unsigned char title_r, title_c, level_r, level_c;
    unsigned char mines_r, mines_c, clock_r, clock_c, cursor_r, cursor_c;
    unsigned char status_r, status_c, status_w, note_r, note_c, note_w;
    unsigned char help_r, help_c, help_lines;
    const char *const *help;
};

static const char *const SIDE_HELP[] = {
    "Pijltjes/WASD",
    "Shift+WASD 5x",
    "RET  openen",
    "F    vlag/?",
    "G    ga naar",
    "TAB  volgende",
    "H    hulp",
    "N    nieuw",
    "ESC  niveau",
    "Q    stoppen",
};

static const char *const WIDE_HELP[] = {
    "Pijltjes/WASD: cursor   Shift+WASD: 5 velden   TAB: volgende",
    "RETURN/spatie: openen   F: vlag/?   G: ga naar, bv. G C12",
    "H: hulp   N: nieuw spel   ESC: ander niveau   Q: stoppen",
};

/* Side layouts: column 48 beside the beginner board, 51 beside gevorderd. */
#define SIDE(c) { 1, c, 2, c, 4, c, 5, c, 6, c, 8, c, 13, 9, c, 13, 11, c, 10, SIDE_HELP }

static const struct layout layouts[LEVELS] = {
    SIDE(48),
    SIDE(51),
    { 0, 1, 0, 14, 0, 28, 0, 46, 1, 28, 1, 1, 24, 1, 46, 17, 18, 1, 3, WIDE_HELP },
};

static const struct layout *lay;
static const char *note_text = "";

void panel_level(void)
{
    lay = &layouts[level];
}

static void at(unsigned char r, unsigned char c)
{
    con_at(ROWCOL(r, c));
}

/* Writes s and pads with spaces to width. */
static void put_padded(const char *s, unsigned char width)
{
    while (*s && width) {
        conout(*s++);
        width--;
    }
    while (width--)
        conout(' ');
}

/* Right-aligned signed number in `width` columns. */
static void put_number(signed int n, unsigned char width)
{
    char buf[7];
    unsigned char i = sizeof buf - 1, negative = n < 0;
    unsigned int u = negative ? -n : n;
    buf[i] = '\0';
    do {
        buf[--i] = '0' + u % 10;
        u /= 10;
    } while (u);
    if (negative)
        buf[--i] = '-';
    while (sizeof buf - 1 - i < width)
        buf[--i] = ' ';
    con_puts(buf + i);
}

void show_mines(void)
{
    at(lay->mines_r, lay->mines_c + 8);
    put_number((signed int)mines - flags, 4);
}

/* h:mm:ss, capped at 9:59:59. */
void show_clock(void)
{
    unsigned int s = clock_seconds(), h, m;
    if (!clock_available)
        return;
    if (s > CLOCK_MAX)
        s = CLOCK_MAX;
    h = s / 3600;
    m = (s / 60) % 60;
    s %= 60;
    at(lay->clock_r, lay->clock_c + 6);
    conout('0' + h); conout(':');
    conout('0' + m / 10); conout('0' + m % 10); conout(':');
    conout('0' + s / 10); conout('0' + s % 10);
}

void show_cursor_name(void)
{
    at(lay->cursor_r, lay->cursor_c + 9);
    if (state != PLAYING) {
        con_puts("  -");
        return;
    }
    if (cur_c < 9)
        conout(' ');
    conout('A' + cur_r);
    put_number(cur_c + 1, 1);
}

void show_note(const char *note)
{
    note_text = note;
    at(lay->note_r, lay->note_c);
    put_padded(note, lay->note_w);
}

void restore_note(void)
{
    show_note(note_text);
}

void show_status(const char *status)
{
    at(lay->status_r, lay->status_c);
    put_padded(status, lay->status_w);
}

void show_goto(const char *typed)
{
    unsigned char n = 0;
    at(lay->status_r, lay->status_c);
    con_puts("Ga naar: ");
    con_puts(typed);
    conout('_');
    while (typed[n])
        n++;
    put_padded("", lay->status_w - 10 - n);
}

void draw_panel(void)
{
    unsigned char i;
    at(lay->title_r, lay->title_c);   con_puts("MIJNENVEGER");
    at(lay->level_r, lay->level_c);   con_puts(level_name[level]);
    at(lay->mines_r, lay->mines_c);   con_puts("Mijnen");
    if (clock_available) {
        at(lay->clock_r, lay->clock_c);
        con_puts("Tijd");
    }
    at(lay->cursor_r, lay->cursor_c); con_puts("Veld");
    for (i = 0; i < lay->help_lines; i++) {
        at(lay->help_r + i, lay->help_c);
        con_puts(lay->help[i]);
    }
    show_mines();
    show_clock();
    show_cursor_name();
}
