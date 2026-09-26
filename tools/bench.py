#!/usr/bin/env python3
"""Benchmark: bytes sent to the terminal board per action, and what they
cost on the 19200-baud link (8N1: 1920 bytes/s), per level. The headless
emulator does not model the link's speed, so this counts the bytes of
--trace-terminal between two runs that differ by one action. Emulated Z80
time (4 MHz) is reported alongside. Run after make build.

  python3 tools/bench.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from render import COM, LEVELS, PLAYING, ROOT, act, first_open, make_image, run, start

BAUD_BYTES = 1920
MHZ = 4.0
TRACE = ROOT / "build/trace.bin"


def measure(actions):
    state, _, _ = run(actions, trace=TRACE)
    assert state["status"] == "ok", state.get("message")
    return TRACE.stat().st_size, state["cycles"]


def cost(before, after):
    b0, c0 = measure(before)
    b1, c1 = measure(after)
    n = b1 - b0
    return f"{n:6d} bytes {n / BAUD_BYTES:5.2f} s link  {(c1 - c0) / MHZ / 1e6:5.2f} s Z80"


def main():
    make_image(COM, ROOT / "build")
    menu = ["--wait-for", "terug naar CP/M"]
    for level, (cols, rows, mines) in enumerate(LEVELS):
        print(f"level {level + 1}: {cols}x{rows}")
        print("  fresh board      ", cost(menu, start(level)))
        print("  ... strepen      ", cost(start(0, 1)[:-4], start(level, 1)))
        print("  cursor move      ", cost(start(level), start(level) + ["--send", "d", "--wait-for", "Veld"]
                                          + ["--run", "2000000"]))
        print("  flag             ", cost(start(level), start(level) + act("f")))
        print("  first open       ", cost(start(level), first_open(level)))
        print("  help and back    ", cost(first_open(level), first_open(level)
                                          + ["--send", "h", "--wait-for", "SPELREGELS", "--run", "3000000",
                                             "--send", " ", "--wait-for", PLAYING]))


if __name__ == "__main__":
    main()
