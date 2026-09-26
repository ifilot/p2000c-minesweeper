#!/usr/bin/env python3
"""Run build/MINES.COM in the headless P2000C emulator and save PNGs.

Usage: python3 tools/render.py                       (the default screenshots)
       python3 tools/render.py --wait-for TEXT [--out FILE.png] [-- ACTIONS]

The PNG is a plain dump of the terminal's dot raster: graphics RAM merged
with the text plane (rendered with the character-ROM glyphs from the sibling
p2000c-emulator font sheet), one dot per 3x5 block, green on black, no CRT
effects. Both rasters are placed on the 640x288-dot text canvas the way the
terminal does it, so text-mode and graphics-mode screenshots have the same
size. Requires the sibling p2000c-cpm-disk-tool checkout (headless
emulator, dist/pro/ images) and the p2000c-emulator checkout (font sheet).

The emulator is deterministic: the same keys give the same minefield. The
game helpers below therefore play in two runs -- the first opens the centre
and reads the mine layout from memory (address from the linker map), the
second repeats those keys and continues with full knowledge of the field.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT.parent / "p2000c-cpm-disk-tool"
EMULATOR = TOOL / "build/emulator/p2000c-mini"
IPL = TOOL / "tools/emulator/firmware/IPLDUMP.BIN"
HD0 = TOOL / "dist/pro/HD0_256.hda"
HD1 = TOOL / "dist/pro/HD1_256.hda"
COM = ROOT / "build/MINES.COM"

WIDTH, HEIGHT, BYTES_PER_LINE = 512, 252, 64
TEXT_RASTER = (640, 288)            # 80x24 by 8x12 dots; sets the dot pitch
FONT_SHEET = TOOL.parent / "p2000c-emulator/assets/font/P2000C font mini.png"

DOT_PITCH = (3, 5)                  # horizontal:vertical dot pitch on the 4:3 CRT
FOREGROUND = (51, 255, 51)          # plain phosphor green
BACKGROUND = (0, 0, 0)

LEVELS = [(9, 9, 10), (16, 16, 40), (30, 16, 99)]      # columns, rows, mines
F_MINE, F_OPEN, F_FLAG, F_QUERY, F_COUNT = 0x80, 0x40, 0x20, 0x10, 0x0F
PLAYING = "Ruim het veld"


def make_image(com: Path, out_dir: Path) -> Path:
    """Copy the F: image and drop MINES.COM in its high partition (F:)."""
    image = out_dir / "hd1.hda"
    shutil.copy(HD1, image)
    cli = ["python3", "-m", "p2000c_disk.cli", "put", str(image), str(com),
           "--partition", "high", "--replace"]
    subprocess.run(cli, cwd=TOOL, env={"PYTHONPATH": "src", "PATH": "/usr/bin:/bin"},
                   check=True, capture_output=True)
    return image


def symbol(name: str) -> int:
    text = (ROOT / "build/mines.map").read_text()
    m = re.search(rf"^{name}\s+= \$([0-9A-F]+)", text, re.M)
    return int(m.group(1), 16)


def run(actions: list[str], dump_cells: bool = False,
        trace: Path | None = None, dump_fb: bool = False):
    """One emulator session; returns the state, graphics RAM and the cells
    array (and, with dump_fb, the program's framebuffer)."""
    out_dir = ROOT / "build"
    dump = out_dir / "graphics.bin"
    extra = ["--dump-memory", f"{symbol('_cells')}:480"] if dump_cells else []
    if dump_fb:
        extra += ["--dump-memory", f"{symbol('_framebuffer')}:16128"]
    if trace:
        trace.unlink(missing_ok=True)
        extra += ["--trace-terminal", str(trace)]
    cmd = [str(EMULATOR), "--ipl", str(IPL), "--hard-disk-0", str(HD0),
           "--hard-disk-1", str(out_dir / "hd1.hda"), "--fast-storage", "--wait-cycles", "150000000",
           "--chunk-cycles", "5000",
           "--wait-for", "A>", "--send", "F:MINES\\r", *actions, "--dump-graphics", str(dump), *extra, "--output", "json"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    state = json.loads(result.stdout)
    if result.returncode != 0:
        print(f"emulator exit {result.returncode}: {state.get('message')}", file=sys.stderr)
    memory = [bytes.fromhex(m["bytes"].replace(" ", "")) for m in state.get("memory", [])]
    cells = memory.pop(0) if dump_cells else b""
    if dump_fb:
        return state, dump.read_bytes(), cells, memory[0]
    return state, dump.read_bytes(), cells


# --- playing ---------------------------------------------------------------------

STYLE_NAMES = ["vierkanten", "strepen"]


def start(level: int, style: int = 0) -> list[str]:
    """From the start screen into a fresh game (level 0..2), after switching
    to another tile style with T if asked (the default is 0, vierkanten)."""
    actions = ["--wait-for", "terug naar CP/M"]
    for _ in range(style):
        actions += ["--send", "t", "--wait-for", f"Tegels: {STYLE_NAMES[style]}", "--wait-for", "terug naar CP/M"]
    return actions + ["--send", str(level + 1), "--wait-for", PLAYING]


def act(keys: str, until: str = PLAYING) -> list[str]:
    """A board action: wait until it has been processed (the status line
    reads 'Bezig...' while the board is updated, then the result)."""
    return ["--send", keys, "--wait-for", "Bezig", "--wait-for", until]


def go(level: int, r: int, c: int) -> str:
    """Keys for G + coordinate; RETURN only when the jump is not automatic."""
    cols = LEVELS[level][0]
    keys = "g" + chr(ord("A") + r) + str(c + 1)
    return keys if (c + 1) * 10 > cols else keys + "\\r"


def first_open(level: int, style: int = 0) -> list[str]:
    return start(level, style) + act("\\r")


def layout(level: int, style: int = 0) -> list[int]:
    """The minefield after opening the centre (identical in every run)."""
    _, _, cells = run(first_open(level, style), dump_cells=True)
    cols, rows, _ = LEVELS[level]
    return list(cells[:cols * rows])


def neighbours(level: int, i: int) -> list[int]:
    cols, rows, _ = LEVELS[level]
    r, c = divmod(i, cols)
    return [(r + dr) * cols + c + dc for dr in (-1, 0, 1) for dc in (-1, 0, 1)
            if (dr or dc) and 0 <= r + dr < rows and 0 <= c + dc < cols]


def cursor_name(level: int, i: int) -> str:
    r, c = divmod(i, LEVELS[level][0])
    return f"{chr(ord('A') + r)}{c + 1}"


def frontier_mines(level: int, cells: list[int]) -> list[int]:
    """Mines next to an open cell, nearest the top left first."""
    return sorted({n for i, v in enumerate(cells) if v & F_OPEN
                   for n in neighbours(level, i) if cells[n] & F_MINE})


def at_cell(level: int, i: int) -> list[str]:
    """Moves the cursor with G + coordinate and waits for the panel's field name."""
    r, c = divmod(i, LEVELS[level][0])
    return ["--send", go(level, r, c), "--wait-for", f"Veld     {cursor_name(level, i):>3}"]


def open_steps(level: int, cells: list[int]) -> list[list[str]]:
    """Actions that open every safe cell with G + RETURN, one list per open,
    simulating the floods so that no cell is opened twice."""
    cells = cells[:]
    steps = []
    for i, v in enumerate(cells):
        if v & (F_OPEN | F_MINE):
            continue
        stack = [i]
        cells[i] |= F_OPEN
        while stack:
            j = stack.pop()
            if cells[j] & F_COUNT:
                continue
            for n in neighbours(level, j):
                if not cells[n] & (F_OPEN | F_FLAG):
                    cells[n] |= F_OPEN
                    stack.append(n)
        left = sum(1 for x in cells if not x & (F_OPEN | F_MINE))
        steps.append(at_cell(level, i) + act("\\r", "Gewonnen!" if left == 0 else PLAYING))
    return steps


# --- raster: what the terminal board puts on the tube ------------------------

def raster_dots(state: dict, graphics: bytes) -> tuple[list[list[int]], int, int]:
    """Per-dot level (0 off, 2 on) for the active raster.

    Mirrors DisplayWidget::rebuild_raster: in high-res mode the 64x21 text
    plane is merged into the 512x252 raster; a character dot on a lit pixel
    goes dark.
    """
    mode = state["graphics_mode"]
    sheet = Image.open(FONT_SHEET).convert("RGB")
    flat = "".join(state["screen"])
    if mode == "character":
        width, height, columns = TEXT_RASTER[0], TEXT_RASTER[1], 80
    else:
        width, height, columns = WIDTH, HEIGHT, 64
    rows = []
    for y in range(height):
        row = [0] * width
        gline = graphics[y * BYTES_PER_LINE:(y + 1) * BYTES_PER_LINE]
        for x in range(width):
            code = ord(flat[(y // 12) * columns + x // 8]) & 0xFF
            char_dot = sheet.getpixel(((code & 15) * 12 + x % 8, (code >> 4) * 12 + y % 12))[1] != 0
            if mode == "character":
                level = 2 if char_dot else 0
            else:
                lit = gline[x // 8] & (0x80 >> (x & 7))
                level = 0 if (lit and char_dot) else 2 if (lit or char_dot) else 0
            row[x] = level
        rows.append(row)
    return rows, width, height


def compose(state: dict, graphics: bytes) -> Image.Image:
    """Plain render: one raster dot -> a 3x5 green block (the CRT dot pitch)."""
    dots, rw, rh = raster_dots(state, graphics)
    raster = Image.new("L", (rw, rh), 0)
    raster.putdata([255 if level else 0 for row in dots for level in row])
    canvas = Image.new("L", TEXT_RASTER, 0)
    canvas.paste(raster, ((TEXT_RASTER[0] - rw) // 2, (TEXT_RASTER[1] - rh) // 2))
    canvas = canvas.resize((TEXT_RASTER[0] * DOT_PITCH[0], TEXT_RASTER[1] * DOT_PITCH[1]), Image.NEAREST)
    return Image.merge("RGB", [canvas.point(lambda v, c=c: c if v else b) for c, b in zip(FOREGROUND, BACKGROUND)])


# --- default screenshots --------------------------------------------------------

def midgame(level: int, flags: int, query: int, more_opens: int, style: int = 0) -> list[str]:
    """Open the centre and a few more safe cells, flag some frontier mines
    and put a question mark on one; the cursor ends on the question mark."""
    actions = first_open(level, style) + sum(open_steps(level, layout(level, style))[:more_opens], [])
    _, _, now = run(actions, dump_cells=True)
    for n, i in enumerate(frontier_mines(level, list(now))[:flags + query]):
        actions += at_cell(level, i) + act("f") + ([] if n < flags else act("f"))
    return actions


def lost(level: int) -> list[str]:
    cells = layout(level)
    mine = next(i for i, v in enumerate(cells) if v & F_MINE)
    wrong = next(i for i, v in enumerate(cells) if not v & (F_MINE | F_OPEN))
    return first_open(level) + at_cell(level, wrong) + act("f") + \
        at_cell(level, mine) + act("\\r", "Verloren!")


def won(level: int) -> list[str]:
    return first_open(level) + sum(open_steps(level, layout(level)), [])


def defaults() -> list[tuple[str, list[str]]]:
    return [
        ("start.png", ["--wait-for", "terug naar CP/M"]),
        ("beginner.png", midgame(0, 3, 1, 1)),
        ("gevorderd.png", midgame(1, 5, 1, 2)),
        ("expert.png", midgame(2, 8, 1, 3)),
        ("lost.png", lost(0)),
        ("won.png", won(1)),
        ("strepen.png", midgame(1, 5, 1, 2, style=1)),
    ]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wait-for", help="screen text to wait for")
    parser.add_argument("--out", type=Path, default=ROOT / "build/screen.png")
    parser.add_argument("extra", nargs="*", help="additional emulator actions")
    args = parser.parse_args()
    out_dir = ROOT / "build"
    out_dir.mkdir(exist_ok=True)
    make_image(COM, out_dir)
    if args.wait_for:
        jobs = [(args.out, ["--wait-for", args.wait_for, *args.extra])]
    else:
        jobs = [(out_dir / name, actions) for name, actions in defaults()]
    for out, actions in jobs:
        state, graphics, _ = run(actions)
        print(f"{out.name}: status={state['status']} mode={state['graphics_mode']} cycles={state['cycles']:,}")
        compose(state, graphics).save(out)


if __name__ == "__main__":
    main()
