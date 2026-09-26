/* SPDX-License-Identifier: GPL-3.0-only */
/* scores.h -- the best time per level and the tile style, kept in
 * MINES.DAT on the current drive. */
#ifndef SCORES_H
#define SCORES_H

#include "game.h"

#define TILE_STYLES 3                       /* 0 raster (dither), 1 vierkanten (squares), 2 strepen */

extern unsigned int best_time[LEVELS];      /* seconds; 0 = none yet */
extern unsigned char tile_style;            /* closed-tile picture */

extern void scores_load(void);              /* missing or foreign file: no records */
extern void scores_save(void);

#endif
