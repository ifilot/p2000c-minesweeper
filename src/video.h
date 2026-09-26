/* SPDX-License-Identifier: GPL-3.0-only */
/* video.h -- framebuffer and terminal-board primitives (see video.asm).
 *
 * Framebuffer offsets are byte positions (line * FB_LINE + column); WH packs a
 * width in bytes with a row count, ROWCOL/COLROW pack text or dot positions. */
#ifndef VIDEO_H
#define VIDEO_H

#define FB_LINE 64          /* bytes per framebuffer line */
#define FB_LINES 252

extern unsigned char framebuffer[FB_LINE * FB_LINES];

/* Pack (width in bytes, rows) or (row, column) into one word argument. */
#define WH(w, h)      ((unsigned int)(w) | ((unsigned int)(h) << 8))
#define ROWCOL(r, c)  ((unsigned int)(r) | ((unsigned int)(c) << 8))
#define COLROW(c, r)  ((unsigned int)(c) | ((unsigned int)(r) << 8))

extern void video_clear(void);
extern void video_fill(unsigned int offset, unsigned int value, unsigned int count);
extern void video_or_col(unsigned int offset, unsigned int mask, unsigned int count);
extern void video_blit(const unsigned char *sprite, unsigned int offset, unsigned int wh);
extern void video_xor(const unsigned char *sprite, unsigned int offset, unsigned int wh);
extern void video_copy(const unsigned char *sprite, unsigned int offset, unsigned int wh);
extern void video_flush_rect(unsigned int col_row, unsigned int wh);
extern void video_graphics(void);
extern void video_text(void);

extern void conout(unsigned int c) __z88dk_fastcall;
extern unsigned char conin(void);
extern unsigned char conready(void);          /* key waiting? (BIOS CONST) */
extern void con_puts(const char *s) __z88dk_fastcall;
extern void con_at(unsigned int row_col) __z88dk_fastcall;
extern void con_write(const unsigned char *p, unsigned int n);   /* n raw bytes */

extern void mem_xor(unsigned char *dst, const unsigned char *src, unsigned int n);   /* dst ^= src */
extern unsigned int runs_cost(const unsigned char *buf, unsigned int width);        /* link bytes, see video.asm */
/* dst = a ^ b over n (1..255) bytes; returns runs_cost(dst, n). */
extern unsigned int xor_cost(unsigned char *dst, const unsigned char *a, const unsigned char *b, unsigned int n);
extern unsigned char entropy(void);           /* Z80 refresh register */
extern unsigned char bdos(unsigned int de, unsigned int c);   /* BDOS function c */

#endif
