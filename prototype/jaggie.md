# jaggie.asm — core routine analysis

`jaggie.asm` is a hand-modified disassembly of *Behind Jaggi Lines!*, the development
title of *Rescue on Fractalus!* (Lucasfilm Games, 1984, Atari 800XL/XE). It assembles to
a single 32,780-byte segment (`$3800-$B7FF`, `run $3c00`).

Sources used: the asm itself (all routine descriptions below were read from it), a
headless AltirraBridge run (title screen, then gameplay, both render correctly), and the
`bjl.pyA8` case-study notes under `AltirraBridgeServer…\examples\case_studies\behind_jaggi_lines`
for addresses I did not trace myself. Items marked **(notes)** come from there only.

Line numbers refer to the file as read; it was being edited while I read it, so treat
them as approximate and trust the `Lxxxx` addresses.

## 1. Build and run

```
..\..\mads.exe jaggie.asm -o:jaggie.obx -t:jaggie.lbl -l:jaggie.lst
```

`jaggie.xex` (2018) differs from a fresh build by 5 bytes: the second render pass
(`$3E23`, `$3E28`, `$3E2D`, now restored to the stock `$03/$00/$03`) and the ridge pixel
write (`$B44D`, `$B44F`). See §2.
Headless run: `AltirraBridgeServer.exe --bridge=tcp:127.0.0.1:6502`, then
`a.boot("jaggie.obx")`; the game waits on the title screen at `$7DA6` until fire.

## 2. What was changed from the stock game

Hand-named symbols (everything else is an auto `Lxxxx`):
`vram_ptr $80`, `heightmap $0900`, `nodetype $0A00`, `line_pixel_height_buffer $2608`,
`vram_lineadress_lo/hi $28EC/$291C`, `bitmask $BC00`, `turn_offset $2886`,
`ship_sink_rate $288E`, `ship_heading_l/h $2899/$289A`, `quater_angle_counter $2893`,
and the routines `main_game_loop`, `update_tables`, `init_pixel_to_collum_table`,
`init_grid_map`, `set_high_mountains`, `init_line_height_buffer`, `set_pixel`,
`set_pixel_line`, `clear_screen`, `render_sky`.

Experiments visible in the source (many are toggled with `;`, so check the file for the
current state):

| Where | Change |
|---|---|
| `init_grid_map` `$8440` | heights `AND #$3F` / `ORA #$80`, commented "height patch for flat map" |
| `set_high_mountains` `$84C4` | has a commented `rts`, so mountain raising can be switched off |
| `set_pixel` `$A754` | starts with `rts`: sprite/explosion/bunker pixels are disabled. The comment says it is not used for the fractal ridge |
| `set_pixel_line` `$B440-$B459` | ridge pixel write now `LDY LBD00,X` / `ORA bitmask,X` (the byte offset comes from the per-column table instead of a fixed 0) |
| `main_game_loop` `$3DD4` | render passes are at the stock values: pass 1 `X=$33/$30/$33`, pass 2 `X=$03/$00/$03` (restored; an earlier edit had set pass 2 to `$33/$30/$33`, and `jaggie.xex` from 2018 still has that) |
| VBI `$80E0-$80F4` | double-buffer display-list flip (`$A7`) is toggled with `bne` / `:2 nop`; currently the `bne` is active (stock behaviour) |
| `L8042` `$8042` | table init now returns immediately after building `bitmask` and `LBD00` |

With the render passes restored, pass 1 draws into the right half of the 96-byte rows
(column offset `$30`) and pass 2 into the left half (offset `$00`); `$A7` (2, then 1)
tells the VBI which display list to show. This is read from the source; I have not
re-run the restored build in the emulator.

## 3. Memory map (as used by this code)

| Range | Content |
|---|---|
| `$80-$B7` | zero-page workspace: `$80/81` pointer; `$82-$86` edge end 1 (x lo/hi, y lo/hi, depth); `$8D-$91` midpoint result; `$A0-$A3` camera X/Z step vectors; `$A4/A5` horizon step; `$A7` buffer/display flag; `$B6/B7` temps |
| `$0600-$06FF` | game state: `$0618` random mask, `$0621` game mode, `$0626` display mode, `$0641` detail level |
| `$0751/$07A7` | row address tables (lo/hi) used by the fill routine |
| `$0900-$09FF` | `heightmap`, 16x16 ring, bit 7 = unresolved |
| `$0A00-$0AFF` | `nodetype` (bunker/pilot info per node) |
| `$1010-$21DA` | bitmap screen, row stride `$60` (96 bytes), 42 byte columns used per pass |
| `$2270-$24BD` | vertex arrays, 45 entries each: grid idx `$2270`, X `$229D/$22CB`, Z `$22F9/$2327`, height `$23AF`, projected X `$23DC/$2409`, projected Y `$2436/$2463`, flags `$2490` |
| `$24BE-$24FF` | fractal recursion stack |
| `$2572-$25EA` | edge subdivision work arrays, stride `$1E`: x lo `$2572`, x hi `$2590`, y lo `$25AE`, y hi `$25CC`, depth `$25EA` |
| `$2608-$26CB` | `line_pixel_height_buffer`, one byte per pixel column (`$FF` = empty) |
| `$2708-$2727` | horizon height profile; `$28D0` perspective delta table |
| `$28EC/$291C` | `vram_lineadress_lo/hi`, screen row address per pixel row |
| `$3800-$3BFF` | cockpit character set |
| `$B6A3-$B756` | four packed vertex-walk tables (one per heading quadrant) |
| `$B757-$B7E6` | 72 edges as vertex-index pairs |
| `$BC00` | pixel bit mask by column; `$BD00` byte offset by column; `$BE00/$BF00` blend tables |

## 4. Frame pipeline

`main_game_loop` (`$3DD4`) renders two passes per iteration (the "2nd buffer" at column
offset `$30`, then "buffer1" at offset `$00`), each:

1. `update_tables` (`$9E8C`): camera, vectors, 45 vertices.
2. `clear_screen` (`$AE3E`).
3. `init_pixel_to_collum_table` (`$A356`): per-frame column map plus edge-column presets.
4. `render_sky` (`$AF32`): fill, see §6.
5. `LAA2F` (`$AA2F`): random effects, sets `$A7` for the buffer being shown.

Between passes it runs the game logic: `L6539`/`L65A9`/`L64A9` (state helpers), the
level/pilot checks around `$3E44-$3EAB`, and an exit to `L3F1A` when `$0634` is set.

```text
; frame pipeline (main_game_loop, $3DD4)
forever:
    for pass in (1, 2):
        col_clear, col_table, col_fill = (pass == 1) ? ($33, $30, $33) : ($03, $00, $03)
        update_tables()                         ; $9E8C
        clear_screen(col_clear)                 ; $AE3E
        init_pixel_to_collum_table(col_table)   ; $A356
        render_sky(col_fill)                    ; $AF32  (edges are drawn from LA3E7 via the fill code)
        LAA2F()                                 ; random effects
        $A7 = (pass == 1) ? 2 : 1               ; VBI shows DL $316B or $3210 from this
        game_logic()                            ; L6539 / L65A9 / L64A9 / pilot checks
```

## 5. Core routines

### 5.1 Camera and vertices: `update_tables` `$9E8C`

* `LAD6C` loads camera state and, via `L9B3C`, turns the heading into sin/cos
  (`$280A-$280D`, copied to `$A0-$A3`). `LA0DB` then places vertex 0 (see §7).
* `$92` (heading quadrant: bits 5-4 of `ship_heading_h`, a full turn is `$40`) picks one of four packed walk tables
  (`$B6FD`, `$B72A`, `$B6D0`, `$B6A3`) into `$80/$81`.
* The loop at `$9EE3` walks 45 vertices. Each byte of the table encodes a step: bit 7
  adds `+A0,-A2` to the previous vertex, bit 6 `-A0,+A2`, bits 5/4 use the vectors swapped.
  Vertex `Y+1` is computed from vertex `Y` (`$229D,Y` to `$229E,Y`), so the grid is built
  incrementally with 16-bit adds and subtracts.
