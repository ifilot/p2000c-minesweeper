/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.c -- the board picture.
 *
 * Everything is composed in the 16 KiB framebuffer; every cell is one whole
 * tile (sprites.h). The 19200-baud link to the terminal is the bottleneck,
 * so two things keep the traffic down:
 *
 * - A fresh picture is not uploaded. A closed tile (in either style: three
 *   concentric squares, or a hatched button) is the result of an ordered
 *   list of full-length line operations, each setting (ESC M) or erasing
 *   (ESC v) one row or one column of every tile, the last one on a dot
 *   winning. A board of closed tiles is therefore a lattice of long
 *   straight lines that the terminal draws itself; tools/gen_sprites.py
 *   derives the lists and checks them. Only the labels and the cells that
 *   are not plain closed tiles (the cursor, and open cells after a return
 *   from the help page) go as row uploads of what differs from the lattice. (Built with -DNO_ERASE, for a
 *   terminal whose ESC v would not erase, the whole picture is uploaded.)
 * - Each cell keeps the appearance last sent. After a change, the span of
 *   changed cells in each board row is redrawn and compared with its
 *   previous bytes, and only the differing runs of each line are sent.
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
#define LOOK_NONE    0xFF                   /* forces a redraw */
#define ESC          27
#define RUN_GAP      7                      /* an ESC r header costs 7 bytes */
#define ERASE_COST   10                     /* ESC m and ESC v with their coordinates */

struct geometry {
    const unsigned char *tiles;             /* the shared tiles (TILE_COUNT - STYLE_TILES) */
    const unsigned char *style_tiles;       /* STYLE_TILES per style */
    const unsigned char *cursor;
    const unsigned char *col_labels;        /* cw bytes x 7 lines per column */
    const unsigned char *ops[STYLE_COUNT];  /* closed-tile line operations per style */
    unsigned char n_ops[STYLE_COUNT];
    unsigned char cw, ch;                   /* cell: bytes, lines */
    unsigned char top;                      /* board's first line */
};

static const struct geometry geometries[3] = {
    { &tiles_40[0][0], &style_tiles_40[0][0][0], cursor_40, col_labels_40,
      { ops_40_0, ops_40_1 }, { sizeof ops_40_0, sizeof ops_40_1 }, 5, 24, 20 },
    { &tiles_24[0][0], &style_tiles_24[0][0][0], cursor_24, col_labels_24,
      { ops_24_0, ops_24_1 }, { sizeof ops_24_0, sizeof ops_24_1 }, 3, 14, 14 },
    { &tiles_16[0][0], &style_tiles_16[0][0][0], cursor_16, col_labels_16,
      { ops_16_0, ops_16_1 }, { sizeof ops_16_0, sizeof ops_16_1 }, 2, 10, 40 },
};

static const struct geometry *g;
static unsigned int tile_size;              /* bytes per tile */
static unsigned int board_x0, board_x1;     /* first and last dot of the board */
static unsigned char board_y1;              /* last line of the board */
static unsigned int frame_x0, frame_x1;
static unsigned char frame_y0, frame_y1;

static unsigned char shown[MAX_CELLS];      /* appearance last sent per cell */
static unsigned char saved[5 * 9 * 24];     /* a board row's changed span before redrawing (largest: beginner) */
static unsigned char delta[FB_LINE];        /* bytes to send: new XOR old */

void screen_level(unsigned char level)
{
    g = &geometries[level];
    tile_size = g->cw * g->ch;
    board_x0 = LEFT_BYTE * 8;
    board_x1 = board_x0 + (unsigned int)cols * g->cw * 8 - 1;
    board_y1 = g->top + rows * g->ch - 1;
    /* one dark dot or line between the frame and the buttons' outer edge */
    frame_x0 = board_x0 - 3;
    frame_x1 = board_x1 + 2;
    frame_y0 = g->top - 2;
    frame_y1 = board_y1 + 2;
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

/* A closed button under the cursor loses its hatching, then the cursor frame is OR-ed on. */
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

/* Walks the nonzero runs of buf[0..width) (runs closer than a header are
 * joined) and, if `send`, uploads each as part of framebuffer line `line`
 * starting at byte column col0. Returns the bytes that costs on the link. */
static unsigned int runs(const unsigned char *buf, unsigned char line, unsigned char col0,
                         unsigned char width, unsigned char send)
{
    unsigned char x = 0, first, last;
    unsigned int bytes = 0;
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
        bytes += RUN_GAP + last - first + 1;
        if (send)
            video_flush_rect(COLROW(col0 + first, line), WH(last - first + 1, 1));
    }
    return bytes;
}

static void stroke(unsigned char cmd, unsigned int x0, unsigned char y0, unsigned int x1, unsigned char y1);

/* Brings one line of the terminal's picture from old to now, where delta[]
 * holds now XOR old over `width` bytes from byte column col0. Either the
 * changed runs are uploaded, or -- when cheaper, as for a hatched button
 * that opens into a dark cell -- the changed stretch is erased with one
 * vector and only its lit bytes are uploaded. */
