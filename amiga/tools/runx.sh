#!/bin/bash
# Build an Amiga program and start the .exe straight from a folder that Amiberry mounts as the boot drive DH0: (no ADF step, quicker for debug cycles; tools/run.sh keeps the ADF route).
#   tools/runx.sh [flight320|flight|...] [make variables, e.g. BUNK=1 PIPE=0]   builds out/<prog><tag>.exe (tag from the variables, as in the Makefile), copies it to out/hd/demo
#   and starts it as an A500 (KS 3.1; MODEL=A600 for an A600).  out/hd/S/startup-sequence is just "demo".  Kills a running Amiberry first.
cd "$(dirname "$0")/.." || exit 1
PROG=flight320
case "$1" in flight|flight320|flight16|fselftest) PROG=$1; shift;; esac
B=${BARTMAN:-/c/Users/karol/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32}
export PATH="$PATH:$B:$B/opt/bin"
TAG=""
for v in "$@"; do case "$v" in BUNK=*) TAG="${TAG}_bk";; LASER=*) TAG="${TAG}_laser";; PROFILE=*) TAG="${TAG}_prof";; PUAE=*) TAG="${TAG}_puae";; esac; done
mkdir -p "obj/$PROG$TAG"
gnumake -j4 PROG=$PROG "$@" || exit 1
EXE="out/$PROG$TAG.exe"
[ -f "$EXE" ] || { echo "no $EXE (the tag of these variables is not known to this script)"; exit 1; }
mkdir -p out/hd/S
cp -f "$EXE" out/hd/demo
printf 'demo\n' > out/hd/S/startup-sequence
taskkill //IM Amiberry.exe //F >/dev/null 2>&1
HD=$(cd out/hd && pwd -W 2>/dev/null || pwd)
cd "${AMIBERRY:-/c/coding/emulators/amiberry}" || exit 1
./Amiberry.exe --model ${MODEL:-A500} -G -r "${KICKSTART:-C:\\coding\\Amiga\\roms\\Kickstart v3.1 r40.063 (1993-07)(Commodore)(A500-A600-A2000)[!].rom}" \
  -s "filesystem2=rw,DH0:DH0:$HD,0" -s use_gui=no -s mouse_untrap=magic > /dev/null 2>&1 &
