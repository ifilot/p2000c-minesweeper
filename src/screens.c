/* SPDX-License-Identifier: GPL-3.0-only */
/* screens.c -- the text-mode screens: start and help.
 *
 * Both use the plain 80x24 text mode, so they appear instantly.
 */
#include "video.h"
#include "game.h"
#include "screens.h"
#include "saver.h"
#include "scores.h"
#include "version.h"

/* Character-ROM glyphs used by the text-mode screens. */
#define CH_BLOCK 0x9F                       /* full 8x12 block */
#define CH_H     0xD0                       /* box drawing: single lines */
#define CH_V     0xFA
#define CH_TL    0xA9
#define CH_TR    0xB9
#define CH_BL    0xAA
#define CH_BR    0xBA
#define ESC      27

/* --- help ------------------------------------------------------------------------ */

static const char *const HELP[] = {
    "MIJNENVEGER v" VERSION " voor de Philips P2000C" "              gecompileerd " BUILD_DATE,
    REPO_URL,
    "",
    "SPELREGELS",
    "  Onder de gesloten velden liggen mijnen verborgen. Open alle velden zonder",
    "  mijn om te winnen; opent u een mijn, dan is het spel verloren. Een geopend",
    "  veld toont hoeveel mijnen er in de acht velden eromheen liggen; een veld",
    "  zonder buurmijnen opent vanzelf zijn hele omgeving. Het eerste veld is altijd",
    "  veilig. Zet een vlag op een mijn. Staan er bij een getal evenveel vlaggen als",
    "  het getal aangeeft, dan opent RETURN op dat getal de andere velden eromheen.",
    "",
    "TOETSEN",
    "  Pijltjes of W A S D   cursor              Shift+W A S D   vijf velden verder",
    "  TAB                   volgend gesloten veld",
    "  G en dan bv. C12      ga naar rij C, kolom 12",
    "  RETURN of spatie      veld openen; op een getal: de velden eromheen openen",
    "  F                     vlag, vraagteken, weer leeg",
    "  H hulp   N nieuw spel   ESC ander niveau   Q stoppen   T (startscherm) tegels",
    "",
    "NIVEAUS",
    "  Beginner 9x9 met 10 mijnen, Gevorderd 16x16 met 40, Expert 30x16 met 99.",
    "  De beste tijd per niveau wordt bewaard in MINES.DAT.",
    "Druk op een toets om terug te keren.",
};

/* Clears the 80x24 text screen and hides the blinking cursor. */
void text_clear(void)
{
    con_at(ROWCOL(0, 0));
    conout(ESC); conout('k');
    conout(ESC); conout('c');
}

static void draw_help_page(void)
{
    unsigned char row;
    text_clear();
    for (row = 0; row < sizeof HELP / sizeof HELP[0]; row++) {
        con_at(ROWCOL(row, 0));
        con_puts(HELP[row]);
    }
}

static void help_page(void)
{
    draw_help_page();
    wait_key(draw_help_page);
}

/* Shows the rules from the game, then restores the board. Leaving graphics
 * mode clears the terminal's picture, but the framebuffer in RAM is intact. */
void help_screen(void)
{
    video_text();
    help_page();
    redraw_game_screen();
}

/* --- start screen -------------------------------------------------------------- */

/* MIJNEN / VEGER in a five-row block font; every pixel becomes two block
 * characters, which is close to square on the CRT. A sea mine beside it. */
static const char *const TITLE1[5] = {
    "#   # ###    # #   # #### #   #",
    "## ##  #     # ##  # #    ##  #",
    "# # #  #     # # # # ###  # # #",
    "#   #  #  #  # #  ## #    #  ##",
    "#   # ###  ##  #   # #### #   #",
};
static const char *const TITLE2[5] = {
    "#   # ####  ### #### ###     # # #",
    "#   # #    #    #    #  #     ###",
    "#   # ###  # ## ###  ###     #####",
    " # #  #    #  # #    # #      ###",
    "  #   ####  ### #### #  #    # # #",
};

