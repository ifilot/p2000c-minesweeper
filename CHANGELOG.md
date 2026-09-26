# Changelog

All notable changes to Mijnenveger for the Philips P2000C are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [semantic versioning](https://semver.org/).

## [1.0.1] - 2026-09-26

### Added

- A third closed-tile style, *raster*: a checkerboard dither filling the
  cell, like the grey buttons of the classic game. It is the new default;
  T on the start screen cycles through *raster*, *vierkanten* and
  *strepen*. Under the cursor the dither steps back from the cursor frame
  so the frame stays visible.

### Changed

- The board draws much faster on a real P2000C. The terminal board draws
  line commands dot by dot, and 1.0.0 built every closed cell from long
  lines, so a new board took roughly 70,000 to 130,000 dots of line
  drawing. Now only the grid is drawn as lines (7,000 to 14,500 dots), and
  everything else goes as bitmap uploads. For each band of changed lines,
  the game sends whichever costs fewer bytes: chunks of up to 15 whole
  lines, or one upload per changed stretch of a line.
- Opening cells, flagging and moving the cursor no longer draw any lines
  at all; they only send the bytes that changed.
- All cells share one continuous grid. The *vierkanten* tiles are the
  grid square with two larger squares inside, and the *strepen* tiles are
  horizontal stripes inside the grid cell. Flags and question marks stand
  on a bare cell.
- `MINES.DAT` has a new format, because the styles are numbered
  differently. A file from 1.0.0 keeps its best times; its tile style is
  ignored.
- The program builds in about half a minute instead of three and a half
  (the compiler's allocator search is limited to 10000 instead of 200000
  per node), with no measurable change in speed.
- The terminal's erase-line command (`ESC v`) is no longer used, so the
  `NO_ERASE` build option is gone.
- `tools/bench.py` also counts the dots the terminal has to draw for line
  commands, because the emulator models neither that nor the link speed.

### Fixed

- A single keypress sometimes acted twice (for example the cursor moving
  two cells, or a flag turning straight into a question mark). The
  terminal board scans the keyboard with the same processor that draws
  the picture, so while it is busy a key can come through again as an
  auto-repeat. A key that repeats the previous one while its screen
  update is still going out, or within 0.15 s after, is now ignored.
  Holding a cursor key still moves the cursor.
- Shift+W could move the cursor down instead of up: the compiler
  mistranslated the signed arithmetic in the cursor movement, which has
  been rewritten.

## [1.0.0] - 2026-09-26

### Added

- Minesweeper for the Philips P2000C under CP/M, in Dutch, in the 512x252
  high-resolution graphics mode.
- Three levels with the largest square cells that fit: Beginner (9x9, 10
  mines), Gevorderd (16x16, 40 mines) and Expert (30x16, 99 mines).
- The first cell opened is always safe and clears an area.
- Cursor keys or WASD, Shift+WASD for five cells, TAB for the next closed
  cell, and G plus a cell name (such as `C12`) to jump anywhere.
- Opening with RETURN or space, including chording on a number whose
  flags are all placed. F cycles through flag, question mark and nothing.
- Two closed-tile styles, *vierkanten* and *strepen*, switched with T on
  the start screen.
- Best time per level and the tile style, kept in `MINES.DAT`.
- Game clock, help screen and screen saver.
- Headless-emulator tests that play whole games at every level and check
  the picture dot for dot, plus screenshot and benchmark tools.

[1.0.1]: https://github.com/ifilot/p2000c-minesweeper/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/ifilot/p2000c-minesweeper/releases/tag/v1.0.0
