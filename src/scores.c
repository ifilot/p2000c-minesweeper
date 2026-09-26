/* SPDX-License-Identifier: GPL-3.0-only */
/* scores.c -- the best time per level and the tile style, kept in MINES.DAT.
 *
 * One 128-byte CP/M record on the current drive: the magic "MS", a format
 * byte, a little-endian word of seconds per level, then the tile style.
 * Format 1 (version 1.0.0) numbered the styles differently, so its style
 * is ignored and its best times kept. Plain BDOS
 * sequential file calls; a file that is missing or does not start with the
 * magic means no records yet.
 */
#include "video.h"
#include "scores.h"

#define F_OPEN   15
#define F_CLOSE  16
#define F_READ   20
#define F_WRITE  21
#define F_MAKE   22
#define F_DMA    26
#define FAILED   0xFF
#define STYLE_AT (3 + 2 * LEVELS)
#define FORMAT   2

unsigned int best_time[LEVELS];
unsigned char tile_style;

static unsigned char fcb[36];
static unsigned char record[128];
static const char NAME[11] = { 'M', 'I', 'N', 'E', 'S', ' ', ' ', ' ', 'D', 'A', 'T' };

static void fcb_init(void)
{
    unsigned char i;
    for (i = 0; i < sizeof fcb; i++)
        fcb[i] = 0;                         /* drive 0: the current drive */
    for (i = 0; i < sizeof NAME; i++)
        fcb[1 + i] = NAME[i];
    bdos((unsigned int)record, F_DMA);
}

void scores_load(void)
{
    unsigned char i;
    for (i = 0; i < LEVELS; i++)
        best_time[i] = 0;
    tile_style = 0;
    fcb_init();
    if (bdos((unsigned int)fcb, F_OPEN) == FAILED)
        return;
    if (bdos((unsigned int)fcb, F_READ) == 0 && record[0] == 'M' && record[1] == 'S'
        && (record[2] == 1 || record[2] == FORMAT)) {
        for (i = 0; i < LEVELS; i++)
            best_time[i] = record[3 + 2 * i] | (record[4 + 2 * i] << 8);
        if (record[2] == FORMAT && record[STYLE_AT] < TILE_STYLES)
            tile_style = record[STYLE_AT];
    }
    bdos((unsigned int)fcb, F_CLOSE);
}

void scores_save(void)
{
    unsigned char i;
    for (i = 0; i < sizeof record; i++)
        record[i] = 0x1A;                   /* CP/M end-of-file filler */
    record[0] = 'M';
    record[1] = 'S';
    record[2] = FORMAT;
    for (i = 0; i < LEVELS; i++) {
        record[3 + 2 * i] = best_time[i] & 0xFF;
        record[4 + 2 * i] = best_time[i] >> 8;
    }
    record[STYLE_AT] = tile_style;
    fcb_init();
    if (bdos((unsigned int)fcb, F_OPEN) == FAILED && bdos((unsigned int)fcb, F_MAKE) == FAILED)
        return;
    fcb[32] = 0;                            /* record 0 */
    bdos((unsigned int)fcb, F_WRITE);
    bdos((unsigned int)fcb, F_CLOSE);
}
