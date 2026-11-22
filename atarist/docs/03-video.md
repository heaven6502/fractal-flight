# Video (Shifter, MMU, GLUE)

## Modes (base ST)
| Mode | $FF8260 | Resolution | Planes | Colours | Refresh |
|---|---|---|---|---|---|
| Low | 0 | 320x200 | 4 | 16 of 512 | 50 Hz PAL / 60 Hz NTSC |
| Medium | 1 | 640x200 | 2 | 4 of 512 | 50/60 |
| High | 2 | 640x400 | 1 | mono (needs SM124) | 71.4 Hz |
All modes use **32000 bytes** of screen RAM.

## Screen memory layout (interleaved planes)
- Low res: each 16-pixel group = 4 consecutive words (plane0..plane3). Line = 20 groups x 8 bytes = **160 bytes**.
- Medium: 2 words per 16 px, line = 160 bytes. High: 1 word per 16 px, line = 80 bytes.
- Pixel colour index = plane0 (LSB) .. plane3 (MSB).
- Screen base must be **256-byte aligned** on ST ($FF8201 high, $FF8203 mid). STE adds low byte $FF820D.
- Planar + interleaved => chunky-to-planar (c2p) needed for texture/3D effects (see 08).

## Palette
- $FF8240-$FF825E, 16 words. ST format `0000 0RRR 0GGG 0BBB` (3 bits/channel, 512 colours).
- STE format `0000 rRRR gGGG bBBB`: the extra low bit of each channel sits in bits 3/7/11 (LSB-first quirk), 4096 colours.
- Palette can change any time => per-scanline palette is the ST's main "copper-like" trick.

## Video address & sync
- $FF8205/07/09: live video counter (read = current fetch address => beam position; used to sync code to the beam).
- $FF820A sync mode: bit1 = 1 -> 50 Hz, 0 -> 60 Hz. Toggling at exact cycles removes top/bottom borders.
- $FF8260 resolution: toggling 0->1->0 or 0->2->0 at exact cycles removes side borders.

## Timing (colour monitor)
| | 50 Hz PAL | 60 Hz NTSC | Mono 71.4 Hz |
|---|---|---|---|
| CPU cycles/line | 512 | 508 | 224 (verify) |
| Lines/frame | 313 | 263 | 501 |
| Cycles/frame | 160 256 | 133 604 | |
| Display lines | 200 | 200 | 400 |
- HBL interrupt (IPL2) once per line; VBL (IPL4) once per frame; MFP Timer B can count display-enable (HBL) events -> per-line IRQ.
- Exact trigger lines/cycles for overscan tricks: take from Hatari source or the ST cycle docs (verify). First PAL display line ~63/64.

## Monitor detection
MFP GPIP bit 7 ($FFFA01) = 0 if mono monitor attached.
