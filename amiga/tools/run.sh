#!/bin/bash
# Build an Amiga program, make a bootable ADF and start it in Amiberry (A500, KS 3.1).
#   tools/run.sh [flight320|flight|fselftest] [prof] [gdb]    (fselftest = the assembly self-test, read it with tools/readself.py)    prof = build with stage timing (PROFILE=1), read it with tools/readprof.py (needs gdb)
#   gdb = also start the GDB server on 127.0.0.1:2345 (screenshots via monitor "screenshot")
cd "$(dirname "$0")/.." || exit 1
PROG=flight320
[ "$1" = "flight" ] || [ "$1" = "flight320" ] || [ "$1" = "flight16" ] || [ "$1" = "fselftest" ] && { PROG=$1; shift; }
PROF=""; TAG=""
[ "$1" = "prof" ] && { PROF="PROFILE=1"; TAG="_prof"; shift; }
B=${BARTMAN:-/c/Users/karol/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32}
export PATH="$PATH:$B:$B/opt/bin"
mkdir -p obj/${PROG}${TAG}
gnumake -j4 PROG=$PROG $PROF || exit 1
exe2adf -i out/$PROG$TAG.exe -a out/$PROG$TAG.adf >/dev/null || exit 1
taskkill //IM Amiberry.exe //F >/dev/null 2>&1
EXTRA=""
[ "$1" = "gdb" ] && EXTRA="-s debugging_features=gdbserver -s gdb_port=2345"
OUT=$(cd out && pwd -W 2>/dev/null || pwd)
cd "${AMIBERRY:-/c/coding/emulators/amiberry}" || exit 1
./Amiberry.exe --model A500 -G -r "${KICKSTART:-C:\\coding\\Amiga\\roms\\Kickstart v3.1 r40.063 (1993-07)(Commodore)(A500-A600-A2000)[!].rom}" \
  -0 "$OUT/$PROG$TAG.adf" -s use_gui=no -s mouse_untrap=magic $EXTRA > /dev/null 2>&1 &
