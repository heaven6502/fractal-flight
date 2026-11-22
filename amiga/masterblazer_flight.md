# Masterblazer intro: the fractal flight, analysed from an Amiberry save state

Source: `masterblazer_fractal_savestate.uss` (Amiberry 8.5, A500: 512 KB chip RAM + 512 KB slow RAM at `$C00000`).
Everything below was read from the memory image and the disassembly (m68k objdump on the dumped chip RAM); the program was **not** run, so
things that need the live program are marked *unknown*.  Addresses are chip RAM unless noted.

## 1. Reading the state file

* ASF format: a chunk is `name(4) size(4) flags(4) data`; `size` includes the 12 byte header, chunks are padded to a multiple of 4.
  If bit 0 of `flags` is set the data is a 4 byte uncompressed size followed by a zlib stream.
* Chunks found (file offset): `CPU ` 76, `CHIP` (custom registers) 1224, `CRAM` (chip RAM, 512 KB) 4236, `BRAM` (slow RAM, 512 KB) 250920, `BLIT` 1908, `CIAA/CIAB`, `AUD0..3`, `SPR0..7`.
* Disassembly: `m68k-atari-mintelf-objdump -D -b binary -m m68k:68000 --start-address=0x5e7c --stop-address=0x5f00 chip.bin`.
* The CPU chunk has the 68000 registers (model 68000 = `$109A0`, then flags, d0-d7, a0-a7).  The program uses `a7` as a data pointer in places (it points into code), so the stack pointer
  is not a hint for the program counter.  I did not find the PC.

## 2. Screen

| | |
|---|---|
| Mode | lowres 320 x 200, **4 bitplanes, not interleaved**, `BPLCON0 = $4200`, 40 bytes per line |
| Plane spacing | 8040 bytes (200 lines + one spare line) |
| Double buffering | screen A at `$704E8`, screen B at `$78288` (planes at +0, +8040, +16080, +24120); the VBL interrupt (`$6AFE`) swaps the plane pointers in the copper list at `$51DC` |
| Screen base variable | `$50B8` (long), row table pointer `$50BC` |
| Row table | `$6AE22` (screen A) / `$6AFB2` (B): word per ridge row, **y is "up"**: row 0 is the bottom line, row 199 the top line (base + 40 * (199 - y)) |
| Plane 3 | all ones in both screens (filled once at start, `$4FB4`), so only colours 8 .. 15 are used |
| Palette | colours 8 .. 15: `$222` (sky), `$900` (dark ground), `$B60`, `$B11` (lit ground), `$C90`, `$600`, `$DB0`, `$300` (ridge line); colours 0 .. 7 (`$000 $028 $24A $46C $79F $9CF $FFF $FD1`) belong to the other parts of the intro |

Copper lists seen in the state: `$51DC` (the flight: four plane pointers `$78288 / $7A1F0 / $7C158 / $7E0C0`, `BPLCON0 $4200`, palette, end), `$707E` (another 4 plane screen, plane spacing 32000, the title),
`$0C0E` (interleaved 4 plane screen at `$14A4`, plane spacing 40).

## 3. How the shading works

The three drawing planes are **flags**, not colour bits (plane 3 is always 1):

| plane | content | set by |
|---|---|---|
| 0 | ground | every ridge pixel, then dripped downward |
| 1 | lit ground | ridge pixels of the **lit group of edges** only, then dripped downward |
| 2 | ridge line | every ridge pixel (not dripped) |

Resulting colour index (plane 3 = 8): `8` sky, `9` plane 0 only = **shadow** (`$900`), `11` planes 0+1 = **lit** (`$B11`), `13` line over shadow (`$600`), `15` line over lit (`$300`).
Counts in the frame I looked at: 30486 sky, 31654 lit, 1068 shadow, 684 + 108 line pixels.

The lit flag is a byte at `$61E8` (bit 1 = also set plane 1).  The edge loop (`$5AAE`, 72 edges, `d7` counts 71 .. 0) sets it to 3 at the start and to 1 when `d7` reaches 30,
so the **first 42 edges of the table are lit and the last 30 are unlit**.  In the game's edge table that is:

* first 42 edges: vertices 0 .. 27 (near row and middle row),
* last 30 edges: vertices 15 .. 44 (middle and far rows).

Vertex rows by camera z in the saved frame: 0 .. 14 nearest, 15 .. 28 middle, 29 .. 44 farthest.  So it is a **depth cue**: terrain seen behind the near ridges is
darker, and the shadow area is the region below an unlit (far) line that is not also below a lit (near) line.  It is *not* shading by slope direction.

