#!/bin/bash
# build the Atari ST programs: ./build.sh [make args]
cd "$(dirname "$0")" || exit 1
WL=${WINLIBS:-/c/Users/karol/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin}      # WinLibs mingw (mingw32-make); override with WINLIBS=
export PATH="$PWD/tools/bin:$PWD/tools/mintelf/mingw64/bin:$WL:$PATH"
mkdir -p obj objz objuw out
mingw32-make "$@"
