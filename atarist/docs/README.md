# Atari ST — Reference Notes

Knowledge base for the Atari ST demo/coding project (follow-up to the Amiga work in `C:\coding\Amiga`).
Written from existing knowledge, NOT yet cross-checked against primary docs. Items marked `(verify)` are
the ones I'm least sure of — check against Hatari source / Atari Compendium / ST Internals before relying on them.

| File | Contents |
|---|---|
| 01-models-and-hardware.md | ST/STE/Mega/TT/Falcon family, ports, drives, what's in the box |
| 02-cpu-memory-map.md | 68000, bus sharing, memory map, interrupts, vectors |
| 03-video.md | Shifter, modes, palette, screen layout, timing, overscan |
| 04-mfp-ikbd-acia.md | MFP 68901 (timers!), ACIA 6850, IKBD 6301, MIDI, serial |
| 05-sound-fdc-dma.md | YM2149, WD1772 FDC, DMA chip, ACSI, STE DMA sound |
| 06-blitter-ste-tt-falcon.md | Blitter, STE extras, TT030, Falcon030 (Videl, DSP) |
| 07-tos-software-tools.md | TOS/BIOS/XBIOS/GEMDOS, PRG format, toolchain, emulators, disk formats |
| 08-demo-techniques.md | No-copper tricks: raster via timers, overscan, sync-scroll, c2p, Amiga→ST mindset |

## Amiga → ST in one paragraph
No blitter (except Mega ST/STE), no copper, no sprites, no hardware scroll (except STE), no bitplane DMA control,
no audio DMA (except STE). One 8 MHz 68000 does everything; the Shifter just reads screen RAM linearly.
Everything fancy = CPU cycle-counting against the video beam, using MFP timers + HBL/VBL interrupts.
