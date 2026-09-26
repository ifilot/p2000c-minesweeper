/* SPDX-License-Identifier: GPL-3.0-only */
/* screen.h -- the board picture: composition in the framebuffer and uploads.
 *
 * Each level has its own cell size, the largest square one that fits: dots
 * have a 3:5 pitch on the CRT, so 40x24, 24x14 and 16x10 dots are square. */
#ifndef SCREEN_H
#define SCREEN_H

/* Selects the geometry and tiles of a level (0..2). */
extern void screen_level(unsigned char level);

/* Composes the whole picture (frame, labels, every cell) in RAM. */
extern void screen_compose(void);

/* Sends the composed picture after ESC 3: the closed buttons and the frame
 * as vectors, everything else as row uploads of what the vectors lack. */
extern void screen_flush(void);

/* Redraws the cells whose appearance changed and sends the bytes that differ. */
extern void screen_sync(void);

/* Sends the lit runs of every framebuffer line (the NO_ERASE fallback). */
extern void flush_sparse(void);

#endif
