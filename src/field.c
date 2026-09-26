/* SPDX-License-Identifier: GPL-3.0-only */
/* field.c -- the minefield: cells, mine placement, opening, flags.
 *
 * Empty areas are opened with an explicit stack of (row, column) pairs; a
 * cell is marked open when it is pushed, so no cell is pushed twice and the
 * stack never holds more than the board. Random numbers come from a 16-bit
 * xorshift generator that is stirred with the refresh register and the
 * timer at every keypress, so the layout depends on the player's timing.
 */
#include "field.h"

unsigned char cells[MAX_CELLS];
unsigned char cols, rows, mines;
unsigned int ncells;
unsigned int opened;
unsigned char flags;
unsigned char laid;
unsigned int boom_cell;
unsigned int row_start[MAX_ROWS];

static unsigned int rng_state = 0xACE1;
static unsigned char stack_r[MAX_CELLS], stack_c[MAX_CELLS];

void field_stir(unsigned int bits)
{
    rng_state ^= bits;
    if (rng_state == 0)
        rng_state = 0xACE1;
}

/* xorshift16 (7, 9, 8): full period over the nonzero states. */
static unsigned int random16(void)
{
    rng_state ^= rng_state << 7;
    rng_state ^= rng_state >> 9;
    rng_state ^= rng_state << 8;
    return rng_state;
}

void field_init(unsigned char ncols, unsigned char nrows, unsigned char nmines)
{
    unsigned int i;
    unsigned char r;
    cols = ncols;
    rows = nrows;
    mines = nmines;
    ncells = (unsigned int)ncols * nrows;
    for (r = 0, i = 0; r < nrows; r++, i += ncols)
        row_start[r] = i;
    for (i = 0; i < MAX_CELLS; i++)
        cells[i] = 0;
    opened = 0;
    flags = 0;
    laid = 0;
    boom_cell = MAX_CELLS;
}

static unsigned char near(unsigned char a, unsigned char b)
{
    return (unsigned char)(a - b + 1) <= 2;  /* |a - b| <= 1 */
}

/* Lays the mines anywhere except on (r0, c0) and its neighbours, then counts. */
static void lay_mines(unsigned char r0, unsigned char c0)
{
    unsigned char n = 0, r, c;
    signed char dr, dc;
    unsigned int i;
    while (n < mines) {
        r = (unsigned char)(random16() % rows);
        c = (unsigned char)(random16() % cols);
        i = CELL(r, c);
        if ((cells[i] & F_MINE) || (near(r, r0) && near(c, c0)))
            continue;
        cells[i] |= F_MINE;
        n++;
    }
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++) {
            n = 0;
            for (dr = -1; dr <= 1; dr++)
                for (dc = -1; dc <= 1; dc++) {
                    if ((!dr && !dc) || (unsigned char)(r + dr) >= rows || (unsigned char)(c + dc) >= cols)
                        continue;
                    if (cells[CELL(r + dr, c + dc)] & F_MINE)
                        n++;
                }
            cells[CELL(r, c)] |= n;
        }
    laid = 1;
}

unsigned char field_open(unsigned char r, unsigned char c)
{
    unsigned int i = CELL(r, c), sp = 0;
    signed char dr, dc;
    unsigned char nr, nc;
    if (cells[i] & (F_OPEN | F_FLAG))
        return OPEN_NONE;
    if (!laid)
        lay_mines(r, c);
    if (cells[i] & F_MINE) {
        cells[i] = (cells[i] | F_OPEN) & ~F_QUERY;
        if (boom_cell == MAX_CELLS)
            boom_cell = i;
        return OPEN_BOOM;
    }
    cells[i] = (cells[i] | F_OPEN) & ~F_QUERY;
    opened++;
    stack_r[sp] = r;
    stack_c[sp++] = c;
    while (sp) {
        sp--;
        r = stack_r[sp];
        c = stack_c[sp];
        if (cells[CELL(r, c)] & F_COUNT)
            continue;
        for (dr = -1; dr <= 1; dr++)
            for (dc = -1; dc <= 1; dc++) {
                nr = r + dr;
                nc = c + dc;
                if (nr >= rows || nc >= cols)
                    continue;
                i = CELL(nr, nc);
                if (cells[i] & (F_OPEN | F_FLAG))
                    continue;
                cells[i] = (cells[i] | F_OPEN) & ~F_QUERY;   /* an empty cell has no mined neighbours */
                opened++;
                stack_r[sp] = nr;
                stack_c[sp++] = nc;
            }
    }
    return OPEN_OK;
}

unsigned char field_chord(unsigned char r, unsigned char c)
{
    unsigned char n = 0, nr, nc, result = OPEN_NONE, got;
    signed char dr, dc;
    unsigned char cell = cells[CELL(r, c)];
    if (!(cell & F_OPEN) || !(cell & F_COUNT))
        return OPEN_NONE;
    for (dr = -1; dr <= 1; dr++)
        for (dc = -1; dc <= 1; dc++) {
            nr = r + dr;
            nc = c + dc;
            if (nr < rows && nc < cols && (cells[CELL(nr, nc)] & F_FLAG))
                n++;
        }
    if (n != (cell & F_COUNT))
        return OPEN_NONE;
    for (dr = -1; dr <= 1; dr++)
        for (dc = -1; dc <= 1; dc++) {
            nr = r + dr;
            nc = c + dc;
            if (nr >= rows || nc >= cols)
                continue;
            got = field_open(nr, nc);
            if (got > result)
                result = got;
        }
    return result;
}

unsigned char field_mark(unsigned char r, unsigned char c)
{
    unsigned char *cell = &cells[CELL(r, c)];
    if (*cell & F_OPEN)
        return 0;
    if (*cell & F_FLAG) {
        *cell = (*cell & ~F_FLAG) | F_QUERY;
        flags--;
    } else if (*cell & F_QUERY)
        *cell &= ~F_QUERY;
    else {
        *cell |= F_FLAG;
        flags++;
    }
    return 1;
}

void field_flag_all(void)
{
    unsigned int i;
    for (i = 0; i < ncells; i++)
        if ((cells[i] & (F_MINE | F_FLAG)) == F_MINE)
            cells[i] = (cells[i] | F_FLAG) & ~F_QUERY;
    flags = mines;
}