* The loop also tracks the 16x16 grid cell (`$B5`/`$B7`) so each vertex can read its
  height from `heightmap`.

```text
; vertex walk inside update_tables, per vertex Y = 0..44 (from the branch cascade at $9EE3)
s = A0A1 (sin), c = A2A3 (cos), b = ($80),Y
pos[Y+1] = pos[Y]                    ; then apply up to one row move and one column move
if b.7:   row -1 : X += s ; Z -= c ; B7 += $F0 ; rest = (b.6, b.5)
elif b.6: row +1 : X -= s ; Z += c ; B7 += $10 ; rest = (b.5, b.4)
else:                                          rest = (b.5, b.4)
if   rest[0]: col -1 : X -= c ; Z -= s ; cellX -= 1
elif rest[1]: col +1 : X += c ; Z += s ; cellX += 1
idx[Y] = B7 | (cellX & $0F)          ; -> $2270,Y and the heightmap lookup
```

### 5.2 Edge pass: `LA3E7` `$A3E7`

* Walks the 72 index pairs in `$B757`. Flags in `$2490`: bit 7 hidden, `$20` / `$40`
  skip start/end, `$10` already projected.
* For each visible edge it projects both ends with `LA157` (once per vertex, flag `$10`
  marks "done"), copies projected X/Y and height into the work arrays at `$2572…`
  (end 2 into index 0, end 1 into `$82-$86`), and calls `LB24D`. `LA690` is not part of
  the projection: it is the node/pilot check (reads `nodetype`, sets `$28A9`, `$290C-$290F`).
* At the end (`$A46F`) it derives the detail level `$0641` from `$28A9`, updates PMG
  positions, and builds the horizon delta table `$28D0` from `$2708`.

```text
; edge loop LA3E7
for Y in 0, 2, 4 ... 142:                      ; 72 pairs in LB757
    v1 = LB757[Y] ; v2 = LB757[Y+1]
    if flags[v1] & ($80|$20) or flags[v2] & ($80|$40): continue   ; trivial reject
    project(v1) if not (flags[v1] & $10)       ; LA157
    project(v2) if not (flags[v2] & $10)
    LA690(v1), LA690(v2)                       ; pilot/bunker bookkeeping
    stack[0] = end2 = projected(v2)            ; $2572.. arrays, index 0
    e1       = projected(v1)                   ; $82-$86
    LB24D(e1, stack)                           ; clip, subdivide, draw
```

### 5.3 Edge subdivision and clipping: `LB24D` / `LB3A7`

* `LB24D` clips an edge to the screen and subdivides it (details in §7.5). `$9F` starts at
  `$14` and counts the midpoint steps allowed; the stack depth limit is `$0F`; a segment
  narrower than `$14` pixels is drawn as is.
* `LB3A7` is the midpoint step: average of two endpoints in 16 bits (`SEC`/`ADC`, so
  it rounds up), applied to X, Y and the depth byte. When the depth byte goes negative
  `LB3DA`/`LB3FA` displace Y by half the horizontal span. That displacement is what makes
  the ridge jagged. The sign/branch reasoning is my reading, not traced at runtime.

```text
; midpoint step LB3A7: e1 = $82-$86, s = stack[X]
m.x = (e1.x + s.x + 1) >> 1          ; 16-bit arithmetic shift
m.y = (e1.y + s.y + 1) >> 1
m.h = e1.h + s.h + 1                 ; 8 bit, not halved
if m.h & $80:                        ; displacement
    d = (m.x - e1.x) >> 1            ; quarter of the segment width
    if carry_from_h_add: m.y += d    ; LB3FA
    else:                m.y -= d    ; LB3DA
return m                             ; $8D-$91
```

### 5.4 Line plotting: `LB45D` and `LB51C`

* `LB45D`/`LB469` interpolate between two points, halving the horizontal gap each step
  (the `$2C` = 44 limit is the byte count of the buffer), and keep a depth value in
  `$0084`.
