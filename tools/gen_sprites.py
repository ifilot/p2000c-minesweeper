#!/usr/bin/env python3
"""Generate src/sprites.h: the tile pictures for the 512x252 high-res mode.

Geometry: the terminal draws graphics at the text dot pitch, and the 9-inch
CRT gives dots a 3:5 horizontal-to-vertical pitch (p2000c-emulator,
docs/hardware.md). The three board sizes use the largest square cell that
fits the screen: 40x24 dots (beginner, 9x9), 24x14 (intermediate, 16x16)
and 16x10 (expert, 30x16).

Every cell is one whole tile, byte aligned, row-major, MSB = leftmost dot.
Closed tiles come in two styles, chosen on the start screen:

  0 vierkanten  three concentric squares
  1 strepen     a raised button with a scanline-hatched face

Either closed tile is the result of an ordered list of full-length line
operations -- set or erase one row, or one column, of every tile on the
board. The terminal applies its line commands (ESC M draws, ESC v erases)
in order, the last one on a dot winning, so a fresh board goes to it as
those long lines instead of uploads. The generator derives the lists and
checks that each reproduces its closed tile exactly.

Labels reuse the terminal's own 8x12 character-ROM glyphs (font sheet from
the sibling p2000c-emulator checkout); the expert board's 30 column numbers
use a narrow 4x7 digit font so that neighbouring labels stay apart.

  python3 tools/gen_sprites.py [--preview]    (preview: build/sprites_preview.png)
"""
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FONT_SHEET = ROOT.parent / "p2000c-emulator/assets/font/P2000C font mini.png"

# name, columns, rows, cell width (dots), cell height (lines)
LEVELS = [("beginner", 9, 9, 40, 24), ("gevorderd", 16, 16, 24, 14), ("expert", 30, 16, 16, 10)]

STYLES = ["vierkanten", "strepen"]

# Strepen, in four passes (the order the lines are sent in): set rows, erase
# gap columns, set columns, erase gap rows. Each button has a scanline-
# hatched face, a thin top/left highlight, a heavy bottom/right shadow and a
# dark gap around it.
LATTICE = {
    40: dict(set_rows=[1, 4, 7, 10, 13, 16, 19, 21, 22], gap_cols=[38, 39],
             set_cols=[1, 35, 36, 37], gap_rows=[0, 23]),
    24: dict(set_rows=[1, 3, 5, 7, 9, 11, 12], gap_cols=[23],
             set_cols=[1, 20, 21, 22], gap_rows=[0]),
    16: dict(set_rows=[1, 3, 5, 7, 8], gap_cols=[15],
             set_cols=[1, 13, 14], gap_rows=[0, 9]),
}

# Vierkanten: the squares from the outside in, (x0, y0, x1, y1) inclusive.
# Spacing is a dark line between squares vertically and two or three dark
# dots horizontally, which look alike on the tube (dots are 3:5). The
# expert's innermost square is a 4x1 dash: ten lines hold no more.
RINGS = {
    40: [(1, 1, 38, 22), (4, 3, 35, 20), (7, 5, 32, 18)],
    24: [(1, 1, 22, 12), (4, 3, 19, 10), (7, 5, 16, 8)],
    16: [(1, 0, 14, 8), (3, 2, 12, 6), (6, 4, 9, 4)],
}

# The first STYLE_TILES tiles differ per style.
STYLE_TILES = ["closed", "selected", "flag", "question"]
TILES = ["closed", "selected", "flag", "question", "open0", "open1", "open2", "open3", "open4",
         "open5", "open6", "open7", "open8", "mine", "boom", "wrong"]

# The expert button's face is only 11x6 dots: hand-drawn symbols.
SMALL_FLAG = ["...####....", ".######....", "...####....", "......#....", "....#####..", "...#######."]
SMALL_QUESTION = [".####.", "##..##", "...##.", "..##..", "......", "..##.."]

