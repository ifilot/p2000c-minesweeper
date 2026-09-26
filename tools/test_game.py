#!/usr/bin/env python3
"""Regression tests: whole games at every level in the headless emulator.

Per level the centre is opened first and the minefield is read from memory
(address from the linker map); the emulator is deterministic, so a second
run with the same keys meets the same field. It checks:

- the layout: the right number of mines, none on or next to the first
  cell, every count right, and the opened area closed under the flood rule
  (every open empty cell has all its neighbours open);
- navigation: Shift+W/A clamp at the edge, TAB skips open cells, the panel
  names the cursor's field;
- flags: F cycles flag -> question mark -> nothing, and the mines counter
  follows;
- repeated keys: a key sent twice at once (as a terminal auto-repeat does
  while it is busy) moves the cursor once; after a pause it moves again;
- chording: RETURN on an open number with its mines flagged opens the rest;
- a whole game: every safe cell opened with G + coordinate, then
  "Gewonnen!", every mine flagged, the counter at 0, and the best time on
  the start screen;
- a lost game: opening a mine gives "Verloren!";
- the picture: after a chord, at the win, after the help page and at the
  loss, the terminal's graphics RAM equals the program's framebuffer dot
  for dot (grid lines, chunks and row uploads together) -- and
  the same for a fresh board and a first open in the other tile styles.

Run after `make build`.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import (COM, PLAYING, F_COUNT, F_FLAG, F_MINE, F_OPEN, F_QUERY, LEVELS, ROOT, act, at_cell,
                    cursor_name, first_open, layout, make_image, neighbours, open_steps, run, start)


def screen_text(state):
    return "\n".join(state["screen"])


def check_layout(level, cells, errors):
    cols, rows, mines = LEVELS[level]
    centre = (rows // 2) * cols + cols // 2
    if sum(1 for v in cells if v & F_MINE) != mines:
        errors.append(f"{sum(1 for v in cells if v & F_MINE)} mines instead of {mines}")
    for i in [centre] + neighbours(level, centre):
        if cells[i] & F_MINE:
            errors.append(f"mine at {cursor_name(level, i)}, next to the first cell")
    for i, v in enumerate(cells):
        n = sum(1 for j in neighbours(level, i) if cells[j] & F_MINE)
        if v & F_COUNT != n:
            errors.append(f"{cursor_name(level, i)} counts {v & F_COUNT}, has {n}")
        if v & F_OPEN and v & F_MINE:
            errors.append(f"mine {cursor_name(level, i)} open")
        if v & F_OPEN and not v & F_COUNT:
            for j in neighbours(level, i):
                if not cells[j] & F_OPEN:
                    errors.append(f"flood stopped at {cursor_name(level, j)}")
    if not cells[centre] & F_OPEN or cells[centre] & F_COUNT:
        errors.append("the first cell did not open an area")


def veld(level, i):
    return f"Veld     {cursor_name(level, i):>3}"


def mines_row(n):
    return f"Mijnen{n:>6}"


def check_navigation(level, cells, errors):
    """Shift+W, Shift+A, TAB, s from the centre, then flag cycling on a closed cell."""
    cols, rows, mines = LEVELS[level]
    r, c = rows // 2, cols // 2
    r = max(0, r - 5)
    c = max(0, c - 5)
    actions = first_open(level) + ["--send", "W", "--wait-for", veld(level, r * cols + cols // 2),
                                   "--send", "A", "--wait-for", veld(level, r * cols + c)]
    i = r * cols + c
    nxt = next(j for j in list(range(i + 1, cols * rows)) + list(range(i + 1))
               if not cells[j] & (F_OPEN | F_FLAG))
    actions += ["--send", "\\t", "--wait-for", veld(level, nxt)]
    closed = next(j for j, v in enumerate(cells) if not v & F_OPEN)
    actions += at_cell(level, closed) + act("f")
    state, _, after = run(actions + ["--wait-for", mines_row(mines - 1)], dump_cells=True)
    if state["status"] != "ok" or not after[closed] & F_FLAG:
        errors.append(f"navigation/flag: {state['status']}, cell {after[closed]:02X}")
    actions += act("f")
    state, _, after = run(actions + ["--wait-for", mines_row(mines)], dump_cells=True)
    if state["status"] != "ok" or (after[closed] & (F_FLAG | F_QUERY)) != F_QUERY:
        errors.append(f"question mark: {state['status']}, cell {after[closed]:02X}")
    actions += act("f")
    state, _, after = run(actions, dump_cells=True)
    if after[closed] & (F_FLAG | F_QUERY):
        errors.append(f"third F left {after[closed]:02X}")


def check_repeats(level, errors):
    """A doubled key moves once; the same key after a pause moves again."""
    cols, rows, _ = LEVELS[level]
    r, c = rows // 2, cols // 2
    actions = start(level) + ["--send", "dd", "--run", "6000000"]
    state, _, _ = run(actions)
    if veld(level, r * cols + c + 1) not in screen_text(state):
        errors.append("a doubled key did not move the cursor exactly once")
    state, _, _ = run(actions + ["--send", "d", "--wait-for", veld(level, r * cols + c + 2)])
    if state["status"] != "ok":
        errors.append("the same key after a pause was dropped")


def chord_actions(level, cells):
    """Flags the mines around an open number that also borders a closed safe
    cell, then presses RETURN on the number. Returns (actions, cells opened)."""
    for i, v in enumerate(cells):
        if not v & F_OPEN or not v & F_COUNT:
            continue
        around = neighbours(level, i)
        safe = [j for j in around if not cells[j] & (F_OPEN | F_MINE)]
        if not safe:
            continue
        actions = []
        for j in around:
            if cells[j] & F_MINE:
                actions += at_cell(level, j) + act("f")
        actions += at_cell(level, i) + act("\\r")
        return actions, safe
    return [], []


def same_picture(what, actions, errors):
    """Graphics RAM of the terminal against the framebuffer in RAM."""
    state, graphics, _, fb = run(actions, dump_fb=True)
    if state["status"] != "ok":
        errors.append(f"{what}: {state['status']}")
        return
    diff = [i for i in range(len(fb)) if fb[i] != graphics[i]]
    if diff:
        line, col = divmod(diff[0], 64)
        errors.append(f"{what}: {len(diff)} bytes differ on screen, first at line {line} byte {col}")


def run_level(level):
    cols, rows, mines = LEVELS[level]
    errors = []
    cells = layout(level)
    check_layout(level, cells, errors)
    check_navigation(level, cells, errors)
    check_repeats(level, errors)

    # chord, then open everything else
    chord, safe = chord_actions(level, cells)
    base = first_open(level) + chord
    _, _, after = run(base, dump_cells=True)
    after = list(after[:cols * rows])
    if not chord or any(not after[j] & F_OPEN for j in safe):
        errors.append("chording did not open the neighbours")
    steps = open_steps(level, after)
    actions = base + sum(steps, []) + ["--send", "\\x1b", "--wait-for", "Kies een niveau",
                                       "--wait-for", "terug naar CP/M"]
    state, _, final = run(actions, dump_cells=True)
    text = screen_text(state)
    final = list(final[:cols * rows])
    if state["status"] != "ok":
        errors.append(f"game did not finish: {state['status']} {state.get('message')}")
    for i, v in enumerate(final):
        if v & F_MINE and not v & F_FLAG:
            errors.append(f"mine {cursor_name(level, i)} not flagged after the win")
        if not v & F_MINE and not v & F_OPEN:
            errors.append(f"safe {cursor_name(level, i)} still closed")
    best = state["screen"][17 + level][55:62]
    if best == "-:--:--" or ":" not in best:
        errors.append(f"no best time on the start screen: '{best}'")

    # the panel at the moment of winning
    state, _, _ = run(base + sum(steps, []))
    text = screen_text(state)
    for want in ("Gewonnen!", mines_row(0)):
        if want not in text:
            errors.append(f"panel lacks '{want}'")

    # a lost game
    mine = next(i for i, v in enumerate(cells) if v & F_MINE)
    lose = first_open(level) + at_cell(level, mine) + act("\\r", "Verloren!")
    state, _, _ = run(lose)
    if state["status"] != "ok" or "Mijn geraakt!" not in screen_text(state):
        errors.append("opening a mine did not lose the game")

    # the terminal shows exactly the framebuffer
    same_picture("after the chord", base, errors)
    same_picture("at the win", base + sum(steps, []), errors)
    same_picture("after the help page", base + sum(steps[:len(steps) // 2], [])
                 + ["--send", "h", "--wait-for", "SPELREGELS", "--send", " ", "--wait-for", PLAYING], errors)
    same_picture("at the loss", lose, errors)
    for style, name in ((1, "vierkanten"), (2, "strepen")):
        same_picture(f"{name}, fresh board", start(level, style), errors)
        same_picture(f"{name}, first open", first_open(level, style), errors)

    print(f"level {level + 1} ({cols}x{rows}, {mines} mines): {len(steps)} opens after the first "
          f"and the chord, {'ok' if not errors else 'FAILED'}")
    for e in errors[:20]:
        print("   ", e)
    return not errors


def main():
    make_image(COM, ROOT / "build")
    results = [run_level(level) for level in range(len(LEVELS))]
    print("PASS" if all(results) else "FAIL")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
