#!/bin/bash
# Flight benchmark: ./run/fbench.sh <tag>  -> builds out/fb_<tag>.prg (100 fixed frames, no music, no flip), runs it in Hatari and prints the VBLs of pass 1 and the checksum of pass 2
# (tag: at most 5 characters, GEMDOS names are 8.3).  The checksum must be the same for every optimisation step.
TAG=${1:?tag}; N=${BENCH_FRAMES:-100}
cd "$(dirname "$0")/.." || exit 1
WL=/c/Users/karol/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
export PATH="$PWD/tools/bin:$PWD/tools/mintelf/mingw64/bin:$WL:$PATH"
./build.sh >/dev/null 2>&1
m68k-atari-mintelf-gcc -m68000 -O2 -nostdlib -nostartfiles -fomit-frame-pointer -fno-tree-loop-distribute-patterns -Wall -DPLOT_PLANAR -DFLIGHT_ASM -DF_ROWREP=1 -DF_BENCH=$N -Isrc -Isrc/inc -c -o obj/fb.o src/flight_st.c || exit 1
m68k-atari-mintelf-gcc -nostdlib -nostartfiles -Wl,-e,_start,--gc-sections -s -o out/fb_$TAG.prg obj/fb.o obj/flight_core.o obj/flight_data.o obj/st_lib.o obj/st_sys.o obj/st_scroll.o obj/font_antic.o obj/st_logo.o obj/logo_data.o obj/st_mod.o obj/st_sndh.o obj/st_crt.o obj/st_finish.o obj/st_vtab.o obj/${LINEOBJ:-flight_line}.o obj/flight_edge.o obj/st_music.o obj/st_mus_f.o obj/st_sndh_asm.o -lgcc || exit 1
rm -f run/shots/fb_$TAG.png
timeout 200 run/st.sh fb_$TAG.prg ${BENCH_VBLS:-2200} > /dev/null
python tools/host/benchnum.py run/shots/fb_$TAG.png dark