## 4. Blitter use (the terrain part)

Per frame, in order (blitter registers in `a6` = `$DFF000`, offsets `$40` BLTCON0 .. `$58` BLTSIZE):

| # | where | what |
|---|---|---|
| 1-3 | `$5138` | clear plane 0, 1, 2: `BLTCON0 $01F0` (D only, `BLTADAT = 0`), `BLTSIZE $1928` = 100 rows x 40 words = 8000 bytes each |
| 4 | `$5A9E` | clear the ridge buffer (320 bytes at `$6BEE2`): `BLTSIZE $2801` = 160 rows x 1 word |
| 5-6 | `$6A98` | **drip**: `BLTCON0 $0DFC` (A, B, D; `D = A or B`), A = the row above (`a1 = a0 - 40`), B = D = the row, `BLTSIZE $3214` = 200 rows x 20 words; once for plane 0, once for plane 1 |

* The ridge lines themselves are drawn by the **CPU** with `bset`, not with the blitter line mode.
* The drip is the same trick as our Amiga port (`plane 1 row r := plane 0 row r OR plane 1 row r-1`), but applied to **two** planes.
* Other blits in the code (`$6402`, `$6486`, `$64FE`, `$653C`, `$65B8`) are cookie-cut / copy / fill operations with `$0FCA`, `$09F0`: the instrument panel, not the terrain.
* The blitter is waited for with `btst #14,2(a6)` polling.

## 5. Vertices and edges

* **45 vertices, 72 edges** (the original game view).
* The edge table at `$5D5C` (72 pairs of vertex indices as words, 288 bytes) is **identical to the game's `EdgeData` (`$B757`) in `jaggie_data.pbi`, same order**.
* So this is the 45/72 fan of the PureBasic port; our ST / Amiga flight uses the larger FAR fan (55 vertices, 90 edges).
* Per vertex arrays (words, index = vertex): camera space `x` at `$6C212`, `z` at `$6C212 + $5A`, `dy` at `$6C212 + $B4`; projected `X` at `$6C0D6`, projected `Y` at `$6C0D6 + $5A`;
  flag bytes per vertex at `$6C18A` (`$80` near, `$40` left, `$20` right, `$10` ...: not all traced).
* The edge loop (`$5AB4 ..`) reads the vertex pair, skips edges with a clip flag combination (`btst #5` / `#6`, `and`), fixes the near end (`bsr $5E7C` re-projects), and calls the line drawing (`$5EF4`).

## 6. Projection (`$5E7C`)

For every vertex (word arithmetic):

```
X = (x * 256) / z + 80          divsw; if z = 0 then z = 12
Y = (dy * 128) / z + 64  +  ramp(X)
ramp(X):  X < 0 -> table[0];  X >= 320 -> table[79];  else table[(X >> 1) & $FE]   (one word per 4 columns)
```

