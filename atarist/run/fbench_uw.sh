#!/bin/bash
# Benchmark of the underwater version: ./run/fbench_uw.sh <tag>   (the same fixed flight as run/fbench.sh; its checksum differs from the normal one because the picture is rippled)
TAG=${1:?tag}; N=${BENCH_FRAMES:-100}
cd "$(dirname "$0")/.." || exit 1
WL=/c/Users/karol/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
export PATH="$PWD/tools/bin:$PWD/tools/mintelf/mingw64/bin:$WL:$PATH"
./build.sh >/dev/null 2>&1
m68k-atari-mintelf-gcc -m68000 -O2 -nostdlib -nostartfiles -fomit-frame-pointer -fno-tree-loop-distribute-patterns -Wall -DPLOT_PLANAR -DFLIGHT_ASM -DF_ROWREP=1 -DUNDERWATER -DF_BENCH=$N -Isrc -Isrc/inc -c -o objuw/fb.o src/flight_st.c || exit 1
O=objuw
m68k-atari-mintelf-gcc -nostdlib -nostartfiles -Wl,-e,_start,--gc-sections -s -o out/fb_$TAG.prg $O/fb.o $O/flight_core.o $O/flight_data.o $O/st_lib.o $O/st_sys.o $O/st_scroll.o $O/font_antic.o $O/st_logo.o $O/logo_data.o $O/st_mod.o $O/st_sndh.o $O/st_crt.o $O/st_finish_uw.o $O/st_vtab.o $O/flight_line.o $O/flight_edge.o $O/st_music.o $O/st_mus_f.o $O/st_sndh_asm.o -lgcc || exit 1
rm -f run/shots/fb_$TAG.png
timeout 200 run/st.sh fb_$TAG.prg ${BENCH_VBLS:-2200} > /dev/null
python tools/host/benchnum.py run/shots/fb_$TAG.png dark
