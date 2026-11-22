#!/bin/bash
# links an unstripped copy of out/flight320.prg and profiles 600 VBLs of flight into run/f3prof.txt (map: run/f3prof.map)
cd "$(dirname "$0")/.." || exit 1
WL=/c/Users/karol/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin
export PATH="$PWD/tools/bin:$PWD/tools/mintelf/mingw64/bin:$WL:$PATH"
m68k-atari-mintelf-gcc -nostdlib -nostartfiles -Wl,-e,_start,--gc-sections,-Map=run/f3prof.map -o out/f3prof.prg obj/flight320_st.o obj/flight_core_f3.o obj/flight_data.o obj/st_lib.o obj/st_sys.o obj/st_scroll.o obj/font_antic.o obj/st_logo.o obj/logo_data.o obj/st_mod.o obj/st_sndh.o obj/st_descent.o obj/st_crt.o obj/st_f320.o obj/st_vtab.o obj/flight_line.o obj/flight_edge.o obj/st_music.o obj/st_mus_f.o obj/st_sndh_asm.o obj/flight_line320_st.o -lgcc || exit 1
A=$(pwd -W 2>/dev/null || pwd)
printf 'profile on\nc\n' > run/f3_on.ini
printf 'profile off\nprofile cycles 14000\nc\n' > run/f3_off.ini
printf 'b VBL = 1800 :once :quiet :file %s/run/f3_on.ini\nb VBL = 2400 :once :quiet :file %s/run/f3_off.ini\n' "$A" "$A" > run/f3_boot.ini
cd /c/coding/emulators/hatari
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
timeout 280 ./hatari.exe --machine st --tos ./tos.img --memsize 1024 --fast-boot true --sound off --frameskips 0 --statusbar false --harddrive $A/out --gemdos-drive C --auto 'C:\F3PROF.PRG' --parse $A/run/f3_boot.ini --run-vbls 2410 > $A/run/f3prof.txt 2>&1