* `LB51C` is the unrolled line routine (comment: "core fractal line unrolled for 3
  pixels"). It stops at column `$D4` (212, right edge) and writes into
  `line_pixel_height_buffer` (`$B432`): values `>= $97` (151 rows) are stored as `$FF`.
* `set_pixel_line` (`$B440`) writes the ridge pixel:
  `Y = vram_lineadress[row]`, `Y' = LBD00,X`, `ORA bitmask,X`. No height check.

```text
; ridge plotting, summary of LB45D / LB51C / set_pixel_line
while segment wider than 1 pixel step:
    x = (x1 + x2) / 2 ; y = (y1 + y2) / 2 ; h = (h1 + h2 + 1)  ; halve until short
for each pixel column x < $D4:
    row = clamp8(y)
    if row >= $97: line_pixel_height_buffer[x] = $FF ; row = $97
    else:          line_pixel_height_buffer[x] = row
    ptr  = vram_lineadress[row]
    ptr[LBD00[x]] |= bitmask[x]                ; set_pixel_line
```

### 5.5 Fill: `render_sky` `$AF32` and `LB20E`

* For each of the 42 byte columns it scans rows from the top (`$1010 + n*$60,Y`) for the
  first non-empty byte. A stub per row (`LB1FF`, `LB202`, … entered by `JMP`) then
  fills the rows above it with `$55` (colour 1, the sky).
* `LB20E` handles the boundary: it merges existing screen bytes with `LBF00` and
  `LBE00` tables to blend partial pixels, using `$0751/$07A7` for row addresses, and
  continues downward until the blend table returns 0.
* So terrain is whatever the ridge line leaves unfilled: lines are drawn first, then the
  sky is flood-filled above them column by column.

```text
; render_sky ($AF32) + LB20E, per byte column Y = 0..41 (counter $9F)
for Y in 0..41:
    r = first row (0..42) where screen[r*$60 + Y] != 0     ; the ridge pixel
    if none: continue
    fill rows 0..r-1 of column Y with $55                  ; unrolled stub entered at row r
    ; LB20E: blend from the ridge row downward
    v95 = 0 ; v96 = $55 ; row = r
    loop:
        b = screen[row][Y]
        screen[row][Y] = (b & v95) | v96 | LBF00[b]
        m = LBE00[b] & v96
        if m == 0: break
        v96 = m ; v95 = LBE00[m] ; row++      ; row address from $0751/$07A7
```
(summary of the unrolled code, not a line-by-line transcription)

### 5.6 Pixel/column tables: `L8042` and `init_pixel_to_collum_table`

* `L8042` builds `bitmask` as `$80,$20,$08,$02` repeating (one pixel of four in a byte) and
  fills `LBD00` with the identity.
* `init_pixel_to_collum_table` rewrites `LBD00` for columns `$20…$D3`: four pixel
  columns share one byte offset, starting at the `X` argument (`$30`), 23 iterations.
  Columns outside that range keep the identity from `L8042`.
* It presets `$2634-$2647` and `$26C8-$26DB` to `$67`, `$2648-$26CA` to `$6B`, and
  initialises `$290C…$293A` (`$80`), seemingly clip limits per column.
* `init_line_height_buffer` (`$7CA7`) fills `$2608-$2707` with `$FF`.

```text
; init_pixel_to_collum_table(X = column offset), $A356
LBD00[$20 + 4*k .. +3] = X + k          for k = 0..45     ; 23 iterations of 2 bytes
clip_left [$2634..$2647] = $67 ; clip_right[$26C8..$26DB] = $67
mid       [$2648..$26CA] = $6B
$290C,$290D,$2937..$293A = $80
; L8042: bitmask[x] = $80,$20,$08,$02 repeating ; LBD00[x] = x
```

### 5.7 Terrain generation: `init_grid_map` `$843C`

* Fills `heightmap` with `(RANDOM & $3F) | $80`; bit 7 marks "unresolved".
* Plants a seed, then iterates a midpoint-displacement pass using the `$24BE` stack and
  `L84E6`, which tests four neighbour directions via `set_high_mountains` (`$84C4`) and
  calls `L8526` to resolve them.
* Random direction pairs (`$01/$10`, `$F0/$01`, …) step by 1 or 16 in the 16x16 ring.

```text
; init_grid_map ($843C), summary (not line-by-line)
for i in 0..255: heightmap[i] = (RANDOM & $3F) | $80     ; $80 = unresolved
$98 = 1 ; $9C = RANDOM ; heightmap[$9C] = RANDOM & $3F     ; first seed
loop:                                                      ; L8460
    $99 = $98
    L84E6()                          ; tries the 4 neighbour directions, may push on $24BE
    if $98 == $99:                   ; nothing pushed: backtrack
        $98-- ; if $98 == 0: return
        $9C = $24BE[$98] ; continue
    else:                            ; walk on in a random direction ($9A/$9B step pair)
        repeat: pick random (step1, step2) from {(1,$10), ($FF,$F0), ($10,$FF), ($F0,1)}
        until set_high_mountains() < 0   ; target still unresolved
        $9C += step1 ; heightmap[$9C] = RANDOM & $3F
        $9C += step1 ; heightmap[$9C] = RANDOM & $3F
```

### 5.8 VBI and DLI

* Gameplay VBI at `$8059`: writes colour/PMG registers from the shadow bytes, ages
  `RTCLOK`, applies attract-mode colour scrambling, and sets `DLISTL/H` from `$A7`
  (DL `$316B` or `$3210`).
* `PRIOR` is `$11` (`GPRIOR`) during play. The DLI vector is `$4CF1`; per-stage handlers use a
  jump table at `$4DD6` indexed by `$C8` **(notes)**.
* Music, sound and PMG routines live around `$9100-$94FF` and the `$61xx` DLI tables
  **(notes)**; I did not trace these.

```text
; gameplay VBI ($8059), summary
colour and PMG registers <- shadow bytes ($D0-$DA, $28xx)
RTCLOK+2++ ; on wrap: if $0632 | $28A5: RTCLOK++, $0631++ and, when it reaches $80,
           ;          scramble the palette copy ($0741 -> $D0) with RTCLOK (attract mode)
if $0626 != 0 and $A7 != 0:
    DLIST = ($A7 == 1) ? $316B : $3210       ; active when the 'bne' at $80E8 is assembled in
```

## 6. Variables, ranges and world coordinates

All flight variables live in `$2840-$28A5`; 16-bit values are little endian (low byte first). The
PureBasic port keeps every one of them as a byte (`TFlight` in `jaggie_flight.pbi`), so the formats below
are exactly what the code uses.

### 6.1 The world

| Item | Format and range |
|---|---|
| Terrain | 16 x 16 cells in `heightmap` (`$0900`, index `Z << 4 \| X`), a torus: both axes wrap with `AND #$0F` |
| Cell value | 0..`$3F` low ground (valley floor); bit 7 set = unresolved, a tall mountain (§5, `set_high_mountains`) |
| Camera X / Z | `$289B/$289C`, `$289D/$289E`: unsigned 16 bit, 0..`$FFFF`; one cell = `$1000`, so the whole map is `$10000` and the position wraps by itself on 16-bit overflow |
| Cell and fraction | cell = `pos >> 12` (0..15); sub-cell fraction = `(pos >> 4) & $FF` (0..255 across one cell; the low 4 bits are dropped) |
| Axes | X to the right, Z forward at heading 0; Y (height) up |

### 6.2 Height

| Item | Format and range |
|---|---|
| Altitude `$289F/$28A0` | unsigned 16 bit, `$0000..$4FFF`: the high byte is clamped (`>= $50` gives `$4F.FF`, an underflow to `$FF` gives 0). The game starts at `$24F8` |
| Height unit | `$40` altitude counts. `alt >> 6` is in the same unit as a `heightmap` value; the camera code keeps `alt << 2` as `$8C.$8B` (whole.fraction, `$8C = $FF` once the high byte is `>= $40`) |
| Ground check | `$27FC = (min(altHi, $3F) << 2) \| altLo >> 6` is compared with the interpolated ground height (§7.11); `$061D` = clearance in height units, 0 after a hit |
| HUD | the window shows `altHi` as "alt" (`$2B00` shows 43) |

### 6.3 Heading, bank, pitch

| Item | Format and range |
|---|---|
| Heading `$2899/$289A` | unsigned, a full turn = `$4000` (0..`$3FFF`). Step size `$40` = 1/256 turn (1.4 degrees); `heading * 360 / $4000` gives degrees (the HUD value) |
| Quadrant | `headingHi >> 4` (0..3), bits 5-4 of the high byte; selects the vertex walk table (`$B6A3`, `$B6D0`, `$B6FD`, `$B72A`, 45 bytes each) |
| Sine index | `headingHi * 4 + (headingLo >> 6)` = 0..255 per turn; interpolation weight `(headingLo >> 3) & 7`; cosine = index + `$40`. Result about +-2040 (§7.2) |
| Bank `$288B/$288C` | signed 8.8, clamped -5.00 .. +4.FF. Heading changes by `bank / 16` per flight tick, so full bank turns `$50` heading counts per tick (about 53 degrees per second at 30 ticks per second) |
| Roll `$2889/$288A` | the displayed bank, a copy one tick late; in landing it fades to 0 by `$14` per tick |
| Pitch `$288D/$288E` | signed 8.8, clamped -12.00 .. +11.FF; negative = nose down (stick up = dive, §7.9). Springs back to 0 when the stick is released |
| Pitch acceleration `$2888` | `$D0`, `$30`, `$FF`, `$01` or 0 from the stick (`L8530`) |
| Turn offset `$2886` | 0 centred, otherwise `(thrust >> 2) + $20 (+ rounding)`, inverted for left |
| Markers `$2878`, `$287A` | HUD roll and pitch markers: value + 5 resp. + `$0C`, rounded |

### 6.4 Speed and velocity

| Item | Format and range |
|---|---|
| Thrust `$2887` | unsigned byte, normal cruise `$1E`; the throttle table `L477C` gives `$92 .. $1E` for throttle steps 0..7 (with offset `$0619` = 8), step 8 = 0. It also scales the turn rate |
| Speed `$2897/$2898` | unsigned 8.8. Cruise settles at about 18.3 (`$1244`): thrust plus a random jitter (`RANDOM & (thrust/2 or 7)`) minus drag; 0 with thrust 0 |
| Velocity `$2891-$2894` | two signed 16-bit values: X = `speedHi * sin(heading)`, Z = `speedHi * cos(heading)` (`L8F67`, via the 8x16 multiply). Added to the position every tick |
| Climb `$2895/$2896` | signed 16 bit = `speed * pitch`, added to the altitude every tick |

Measured at cruise (speed 18): the camera moves about 23 position units per tick, so one cell (`$1000`
units) takes roughly 180 ticks, 6 seconds at 30 ticks per second. This is a rough figure from one
recorded run, not a value read from the code.

### 6.5 Timing

The flight tick runs from the vertical blank on every second frame (`$0642` counts 2, 1), so 25 ticks per
second on PAL and 30 on NTSC. The port runs it at 30 Hz. Each game frame renders two passes into two
alternating screen buffers (`$A7`, §4).

### 6.6 Screen coordinates

| Item | Format and range |
|---|---|
| Camera space | X right, Z forward (`$229E`.. / `$22FA`..), `Y = dy`: 16-bit signed, `dy = ((h << 8) - alt88) >> 4` |
| Frustum flags | `$80` Z < 6 (behind / too near), `$40` left of the 90 degree frustum, `$20` right |
| Projected X | `Xs = $80 +- 96 * \|X\| / Z`, stored 16 bit; the visible pixel columns are 44..211 (168 columns, centre `$80`), the stored x of a point is its column + 1 |
| Projected Y | ridge value `Y`: `$68` (bottom row) .. `$97` (top row), 48 rows; computed in 16 bit and saturated to a byte (0..`$FF`) before the line routine |
| Ridge buffer `$2608` | one byte per column: floor `$6B` (`$67` at the left and right edge columns), then the highest plotted Y; `$FF` = column closed at the top |
| Horizon | `$2708` ramp: bank gives a vertical shear per 8 columns, pitch gives the base `$A6 = 6 - (pitch << 2).hi` |
| Window | the cockpit covers the bottom 5 of the 48 rows (Y <= `$6C`), 43 are visible |

### 6.7 Mode flags

| Variable | Normal flight | Meaning |
|---|---|---|
| `$0626` | 2 | display / game mode; 0 = no flight tick, 2 = in the cockpit |
| `$28A5` | 0 | crashed: no physics, pitch/bank kept |
| `$28A4` | 0 | landed (1; later 2): §7.12 |
| `$061C` | 0 | autopilot-like mode (`L8EC9`), not ported |
| `$065F` | `$FF` | cockpit mode: `$FF` normal, 0 landing, 2 another sequence (not ported) |
| `$0625` | `$DB` counting down | enables the speed loss on a ground hit and the throttle / landing keys |
| `$0611` | 7 | throttle step 0..8 |
| `$293D` | `$FF` | enables the stick pitch response and drag |

## 7. 3D math, perspective and clipping

Everything is 16-bit fixed point on a 6502 with no real multiply or divide. The design
avoids both wherever it can.

### 7.1 World and camera

* The world is the toroidal 16x16 `heightmap` (`$0900`, index `Z<<4 | X`, wraps via `AND #$0F`).
* Camera X/Z are `$289B/$289C` and `$289D/$289E`. `LAD6C` shifts each right by 4:
  the high nibble becomes the grid cell (`$88` = X cell, `$8A` = Z cell) and the rest is
  the sub-cell fraction (`$87` = X, `$89` = Z).
* Altitude `$289F/$28A0` becomes `$8B` (fraction) and `$8C` (whole units, clamped to `$FF`
  when `$28A0 >= $40`).
* Heading `$2899/$289A` selects the quadrant `$92` (bits 5-4 of the high byte; a full turn is `$40`) and feeds the
  sine lookup (index `h*4 + (l>>6)`, 0-255 per turn, 3-bit interpolation weight `(l>>3)&7`).

```text
; camera state (LAD6C)
cellX = (camX16 >> 4) >> 8 ; fracX = (camX16 >> 4) & $FF        ; $88 / $87
cellZ = (camZ16 >> 4) >> 8 ; fracZ = (camZ16 >> 4) & $FF        ; $8A / $89
alt88 = (alt16 << 2) clamped ; $8C.$8B = whole.fraction         ; $8C = $FF if altHi >= $40
$92   = headingHi << 2                                          ; bit7/6 (= headingHi bits 5/4) select the walk table
```

### 7.2 Rotation without a rotation matrix

* `L9B3C` / `L9B80` / `L9C0E` compute sine from two 65-byte tables: `sintable` (`$900E`, first
  entries `$00,$06,$0C,$12,…`) is the high byte and the table at `$904F` (`$00,$42,$83,$C2,…`)
  the low byte of a quarter wave that runs 0 to 255.0. `L9C0E` mirrors the index with
  `i6 XOR $3F` in the odd quadrants (`L9B38` = `00 3F 00 3F`) and negates the value in the lower
  half (`L9B34` = `00 00 FF FF`). `L9B80` interpolates with the 3 bits `(heading_lo >> 3) & 7`:
  `acc24 = B*(8-w) + A*w` with `B = value(idx)`, `A = value(idx+1)` (the accumulator starts
  with B, then adds A or B shifted by 0, 1, 2 for each bit of w), and `L9B3C` keeps the top two
  bytes, `acc24 >> 8`. Cosine is the same at idx + `$40`. Results: sin `$280A/B`, cos `$280C/D`,
  copied to `$A0/$A1` and `$A2/$A3`; amplitude about 2040.
* `L9CCF` is the only multiply: `A` (unsigned 8-bit) times a signed 16-bit value in
  `$AB/$AC`, unrolled 8 shift-and-add steps, result `>>8` in `$A9/$AA`. It is called four
  times, all in `LA0DB`.
* `LA0DB` computes the camera-space position of vertex 0 (the nearest grid corner) from the
  camera's sub-cell fraction:
  `X0 = A0A1*frac$89 - A2A3*frac$87`, `Z0 = -(A2A3*frac$89 + A0A1*frac$87)`.
  That is a 2D rotation of the fractional offset, stored in `$229D/$22CB` and `$22F9/$2327`.
* The other 44 vertices cost no multiplies at all: `update_tables` adds or subtracts the
  basis vectors `(A0A1, A2A3)` to the previous vertex's X/Z with 16-bit adds (§5.1). The
  per-quadrant walk table only decides which of the 8 add/sub/swap combinations applies.

```text
; sine / cosine (L9B3C, L9B80)
idx = (headingHi << 2) | (headingLo >> 6)        ; 0..255 per turn
w   = (headingLo >> 3) & 7                       ; interpolation weight
V(i)  = (sintable[y]<<8 | table904F[y]) with y = (i&63) ^ ((i>>6)&1 ? $3F : 0), negated if (i&255) >= $80
sin = (V(idx)*(8-w)     + V(idx+1)*w)     >> 8      -> $280A/B -> $A0/$A1
cos = (V(idx+$40)*(8-w) + V(idx+$41)*w)   >> 8      -> $280C/D -> $A2/$A3

; 8x16 multiply (L9CCF): A * ($AB:$AC) >> 8
mul(A, m):  neg = m < 0 ; m = |m| ; acc = 0
            for bit in A (lsb first): if bit: acc += m ; acc >>= 1
            return neg ? -acc : acc

; vertex 0 / walk origin (LA0DB)
origin.X = mul(fracZ, sin) - mul(fracX, cos)                 ; $229D / $22CB
origin.Z = -(mul(fracZ, cos) + mul(fracX, sin))              ; $22F9 / $2327
```

### 7.3 Height (the Y axis)

For each vertex `update_tables` also reads `heightmap` and computes
`dy = (height - $8C)` with `-$8B` as the low fraction, then `>>4` as a 16-bit signed value
(`LA010` sign-extends with `EOR #$F0`). `dy` goes to `$2382` (hi) / `$2355` (lo); the raw
height stays in `$23AF`.

```text
; height of vertex Y (update_tables, $9FEA)
h  = heightmap[idx]
dy = ((h << 8) - ($8C << 8 | $8B)) >> 4          ; 16 bit signed
dy_hi:dy_lo -> $2382,Y : $2355,Y ; h -> $23AF,Y
```

### 7.4 Perspective: `LA157` `$A157`

Camera space is `X` right, `Z` forward (`$229E/$22CC` and `$22FA/$2328`), `Y = dy`.
The code computes `Xs = $80 + 96 * X/Z` and `Ys = $80 + 96 * dy/Z` (96 = 0.375 * 256),
then adds the horizon offset (§7.6). The divide is a floating-point style division:

1. Take `|X|`; double `Z` (`$AF/$B0`) until `Z > |X|`, counting `Y` doublings (max 8; if
   `|X| >= Z * 256` the result saturates to `$4000` / `$C000`).
2. `L9DA7` normalises the divisor so bit 14 is set (shifting both), then 8 restoring-division
   steps give an 8-bit quotient in `$B3` (the fraction `|X| / (Z<<Y)`).
3. Shift the quotient left `Y` times to undo step 1, scale by `x + x/2` then `/4`
   (= 0.375), and add or subtract from `$80` depending on the sign of `X`.

Edge check: `|X| = Z` gives `$80 ± 96 = $20 / $E0`, so the 90-degree frustum edge lands on
column `$20`. The visible window is narrower: columns 44 to 211 (42 bytes of 4 pixels), so the
`|X| = Z` plane is just outside the left edge of the picture. (The ridge buffer is `$FF` for
columns below 44 and `set_pixel_line` ignores x below 45.)

The same routine is run a second time for `dy`, giving `Ys` in `$2436/$2463`.

```text
; project(i), LA157
Z  = Zarr[i] ; ax = abs(Xarr[i])
n = 0
while ax >= Z:
    Z <<= 1 ; n++
    if n == 8: Xs = (X < 0) ? $C000 : $4000 ; goto Y     ; saturate, off screen
q = divide8(ax, Z)                  ; L9DA7: normalise, 8 restoring steps, q = 256*ax/Z
r = q << n                          ; undo the doublings
off = (r + (r >> 1)) >> 2           ; r * 0.375
Xs  = (X < 0) ? $80 - off : $80 + off
; same again with ax = abs(dy), Z (not the doubled one): Ys = $80 +/- off
Ys += ramp[Xs >> 3]                 ; roll/pitch, see 7.6
flags[i] |= $10
```

### 7.5 3D clipping (view frustum, done per vertex)

At the end of the vertex loop (`LA01E-LA075`) each vertex gets a flag in `$2490`:

| Flag | Test |
|---|---|
| `$80` | `Z < 6` (includes negative `Z`): behind or too near the camera |
| `$40` | `-X >= Z`: outside the left frustum plane |
| `$20` | `X >= Z`: outside the right plane |
| `$00` | inside |
| `$10` | OR-ed in by `LA157`: already projected |

The vertical planes are not clipped in 3D; they are handled in 2D (§7.7).

`LA080-LA0DA` is the near-plane fix-up. It runs only over the first 6 edge pairs
(`CPY #$0C`, i.e. the nearest row/column). If exactly one end is `$80` and the other is
inside, the bad vertex is not intersected but simply moved to `Z = 6` and marked inside.
X and height are left unchanged, so the edge slides slightly. Any other `$80` vertex makes
its edges get skipped.

Trivial rejection in `LA3E7`: an edge is dropped if vertex 1 has `$80` or `$20`, or vertex 2
has `$80` or `$40`. That works because the edge table is ordered left to right in camera
space, so "left end is beyond the right plane" or "right end beyond the left plane" means
the whole edge is outside. `LB24D` also returns at once when `x1 >= x2`, so edges that
fold backwards on screen are culled.

```text
; frustum flags per vertex (LA01E) and near-plane fix-up (LA080)
if Z < 6:                 flag = $80
elif X < 0:  flag = (-X >= Z) ? $40 : 0
else:        flag = ( X >= Z) ? $20 : 0
flags[i] = flag

for Y in 0, 2, ... 10:                   ; first 6 edge pairs only
    a = LB757[Y] ; b = LB757[Y+1]
    if flags[a] == $80 and flags[b] == 0:  Z[a] = 6 ; flags[a] = 0
    if flags[a] == 0   and flags[b] == $80: Z[b] = 6 ; flags[b] = 0
```

### 7.6 Pitch and roll

* Pitch: `$A6 = 6 - (sink rate bits)` in `LAD6C` is the horizon base.
* Roll: `LAE0A` builds a 32-entry horizon ramp in `$2708-$2727`, `$A4/$A5` per step from
  the roll angle (`$288A/$2889`). `LA315` adds `ramp[Xs >> 3]` to `Ys` (sign extended; far
  off-screen `X` uses the first or last entry). So roll is a vertical shear per column, not a rotation.

```text
; horizon ramp (LAE0A) and its use (LA315)
ramp[16] = A6                                    ; base from pitch
for k in 1..15: ramp[16+k] = ramp[15+k] + slope  ; slope = $A4:$A5 (8.8) from roll
                ramp[16-k] = ramp[17-k] - slope
Ys += (Xs hi == 0) ? ramp[Xs >> 3] : (Xs < 0 ? ramp[0] : ramp[31])
```

### 7.7 2D clipping and ridge drawing: `LB24D` `$B24D`

Screen X is a signed 16-bit value, so off-screen points keep a valid coordinate. The
clipper works on the segment `end1 = ($82-$86)` to `end2 = array[0]`, with a stack of
pending points at `$2572+n`:

* **Left side.** While `x1 < 0` the segment is bisected with `LB3A7` (midpoint, no
  division). A midpoint with `x < 40` (`$28`) replaces `end1` (the left part is thrown away);
  otherwise it is pushed on the stack and bisection continues toward the boundary. At most
  `$14` bisections, then the edge is abandoned.
* **Right side.** If `x1 >= 256` or `x1 >= $D8` the edge is rejected; points beyond
  column `$D4` (212) are ignored by the line routine (`CPX #$D4` in `LB51C`).
* **Vertical.** Y is not clipped geometrically. `LB356-LB376` saturates the 16-bit `Y` to
  `$00`/`$FF` and uses the test value `$6C` to decide whether the end points are on the
  visible side. `set_pixel_line` then stores row values `>= $97` (151) as `$FF`
  ("empty column") in `line_pixel_height_buffer`.
* **Subdivision with fractal displacement.** `LB3A7` averages X, Y and the byte in
  `$86`/`$25EA`. If bit 7 of `(h1 + h2 + 1)` is set, `LB3DA`/`LB3FA` move the midpoint's Y
  by a quarter of the segment width (`(mx - x1)/2`), subtracting or adding depending on the
  carry. That is the "jaggedness". Which heights have that bit set (the `$80` flag in
  `heightmap`) is my open question below.
* Segments narrower than `$14` pixels stop subdividing and go to `LB45D`/`LB51C`, which
  interpolates and plots the ridge into `line_pixel_height_buffer` and the bitmap.

```text
; clip_edge (LB24D), simplified; the real code has several exits
if e1.x >= e2.x: return                          ; back-facing
n = $14 ; top = 0                                ; stack[0] = e2
while true:
    if e1.x < 0:                                 ; left clip by bisection
        if --n < 0: return
        m = midpoint(e1, stack[top])
        if m.x < 40: e1 = m                      ; drop the left part
        else:        stack[++top] = m            ; keep it as the new right end
    else:
        if e1.x >= 256 or e1.x >= $D8: return
        if stack[top].x - e1.x < $14:            ; short enough
            y = clamp8(y) ; draw(e1 -> stack[top]) ; set_pixel_line / LB51C
            if top == 0: return
            e1 = stack[top--]
        else:                                    ; subdivide
            if top + 1 == $0F or --n < 0: return   ; stack full / out of steps
            m = midpoint(e1, stack[top])
            stack[++top] = m
```
(an extra vertical test on y also chooses between "draw" and "subdivide" for wide segments)

### 7.8 Runtime check of the rotation (headless AltirraBridge, `jaggie.atstate2`)

Method: load `jaggie.atstate2`, breakpoint at `clear_screen` (`$AE3E`, right after
`update_tables`), set the heading, wait for the second hit so the arrays are fresh, then dump
the 45 vertices (camera X/Z from `$229E+i` / `$22FA+i` with the grid index from `$2270+i`) and
fit `(X,Z) = M * (cell dx, cell dz)` over all neighbouring vertex pairs (about 300 per
heading, excluding vertices clamped to Z=6).

| Heading (hi,lo) | `$92` | fitted angle | expected `h*360/$4000` | error |
|---|---|---|---|---|
| `$0069` | `$00` | 2.28 | 2.31 | -0.03 |
| `$0F79` | `$3C` | 88.43 | 87.04 | +1.39 |
| `$107C` | `$40` | 92.65 | 92.73 | -0.08 |
| `$1F80` | `$7C` | 178.59 | 177.19 | +1.41 |
| `$2080` | `$80` | -177.16 | -177.19 | +0.03 |
| `$2F80` | `$BC` | -91.43 | -92.81 | +1.38 |
| `$3080` | `$C0` | -87.19 | -87.19 | 0.00 |

Results (13 headings sampled, all consistent):

* The fit residual is exactly 0, the two axes are perpendicular and equal in length: the 45
  vertices are an exact rotation plus uniform scale of the cell grid.
* Scale: about 2035 camera units per cell (2009-2038 depending on heading; the spread is the
  sine table's interpolation error).
* Angle follows the heading, `full turn = $4000`, with a periodic error of 0 to +1.4 degrees
  (also the table interpolation).
* Quadrant boundaries (`$92` bit 6/7 change at `h = $10, $20, $30`) show no jump: for example
  `$1F80` to `$2080` goes 178.59 to -177.16 degrees (+4.25 for a +4.2 degree heading step).
  So the four walk tables line up with the sin/cos rotation.
* At heading 0 the map is not mirrored: cell +X maps to camera +X and cell +Z to camera +Z.
* The fit only works with the vertex meta data at index `i` paired with the position at
  `$229E+i`. That confirms the one-slot offset: `$229D` / `$22CB` / `$22F9` / `$2327` hold the
  walk origin written by `LA0DB`, not a mesh vertex; mesh vertex 0 is the first step away
  from it.

Test notes for later: the intro runs about 1,700 emulated frames after START, so use the
savestate. START or OPTION starts the game (fire does not). With a breakpoint set, `frame(n)`
can block; use `resume()` and poll `regs()["PC"]` instead.

```text
; the rotation check (runtime test)
for heading in sample:
    poke(heading) ; run to $AE3E twice
    for i, j over neighbouring vertices (|dcell| <= 3, Z > 6):
        pairs += ( cell_delta(i,j), cam_delta(i,j) )
    fit M by least squares:  cam = M * cell
    assert residual == 0 ; assert M.col1 . M.col2 == 0 ; assert |col1| == |col2|
    angle = atan2(M[1][0], M[0][0])               ; expect heading * 360 / $4000
```

### 7.9 Banking and pitch (flight model, `$8530-$88xx`)

There is no real camera roll or pitch rotation. Both are driven by two angles that the
flight code integrates from the joystick, and the renderer turns them into cheap screen
effects (§7.6).

```text
; read PORTA ($D300): bit2 = left, bit3 = right, bit0 = up, bit1 = down (active low)
turn_offset = ($2887 >> 2) + $20                ; $2887 = speed
left:  turn_offset = ~turn_offset  ; right: keep ; none: 0
bank   ($288C:$288B, 8.8 signed) += turn_offset ; clamp to -5 .. +5
if turn_offset == 0: bank -= bank / 32          ; spring back to level (5 shifts)
heading ($2899/$289A) += bank / 16 ; heading &= $3FFF   ; turn rate follows bank angle

pitch_accel ($2888) = up ? -$30 : down ? +$30 : 0     ; larger magnitude near the limits
pitch ($288E:$288D, 8.8 signed) += pitch_accel   ; clamp to -12 .. +11
if no stick: pitch decays toward 0
climb = speed($2898) * pitch                     ; L8FF9 multiply, then altitude += climb << 3
altitude ($28A0:$289F) += climb ; clamp to < $50
position += velocity(heading, speed)             ; $289B/$289D += $2891/$2893 (L8F67)

; used by the renderer (LAD6C)
roll   = $288A:$2889   (copy of bank, L860E)
$A4:$A5 = roll / 2                               ; horizon ramp slope per 8 pixel columns
$A6     = 6 - (pitch << 2).hi                    ; horizon base
$2825   = clamp(roll.hi + 4, 0, 8)               ; cockpit tilt gauge frame (artificial horizon "AH")
$2878   = roll.hi + 5 ; $287A = pitch.hi + $0C   ; HUD marker positions (PMG)
```

Measured in the emulator from `jaggie.atstate2` (stick held, values read back):

| Input | bank | pitch | horizon ramp `[0] / [16] / [31]` | heading / altitude |
|---|---|---|---|---|
| none | 0 | 0 | 6 / 6 / 6 | steady |
| left 15 / 30 / 45 / 60 frames | -1.6 / -2.7 / -4.0 / -5.0 | ~0 | 15/5/-5, 24/5/-13, 35/6/-22, 44/6/-30 | heading falls `$3675` to `$3137` |
| released | decays to 0 in about 90 frames | | flattens to 6 / 6 / 6 | |
| right 15 / 30 / 45 / 60 | +1.3 / +2.3 / +3.6 / +4.7 | ~0 | 3/6/8, -8/6/18, -19/6/29, -26/6/39 | heading rises |
| up 15 / 30 / 45 / 60 | 0 | -1.5 / -2.8 / -4.3 / -5.6 | 9, 16, 22, 28 (whole ramp shifts) | altitude falls `$24F8` to `$2390` |
| down 15 ... 90 | 0 | -4.1 ... +2.8 | 27 ... -3 | altitude falls then rises |

* **Tilting when turning: yes.** Left/right changes the bank angle, the bank sets the turn
  rate, and the bank tilts the horizon by adding a linear ramp (about +44 at the left edge
  to -30 at the right edge at full left bank) to every projected Y. Terrain, ridge and sky
  all shear together; the "AH" gauge frame and HUD markers follow. It is a vertical shear
  per column, not a rotation, so vertical edges stay vertical.
* **Up/down: yes, as a vertical shift plus real altitude change.** Pitch moves the whole
  horizon base (`$A6`) up or down; because the stick also changes altitude, the projected
  heights `dy/Z` change too. Stick up = push forward = dive: pitch goes negative, the horizon
  base rises (a larger ramp value is higher on screen) and altitude falls.
* There is no pitch rotation of the vertices: the 3D data is rotated only by heading (§7.2).

### 7.10 Findings from the PureBasic renderer (`jaggie_3d.pb`)

Porting the whole view to PureBasic and comparing it with the emulator (same heightmap and
camera dumped from `jaggie.atstate2`) settled three things:

* **Screen Y counts up, and the view is 48 rows.** The ridge value `Y` runs from `$68` (bottom
  row) to `$97` (top row): the ridge buffer is initialised to `$6B`/`$67`, `cpy #$97` is the top
  limit, and the row address tables hold 48 entries (ANTIC mode D rows, 2 scanlines each; the
  `$28EC` / `$291C` equates in the source are bases offset by `$68`). A larger `Ys` is higher on
  screen. Pixel aspect is 1 colour clock wide by 2 scanlines tall.
* **The fractal displacement fires on mountain-to-mountain edges.** `LB3A7` computes
  `h = h1 + h2 + 1` in 8 bits and displaces when bit 7 is set. Heights with bit 7 set
  (`$80-$BF`, the "unresolved" cells of `init_grid_map`) are the tall mountains; resolved cells
  (`$00-$3F`) are valleys. Two mountains give a sum above 255 (carry, midpoint pushed up), a
  mountain and a valley give `$80-$FF` without carry (midpoint pushed down), two valleys never
  displace. So the generator's unresolved cells are the jagged high ground.
* **`dy` is not a 16 bit wrapped value.** `(h*256 - altitude) >> 4` is negative for most
  terrain below the camera and can be below -32768 before the shift; the code takes the sign from
  the subtract's borrow and sign-extends (`EOR #$F0`). Treating it as wrapped 16 bit turns valleys
  into spikes.

**The ridge line routine and the height buffer** (ported literally in `jaggie_3d.pb`):

* **`line_pixel_height_buffer` (`$2608`)** holds, per column, the highest ridge value plotted so
  far. Each frame it starts as `$FF` for columns 0-43 and 212-255, `$67` for 44-63 and 196-211,
  `$6B` for 64-195 (the floor of the view). A pixel is plotted only if its `Y` is greater than the
  column's value (`CMP buf,X; BCC/BEQ skip`); the buffer is then updated, and a value `>= $97` is
  stored as `$FF` (column closed) and drawn at the top row `$97`. That is why only a sparse set of
  interior lines shows up: an edge draws a pixel only where it rises above everything drawn before
  it in that column, and the sky fill then starts at the topmost pixel of the column.
* **Two levels of bisection.** `LB24D` works on 16 bit points: it clips to `x >= 0` (bisecting
  while `x1 < 0`, keeping midpoints with `x >= 40`), subdivides until the end point is below 256, culls
  a piece when both ends are below `$6C`, and subdivides a piece that crosses `$6C` while it is at
  least `$14` wide and its low end would still be below zero after rising by width/4. A finished
  piece goes to `set_pixel_line` with Y saturated to a byte.
* **`set_pixel_line` / `LB45D` / `LB51C` are the byte "fractal line".** The state is end 1 in
  `$82/$84/$86` (x, y, h) and a zero page stack at `$95+Y` (x), `$E3+Y` (y), `$ED+Y` (h) whose top
  is end 2. While end 1 is left of column 44, `LB45D` halves towards the right (dropping the left
  half when the midpoint is `<= 44`, pushing it otherwise). `LB51C` then pushes midpoints until the
  gap is 1 or 2 columns: for a gap of 2 it plots the middle column with `(y1 + y2 + 1) >> 1` and
  then the end column with `y2`; for a gap of 1 only the end column with `y2` (the stored x of a
  point is its column + 1). Every midpoint gets the same displacement as `LB3A7`
  (`h = h1 + h2 + 1`, bit 7 set means displace by `(x_mid - x1) >> 1`, subtract without a carry out
  of the h sum and add with one, with the exact `SBC`/`ADC` carry quirks of each of the four copies
  of the code).

**Clipping in Y.** There is no geometric clip against the top or bottom of the view. Four stages
decide what ends up on screen:

1. *16 bit cull and subdivide (`LB2EC`-`LB338` in `LB24D`).* The only vertical test on the 16 bit
   values, with the threshold `$6C` (just above the bottom row `$68`). Both ends below `$6C`: the
   piece is skipped. One end below and one above: narrower than `$14` columns is drawn as it is,
   wider is drawn only if the low end raised by width/4 is still `>= 0`, otherwise it is
   subdivided (the midpoint is displaced like any other). A top end at or above 256 means draw. There
   is no test against the top of the view here. Subdivision stops at `$9F` = `$14` steps or stack
   depth `$0F`.
2. *Byte saturation (`LB356`).* Both end Y values of a piece are clamped to a byte before the byte
   routine: high byte > 0 gives `$FF`, negative gives `0`. Pieces with a far-out end therefore get
   a distorted (shallower) slope instead of a true intersection.
3. *Byte arithmetic (`LB45D`, `LB51C`).* All in 8 bits: averages use the carry as bit 8, adding the
   displacement saturates at `$FF`, subtracting it wraps without a clamp.
4. *At plot time.* A pixel is skipped unless its Y is greater than `line_pixel_height_buffer[x]`;
   the buffer starts at `$6B` (`$67` at the corners), so that value is the bottom clip. A Y of `$97`
   or more is drawn on the top row `$97` and closes the column (buffer set to `$FF`).

```text
; Y handling of one edge piece (e1 = end 1, t = stack top, w = (t.x - e1.x) & $FF)
if e1.y < $6C:
    if t.y < $6C:                      skip the piece                       ; both below the view
    elif w >= $14 and e1.y - w/4 < 0:  subdivide (midpoint + displacement)   ; steeply rising from far below
    else:                              draw
else:                                  ; e1.y >= $6C
    if t.y < $6C and w >= $14 and t.y - w/4 < 0: subdivide
    else:                              draw
; draw:
y1 = clamp8(e1.y) ; y2 = clamp8(t.y)                                         ; 0 .. 255
for each column x the byte routine produces a value y (8 bit arithmetic):
    if y <= buf[x]: continue                                                 ; floor $6B / $67 or an earlier, higher pixel
    buf[x] = y
    if y >= $97: buf[x] = $FF ; y = $97                                      ; top row, column closed
    plot(x, y)
```

**Verification.** The renderer was run on the camera and heightmap captured from the emulator at
the call of `update_tables` and compared with the game's ridge buffer captured at `render_sky`
(`$AF32`) of the same frame. For two different frames all 168 columns (44-211) are identical to the
game's `line_pixel_height_buffer` (the one remaining column in the second frame is the game's floor
value, i.e. no pixel). Before the exact 16 bit sine tables were ported the same comparison was only
within 1-3 units, so the sine precision matters. The perspective divide is computed with an
integer division, which is equivalent to the normalising restoring loop of `L9DA7`.

Files: `jaggie_flight.pbi` (flight model include), `jaggie_data.pbi` (walk and edge tables from
the obx), `jaggie_3d.pb` (window, arrow keys; `--shot out.png --state state.txt` renders one frame
without a window), `jaggie_flight.pb` (console demo).

### 7.11 Keeping the camera out of the mountains (`L97A9` / `L99C2`)

The engine does not prevent the camera from entering terrain, it pushes it back out. `L97A9` runs
right after the flight tick (`L8216: jsr L8530 / jsr L97A9`), i.e. every second frame, and has
three steps.

1. **Terrain height under the camera (`L99C2`).** The camera position gives a cell and a fraction
   (`$27FE` / `$27FD` for X, `$2800` / `$27FF` for Z, from `pos >> 4`). The four surrounding
   `heightmap` cells (X wraps inside the row, rows wrap in the byte) are interpolated along X for
   both rows and then along Z with an 8 step shift-and-add loop (MSB of the fraction first:
   `bit = 0` adds the halved first value, `bit = 1` the halved second value, with the shifted-out
   bit as carry). The result `A0` is scaled to `$2801 = A0 + (A0>>4) + (A0>>5) + ((A0>>4)&1)`
   (about 1.09, 8 bit wrap).
2. **Compare.** Camera altitude in height units is `$27FC = ((alt_hi clamped to $3F) << 2) | alt_lo >> 6`
   (the same value as `$8C` in `LAD6C`, so the vertex heights and the collision use the same scale).
3. **Respond.** Altitude at or above ground: `$061D = altitude - ground` (height above ground) and
   nothing else. Below ground:
   * `L9AA5` (only if `$065F <> 0` and `$0625 <> 0`): speed high byte `$2898` is decremented if
     not zero, plus a sound and a text change;
   * `INC $28A0`, `$289F = 0`: the camera is lifted by one high byte step (4 height units) per tick;
   * `$061D = 0`;
   * if the pitch is negative (`$288E` bit 7): `INC $288E`, `$288D = 0`, the nose is pushed up one step.

```text
; L97A9, once per flight tick, after L8530
alt    = ((min(alt_hi, $3F) << 2) | (alt_lo >> 6)) & $FF
cx, fx = (posX >> 12) & $FF, (posX >> 4) & $FF            ; cell, fraction (same for Z)
row    = (cz << 4) & $FF
h00 = hm[(cx & 15) | row] ;  h01 = hm[(((cx_idx + 1) & 15) | row)]          ; X + 1 stays in the row
h10 = hm[(cx & 15) | (row+16 & $FF)] ; h11 = ... same with X + 1
a0  = lerp8(lerp8(h00, h01, fx), lerp8(h10, h11, fx), fz)
g   = (((a0 + (a0>>4)) & $FF) + (a0>>5) + ((a0>>4) & 1)) & $FF
if alt >= g:   height_above_ground = alt - g
else:          speed_hi -= 1 (if > 0) ; alt_hi += 1 ; alt_lo = 0 ; height_above_ground = 0
               if pitch_hi < 0: pitch_hi += 1 ; pitch_lo = 0
lerp8(a, b, f):  acc = 0 ; repeat 8: bit = f.7 ; f <<= 1
                    if bit == 0: c = a & 1 ; a >>= 1 ; b >>= 1 ; acc += a + c
                    else:        c = b & 1 ; b >>= 1 ; a >>= 1 ; acc += b + c      ; (8 bit)
```

The test is a single point at the camera, with no look-ahead. It is enough because the
interpolated surface is smooth and the ship moves only about 18 units per tick against a cell of
4096 units, so the camera meets a slope gradually and is lifted 4 height units per tick while it
scrapes along it; the speed loss makes you slow down on the way. The ports of the flight model
(`jaggie_flight.py`, `jaggie_flight.pbi`) include it (pass the heightmap), and `jaggie_3d.exe`
calls it after every flight tick, so you can no longer fly through terrain there. Without it the
camera can end up inside a mountain, where vertices fall behind or next to the camera and edges
are culled or clamped, which leaves gaps in the picture.

**Verification.** `ground_height_check.py` stops the game right after the `jsr L99C2` of `L97A9`
(`$981C`) and compares `$2801` with the model for 200 random camera positions: identical in all of
them. `flight_validate_ground.py` forces the altitude low several times and compares every state
byte on each flight tick: 185 ticks including 27 ground hits, all bytes identical (altitude, pitch,
speed high byte, `$061D`, velocity, position). Two notes from getting there: `$2801` cannot be read
at a random moment because other game code calls `L99C2` too (target and shot checks), and the
speed seen after a hit is already reduced, so the check runs the model with that speed and with
speed + 1 and keeps the variant that matches the game.

### 7.12 Landing (key `L`, `L8BDC`)

Landing is a mode of the normal flight tick, not a separate loop. The keyboard IRQ handler (`$4956`,
table `L4B44`: `L`, `A`, `B`, `E`, `*`, `+`, Backspace, Esc) sets the mode variable `$065F`; the flight
tick (`L8530` at `L85AF`) then calls the descent routine. The bank, pitch and speed code after it keeps
running, so the ship still turns and slows while it comes down.

| Variable | Meaning |
|---|---|
| `$065F` | cockpit mode: `$FF` normal, `0` landing (key `L`), `2` another sequence (not ported) |
| `$28A4` | landed flag: 0 flying, 1 touched down (the pilot code later raises it to 2) |
| `$061D` | height above the ground in height units, written by the ground check (`L97A9`), 0 after a ground hit |
| `$0611` | throttle step 0..8; step 8 gives thrust 0, otherwise thrust = table `L477C[step + $0619]` into `$2887` |

**Key `L`** (`L49A0` -> `L4AC2`): needs `$0625 <> 0`, `$28A4 = 0`, `$061C = 0`. If `$061D >= $64` the
landing is refused (`$065F` stays `$FF`, message `$C5`). Otherwise throttle step 8 is set (thrust `$2887`
= 0) and `$065F = 0`.

**Descent** (`L8BDC`, called every flight tick while `$065F = 0` and `$28A4 = 0`):
1. The displayed roll (`$2889/$288A`) moves towards 0 by `$14`: a positive roll is reduced and stops at 0,
   a negative roll gets `+$14` (no clamp).
2. If `$061D <> 0` the altitude `$289F/$28A0` drops by `$0030` per tick (a straight descent at constant
   speed, independent of pitch).
3. At `$061D = 0` the ship is on the ground: `$28A4` becomes 1 (if `$067A = 0`). In landed state the roll is
   no longer copied from the bank and the pitch spring-back is skipped.

**Take-off** (key `*`, `L4A65`, `cpy #4`): with `$28A4 <> 0` it clears the landed flag (or the crash flag
`$28A5` first, if set), zeroes the bank, selects throttle step 7 (thrust `$1E`) and sets `$065F = $FF`.
`*` and `+` while flying (not landed) change the throttle step by one and also set `$065F = $FF`, which
cancels a landing in progress.

```
tick (mode checks, L85AF..L8636):
    if $065F == 2:  autopilot-like sequence                    ; not ported
    elif $28A4 != 0: pass                                      ; landed, roll kept
    elif $065F == 0: land_step()
    else:            roll = bank                               ; L860E

land_step():                                                   ; L8BDC
    if roll < 0: roll += $0014  else: roll = max(roll - $0014, 0)
    if $061D != 0:  altitude -= $0030
    elif $28A4 == 0 and $067A == 0:  $28A4 = 1                 ; touched down

key L:   if $0625 and not $28A4 and not $061C and $065F != 0:
             if $061D >= $64: $065F = $FF          # too high
             else:            $065F = 0; thrust = 0
key *:   if $28A4: ($28A5 ? $28A5 = 0 : $28A4 = 0); bank = 0; thrust = $1E; $065F = $FF
```

**Port.** `land_step`, `key_land`, `key_takeoff` in `jaggie_flight.py`; `LandStep`, `KeyLand`,
`KeyTakeoff` in `jaggie_flight.pbi`. `jaggie_3d.pb`: `L` lands, `T` or `*` take off, a throttle key
(`+`, `-`, `PgUp`, `PgDn`) cancels a landing, and the HUD shows LANDING / LANDED. The ground check runs
after each tick as before, so the descent ends on the real terrain height.

**What was checked against the game, and what was not.**
- Checked by hand in the emulator (`key_probe.py`): after `L` the game sets `$065F = 0`, thrust 0, and the
  altitude falls by `$30` per tick until `$061D` reaches 0, then `$28A4` becomes 2 and the altitude stays
  constant; speed, pitch and bank run down to 0. At `$061D = $66` the key is refused.
- `flight_validate_land.py` (tick-by-tick comparison of all state bytes with the Python model) is **not yet
  conclusive**: its last run showed no mismatch in the landing phase, but it never logged the landing
  transition, so it may not have exercised it, and it showed a few speed/position byte differences after
  take-off (around tick 260). Both are open.
- Take-off is tested only by poking what the handler does (the bridge has no key name for `*`), not by the
  key handler itself.
- Not ported: the `$28A4` = 2 pilot phase, the message/graphics side effects (`L861D`, `L8E6B`), the
  `$065F = 2` sequence, and the throttle table `L477C` (the window keeps its own thrust +/- keys).

## 8. Open questions

* The role of the `$6C` test and of the `$D8`/`$28` limits in `LB24D` is read from the
  branches, not traced at runtime (the renderer clips exactly instead).
* Runtime confirmation that the restored render passes display correctly (the earlier
  headless run was done with both passes on `$30`).
* `$290C-$293A` and `$28F4/F5` semantics.
* Sound, entity queue (`$071B`, 32 slots) and pilot logic are not covered here.

## 9. The Atari 800XL flight port (`flight800\`)

`flight800\flight800.xex` is the flight of the game as a standalone 6502 program for an 800XL (64K, PAL), written from
the PureBasic port, without game logic, sound or cockpit. All code is new; only the constant tables and the start
terrain come from the game. Details, controls and build line: `flight800\README.md`.

| PureBasic port | 6502 file and routine | Replaces in the game |
|---|---|---|
| `FlightTick`, `Velocity`, `Mul8` | `flight.asm` `flight_tick`, `velocity`, `mul8` | `L8530`, `L8F67`, `L8FF9` |
| `GroundHeight`, `Lerp8`, `GroundCollision` | `flight.asm` `ground_height`, `lerp8`, `ground_collision` | `L99C2`, `L97A9` |
| `LandStep`, `KeyLand`, `KeyTakeoff` | `flight.asm` | `L8BDC`, keyboard handler |
| `UpdateTables`, `SinInterp`, `MulS`, `BuildRamp` | `transform.asm` | `update_tables`, `LAD6C`, `L9B3C..L9C0E`, `L9CCF`, `LA0DB`, `LAE0A` |
| `ProjCoord`, `ProjectVertex`, `RenderScene` | `render.asm` `proj_coord`, `project_vertex`, `edge_loop` | `LA157`, `LA3E7` |
| `DrawEdge`, `Mid3A7`, `SetPixelLine`, `Line51C`, `PlotCol` | `draw.asm` | `LB24D`, `LB3A7`, `B418`, `LB45D`, `LB51C` |
| sky fill | `render.asm` `sky_fill`, `clear_view`, `init_view` | `render_sky`, `clear_screen`, `init_pixel_to_collum_table` |

Data formats are those of §3 and §6 (the arrays keep the game's addresses, so a state can be compared with the game
at any point).

**Verification** (against the original game running in Altirra, same terrain and camera poked into both, stopped at the
start of render pass 1):
* ridge buffer `$2608`: identical in all 168 columns, bitmap rows 1..43 identical byte for byte after the sky fill, for
  six terrains and cameras including low altitude and empty columns;
* the new line drawing, the new 3D front end and the new projection / edge loop / sky fill were each swapped in
  one after the other and gave the same result at every step;
* the flight routines are checked tick by tick against the Python model (`flight800\validate800.py`).

Findings from the port: row 0 of the bitmap (ridge value `$97`) is above the display and the game never fills it
with sky; the display list starts at row 1 (`$1070`) and shows 47 rows; the game's blend step of the sky fill
overwrites the ridge pixels in the boundary byte, so the silhouette itself has no outline.

**Speed.** One render pass costs the port about 1.06 times the cycles of the game's renderer (`state_a`: 199,791 against
188,823; `flight800\speed800.py`). The first structured version was 1.4 to 1.7 times slower; the clear and sky fill are
unrolled over the 48 rows, the hot scratch variables live in zero page, the byte line routine keeps the column in X
and the projection divides with an unrolled restoring loop. The sky fill is now faster than the game's (27,664 against
32,085 cycles), the edge loop with the line drawing is about 6% slower (133,351 against roughly 125,000).

