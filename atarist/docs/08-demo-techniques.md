# Demo-Coding Techniques on ST (no Copper, no Blitter)

## Fundamentals
- Count cycles in multiples of 4 (NOP = 4). Plan every effect in cycles-per-scanline (512 PAL).
- Take over the machine: Supexec, disable interrupts (move.w #$2700,sr), save/restore MFP + vectors + palette + res + screen address.
- Double-buffering: swap screen base via $FF8201/03 on VBL. Align screens to 256 bytes.
- VBL sync: wait on VBL flag or use `Vsync`; HBL via Timer B.

## Raster effects (Copper replacement)
- Per-line palette: Timer B in event-count mode = 1 -> interrupt every line; write palette entries during HBL. Cost: ~every line CPU time.
- "Rasters" in color 0 only: write 1-2 MOVE.W per line in a fully cycle-counted loop (no IRQ), CPU fully busy.
- Sync to beam: stabilise Timer B jitter by reading $FF8209 and shifting with nops; stable raster = precise start cycle.

## Overscan (open the borders)
- **Top/bottom border**: at line ~ end of display switch $FF820A to 60 Hz (0) then back to 50 Hz (2) before the next line starts.
- **Left/right borders**: at precise cycles toggle $FF8260 (0 -> 2 -> 0), and shortcut 50/60 Hz switch; yields 184/186/204-230 byte lines instead of 160.
- Needs a 4-cycle exact loop, and 512-cycle line. Real hardware differs from emulator (Hatari good, Steem good).
- STE: right border can be removed more easily via line offset register.

## Scrolling and big screens
- ST has no hardware scroll => sync-scroll (shift 1-15 px via mid-line mode flips), software scroll (1 px = shift in registers), or large pre-shifted tables.
- Sync-scroll uses resolution toggles at specific positions to start fetching later => 4-px moves; combine with 4-px words for 1px shifts.
- STE: use $FF8264/$FF820F for real hardware fine scroll + wide virtual screen.

## Graphics
- Planar needs c2p for chunky effects (plasma, texture mapper). Typical approaches: precomputed tables, 4-plane merging with MOVEP.W to write bit planes in one instruction (MOVEP is slow: 16/24 cycles).
- Sprites: software (pre-shifted 16 shifts, masked, 4 planes), "clipped sprites", use of movem.l for fast copy (MOVEM.L: 8 + 4n cycles + 4 bus cycles per longword).
- Fastest copy/clear: MOVEM.L d0-a6 -> (an)+/-(an); stack-clear trick with SP pointed at end of screen.
- Vector/3D: filled polygons using line-by-line fill with 16-pixel mask tables, flat/gouraud rare; texture mapper with c2p dominate.
- Dot/tunnel effects: precalculated offset tables; horizontal lines.

## Sound
- YM: 50 Hz play routine from VBL (SNDH). Sample-quality via sample playback at timer rate (e.g. ~8-16 kHz) by volume-register writes; costs lots of CPU.
- STE: use DMA sound - frees CPU entirely (up to 50 kHz stereo 8-bit).
- MOD players on ST: 4-ch mixing in software at ~ 10-20 kHz costs ~20-40% CPU on ST; on STE use DMA.

## Typical effects list
Rasters, plasma, rotozoomer, twister, sine scroller, 3D vector cubes, bobs (software sprites), dots, voxel-lite, fractals, interference/moire, full-screen overscan pictures, STE-only 4096-colour pics, hardware scroll.

## Famous reference demos (to study)
Union Demo (The Union), Cuddly Demos (TCB), Enchanted Land (Thalion), Punish Your Machine (Delta Force). Modern ST scene: Sommarhack, Silly Venture, Revision compos.

## Amiga -> ST thinking changes
| Amiga | ST equivalent |
|---|---|
| Copper (palette per line) | Timer B IRQ or cycle-counted loops |
| Blitter | CPU (MOVEM, unrolled code), blitter only on Mega/STE |
| Sprites | software only |
| Planar chip RAM / playfields | single bitmap, 16 colours max |
| Paula DMA 4ch audio | YM (3 ch) or STE DMA |
| Hardware scroll | none (STE has it) |
| 7.09 MHz 68000 | 8 MHz 68000 (4-cycle granularity) |
