# Amiga: the Rescue on Fractalus style flight (A500 / OCS)

C port of the terrain flight of *Behind Jaggi Lines!* with 68000 assembly hot loops, planar bitplane plotting and blitter work.

| Program | What |
|---|---|
| `flight320` | 320 x 200 flight, FAR fan geometry, 3 bitplane depth bands, bunker houses (`BUNK=1`), enemy lasers (`LASER=1`), procedural planets / palettes, hardware scroller, P61 music |
| `flight`, `flight16` | the original 160 x 80 pixel versions (`flight16` = 16 bit fractal line) |
| `fselftest` | on-Amiga self-test: asm vs C vs reference |

## Build and run (Windows)
Needs the VS Code extension *BartmanAbyss.amiga-debug* (GCC 15, vasm, elf2hunk, exe2adf), Amiberry and a Kickstart ROM. Paths are overridable: `BARTMAN`, `AMIBERRY`, `KICKSTART`.

    tools/runx.sh flight320 LASER=1         build + start in Amiberry (MODEL=A600 for an A600)
    tools/run.sh flight320 | flight         build, make a bootable ADF, start Amiberry (A500, KS 3.1)
    tools/run.sh <prog> prof gdb            profiling build (stage timing) with the GDB server on port 2345
    python tools/readprof.py <prog>         stage times of a running profiling build
    tools/run.sh fselftest gdb              then python tools/readself.py flight

## Tests on the PC (gcc)
    gcc -O2 -Isrc [-DPLOT_PLANAR] [-DFLIGHT_WIDE] src/flight_core.c src/flight_data.c test/test_far_host.c
    gcc -O2 -DFLIGHT_LEGACY -Isrc src/flight_core.c src/flight_data.c test/test_flight_host.c      (original geometry + flight model)

## Layout
`src/` core (`flight_core`), Amiga front ends (`flight320_main.c`, `flight_main.c`), shared system code (`amiga_sys`, `blitq`), assembly (`flight_line*.asm`, `flight_edge.asm`), generated tables (`flight_data.c`), procedural planets / palettes (`planetgen`, `palgen`).
`tools/` generators for tables and reference vectors, profiling and self-test readers. `testdata/` reference data taken from the Atari game.

Game-derived data (tables, test vectors) is included: keep this repository private.
