#!/bin/bash
# benchmarks the flight optimisation steps one after the other (results in run/fsteps.txt)
cd "$(dirname "$0")/.." || exit 1
S=/c/Users/karol/AppData/Local/Temp/claude/97ae990a01470723/c1df81b0-6f9f-4756-8ca3-f875b5afa8a2/scratchpad
while tasklist 2>/dev/null | grep -qi hatari; do sleep 5; done
: > run/fsteps.txt
cp $S/flight_line_new.asm src/flight_line.asm
echo "A inlined plot      : $(./run/fbench.sh fa 2>&1 | tail -1)" >> run/fsteps.txt
cp $S/st_finish_new.s src/st_finish.s
echo "B + 2 column finish : $(./run/fbench.sh fb 2>&1 | tail -1)" >> run/fsteps.txt
echo done >> run/fsteps.txt