static void send_line(unsigned char line, unsigned char col0, unsigned char width, const unsigned char *now)
{
    unsigned char a = 0, b = width;
    unsigned int upload;
    while (a < width && delta[a] == 0)
        a++;
    if (a == width)
        return;
    while (delta[b - 1] == 0)
        b--;
    upload = runs(delta, line, col0, width, 0);
#ifndef NO_ERASE
    if (ERASE_COST + runs(now + a, line, col0 + a, b - a, 0) < upload) {
        stroke('v', (col0 + a) * 8, line, (col0 + b) * 8 - 1, line);
        runs(now + a, line, col0 + a, b - a, 1);
        return;
    }
#endif
    runs(delta, line, col0, width, 1);
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
        for (y = 0, s = saved; y < g->ch; y++, s += width) {
            memcpy(delta, framebuffer + base + y * FB_LINE, width);
            mem_xor(delta, s, width);
            send_line(g->top + r * g->ch + y, LEFT_BYTE + first * g->cw, width, framebuffer + base + y * FB_LINE);
        }
    }
}

/* --- composition ---------------------------------------------------------------- */

static void fb_dots(unsigned char line, unsigned int x0, unsigned int x1)
{
    unsigned int x;
    for (x = x0; x <= x1; x++)
        framebuffer[line * FB_LINE + (x >> 3)] |= 0x80 >> (x & 7);
}

static void compose_frame(void)
{
    unsigned char line;
    fb_dots(frame_y0, frame_x0, frame_x1);
    fb_dots(frame_y1, frame_x0, frame_x1);
    for (line = frame_y0 + 1; line < frame_y1; line++) {
        fb_dots(line, frame_x0, frame_x0);
        fb_dots(line, frame_x1, frame_x1);
    }
}

static void compose_labels(void)
{
    unsigned char i;
    unsigned int wh = WH(g->cw, LABEL_LINES);
    for (i = 0; i < cols; i++)
        video_blit(g->col_labels + i * g->cw * LABEL_LINES,
                   (frame_y0 - 2 - LABEL_LINES) * FB_LINE + LEFT_BYTE + i * g->cw, wh);
    for (i = 0; i < rows; i++)
        video_blit(row_labels + i * LABEL_LINES,
                   (unsigned int)(g->top + i * g->ch + (g->ch - LABEL_LINES) / 2) * FB_LINE, WH(1, LABEL_LINES));
}

void screen_compose(void)
{
    unsigned char r, c;
    video_clear();
    compose_frame();
    compose_labels();
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++)
            draw_cell(r, c, look(r, c));
}

/* --- fresh picture --------------------------------------------------------------- */

static void vector(unsigned char cmd, unsigned int x, unsigned char line)
{
    conout(ESC); conout(cmd); conout(x & 0xFF); conout(x >> 8); conout(251 - line);
}

/* A straight line; cmd 'M' draws it, 'v' erases it. */
static void stroke(unsigned char cmd, unsigned int x0, unsigned char y0, unsigned int x1, unsigned char y1)
{
    vector('m', x0, y0);
    vector(cmd, x1, y1);
}

/* Replays the style's line operations over the whole board, then the frame. */
static void send_lattice(void)
{
    const unsigned char *op = g->ops[tile_style];
    unsigned char k, n = g->n_ops[tile_style], i, offset, cmd;
    unsigned char y;
    unsigned int x;
    for (k = 0; k < n; k++, op++) {
        offset = *op & 0x3F;
        cmd = (*op & OP_SET) ? 'M' : 'v';
        if (*op & OP_COL)
            for (i = 0, x = board_x0 + offset; i < cols; i++, x += g->cw * 8)
                stroke(cmd, x, g->top, x, board_y1);
        else
            for (i = 0, y = g->top + offset; i < rows; i++, y += g->ch)
                stroke(cmd, board_x0, y, board_x1, y);
    }
    stroke('M', frame_x0, frame_y0, frame_x1, frame_y0);
    stroke('M', frame_x1, frame_y0, frame_x1, frame_y1);
    stroke('M', frame_x1, frame_y1, frame_x0, frame_y1);
    stroke('M', frame_x0, frame_y1, frame_x0, frame_y0);
}

void screen_flush(void)
{
    unsigned char r, c, y, first, last = 0, width, label_top = frame_y0 - 2 - LABEL_LINES;
    unsigned int base;
    const unsigned char *closed = tile(TILE_CLOSED);
#ifdef NO_ERASE
    flush_sparse();                         /* fallback without ESC v: upload everything */
    return;
#endif
    send_lattice();
    width = cols * g->cw;
    for (y = 0; y < LABEL_LINES; y++) {
        runs(framebuffer + (label_top + y) * FB_LINE + LEFT_BYTE, label_top + y, LEFT_BYTE, width, 1);
    }
    for (r = 0; r < rows; r++) {
        video_flush_rect(COLROW(0, g->top + r * g->ch + (g->ch - LABEL_LINES) / 2), WH(1, LABEL_LINES));
        first = 0xFF;
        for (c = 0; c < cols; c++)
            if (shown[CELL(r, c)] != TILE_CLOSED) {
                if (first == 0xFF)
                    first = c;
                last = c;
            }
        if (first == 0xFF)
            continue;
        base = cell_offset(r, first);
        width = (last - first + 1) * g->cw;
        for (y = 0; y < g->ch; y++) {
            memcpy(delta, framebuffer + base + y * FB_LINE, width);
            for (c = 0; c < width; c += g->cw)
                mem_xor(delta + c, closed + y * g->cw, g->cw);
            send_line(g->top + r * g->ch + y, LEFT_BYTE + first * g->cw, width, framebuffer + base + y * FB_LINE);
        }
    }
}

/* Uploads only the lit parts of the framebuffer: one ESC r per run of
 * non-zero bytes in a line (short gaps are bridged). */
void flush_sparse(void)
{
    unsigned char line;
    for (line = 0; line < FB_LINES; line++) {
        runs(framebuffer + line * FB_LINE, line, 0, FB_LINE, 1);
    }
}
