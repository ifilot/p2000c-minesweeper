/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.c -- the board picture.
 *
 * Everything is composed in the 16 KiB framebuffer; every cell is one whole
 * tile (sprites.h) that carries its share of the board's grid (its top row
 * and left column). Two costs decide how the picture goes to the terminal
 * board: the 19200-baud link (about 1920 bytes/s), and the terminal's own
 * Z80, which draws a line dot by dot -- slow for long or many lines -- but
 * stores uploaded bytes about as fast as they arrive. So:
 *
 * - Only the grid goes as lines (ESC m / ESC M): one per row and column of
 *   cells, plus the frame. Everything else -- the tiles' insides, labels --
 *   goes as bitmap uploads of what differs from those lines.
 * - Uploads are grouped per band of consecutive changed lines, at most
 *   PIECE_LINES at a time, and each band goes the cheaper way: as one chunk
 *   of whole 64-byte lines (a single ESC r, sent from its lowest line up,
 *   since the terminal's picture RAM runs bottom-up) or as one ESC r per
 *   run of changed bytes on each line.
 * - Each cell keeps the appearance last sent. After a change, the span of
 *   changed cells in each board row is redrawn and compared with its
 *   previous bytes, and only the lines that differ are sent.
 */
#include <string.h>
#include "video.h"
#include "field.h"
#include "game.h"
#include "screen.h"
#include "scores.h"
#include "sprites.h"

#if STYLE_COUNT != TILE_STYLES
#error "sprites.h and scores.h disagree on the number of tile styles"
#endif

#define LEFT_BYTE    2                      /* board's first byte (x = 16); row letters in byte 0 */
#define LABEL_LINES  7
#define LOOK_CURSOR  0x80
#define ESC          27
#define RUN_GAP      7                      /* an ESC r header costs 7 bytes */
#define PIECE_LINES  15                     /* lines per chunk, as p2000c-chess sends them */

struct geometry {
    const unsigned char *tiles;             /* the shared tiles (TILE_COUNT - STYLE_TILES) */
    const unsigned char *style_tiles;       /* STYLE_TILES per style */
    const unsigned char *cursor;
    const unsigned char *col_labels;        /* cw bytes x 7 lines per column */
    unsigned char cw, ch;                   /* cell: bytes, lines */
    unsigned char top;                      /* board's first line */
};

static const struct geometry geometries[3] = {
    { &tiles_40[0][0], &style_tiles_40[0][0][0], cursor_40, col_labels_40, 5, 24, 20 },
    { &tiles_24[0][0], &style_tiles_24[0][0][0], cursor_24, col_labels_24, 3, 14, 14 },
    { &tiles_16[0][0], &style_tiles_16[0][0][0], cursor_16, col_labels_16, 2, 10, 40 },
};

static const struct geometry *g;
static unsigned int tile_size;              /* bytes per tile */
static unsigned int grid_x0, grid_x1;       /* the grid's first and last column of dots */
static unsigned char grid_y1;               /* the grid's last line (its first is g->top) */
static unsigned int frame_x0, frame_x1;
static unsigned char frame_y0, frame_y1;

/* What the lines put on a line of the picture (64 bytes each). */
static unsigned char pat_edge[FB_LINE];     /* the frame's top or bottom line */
static unsigned char pat_side[FB_LINE];     /* the frame's sides only */
static unsigned char pat_across[FB_LINE];   /* sides and a grid line across the board */
static unsigned char pat_down[FB_LINE];     /* sides and the dots of the grid's columns */
static const unsigned char pat_none[FB_LINE];      /* outside the frame: nothing */

static unsigned char shown[MAX_CELLS];      /* appearance last sent per cell */
static unsigned char saved[5 * 9 * 24];     /* a board row's changed span before redrawing (largest: beginner) */

static unsigned char piece[PIECE_LINES * FB_LINE];   /* deltas of a band of consecutive lines */
static unsigned int piece_cost[PIECE_LINES];         /* what each line costs as runs */
static unsigned char piece_first, piece_n, piece_col0, piece_width;

/* Sets dots x0..x1 of a 64-byte line: whole bytes where it can. */
static void dots(unsigned char *line, unsigned int x0, unsigned int x1)
{
    unsigned char first = x0 >> 3, last = x1 >> 3;
    unsigned char head = 0xFF >> (x0 & 7), tail = 0xFF << (7 - (x1 & 7));
    if (first == last) {
        line[first] |= head & tail;
        return;
    }
    line[first] |= head;
    if (last > first + 1)
        memset(line + first + 1, 0xFF, last - first - 1);
    line[last] |= tail;
}

void screen_level(unsigned char level)
{
    unsigned char c;
    g = &geometries[level];
    tile_size = g->cw * g->ch;
    grid_x0 = LEFT_BYTE * 8;
    grid_x1 = grid_x0 + (unsigned int)cols * g->cw * 8;
    grid_y1 = g->top + rows * g->ch;
    frame_x0 = grid_x0 - 3;                 /* two dark dots or lines around the grid */
    frame_x1 = grid_x1 + 3;
    frame_y0 = g->top - 3;
    frame_y1 = grid_y1 + 3;
    memset(pat_edge, 0, FB_LINE);
    dots(pat_edge, frame_x0, frame_x1);
    memset(pat_side, 0, FB_LINE);
    dots(pat_side, frame_x0, frame_x0);
    dots(pat_side, frame_x1, frame_x1);
    memcpy(pat_across, pat_side, FB_LINE);
    dots(pat_across, grid_x0, grid_x1);
    memcpy(pat_down, pat_side, FB_LINE);
    for (c = 0; c <= cols; c++)
        dots(pat_down, grid_x0 + c * g->cw * 8, grid_x0 + c * g->cw * 8);
}

/* A tile's bitmap: the first STYLE_TILES in the chosen style. */
static const unsigned char *tile(unsigned char t)
{
    if (t < STYLE_TILES)
        return g->style_tiles + (tile_style * STYLE_TILES + t) * tile_size;
    return g->tiles + (t - STYLE_TILES) * tile_size;
}

static unsigned int cell_offset(unsigned char r, unsigned char c)
{
    return (unsigned int)(g->top + r * g->ch) * FB_LINE + LEFT_BYTE + c * g->cw;
}

/* What the cell should look like right now: a tile, plus the cursor bit. */
static unsigned char look(unsigned char r, unsigned char c)
{
    unsigned char cell = cells[CELL(r, c)], t;
    if (cell & F_OPEN)
        t = (cell & F_MINE) ? TILE_BOOM : TILE_OPEN0 + (cell & F_COUNT);
    else if (state == LOST && (cell & (F_MINE | F_FLAG)) == F_MINE)
        t = TILE_MINE;
    else if (state == LOST && (cell & (F_MINE | F_FLAG)) == F_FLAG)
        t = TILE_WRONG;
    else if (cell & F_FLAG)
        t = TILE_FLAG;
    else if (cell & F_QUERY)
        t = TILE_QUESTION;
    else
        t = TILE_CLOSED;
    if (state == PLAYING && r == cur_r && c == cur_c)
        t |= LOOK_CURSOR;
    return t;
}

/* A closed tile under the cursor is swapped for its 'selected' version (a
 * dither clears a ring for the frame), then the cursor is OR-ed on. */
static void draw_cell(unsigned char r, unsigned char c, unsigned char lk)
{
    unsigned int offset = cell_offset(r, c), wh = WH(g->cw, g->ch);
    unsigned char t = lk & ~LOOK_CURSOR;
    if ((lk & LOOK_CURSOR) && t == TILE_CLOSED)
        t = TILE_SELECTED;
    video_copy(tile(t), offset, wh);
    if (lk & LOOK_CURSOR)
        video_blit(g->cursor, offset, wh);
    shown[CELL(r, c)] = lk;
}

/* --- uploads ---------------------------------------------------------------------- */

/* Uploads the nonzero runs of buf[0..width) (runs closer than a header are
 * joined) as parts of framebuffer line `line` from byte column col0; what
 * that costs is runs_cost() (video.asm). */
static void runs(const unsigned char *buf, unsigned char line, unsigned char col0, unsigned char width)
{
    unsigned char x = 0, first, last;
    while (x < width) {
        while (x < width && buf[x] == 0)
            x++;
        if (x == width)
            break;
        first = last = x;
        while (x < width) {
            if (buf[x] != 0)
                last = x;
            else if (x - last >= RUN_GAP)
                break;
            x++;
        }
        video_flush_rect(COLROW(col0 + first, line), WH(last - first + 1, 1));
    }
}

/* One ESC r with framebuffer lines first..first+n-1 whole: it starts on the
 * lowest line and continues upward, as the terminal's picture RAM runs
 * bottom-up. n must not be a multiple of 4 (a count with a zero low byte
 * fails on the terminal). */
static void send_chunk(unsigned char first, unsigned char n)
{
    unsigned int count = (unsigned int)n * FB_LINE;
    unsigned char line = first + n - 1;
    conout(ESC); conout('r'); conout(0); conout(0); conout(251 - line);
    conout(count & 0xFF); conout(count >> 8);
    for (;;) {
        con_write(framebuffer + line * FB_LINE, FB_LINE);
        if (line-- == first)
            break;
    }
}

/* Sends the band of lines collected in piece[] the cheaper way. */
static void piece_flush(void)
{
    unsigned char i, n = piece_n, k;
    unsigned int by_runs = 0, by_chunk;
    const unsigned char *d;
    if (!n)
        return;
    piece_n = 0;
    for (i = 0; i < n; i++)
        by_runs += piece_cost[i];
    k = (n & 3) ? n : n - 1;                /* the last line of a multiple of 4 goes as runs */
    by_chunk = RUN_GAP + k * FB_LINE + (k < n ? piece_cost[k] : 0);
    if (by_chunk < by_runs) {
        send_chunk(piece_first, k);
        if (k < n)
            runs(piece + k * piece_width, piece_first + k, piece_col0, piece_width);
        return;
    }
    for (i = 0, d = piece; i < n; i++, d += piece_width)
        runs(d, piece_first + i, piece_col0, piece_width);
}

/* Starts collecting lines that differ in bytes col0..col0+width-1. */
static void piece_start(unsigned char col0, unsigned char width)
{
    piece_flush();
    piece_col0 = col0;
    piece_width = width;
}

/* Adds a line whose picture goes from old to now over the band's bytes; a
 * line without changes ends the band. */
static void piece_line(unsigned char line, const unsigned char *now, const unsigned char *old)
{
    unsigned char *d;
    unsigned int cost;
    if (piece_n && line != piece_first + piece_n)
        piece_flush();
    d = piece + piece_n * piece_width;
    cost = xor_cost(d, now, old, piece_width);
    if (!cost) {
        piece_flush();
        return;
    }
    if (!piece_n)
        piece_first = line;
    piece_cost[piece_n] = cost;
    if (++piece_n == PIECE_LINES)
        piece_flush();
}

void screen_sync(void)
{
    unsigned char r, c, first, last = 0, y, width, lk;
    unsigned int base;
    unsigned char *s;
    for (r = 0; r < rows; r++) {
        first = 0xFF;
        for (c = 0; c < cols; c++)
            if (look(r, c) != shown[CELL(r, c)]) {
                if (first == 0xFF)
                    first = c;
                last = c;
            }
        if (first == 0xFF)
            continue;
        base = cell_offset(r, first);
        width = (last - first + 1) * g->cw;
        for (y = 0, s = saved; y < g->ch; y++, s += width)
            memcpy(s, framebuffer + base + y * FB_LINE, width);
        for (c = first; c <= last; c++) {
            lk = look(r, c);
            if (lk != shown[CELL(r, c)])
                draw_cell(r, c, lk);
        }
        piece_start(LEFT_BYTE + first * g->cw, width);
        for (y = 0, s = saved; y < g->ch; y++, s += width)
            piece_line(g->top + r * g->ch + y, framebuffer + base + y * FB_LINE, s);
        piece_flush();
    }
}

/* --- composition ---------------------------------------------------------------- */

void screen_compose(void)
{
    unsigned char r, c, line;
    video_clear();
    /* the frame, and the grid's closing column and row (the tiles carry the rest) */
    memcpy(framebuffer + frame_y0 * FB_LINE, pat_edge, FB_LINE);
    memcpy(framebuffer + frame_y1 * FB_LINE, pat_edge, FB_LINE);
    for (line = frame_y0 + 1; line < frame_y1; line++)
        memcpy(framebuffer + line * FB_LINE, pat_side, FB_LINE);
    for (line = g->top; line < grid_y1; line++)
        dots(framebuffer + line * FB_LINE, grid_x1, grid_x1);
    memcpy(framebuffer + grid_y1 * FB_LINE, pat_across, FB_LINE);
    /* labels: column numbers above, row letters in byte 0 */
    for (c = 0; c < cols; c++)
        video_blit(g->col_labels + c * g->cw * LABEL_LINES,
                   (frame_y0 - 2 - LABEL_LINES) * FB_LINE + LEFT_BYTE + c * g->cw, WH(g->cw, LABEL_LINES));
    for (r = 0; r < rows; r++)
        video_blit(row_labels + r * LABEL_LINES,
                   (unsigned int)(g->top + r * g->ch + (g->ch - LABEL_LINES) / 2) * FB_LINE, WH(1, LABEL_LINES));
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++)
            draw_cell(r, c, look(r, c));
}

