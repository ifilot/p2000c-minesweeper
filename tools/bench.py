#!/usr/bin/env python3
"""Benchmark: what each action costs the terminal board, per level. Two
costs count on the real machine: the bytes over the 19200-baud link (8N1:
1920 bytes/s), and the dots the terminal's own Z80 has to plot for line
commands (ESC M / ESC v), which it draws dot by dot. The headless emulator
models neither, so this reads both from --trace-terminal: the difference
between two runs that differ by one action. Emulated main-CPU time (4 MHz)
is reported alongside. Run after make build.

  python3 tools/bench.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import COM, LEVELS, PLAYING, ROOT, act, first_open, make_image, run, start

BAUD_BYTES = 1920
MHZ = 4.0
TRACE = ROOT / "build/trace.bin"


def line_dots(trace: bytes) -> int:
    """Dots plotted for line commands: ESC m sets the pen, ESC M / ESC v draw
    or erase to a point; picture uploads (ESC r) are skipped over."""
    i, dots, pen = 0, 0, (0, 0)
    while i < len(trace):
        if trace[i] == 27 and i + 1 < len(trace):
            cmd = trace[i + 1]
            if cmd in b"mMv" and i + 4 < len(trace):
                x, y = trace[i + 2] | trace[i + 3] << 8, trace[i + 4]
                if cmd != ord("m"):
                    dots += max(abs(x - pen[0]), abs(y - pen[1])) + 1
                pen = (x, y)
                i += 5
                continue
            if cmd == ord("r") and i + 6 < len(trace):
                i += 7 + (trace[i + 5] | trace[i + 6] << 8)
                continue
        i += 1
    return dots


def measure(actions):
    state, _, _ = run(actions, trace=TRACE)
    assert state["status"] == "ok", state.get("message")
    data = TRACE.read_bytes()
    return len(data), line_dots(data), state["cycles"]


def cost(before, after):
    b0, d0, c0 = measure(before)
    b1, d1, c1 = measure(after)
    n = b1 - b0
    return f"{n:6d} bytes {n / BAUD_BYTES:5.2f} s link {d1 - d0:7d} line dots  {(c1 - c0) / MHZ / 1e6:5.2f} s Z80"


def main():
    make_image(COM, ROOT / "build")
    menu = ["--wait-for", "terug naar CP/M"]
    for level, (cols, rows, mines) in enumerate(LEVELS):
        print(f"level {level + 1}: {cols}x{rows}")
        print("  fresh board      ", cost(menu, start(level)))
        print("  ... vierkanten   ", cost(start(0, 1)[:-4], start(level, 1)))
        print("  ... strepen      ", cost(start(0, 2)[:-4], start(level, 2)))
        print("  cursor move      ", cost(start(level), start(level) + ["--send", "d", "--wait-for", "Veld"]
                                          + ["--run", "2000000"]))
        print("  flag             ", cost(start(level), start(level) + act("f")))
        print("  first open       ", cost(start(level), first_open(level)))
        print("  help and back    ", cost(first_open(level), first_open(level)
                                          + ["--send", "h", "--wait-for", "SPELREGELS", "--run", "3000000",
                                             "--send", " ", "--wait-for", PLAYING]))


if __name__ == "__main__":
    main()