* 2 `divsw` per vertex, 90 per frame (about 13,000 cycles, my estimate).
* The ramp table is at `$6BE42` (80 words) and is **rebuilt every frame** by an unrolled routine at `$5B5C` (80 triples `addb d2,d3 / addxw d4,d1 / movew d1,(a0)+`):
  a Bresenham style line with the slope from the roll value (`$5448 >> 2`).  Roll is therefore a ramp added to the rows (the game's horizon ramp), not an image rotation.
* In the saved frame `X = x*256/z + 80` matched 19 of 45 vertices exactly; the others are clipped / fixed up (not traced).
* **No scaling from a smaller picture:** the projected X / Y are used 1:1 as screen pixels.  Check: in the work buffer (the current frame, lines only) 6 of the 9 vertices that project onto the screen have a
  ridge pixel within 1 pixel of (X, 199 - Y); with X doubled or shifted none do.
* **Compared with the game** (`ProjCoord` in `jaggie_3d.pb`): the game uses `X = 128 +- x/z * 96` (about 0.375 * 256) on its 168 visible columns (44 .. 211), i.e. +-0.875 = +-41 degrees across the picture.
  Masterblazer uses 256 per unit of x/z: the same terrain is magnified about 2.7 times and drawn on 320 pixels.
* **Field of view** (from the constants, screen 0 .. 319 x 0 .. 199, centre 80 / 64): horizontal x/z from -0.31 to +0.94, i.e. **-17 to +43 degrees, about 60 degrees, not symmetric**;
  vertical (Y up, `Y = 128 * dy/z + 64`) dy/z from -0.50 to +1.05, i.e. about -27 to +46 degrees (the horizon without pitch / roll lies at row 64 from the bottom).  The flight direction (x = 0) lands on column 80, the left quarter of the screen:
  either the camera is deliberately looking to one side, or the view is the original window with extra picture on the right.  *Unknown which.*  The camera x values themselves are not centred
  (about -894 .. 1154, mean near 130).

## 7. The ridge line routine

* Fractal rule = the game's: hash `h = h0 + h1 + 1`; `bmi` after the add (bit 7 set) selects a displacement, `bcs` (carry) picks the direction; displacement `(mid - x) >> 1`;
  the midpoint `y = (y0 + y1) >> 1`.  In words, with a word stack at `a1` offsets `0` (x), `$3C` (y), `$78` (h) (about 30 entries).  Code `$5F80 .. $61E2`, width 2 has its own case (`$6044`).
* Resolution: **320 distinct columns and 200 distinct rows, single pixels**.  In the work buffer all 320 columns carry a ridge pixel, only 55 / 49 of the even-odd / odd-even column pairs have equal
  heights (a doubled 160 column picture would show nearly all), and the line pixels use 168 different rows, odd and even equally.  (Our Amiga port: 160 columns x 75 rows, each shown as 2 x 2.)
* Pixel plot (`$5F2E ..`), per pixel:
  1. `d2 = lineBuf[x]` (byte per column, `$FF` = closed); skip if `$FF` or `y <= d2`
  2. if `y >= 200`: `lineBuf[x] = $FF`, `y = 199`; else `lineBuf[x] = y`
  3. address = `screenBase | x >> 3` + `rowTable[y]`; `bset` in plane 0; `bset` in plane 1 if bit 1 of `$61E8`; `bset` in plane 2.
* The ridge buffer is at `$6BEE2` (320 bytes, cleared by the blitter every frame).

## 8. Memory map (chip RAM) of the flight part

| address | content |
|---|---|
| `$0000 .. $0C0E` | system / loader code, copper list `$0C0E` |
| `$2000 .. $6000` | code (music player with a jump table at `$62EEC`, interrupt handlers, level 3 handler `$6AFE`) |
| `$4F7C .. $5138` | setup: clears `$704C0 .. $80000` and `$6BE42 .. $6C320`, plane 3 := ones, row tables |
| `$5138 ..` | per frame: clear planes, then the frame drawing (`$64B4` ..) |
| `$5AB4 .. $5B56` | the edge loop |
| `$5B5C .. $5D5A` | ramp table builder (unrolled) |
| `$5D5C .. $5E7C` | edge table |
| `$5E7C .. $5EF2` | projection |
| `$5EF4 .. $61E2` | edge drawing and the fractal line routine |
| `$61E8` | lit flag, `$61EA` counter |
| `$6A98` | the two drip blits |
| `$6AE22`, `$6AFB2` | row tables (y -> address offset) of the two screens |
| `$6B142` | table of (mask, byte offset) pairs for the 320 columns |
| `$6BE42` | ramp table (80 words) |
| `$6BEE2` | ridge buffer (320 bytes) |
| `$6C0D6`, `$6C18A`, `$6C212` | projected X/Y, vertex flags, camera space vertex arrays |
| `$704E8`, `$78288` | the two screens |

## 9. Compared with our Amiga flight

| | Masterblazer | our Amiga port |
|---|---|---|
| vertices / edges | 45 / 72 | 55 / 90 (FAR) |
| resolution | 320 x 200 single pixels (16 bit coordinates) | 160 x 75 picture points, 2 x 2 pixels |
| planes | 4 (plane 3 constant), 3 drawn | 2 (plus 2 for the sun) |
| ridge pixels | CPU `bset`, up to 3 planes | CPU `or.b`, 1 plane |
| clear | 3 blits + ridge buffer | 1 blit (plane 0) |
| drip | 2 blits (plane 0 and 1) | 1 blit chain (plane 1 from plane 0, then line OR sky) |
| shading | 2 depth groups (near lit, far unlit) via plane 1 | none (depth bands exist only in the ST experiment flightz) |
| roll | ramp table, 80 words per frame | roll in the projection (same idea) |

Ideas to use (not done): a second dripped plane for a group of edges (one more blit on the Amiga, a CPU drip on the ST), the 320 column ridge buffer, the `D = A or B` drip with
`A = row above` for two planes at once.

## 10. Open points

* The program counter and the main loop structure (the frame function entered from `$51XX`) were not traced.
* How the field of view works out with the centre at (80, 64).
* The clip flag handling in the edge loop (`$5AD0 .. $5B46`) and the near fix-up (`$5E7C` called with the vertex pair).
* What the second palette half (colours 0 .. 7) is used for.
* The sun / sky, if any: the sky is plain `$222` in this state.
