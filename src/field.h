/* SPDX-License-Identifier: GPL-3.0-only */
/* field.h -- the minefield: cells, mine placement, opening, flags.
 *
 * Cells are row-major (index = row * cols + col), at most 30x16. The mines
 * are laid on the first open, never on or next to that cell, so the first
 * open always clears an area. */
#ifndef FIELD_H
#define FIELD_H

#define MAX_COLS  30
#define MAX_ROWS  16
#define MAX_CELLS (MAX_COLS * MAX_ROWS)

#define F_MINE   0x80
#define F_OPEN   0x40
#define F_FLAG   0x20
#define F_QUERY  0x10                       /* question mark */
#define F_COUNT  0x0F                       /* mines among the neighbours */

/* field_open() / field_chord() results */
#define OPEN_NONE 0                         /* nothing to open (flagged, already open, flags missing) */
#define OPEN_OK   1
#define OPEN_BOOM 2

extern unsigned char cells[MAX_CELLS];
extern unsigned char cols, rows, mines;
extern unsigned int ncells;
extern unsigned int opened;                 /* safe cells opened */
extern unsigned char flags;                 /* flags placed */
extern unsigned char laid;                  /* mines placed yet (first open done) */
extern unsigned int boom_cell;              /* the mine that went off */
extern unsigned int row_start[MAX_ROWS];    /* index of each row's first cell */

#define CELL(r, c) (row_start[r] + (c))

/* Empties the field for a board of the given size. */
extern void field_init(unsigned char ncols, unsigned char nrows, unsigned char nmines);

/* Stirs unpredictable bits (refresh register, timer) into the generator. */
extern void field_stir(unsigned int bits);

/* Opens a closed cell (question marks too, flags not); an empty cell opens
 * its whole empty area and its border. Lays the mines on the first call. */
extern unsigned char field_open(unsigned char r, unsigned char c);

/* On an open number whose flags are all placed: opens the other neighbours. */
extern unsigned char field_chord(unsigned char r, unsigned char c);

/* Closed cell: nothing -> flag -> question mark -> nothing. Returns 0 if open. */
extern unsigned char field_mark(unsigned char r, unsigned char c);

/* Every safe cell open? */
#define field_cleared() (opened == ncells - mines)

/* After a win: a flag on every mine. */
extern void field_flag_all(void);

#endif
