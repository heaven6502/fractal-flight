#!/bin/bash
# Atari ST flight in Hatari and the Amiga flight in Amiberry side by side: ./run/sidebyside.sh [delay]    (window positions: HX HY / AX AY, default Hatari 10,40 and Amiberry 860,40; SDL_VIDEO_WINDOW_POS)
#   Amiberry is started first (silent, floppy at turbo speed, so the program loads at once), Hatari <delay> seconds later (default 3): Amiberry boots the Kickstart and reads the program from the
#   disk image, which takes longer than Hatari's GEMDOS folder; tune <delay> until both logos start together.  Both show the logo for 380 vertical blanks before the flight.
DELAY=${1:-3}
B=/c/Users/karol/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32
export PATH="$PATH:$B:$B/opt/bin"
taskkill //F //IM Amiberry.exe //IM hatari.exe >/dev/null 2>&1; sleep 1
cd /c/coding/Amiga/demo && exe2adf -i out/flight.exe -a out/flight.adf >/dev/null
cd /c/coding/emulators/amiberry && (SDL_VIDEO_WINDOW_POS=${AX:-860},${AY:-40} ./Amiberry.exe --model A500 -G -r "C:\coding\Amiga\roms\Kickstart v3.1 r40.063 (1993-07)(Commodore)(A500-A600-A2000)[!].rom" -0 "C:\coding\Amiga\demo\out\flight.adf" -s use_gui=no -s mouse_untrap=magic -s sound_output=none -s floppy_speed=0 > /dev/null 2>&1 &)
sleep $DELAY
cd "$(dirname "$0")/.." && SDL_VIDEO_WINDOW_POS=${HX:-10},${HY:-40} run/play.sh flight.prg