static void put_repeat(unsigned char ch, unsigned char count)
{
    while (count--)
        conout(ch);
}

static void draw_box(unsigned char row, unsigned char col, unsigned char width, unsigned char height)
{
    unsigned char r;
    con_at(ROWCOL(row, col));
    conout(CH_TL); put_repeat(CH_H, width - 2); conout(CH_TR);
    for (r = row + 1; r < row + height - 1; r++) {
        con_at(ROWCOL(r, col)); conout(CH_V);
        con_at(ROWCOL(r, col + width - 1)); conout(CH_V);
    }
    con_at(ROWCOL(row + height - 1, col));
    conout(CH_BL); put_repeat(CH_H, width - 2); conout(CH_BR);
}

static void draw_title(const char *const *title, unsigned char row, unsigned char col)
{
    unsigned char r;
    const char *pixel;
    for (r = 0; r < 5; r++) {
        con_at(ROWCOL(row + r, col));
        for (pixel = title[r]; *pixel; pixel++) {
            conout(*pixel == '#' ? CH_BLOCK : ' ');
            conout(*pixel == '#' ? CH_BLOCK : ' ');
        }
    }
}

/* h:mm:ss, or dashes when there is no record. */
static void put_time(unsigned int s)
{
    unsigned int h, m;
    if (s == 0) {
        con_puts("-:--:--");
        return;
    }
    h = s / 3600;
    m = (s / 60) % 60;
    s %= 60;
    if (h > 9) {
        con_puts("9:59:59");
        return;
    }
    conout('0' + h); conout(':');
    conout('0' + m / 10); conout('0' + m % 10); conout(':');
    conout('0' + s / 10); conout('0' + s % 10);
}

static const char *const STYLE_NAME[TILE_STYLES] = { " Tegels: vierkanten ", " Tegels: strepen " };

static const char *const LEVEL_LINE[LEVELS] = {
    "1   Beginner     9 x 9    10 mijnen",
    "2   Gevorderd   16 x 16   40 mijnen",
    "3   Expert      30 x 16   99 mijnen",
};

static void draw_start_screen(void)
{
    unsigned char i;

    text_clear();
    draw_box(0, 0, 80, 23);                  /* row 23 stays empty: writing its last cell scrolls */
    draw_title(TITLE1, 2, 9);
    draw_title(TITLE2, 8, 9);
    con_at(ROWCOL(14, 29)); con_puts("voor de Philips P2000C");
    con_at(ROWCOL(15, 10)); con_puts("versie " VERSION "   -   " REPO_URL);

    draw_box(16, 11, 58, 5);
    con_at(ROWCOL(16, 14)); con_puts(" Kies een niveau ");
    con_at(ROWCOL(16, 52)); con_puts(" Beste tijd ");
    for (i = 0; i < LEVELS; i++) {
        con_at(ROWCOL(17 + i, 14));
        con_puts(LEVEL_LINE[i]);
        con_at(ROWCOL(17 + i, 55));
        put_time(best_time[i]);
    }
    con_at(ROWCOL(20, 14)); con_puts(STYLE_NAME[tile_style]);
    con_at(ROWCOL(21, 7));  con_puts("1, 2 of 3: spelen    T: tegels    H: spelregels    Q: terug naar CP/M");
}

/* Text-mode start screen; returns the chosen level 1-3, or 0 to leave the program. */
unsigned char start_screen(void)
{
    unsigned char key;
    draw_start_screen();
    for (;;) {
        key = wait_key(draw_start_screen);
        if (key >= '1' && key <= '3')
            return key - '0';
        if (key == 'q' || key == 'Q')
            return 0;
        if (key == 't' || key == 'T') {          /* the other tile style, remembered */
            tile_style = (tile_style + 1) % TILE_STYLES;
            scores_save();
            draw_start_screen();
        }
        if (key == 'h' || key == 'H') {
            help_page();
            draw_start_screen();
        }
    }
}
