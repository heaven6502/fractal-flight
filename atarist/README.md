# Atari ST: the Rescue on Fractalus style flight (1040ST, 68000, no blitter)

Port of the Amiga flight (`../amiga`, verified portable core `flight_core.c`) to the plain Atari ST: 8 MHz 68000, 1 MB, low resolution, CPU instead of blitter/copper.

| Program | Picture | Notes |
|---|---|---|
| `flight` | 160 x 80 pixels shown 2 x 2, 150 lines | ridge pixels into a work plane, CPU drip = ground (`st_finish.s`) |

Double buffered, joystick (IKBD) or cursor keys, autopilot until the first input, Esc / Space quit, YM2149 music (SNDH tune from `music/tune.sndh`, keys 1-9/0 pick the sub tune; fallback = demo `.ymr` stream).

## Build and run (Windows, bash)
    ./build.sh FPS=0                 the final programs without the picture rate number (default FPS=1 draws it: pictures per second x 10, top bar / top left corner)
    ./build.sh                       needs tools/mintelf (m68k-atari-mintelf-gcc), tools/bin (vasmm68k_mot, vlink): see docs/07-tos-software-tools.md
    run/st.sh flight.prg 400         headless Hatari run + screenshot in run/shots (Hatari in C:\coding\emulators\hatari)
    run/play.sh f320lz.prg          visible Hatari window

## Music
- ST: the SNDH tune `music/flight.sndh` (Pajaro by Scavenger) from the SNDH player (`st_sndh*`), YM2149.
- STE (the `_MCH` cookie, `StIsSTE()` in `st_sys.c`): the MOD `music/mod/DESERTD2.MOD` (Desert Dream, Kefrens) through the STE DMA sound at 12.5 kHz: `src/st_hackl.s` is the replay core of Hacking Lance v13 (Paulo Simoes 2013, after Lance / Marten Ronge 1993; "use it if you want"), made by `tools/mkhackl.py` from `music/mod/HACKL00I.s`; glue `ModInit / ModVbl / ModStop`, switch in `src/st_mod.c`.  It needs a 420 KB arena (BSS) for the module copy, the generated mixer code and the tables.
- `music/mod/` and `music/sndh` are included.  Tried and dropped: `Virgill-atariklang.mod` with the module in place and a 280 KB arena crashed (double bus error), not understood.


No program seeds its random numbers from the clock, so every launch flies / shows exactly the same.  That is deliberate: the benchmarks (`F_BENCH`), the screenshot comparisons and the equivalence tests need the same flight and terrain each time, and the demo gets the same opening picture and the same music sync on every run.

| Where | Generator | Start value |
|---|---|---|
| flight terrain (`InitGridMap`), `flight_st*.c` | `FSeed(20261106)` in `main` | fixed: the same landscape on every launch |
| flight wind (`FWind`, `flight_core.c`) | 16 bit xorshift `wSeed` | `0xACE1` |
| flight autopilot (`FAutopilot`) | no random numbers | deterministic |

The ST has no battery clock to read at start.  If a different landscape per launch is wanted: seed from the vertical blank counter (`stFrame`) or the 200 Hz system timer at the moment the first key / fire press ends the black start screen, and keep the fixed seed for the benchmark and the demo mode.

`docs/` has the Atari ST hardware reference notes; `tools/` the YM / SNDH helpers and host test aids.
The game derived tables (`src/*_data.c`) come from the private Amiga repository: keep this repository private.