/* --- fresh picture --------------------------------------------------------------- */

static void vector(unsigned char cmd, unsigned int x, unsigned char line)
{
    conout(ESC); conout(cmd); conout(x & 0xFF); conout(x >> 8); conout(251 - line);
}

static void stroke(unsigned int x0, unsigned char y0, unsigned int x1, unsigned char y1)
{
    vector('m', x0, y0);
    vector('M', x1, y1);
}

/* The grid (one line per row and column of cells, and the closing ones)
 * and the frame, drawn by the terminal. */
static void send_grid(void)
{
    unsigned char i;
    unsigned int x;
    for (i = 0; i <= rows; i++)
        stroke(grid_x0, g->top + i * g->ch, grid_x1, g->top + i * g->ch);
    for (i = 0, x = grid_x0; i <= cols; i++, x += g->cw * 8)
        stroke(x, g->top, x, grid_y1);
    stroke(frame_x0, frame_y0, frame_x1, frame_y0);
    vector('M', frame_x1, frame_y1);
    vector('M', frame_x0, frame_y1);
    vector('M', frame_x0, frame_y0);
}

/* After ESC 3 (a blank picture): the lines, then everything that differs from them. */
void screen_flush(void)
{
    unsigned char line, t = 0;
    const unsigned char *pat;
    unsigned char width = (frame_x1 >> 3) + 1;       /* nothing lies right of the frame */
    send_grid();
    piece_start(0, width);
    for (line = frame_y0 - 2 - LABEL_LINES; line <= frame_y1; line++) {   /* labels to frame */
        if (line == frame_y0 || line == frame_y1)
            pat = pat_edge;
        else if (line < frame_y0 || line > frame_y1)
            pat = pat_none;
        else if (line < g->top || line > grid_y1)
            pat = pat_side;
        else {
            pat = t == 0 ? pat_across : pat_down;
            if (++t == g->ch)
                t = 0;
        }
        piece_line(line, framebuffer + line * FB_LINE, pat);
    }
    piece_flush();
}
