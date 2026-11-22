# Fractal Flight

A Rescue-on-Fractalus style terrain flight for two 16 bit machines, plus the research and prototypes it grew out of.
Idea by Heaven, title screen by Lowlife, vibe coded with Claude Code on top of decades of notes about the original Atari 8 bit game.

| Folder | What |
|---|---|
| `amiga/` | **Amiga (OCS, plain A500)**: `flight320` - 320x200 flight, blitter ground drip, copper colour fades, hardware sprite sun and stars, bunker houses, enemy lasers, procedural planets and palettes, hardware scroller below the picture, P61 music |
| `atarist/` | **Atari ST (plain 1040 ST, no blitter)**: `flight320_st` (`flight320.prg`, `f320bk.prg`, `f320lz` = houses + lasers), the same picture on the 68000 alone; SNDH music; hardware reference notes in `atarist/docs/` |
| `prototype/` | PureBasic prototypes and the analysis of the original game: `jaggie.md` (analysis of the core routines of *Behind Jaggi Lines!*; the disassembly itself is not included), `jaggie_3d.pb` (3D flight prototype with the gun emplacements / green lasers), 16 bit and table variants, descent prototype |
| `docs/` | notes, screenshots / GIFs of both versions |

Both programs share the same C core (`flight_core.c`, `planetgen.c`, `palgen.c`, `font_antic.c`, `scrolltext.inc` with a per-machine text) - verified against the original game.

## Ready-made binaries (`release/`)
| File | What |
|---|---|
| `release/amiga/flight320_laser.exe` / `.adf` | **the full Amiga demo**: houses + lasers (the ADF boots on an A500 / A600, KS 1.3+) |
| `release/amiga/flight320_bk.*` | with houses, no lasers |
| `release/amiga/flight320.*` | the plain flight |
| `release/atarist/f320lz.prg` | **the full ST demo**: houses + lasers (plain 1 MB ST, low resolution) |
| `release/atarist/f320bk.prg` | with houses, no lasers |
| `release/atarist/flight320.prg` | the plain flight |

## Amiga
Needs the VS Code extension *BartmanAbyss.amiga-debug* (GCC, vasm, elf2hunk, exe2adf), Amiberry and a Kickstart ROM. Paths are overridable: `BARTMAN`, `AMIBERRY`, `KICKSTART`.

    cd amiga
    tools/runx.sh flight320 BUNK=1          houses (4th plane)               -> out/flight320_bk.exe
    tools/runx.sh flight320 LASER=1         houses + lasers, the full demo   -> out/flight320_laser.exe
    MODEL=A600 tools/runx.sh flight320 LASER=1
    exe2adf -i out/flight320_laser.exe -a out/flight320_laser.adf            bootable disk

Music: `src/flight320_main.c` includes `music/treasure_island.p61` (a ProTracker module packed with P61con, included). Convert another MOD with `tools/modpack` (`modpack -in:mod x.mod -out:p61a music/treasure_island.p61`) or point the `INCBIN_CHIP(module, ...)` line at another `.p61` (`music/testmod.p61` is included). Scroll text: `src/scrolltext.inc`; readable copy: `amiga/amiga_readme.txt`.

## Atari ST
The cross toolchain is included (`atarist/tools/mintelf` = GCC 15 for the ST, `tools/bin` = vasm, vlink, upx; how it was installed: `atarist/docs/07-tos-software-tools.md`). You still need WinLibs `mingw32-make` (`WINLIBS` env var) and, to run, Hatari + EmuTOS (`HATARI` env var).

    cd atarist
    ./build.sh out/flight320.prg            plain flight320
    ./build.sh out/f320lz.prg               houses + lasers, the full demo
    run/play.sh f320lz.prg                  visible Hatari window
    run/st.sh f320lz.prg 600 run/shots/a.png   headless run + screenshot

Music: SNDH tune `music/flight.sndh` (Pajaro by Scavenger) is included (`src/st_mus_f.s` includes it); the STE MOD tune and the Hacking Lance replay sources are in `music/mod/`. Scroll text: `src/scrolltext.inc`; readable copy `atarist/st_readme.txt`.

## Prototype / original game
`prototype/jaggie_3d.pb` (PureBasic 6) needs `jaggie_flight.pbi` and `jaggie_data.pbi` next to it. Compile: `pbcompiler jaggie_3d.pb /EXE jaggie_3d.exe`; `--guns --fire` shows the enemy guns and laser beams. `jaggie.md` explains the original renderer (heightmap rings, fan rows, laser drawing in `LAA2F`).

## Terminology
*rings* = 3D distance shells, *colour bands* = bitplane colour groups, *rows* = fan z steps.

## Legal / status
The game-derived data (tables, test vectors, the disassembly in `prototype/`) comes from *Rescue on Fractalus! / Behind Jaggi Lines!* (Lucasfilm Games 1984): **keep this repository private.** The third-party music is included for building (Treasure Island by Maze, Pajaro by Scavenger, Desert Dream by Kefrens, P61 player by Jarno Paananen, Hacking Lance replay) and belongs to its authors; do not make the repository public without clearing that.
