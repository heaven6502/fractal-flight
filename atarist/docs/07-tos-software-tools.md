# TOS, Software, Toolchain

## TOS versions
| TOS | Machine | Notes |
|---|---|---|
| 1.00 | early 520ST | Loaded from disk initially, then 6 ROM chips; buggy |
| 1.02 | Mega ST / "Rainbow TOS" | Blitter support |
| 1.04 | STF | Bug fixes |
| 1.06 / 1.62 | STE | STE hardware support |
| 2.05/2.06 | Mega STE / ST-compatible | Faster desktop, HD |
| 3.0x | TT030 | |
| 4.0x | Falcon | MultiTOS-capable |

## Calling OS
- TRAP #1 GEMDOS (files, Super $20, Cconws $09, Pterm0 $00, Fopen etc.)
- TRAP #13 BIOS, TRAP #14 XBIOS: Physbase $02, Logbase $03, Getrez $04, Setscreen $05, Setpalette $06, Vsync $25, Supexec $26, Giaccess $1C, Dosound $20, Ikbdws $19, Mfpint $0D, Xbtimer $1F, Random $11, Floprd/Flopwr $08/$09 ...
- TRAP #2 GEM (AES/VDI) for GUI apps.
- Scene code usually uses Supexec once and then bashes hardware directly, restoring state on exit.

## Executable format (.PRG / .TOS / .TTP)
Header (28 bytes): $601A magic, text size, data size, bss size, symbol size, reserved, flags, absflag (0 = relocation table follows symbols).
Position-independent only via relocation table (programs relocate at load). Demos often ship as PRG or auto-boot disk with custom boot sector ($0000 words sum to $1234 = executable bootsector).

## Toolchain (to evaluate)
- **vasm** (m68k, Motorola syntax, `-Ftos` output) + **vlink** (`-batari`) — matches the Amiga workflow
- **Devpac** (native), **rmac**, **GCC m68k-atari-mint** (C)
- Emulators: **Hatari** (cycle exact-ish, debugger, YM/STE support), **Steem SSE** (popular, good debugger), **Pasti** (STX). Set machine to ST (not STE) first for tight cycle-exact effects.
- Packers: Atomik/ICE/ DoubleIce, LZ77-based; Ice packer on PRG.
- Music: YM exporter, SNDH players (e.g. "tunes by Mad Max, Jochen Hippel (Mad Max), Big Alec, Count Zero").
- Graphics: GrafX2 (you already have it), NEOchrome (.NEO), DEGAS (.PI1/.PI2/.PI3), Multipaint (already installed) supports ST palettes.
- Existing local tools: `C:\coding\Mad-Assembler` is 6502 only; C:\coding\Cad_3D_Collection_II.MSA is a ST disk image (already present).

## File formats
- DEGAS .PI1: 2-byte res, 32-byte palette, 32000 bytes screen data.
- NEOchrome .NEO: 128 byte header incl palette + 32000 bytes.
- MSA disk image: 10-byte header, track-RLE-compressed; ST: raw sectors.

## Installed toolchain (2026-11-06) — what is actually used
- Compiler: `m68k-atari-mintelf-gcc` 15.2 + binutils 2.45 (tho-otto.de build) in `tools/mintelf/mingw64/bin`. No libc/headers: build with `-nostdlib -nostartfiles -lgcc`, own `string.h` in `src/inc`, entry `_start` in `src/st_crt.s`. Output is a MiNT "ELF-in-PRG".
- Assembler/linker: vasm 2.0e (`vasmm68k_mot -Felf`) and vlink 0.18 built from source into `tools/bin` (vlink needed `#include <io.h>` + `-std=gnu99` on mingw).
- Build: `./build.sh` (Makefile; puts tools on PATH). Output `out/*.prg`.
- Emulator: Hatari 2.6.1 in `C:\coding\emulators\hatari` (EmuTOS `tos.img`). Windows exe discards console output unless run from msys bash as above.
- `run/st.sh <prg> [vbls]` headless run + screenshot (SDL dummy video, debugger breakpoint `b VBL = n :once :file` runs `screenshot`); `run/play.sh <prg>` visible window.
- Gotchas: buffers used with MOVEM/word access must be `aligned(4)` (odd address = address error); `--auto` path must be `C:\NAME.PRG`; screen base must be 256-byte aligned (align at run time).
- Double buffering: the screen base written in the VBL handler may only be on display one frame later (seen in Hatari): never touch the old buffer right after the flip. `StWaitPresented()` waits for the flip plus one more vertical blank; programs do their off-screen work first, then wait, then draw (flight_st.c).
- SNDH music: `music/tune.sndh` is incbin'd (src/st_music.s) and played by `StSndhStart` (src/st_sndh.c + st_sndh_asm.s): INIT with the sub tune in d0, PLAY from our Timer C handler (200 Hz) through an accumulator at the tune's rate (default 50 Hz), EXIT on `StExit`. The tune keeps timers A, B, D for itself. Check a file with `python tools/sndhinfo.py x.sndh`. Verified in Hatari: 86 PLAY calls per 100 VBL at 60 Hz = 50 Hz in emulated time. ICE! packed files need unpacking first; the programs fall back to the demo .ymr stream if the file is not an SNDH.
