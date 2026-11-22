# flight320: what to optimise next

State on 2026-11-11: `out/flight320.exe` (PROG=flight320), 320 x 200 single pixels, the FAR fan (55 vertices / 90 edges), shaded (near edges lit, far unlit),
logo + descent + fade + P61 music + picture rate display.  About **9 pictures per second** (5.5 .. 6 vertical blanks per picture) on an A500 as emulated by Amiberry
(512 KB chip + 512 KB slow RAM, no fast RAM).  Code, tables and the ridge buffer are in slow RAM, which sits on the chip bus.

## 1. Measured profile (PROFILE=1 build, `tools/run.sh flight320 prof gdb`, `python tools/readprof.py flight320`)

| stage | ms per picture | share |
|---|---|---|
| F3Prepare: camera tables (UpdateWalkAsm + the C parts of UpdateTables: heading, flags, ApScan) | 15.3 (9.8 without the blitter running) | 13% |
| WaitBlt: the rest of the blitter clear that the CPU could not hide | 7.2 | 6% |
| F3Plot320, of which | 94.2 | 80% |
| &nbsp;&nbsp;projection (F3Project, C) | 15.9 | 14% |
| &nbsp;&nbsp;lines (F3SegAsm, assembly with kernels) | 68.5 | 58% |
| &nbsp;&nbsp;edge loop, flags, profile overhead | about 10 | |
| the rest (snapshot, QueueFinish, FPS digits) | < 1 | |
| **total** | **about 119 (5.9 vertical blanks)** | |

How it got here (vertical blanks per picture): plain C 13.6, with culling in C about 11, line routine in assembly with kernels 6.3, plus asm vertex walk, clear overlapped with the CPU and a cheaper
projection 5.9.  The assembly line is checked against the C line picture by picture (`gnumake PROG=flight320 F3CHECK=1`, 73 pictures: 0 differing bytes).

Counters of the C version, per picture: 1268 `F3Seg` calls, 977 plot tries, **803 pixels really plotted**, 1175 columns scanned by the cull tests, 64 pieces culled.

## 2. What to do next, in the order I would do it

1. **Three bitplanes instead of four for the flight** (cheap, probably the biggest single gain).  Plane 3 is only a constant 1.  With colours 0 .. 7 instead of 8 .. 15 (sky = colour 0,
   copper list `bplcon0 = $3200`, `color[0..7]`), the display DMA drops from 4 to 3 planes, which gives the CPU and the blitter more bus slots in slow / chip RAM.  The descent keeps its 4 plane list.
   Estimate: 10 .. 25% faster lines and tables (not measured).
2. **Restrict the blits to the rows that are used.**  The clear (3 planes x 200 rows) and the drip (2 planes x 200 rows, 3 channels each) move about 22 ms worth of bus time per picture, although about half
   of the rows (the sky above the highest ridge) are empty.  Keep `yTop` (the highest ridge row of the picture) per frame: drip only from that row down, clear only the rows the frame's last picture used
   (store its `yTop` with the frame; the FPS digits live in rows 4 .. 14 of plane 2 and need their own small clear).  Estimate: halves the blitter time, most of `WaitBlt` (7 ms) and part of the contention
   in the 15 ms of camera tables.
3. **Projection in assembly (16 ms).**  Today two `DivU` per coordinate through C (`Proj88s`, a shift loop, two divisions, sign handling) plus the roll multiply.  One reciprocal per vertex
   (`65536 / z` with one `divu`), then two `mulu` for X and Y, needs a check of the precision (the C version is exact to 1/256 pixel; the rows only need whole pixels plus the exact y of the line).
   Estimate: 16 -> 5 ms.
4. **The 800 plotted pixels (about 17% of the lines).**  Every column is raised about 2.5 times per picture (far lines first, then nearer lines that rise above them).  Ideas:
   a. draw the edges near to far when the sort is cheap (fewer pixels are plotted, the far ones are hidden at once); the lit group (near) is the first half of the table already;
   b. a cheaper plot: a combined table of (byte, mask) per column as one word, the row offset multiplied once per pixel, `or.b` of plane 0 and 2 together if plane 2 is dropped for the lit rows;
   c. optionally drop plane 2 (the ridge line outline): one plane write less per pixel, one clear and one DMA plane less.  Changes the look (no dark outline), so only if you accept that.
5. **Cull tests (1175 columns scanned per picture).**  `cmp.b (a1)+,d5 / bhi / dbra` is about 20 cycles per column.  A scan of longwords (4 columns at a time, with the bound replicated in all
   four bytes and an unsigned per byte compare trick) or a block minimum (16 column blocks, updated when a column's ridge buffer changes) cuts that.  Estimate: about 5 .. 8 ms.
6. **Kernels.**  Pieces of 3 .. 16 columns are unrolled with the bottom midpoints in `d7`.  The next steps are the register version for the upper midpoints (on the ST this gave 254 -> 249 vblanks
   per 100 pictures for the first kernel step, the register step was the larger part), kernels up to 24 columns (more code, 12 KB per 8 columns), and a cull test inside the kernels for the 4+ column pieces.
7. **Camera tables (about 10 ms alone).**  What is left in C: `SinHead` (two table lookups per picture), the frustum flag decisions, `ApScan` (the autopilot's ray scan: it only has to run while the
   autopilot flies, and not every picture) and `vpdone` clearing.  Profile this part separately first (`PROF` around `UpdateWalkAsm` and `ApScan`).
8. **Slow RAM.**  With real fast RAM (A500 with a 512 KB fast board, A1200, emulator config with fast RAM) the code and the tables would not compete with the display; test `flight320` with 1 MB chip
   or fast RAM to see how much of the time is bus contention rather than CPU work.  Anything hot (ridge buffer 320 bytes, `f3Mask`, `f3ColOff`, `f3RowOff`, the word stack) is small enough
   to be worth copying to chip RAM if only the code is in slow RAM and the display uses the other bank.

## 3. Cheap measurements to do before changing code

* `tools/run.sh flight320 prof gdb` then `python tools/readprof.py flight320`: the stage table above.  Counters through `PCOUNT` are in `prof.t[10 .. 14]` (the C line only).
* `gnumake PROG=flight320 F3CHECK=1`, run for about 40 s, `scratchpad/readchk.py`-style read of `f3Checked / f3Diff / f3LbDiff` through the debugger: must stay at 0 differences after every change to the line routine.
* The picture rate shows in the top left corner (pictures per second x 10), `f3Renders` can be read through the debugger for an exact count.

## 4. Open questions / smaller items

* The Masterblazer intro uses a **16 bit hash** per vertex (words like `$88CA`); flight320 uses the game's 8 bit hash.  The look of the fine detail of the lines differs; the 16 bit rule would need a
  hash per vertex that we do not have (it looked like the cell value times 256 plus a fraction).
* The projection centre of Masterblazer is (80, 64); flight320 is centred (160, row 112) with the game's field of view.
* Autopilot: the flight starts low and climbs (`AP_CLR0`); at 320 x 200 the horizon row (`F3_Y0`) and the scale (`F3_SX / F3_SY`) were chosen by eye.
* Sun and the heat shimmer of `flight` are not in `flight320`.
* Nothing of flight320 is committed yet (`src/flight320_main.c`, `src/flight_line320.asm`, `tools/mkline320.py`, and changes in `Makefile`, `src/flight_core.c`, `tools/readprof.py`, `tools/run.sh`).
