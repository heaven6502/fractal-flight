# Where the 8 bit game loses precision, and what a 16 / 32 bit CPU can do about it

Prototype: `jaggie_3d_16bit.pb` + `jaggie_flight_16bit.pbi` (copies of `jaggie_3d.pb` / `jaggie_flight.pbi`; the originals are untouched).
`jaggie_3d_16bit.exe --seed 7 --analyse` prints the numbers below (compile with `pbcompiler jaggie_3d_16bit.pb /CONSOLE /EXE jaggie_3d_16bit.exe`).
`--wide n` / the keys 1 .. 6 (0 = original, 9 = all) switch the stages. `wide = 0` is pixel identical to `jaggie_3d.pb` (checked with `--ridge`).

The Atari ST / Amiga C core (`flight_core.c`) is a line by line translation of this code, so it has exactly the same 8 bit behaviour.

## Measured loss (8 scenarios: level, banked left / right, climb, dive; flown from the game's start state)

Screen error of the projected vertices that are inside the picture, against the all wide computation (pixels: columns / rows of the 48 row game view):

| stage that stays 8 bit                      | max dx | mean dx | max dy | mean dy |
|---------------------------------------------|-------:|--------:|-------:|--------:|
| everything (the game)                       |   3.34 |    1.57 |   3.75 |    0.93 |
| #W_TRIG: heading / sine                     |   3.26 |    1.50 |   1.05 |    0.25 |
| #W_PROJ: perspective quotient + horizon ramp|   0.99 |    0.45 |   2.73 |    0.91 |
| #W_POS: position fraction in the cell       |   0.66 |    0.06 |   0.22 |    0.02 |

Direct measurements of single stages:

* **Sine / cos of the heading** (all 16384 headings): the camera direction is off by up to **1.43 deg** (mean 0.64 deg); the length of the cell step varies by 1.3 %.
  Cause 1: the table has 65 entries (0 .. 64) but the game mirrors the odd quadrants with `y XOR $3F` = 63 - y instead of 64 - y: one step (1.4 deg) off in the quadrants 1 and 3, so the view
  jumps by 1.4 deg (about 2.3 columns) when the heading crosses 90 / 180 / 270 deg, and the camera does not look where the velocity goes (`Velocity` uses the correct `+1`).
  Cause 2: the angle is only 11 bit (idx 8 bit + 3 bit weight) and the 16 bit table values are shifted down by 5 (+-2040).
  16 bit version: 14 bit angle, 6 bit weight, full 16 bit table, mirror 64 - y: error 0.002 deg.
* **Horizon ramp (roll / pitch)**: the ramp is a 32 byte table, one entry per 8 columns, whole rows. Against the exact linear ramp it is off by up to **2.2 rows at 4 deg bank, 3.2 rows at the maximum bank**
  (rows of a 48 row view!), 0.5 - 0.8 rows with only pitch. The vertices get the ramp of the start of their 8 column block.
* **Perspective**: 8 bit quotient (`q = num << 8 / zz`, `off = 1.5 q / 4`) and whole pixel vertices: up to 1 column / 2.7 rows (the rows include the ramp), mean 0.45 px.
  Vertices that are far outside the picture are replaced by +-16384 (|x / z| >= 128), which also changes the slope of the ridge lines that run to them.
* **Position inside the cell**: 8 bit fraction (`posX >> 4 & $FF`, the 4 low bits of the 16 bit position are dropped): the nearest vertices move in steps of 1/256 cell: mean 0.06 px, up to 0.7 px at the
  nearest rows. Small, but it is the judder you see at low speed.
* **Flight model, ground track against the heading** (`Velocity`: 8 bit speed x 8 bit sine, table index in 1.4 deg steps, rounded down to whole units, negative values get `a + 2` instead of `a`):
  up to **12.7 deg** off at speed 12 (mean 4.0 deg), 5.5 deg at speed 30 (mean 1.8 deg). The aircraft does not fly where it points; small left turns near north steer much too hard.
  16 bit version (speed 8.8 x 16 bit sine, 8 more position bits): 0.03 deg.
