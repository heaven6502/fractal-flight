#!/bin/bash
# Hatari profile of the flight benchmark (100 fixed frames, no music, no flip): ./run/fprof.sh  -> run/fprof.txt (per symbol cycles) and run/fprof.map (link map)
# the profile window is the VBLs PROF_FROM .. PROF_TO of the first benchmark pass (default 300 .. 700)
cd "$(dirname "$0")/.." || exit 1
WL=/c/Users/karol/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
export PATH="$PWD/tools/bin:$PWD/tools/mintelf/mingw64/bin:$WL:$PATH"
./build.sh >/dev/null 2>&1
m68k-atari-mintelf-gcc -m68000 -O2 -nostdlib -nostartfiles -fomit-frame-pointer -fno-tree-loop-distribute-patterns -DPLOT_PLANAR -DFLIGHT_ASM -DF_ROWREP=1 -DF_BENCH=${BENCH_FRAMES:-100} -Isrc -Isrc/inc -c -o obj/fp.o src/flight_st.c || exit 1
m68k-atari-mintelf-gcc -nostdlib -nostartfiles -Wl,-e,_start,--gc-sections,-Map=run/fprof.map -o out/fp_prof.prg obj/fp.o obj/flight_core.o obj/flight_data.o obj/st_lib.o obj/st_sys.o obj/st_scroll.o obj/font_antic.o obj/st_logo.o obj/logo_data.o obj/st_mod.o obj/st_sndh.o obj/st_crt.o obj/st_finish.o obj/st_vtab.o obj/flight_line.o obj/flight_edge.o obj/st_music.o obj/st_mus_f.o obj/st_sndh_asm.o -lgcc || exit 1
A=$(pwd -W 2>/dev/null || pwd); H=${HATARI:-/c/coding/emulators/hatari}
FROM=${PROF_FROM:-300}; TO=${PROF_TO:-700}
printf 'b VBL = %s :once :quiet :file %s/run/fprof_on.ini\nb VBL = %s :once :quiet :file %s/run/fprof_off.ini\n' "$FROM" "$A" "$TO" "$A" > run/fprof_boot.ini
printf 'profile on\nc\n' > run/fprof_on.ini
printf 'profile off\nprofile cycles 2300\nc\n' > run/fprof_off.ini
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
cd $H && timeout 280 ./hatari.exe --machine st --tos ./tos.img --memsize 1024 --fast-boot true --sound off --frameskips 0 --statusbar false --harddrive $A/out --gemdos-drive C --auto 'C:\FP_PROF.PRG' --parse $A/run/fprof_boot.ini --run-vbls $(( TO + 10 )) > $A/run/fprof3.txt 2>&1
echo done > $A/run/fprof.done
