#!/bin/bash
# Start a program in a visible Hatari window (1040ST, 1 MB, EmuTOS).  usage: run/play.sh flight.prg    (cursor keys = joystick, Esc / Space quits the program)
PRG=${1:?prg in out/}; A=${ST_ROOT:-$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)}; H=${HATARI:-/c/coding/emulators/hatari}
UP=${PRG^^}
cd $H && ./hatari.exe --machine ${MACHINE:-st} --tos ./tos.img --memsize ${MEMSIZE:-$([ "${MACHINE:-st}" = falcon ] && echo 14336 || echo 1024)} --fast-boot true --harddrive $A/out --gemdos-drive C --auto 'C:\'"$UP" > /dev/null 2>&1 &