* **Ground collision**: terrain height with 8 bit interpolation (`Lerp8`), altitude compared in whole height units, and the response lifts the camera by 4 height units at once (a pop). 16 bit version: bilinear in 1/4096
  height units, lifted exactly onto the ground.
* **Ridge lines** (`#W_LINE`): the end points are whole pixels and the 8 bit y gets the displacement in whole rows; 8.8 y with the line sampled at the column centres of the exact end points makes the ridge move
  smoothly (sub pixel) instead of in steps of one row. (Shown by looking at it; 136 of 168 ridge columns differ from the game by up to 4 rows in a banked test picture.)

## What stays 8 bit on purpose

The heightmap (`h`, 8 bit), the walk table flags, the edge list and the fractal hash (bit 7 of `h1 + h2 + 1`, it decides the displacement direction) are data, not arithmetic: wider values would change the look of the terrain,
not make it more exact.

## Cost on a 68000 (8 MHz, no blitter), per frame

| stage | change | extra cost |
|-------|--------|-----------|
| #W_TRIG | 16 bit sine table (65 x word, 130 bytes), 2 muls per heading | ~ 0.1 ms |
| #W_POS  | 12 bit fraction: 4 `muls.w` per frame | nothing |
| walk    | 45 vertices: `add.l` instead of `add.w` (x and z) | ~ 0.2 ms |
| #W_PROJ | 2 divisions per vertex, `(num * 96 << 8) / z` as a 32 / 16 bit `divs` (~ 150 cycles each), 90 per frame | ~ 1.7 ms; the ramp: 1 `muls.w` per vertex |
| #W_LINE | y as 16 bit words in the line routines (the ST draw loop already works on words), 2 `muls` + `divs` per edge | ~ 0.3 ms |
| #W_VEL / #W_GND | 2 `muls.w` per tick; bilinear terrain: 3 muls per tick | nothing |

The whole frame costs more than 100 ms, so the wide arithmetic is below 3 %.

## Suggested order for the ST / Amiga core

1. `#W_TRIG` and `#W_VEL` (the biggest visible errors, almost free): the 65 entry table, mirror 64 - y, 14 bit heading; velocity from speed 8.8 x sine.
2. The exact horizon ramp (`#W_PROJ` part): 3 rows of error when banked.
3. Exact perspective division and sub pixel vertices; `#W_LINE`; `#W_POS`; `#W_GND`.

## Skipping hidden line pieces (`#W_CULL`, key 7, `--culltest`)

Found while optimising the 68000 line routine (profile: the line drawing is about half of the picture time).  The byte line routine halves a piece until it is 1 or 2 columns wide and plots; two thirds of
the plot attempts are hidden behind the ridges drawn before.  A piece of n >= 3 columns cannot draw higher than max(y0, y1) + n / 2 + 3 (the largest displacements add up to n / 2 rows), so if every
column it covers already holds a ridge value >= that bound, nothing of it can be plotted and the whole piece is skipped (state after the piece: X = ZX, y0 = ZY, h0 = ZH, pop).  It is not tried when
min(y0, y1) < n / 2 + 3 (the byte arithmetic of the game can wrap there: the "spikes") or when the bound exceeds 255.

`jaggie_3d_16bit.exe --seed 7 --culltest`: 6 scenarios x (the game path, all wide): 12 of 12 pictures identical, 24 - 44 % fewer line steps.  On the Atari ST the benchmark (100 fixed frames) went from
343 to 308 vertical blanks (10 %) with the same checksum; on the Amiga the assembly RenderScene from 65.2 to 59.6 ms.  The 68000 specific parts of that work (word stack walked with a pointer, a table
for the new ridge value and the row address of the plot, the end of view test only when X changes) have no meaning in a PureBasic program and were not ported.