NARROW_DIGITS = {
    "0": [".##.", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."],
    "1": ["..#.", ".##.", "..#.", "..#.", "..#.", "..#.", ".###"],
    "2": [".##.", "#..#", "...#", "..#.", ".#..", "#...", "####"],
    "3": ["###.", "...#", "...#", ".##.", "...#", "...#", "###."],
    "4": ["#..#", "#..#", "#..#", "####", "...#", "...#", "...#"],
    "5": ["####", "#...", "###.", "...#", "...#", "#..#", ".##."],
    "6": [".##.", "#...", "#...", "###.", "#..#", "#..#", ".##."],
    "7": ["####", "...#", "..#.", "..#.", ".#..", ".#..", ".#.."],
    "8": [".##.", "#..#", "#..#", ".##.", "#..#", "#..#", ".##."],
    "9": [".##.", "#..#", "#..#", ".###", "...#", "...#", ".##."],
}


class Bitmap:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0] * w for _ in range(h)]

    def copy(self):
        b = Bitmap(self.w, self.h)
        b.px = [row[:] for row in self.px]
        return b

    def set(self, x, y, v=1):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y][x] = v

    def get(self, x, y):
        return self.px[y][x] if 0 <= x < self.w and 0 <= y < self.h else 0

    def rect(self, x0, y0, x1, y1, v=1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(x, y, v)

    def paste(self, pattern, x0, y0, v=1):
        for y, row in enumerate(pattern):
            for x, on in enumerate(row):
                if on:
                    self.set(x0 + x, y0 + y, v)

    def to_bytes(self):
        out = bytearray()
        for row in self.px:
            for b in range(0, self.w, 8):
                bits = 0
                for x in range(b, b + 8):
                    bits = (bits << 1) | (row[x] if x < self.w else 0)
                out.append(bits)
        return bytes(out)


# --- glyphs ----------------------------------------------------------------------

def rom_glyph(sheet, ch):
    """8x12 ROM glyph as rows of 0/1 (ink occupies rows 1-7, columns 1-6)."""
    code = ord(ch)
    return [[int(sheet.getpixel(((code & 15) * 12 + x, (code >> 4) * 12 + y))[1] != 0)
             for x in range(8)] for y in range(12)]


def ink(glyph):
    """Crops a glyph to its lit rows and columns."""
    ys = [y for y, row in enumerate(glyph) if any(row)]
    xs = [x for x in range(len(glyph[0])) if any(row[x] for row in glyph)]
    return [row[xs[0]:xs[-1] + 1] for row in glyph[ys[0]:ys[-1] + 1]]


def scale(pattern, sx, sy):
    return [[v for v in row for _ in range(sx)] for row in pattern for _ in range(sy)]


def bold(pattern):
    """Widens every stroke by one dot to the right."""
    w = len(pattern[0]) + 1
    return [[int((x < w - 1 and row[x]) or (x > 0 and row[x - 1])) for x in range(w)] for row in pattern]


def narrow(ch):
    return [[int(c == "#") for c in row] for row in NARROW_DIGITS[ch]]


# --- tiles -------------------------------------------------------------------------

def lattice_ops(w):
    """Strepen: (kind, value, offset) in sending order."""
    lat = LATTICE[w]
    return ([("row", 1, y) for y in lat["set_rows"]] + [("col", 0, x) for x in lat["gap_cols"]]
            + [("col", 1, x) for x in lat["set_cols"]] + [("row", 0, y) for y in lat["gap_rows"]])


def ring_ops(w, h):
    """Vierkanten: the operations by priority, highest first, then reversed
    into sending order. Gaps around the outer square win over everything;
    each square's edge rows, then its side columns, win over the rows and
    columns erased between it and the next square inside, which in turn
    win over everything of the inner squares."""
    rings = RINGS[w]
    x0, y0, x1, y1 = rings[0]
    prio = [("row", 0, y) for y in range(h) if not y0 <= y <= y1]
    prio += [("col", 0, x) for x in range(w) if not x0 <= x <= x1]
    for i, (ax, ay, bx, by) in enumerate(rings):
        prio += [("row", 1, ay), ("row", 1, by)] if ay != by else [("row", 1, ay)]
        prio += [("col", 1, ax), ("col", 1, bx)]
        if i + 1 < len(rings):
            nax, nay, nbx, nby = rings[i + 1]
            prio += [("row", 0, y) for y in list(range(ay + 1, nay)) + list(range(nby + 1, by))]
            prio += [("col", 0, x) for x in list(range(ax + 1, nax)) + list(range(nbx + 1, bx))]
    return prio[::-1]


def prune(ops, w, h):
    """Drops every operation that the tile does not need (an erase of dots
    nothing set before it, a set that later operations cover anyway)."""
    want = apply_ops(ops, w, h).px
    k = 0
    while k < len(ops):
        trial = ops[:k] + ops[k + 1:]
        if apply_ops(trial, w, h).px == want:
            ops = trial
        else:
            k += 1
    return ops


def style_ops(style, w, h):
    return prune(ring_ops(w, h) if STYLES[style] == "vierkanten" else lattice_ops(w), w, h)


def apply_ops(ops, w, h):
    t = Bitmap(w, h)
    for kind, value, k in ops:
        if kind == "row":
            t.rect(0, k, w - 1, k, value)
        else:
            t.rect(k, 0, k, h - 1, value)
    return t


def closed_tile(style, w, h):
    t = apply_ops(style_ops(style, w, h), w, h)
    if STYLES[style] == "vierkanten":            # the list must draw exactly the squares
        want = Bitmap(w, h)
        for x0, y0, x1, y1 in RINGS[w]:
            want.rect(x0, y0, x1, y0)
            want.rect(x0, y1, x1, y1)
            want.rect(x0, y0, x0, y1)
            want.rect(x1, y0, x1, y1)
        assert t.px == want.px, f"ring operations for {w}x{h}"
    return t


def face(style, w, h):
    """Where the symbols go on a closed tile: inside the outer square, or
    the hatched face of the button."""
    if STYLES[style] == "vierkanten":
        x0, y0, x1, y1 = RINGS[w][0]
        return x0 + 1, y0 + 1, x1 - 1, y1 - 1
    return interior(w, h)


def inset_tile(style, w, h):
    """A closed tile with a dark face: under the cursor, and for the flag
    and the question mark."""
    t = closed_tile(style, w, h)
    x0, y0, x1, y1 = face(style, w, h)
    t.rect(x0, y0, x1, y1, 0)
    return t


def interior(w, h):
    """The hatched button's face: inside the highlight and the shadow."""
    lat = LATTICE[w]
    shadow_rows = [y for y in lat["set_rows"] if y + 1 in lat["set_rows"] or y - 1 in lat["set_rows"]]
    return lat["set_cols"][0] + 1, lat["set_rows"][0] + 1, lat["set_cols"][1] - 1, min(shadow_rows) - 1


def open_tile(w, h):
    """Flat: a dotted grid line along the top and left edge, dark inside."""
    t = Bitmap(w, h)
    for x in range(0, w, 2):
        t.set(x, 0)
    for y in range(0, h, 2):
        t.set(0, y)
    return t


def centre(t, pattern, w, h, dx=0, dy=0, v=1, box=None):
    x0, y0, x1, y1 = box or (1, 1, w - 1, h - 1)
    pw, ph = len(pattern[0]), len(pattern)
    t.paste(pattern, x0 + (x1 - x0 + 1 - pw) // 2 + dx, y0 + (y1 - y0 + 1 - ph) // 2 + dy, v)


def digit_pattern(sheet, w, ch):
    g = ink(rom_glyph(sheet, ch))
    if w == 40:
        return scale(g, 3, 2)                 # 18x14: strokes 9:10 on the tube
    if w == 24:
        return scale(g, 2, 1)                 # 12x7
    return bold(g)                            # 7x7


def mine(w, h):
    """Round sea mine with spikes and a glint; returns a pattern and its size."""
    if w == 40:
        rx, ry, spike_x, spike_y, diag = 7.5, 4.6, 13, 8, (10, 6)
    elif w == 24:
        rx, ry, spike_x, spike_y, diag = 4.6, 2.9, 8, 5, (6, 4)
    else:
        rx, ry, spike_x, spike_y, diag = 3.6, 2.3, 6, 4, None
    pw, ph = 2 * spike_x + 1, 2 * spike_y + 1
    cx, cy = spike_x, spike_y
    p = [[0] * pw for _ in range(ph)]
    for y in range(ph):
        for x in range(pw):
            if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                p[y][x] = 1
    for x in range(pw):                       # horizontal spike, 1 line
        p[cy][x] = 1
    for y in range(ph):                       # vertical spike, 2 dots wide on the big tiles
        p[y][cx] = 1
        if w >= 24:
            p[y][cx - 1] = 1 if abs(y - cy) > ry else p[y][cx - 1]
    if w == 40:
        for y in range(ph):
            p[y][cx + 1] = 1 if abs(y - cy) > ry else p[y][cx + 1]
    if diag:
        dx, dy = diag
        for k in range(dy + 1):
            for sx in (-1, 1):
                for sy in (-1, 1):
                    xx = cx + sx * round(k * dx / dy)
                    yy = cy + sy * k
                    p[yy][xx] = 1
                    if w == 40:
                        p[yy][xx + sx] = 1
    # glint: a dark spot upper left in the body
    if w == 40:
        gx, gy, gw, gh = cx - 4, cy - 2, 3, 2
    elif w == 24:
        gx, gy, gw, gh = cx - 2, cy - 1, 2, 1
    else:
        gx, gy, gw, gh = cx - 2, cy - 1, 1, 1
    for y in range(gy, gy + gh):
        for x in range(gx, gx + gw):
            p[y][x] = 0
    return p


def flag(w, h):
    """Pennant on a pole with a foot, for the face of a closed button."""
    if w == 40:
        pole_x, top, bottom, pw, ph = 20, 4, 17, 11, 7
        foot = [(17, 16, 23), (18, 13, 26)]
        pole_w = 2
    elif w == 24:
        pole_x, top, bottom, pw, ph = 12, 2, 9, 7, 5
        foot = [(9, 9, 15), (10, 7, 17)]
        pole_w = 1
    else:
        pole_x, top, bottom, pw, ph = 8, 2, 6, 5, 3
        foot = [(6, 5, 10), (7, 4, 11)]
        pole_w = 1
    t = Bitmap(w, h)
    t.rect(pole_x, top, pole_x + pole_w - 1, bottom)
    for k in range(ph):                        # triangle pointing left from the pole top
        half = (ph - 1) / 2
        length = round(pw * (1 - abs(k - half) / (half + 0.5)))
        t.rect(pole_x - length, top + k, pole_x - 1, top + k)
    for y, x0, x1 in foot:
        t.rect(x0, y, x1, y)
    return t


def small_symbol(rows):
    t = Bitmap(16, 10)
    t.paste([[int(c == "#") for c in row] for row in rows], 2, 2)
    return t


def or_into(t, other):
    for y in range(t.h):
        for x in range(t.w):
            if other.px[y][x]:
                t.px[y][x] = 1


def cross(t, w, h):
    """Diagonal cross over the tile face (a flag that was wrong)."""
    x0, y0, x1, y1 = interior(w, h)
    n = max(x1 - x0, 1)
    thick = 3 if w == 40 else 2
    for i in range(n + 1):
        y = y0 + round(i * (y1 - y0) / n)
        for dx in range(thick):
            t.set(x0 + i + dx, y, 1)
            t.set(x1 - i - dx, y, 1)


def make_tiles(sheet, style, w, h):
    tiles = {}
    tiles["closed"] = closed_tile(style, w, h)
    tiles["selected"] = inset_tile(style, w, h)
    t = inset_tile(style, w, h)
    or_into(t, small_symbol(SMALL_FLAG) if w == 16 else flag(w, h))
    tiles["flag"] = t
    t = inset_tile(style, w, h)
    if w == 16:
        or_into(t, small_symbol(SMALL_QUESTION))
    else:
        centre(t, digit_pattern(sheet, w, "?"), w, h, box=face(style, w, h))
    tiles["question"] = t
    for n in range(9):
        t = open_tile(w, h)
        if n:
            centre(t, digit_pattern(sheet, w, str(n)), w, h)
        tiles[f"open{n}"] = t
    t = open_tile(w, h)
    centre(t, mine(w, h), w, h)
    tiles["mine"] = t
    t = open_tile(w, h)
    t.rect(1, 1, w - 1, h - 1)
    centre(t, mine(w, h), w, h, v=0)
    tiles["boom"] = t
    t = open_tile(w, h)
    or_into(t, small_symbol(SMALL_FLAG) if w == 16 else flag(w, h))
    cross(t, w, h)
    tiles["wrong"] = t
    return tiles


def cursor_overlay(w, h):
    """OR-ed over the tile (a closed tile is first swapped for the dark-faced
    'selected' one). Beginner: thick corner brackets, as in Othello; the
    smaller tiles: a frame around the whole cell, clear of the digits."""
    t = Bitmap(w, h)
    if w == 40:
        x0, y0, x1, y1 = 0, 0, w - 3, h - 1
        lx, ly = 9, 5
        for (cx, sx) in ((x0, 1), (x1, -1)):
            for (cy, sy) in ((y0, 1), (y1, -1)):
                for i in range(lx):
                    t.set(cx + sx * i, cy)
                    t.set(cx + sx * i, cy + sy)
                for i in range(ly + 1):
                    for d in range(3):
                        t.set(cx + sx * d, cy + sy * i)
    else:
        x1 = w - 2
        t.rect(0, 0, x1, 0)
        t.rect(0, h - 1, x1, h - 1)
        t.rect(0, 0, 1, h - 1)
        t.rect(x1 - 1, 0, x1, h - 1)
    return t


def row_span(bitmap):
    ys = [y for y, row in enumerate(bitmap.px) if any(row)]
    return ys[0], ys[-1]


def column_label(sheet, w, n):
    """Column number centred in a cell-wide, 7-line label."""
    text = str(n)
    if w == 16:
        parts = [narrow(c) for c in text]
    else:
        parts = [ink(rom_glyph(sheet, c)) for c in text]
    gap = 1 if w == 16 else 2
    width = sum(len(p[0]) for p in parts) + gap * (len(parts) - 1)
    t = Bitmap(w, 7)
    x = (w - width) // 2
    for p in parts:
        t.paste(p, x, 0)
        x += len(p[0]) + gap
    return t


def row_label(sheet, ch):
    """Row letter: the ROM glyph's 7 ink lines, centred in one byte."""
    g = ink(rom_glyph(sheet, ch))
    t = Bitmap(8, 7)
    t.paste(g, (8 - len(g[0])) // 2, 0)
    return t


# --- output -------------------------------------------------------------------------

def c_bytes(data, indent="    ", per_line=20):
    items = [f"0x{b:02X}" for b in data]
    return ",\n".join(indent + ", ".join(items[i:i + per_line]) for i in range(0, len(items), per_line))


def encode(op):
    """Bit 7: a column (else a row); bit 6: set (else erase); bits 0-5: offset."""
    kind, value, k = op
    return (0x80 if kind == "col" else 0) | (0x40 if value else 0) | k


def generate(sheet):
    out = ["/* Generated by tools/gen_sprites.py -- do not edit. */",
           "#ifndef SPRITES_H", "#define SPRITES_H", "",
           "/* Tile order (appearance codes in screen.c); the first STYLE_TILES",
           " * come per style, the others are shared. */"]
    for i, name in enumerate(TILES):
        out.append(f"#define TILE_{name.upper()} {i}")
    out.append(f"#define TILE_COUNT {len(TILES)}")
    out.append(f"#define STYLE_TILES {len(STYLE_TILES)}")
    out.append(f"#define STYLE_COUNT {len(STYLES)}")
    out.append("/* closed-tile operations: bit 7 column (else row), bit 6 set (else erase), bits 0-5 offset */")
    out.append("#define OP_COL 0x80")
    out.append("#define OP_SET 0x40")
    out.append("")
    all_tiles = {}
    for k, (name, cols, rows, w, h) in enumerate(LEVELS):
        per_style = [make_tiles(sheet, st, w, h) for st in range(len(STYLES))]
        all_tiles[w] = per_style
        wb = w // 8
        cur = cursor_overlay(w, h)
        out.append(f"/* {name}: {cols}x{rows} cells of {w}x{h} dots ({wb} bytes x {h} lines) */")
        out.append(f"static const unsigned char tiles_{w}[{len(TILES) - len(STYLE_TILES)}][{wb * h}] = {{")
        for tname in TILES[len(STYLE_TILES):]:
            out.append(f"  {{ /* {tname} */\n{c_bytes(per_style[0][tname].to_bytes())} }},")
        out.append("};")
        out.append(f"static const unsigned char style_tiles_{w}[{len(STYLES)}][{len(STYLE_TILES)}][{wb * h}] = {{")
        for st, sname in enumerate(STYLES):
            out.append(f"  {{ /* {sname} */")
            for tname in STYLE_TILES:
                out.append(f"    {{ /* {tname} */\n{c_bytes(per_style[st][tname].to_bytes(), indent='      ')} }},")
            out.append("  },")
        out.append("};")
        out.append(f"static const unsigned char cursor_{w}[{wb * h}] = {{\n{c_bytes(cur.to_bytes())} }};")
        for st, sname in enumerate(STYLES):
            ops = style_ops(st, w, h)
            out.append(f"/* {sname}: {len(ops)} operations, sent in this order */")
            out.append(f"static const unsigned char ops_{w}_{st}[{len(ops)}] = {{\n{c_bytes(bytes(encode(o) for o in ops))} }};")
        labels = b"".join(column_label(sheet, w, n).to_bytes() for n in range(1, cols + 1))
        out.append(f"/* column numbers 1-{cols}: {wb} bytes x 7 lines each */")
        out.append(f"static const unsigned char col_labels_{w}[{len(labels)}] = {{\n{c_bytes(labels)} }};")
        out.append("")
    letters = b"".join(row_label(sheet, chr(ord("A") + i)).to_bytes() for i in range(16))
    out.append("/* row letters A-P: 1 byte x 7 lines each (the ROM glyphs' ink) */")
    out.append(f"static const unsigned char row_labels[{len(letters)}] = {{\n{c_bytes(letters)} }};")
    out += ["", "#endif", ""]
    (ROOT / "src/sprites.h").write_text("\n".join(out))
    print("wrote src/sprites.h")
    for name, cols, rows, w, h in LEVELS:
        counts = []
        for st in range(len(STYLES)):
            ops = style_ops(st, w, h)
            n = sum(rows if kind == "row" else cols for kind, _, _ in ops)
            counts.append(f"{STYLES[st]} {n} lines ({n * 10} bytes)")
        print(f"  {name}: fresh board " + ", ".join(counts))
    return all_tiles


# --- preview ---------------------------------------------------------------------------

MOCK = [  # a small board in every state, the cursor on the digit 2
    "CCCCCCCC",
    "C1112CFC",
    "C10013QC",
    "C1004567",
    "CC1128MM",
    "CCCC1BWC",
]
MOCK_TILE = {"C": "closed", "F": "flag", "Q": "question", "M": "mine", "B": "boom", "W": "wrong"}


def preview_panel(tiles, w, h):
    SX, SY = 3, 5
    cur = cursor_overlay(w, h)
    bw, bh = len(MOCK[0]) * w, len(MOCK) * h
    img = Image.new("L", (bw + 16, bh + 16), 0)
    for r, line in enumerate(MOCK):
        for c, ch in enumerate(line):
            name = MOCK_TILE.get(ch, f"open{ch}")
            on_cursor = (r, c) in ((1, 4), (4, 1))
            t = tiles["selected" if on_cursor and name == "closed" else name].copy()
            if on_cursor:
                or_into(t, cur)
            for y in range(h):
                for x in range(w):
                    if t.px[y][x]:
                        img.putpixel((8 + c * w + x, 8 + r * h + y), 255)
    return img.resize((img.width * SX, img.height * SY), Image.NEAREST)


def preview(sheet, all_tiles):
    """One row of the three sizes per style."""
    rows_img = []
    for st in range(len(STYLES)):
        rows_img.append([preview_panel(all_tiles[w][st], w, h) for w, h in ((40, 24), (24, 14), (16, 10))])
    total_w = sum(p.width for p in rows_img[0]) + 40 * (len(rows_img[0]) - 1)
    row_h = max(p.height for p in rows_img[0])
    sheet_img = Image.new("RGB", (total_w, row_h * len(rows_img) + 40 * (len(rows_img) - 1)), (30, 30, 30))
    for k, panels in enumerate(rows_img):
        x = 0
        for p in panels:
            green = Image.merge("RGB", [p.point(lambda v, c=c: c if v else 0) for c in (51, 255, 51)])
            sheet_img.paste(green, (x, k * (row_h + 40)))
            x += p.width + 40
    (ROOT / "build").mkdir(exist_ok=True)
    sheet_img.save(ROOT / "build/sprites_preview.png")
    print("wrote build/sprites_preview.png")


if __name__ == "__main__":
    font = Image.open(FONT_SHEET).convert("RGB")
    tiles = generate(font)
    if "--preview" in sys.argv:
        preview(font, tiles)
