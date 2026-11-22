#!/bin/bash
# Headless Hatari runner.  usage: run/st.sh <prg in out/> [vbls=250] [shot.png]    env: MACHINE=st|ste  CONOUT=1  EXTRA="more hatari options"
# Mounts <repo>/atarist/out as GEMDOS drive C:, autostarts the PRG, saves a screenshot 10 VBLs before the end, exits after <vbls> VBLs.
PRG=${1:?prg name in out/}; VBLS=${2:-250}; SHOT=${3:-run/shots/${PRG%.*}.png}
A=${ST_ROOT:-$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)}; H=${HATARI:-/c/coding/emulators/hatari}
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
mkdir -p $A/run/shots
SHOTABS=$A/$SHOT; [[ $SHOT == *:* || $SHOT == /* ]] && SHOTABS=$SHOT
UP=${PRG^^}
AUTOPATH='C:\'"$UP"
printf 'screenshot %s\nc\n' "$SHOTABS" > $A/run/shot.ini
printf 'b VBL = %s :once :quiet :file %s/run/shot.ini\n' "$((VBLS-10))" "$A" > $A/run/boot.ini
cd $H && timeout 120 ./hatari.exe --machine ${MACHINE:-st} --tos ./tos.img --memsize ${MEMSIZE:-$([ "${MACHINE:-st}" = falcon ] && echo 14336 || echo 1024)} --fast-boot true --sound off --frameskips 0 --statusbar false \
  ${CONOUT:+--conout 2} --harddrive $A/out --gemdos-drive C --auto "$AUTOPATH" --parse $A/run/boot.ini --run-vbls $VBLS \
  ${EXTRA} 2>&1 | grep -v -E "Bus Error|^INFO|debug mode|symbol|section|Offsetting|Skipping|Loaded|CPU=|^[0-9a-f]{8} |^$|No GEMDOS dir|ELF header"
