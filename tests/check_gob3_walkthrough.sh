#!/bin/sh
# gob2native - headless check: click scripts play Goblins Quest 3 from the
# intro, room by room (tests/gob3/NN-ROOM-name.txt, gobrun --script).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_gob3_walkthrough.sh <gob3 gamedir> [build dir] [last file number]
#
# The room files are played in order as one script from the start of the
# game (each begins with `room ROOM`, which waits for the room; the last one
# watches FINAL to the end of the game). A run passes when every room is
# reached in turn, the last one is left for the next one (or the game ends),
# the script ends without a failed step and no stub or error shows up.

set -eu
game=${1:?usage: $0 <gob3 gamedir> [build dir] [last file number]}
build=${2:-build}
last=${3:-99}
here=$(dirname "$0")
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

rooms=""
for f in "$here"/gob3/[0-9][0-9]-*.txt; do
	n=$(basename "$f" | cut -c1-2)
	[ "$n" -le "$last" ] || break
	cat "$f" >>"$dir/script.txt"
	rooms="$rooms $(basename "$f" | cut -d- -f2)"
done
printf 'wait 60\nend\n' >>"$dir/script.txt"
"$build/gobrun" "$game" --press-every 5 --press-stop 60 --save-dir "$dir/saves" --script "$dir/script.txt" \
	--frames 1000000 >"$dir/out.txt" 2>&1 || { grep -- '--' "$dir/out.txt" | tail -3; echo "FAIL: the script didn't finish"; exit 1; }
if grep -q 'stub\|ERROR' "$dir/out.txt"; then
	grep 'stub\|ERROR' "$dir/out.txt" | head -3
	echo "FAIL: stubs or errors"
	exit 1
fi
started=$(sed -n 's/.*\[MENU\] totSub \([A-Za-z0-9]*\) flags.*/\1/p' "$dir/out.txt" | tr 'a-z' 'A-Z' | tr '\n' ' ')
for r in $rooms; do
	started=" $started "
	case "$started" in
	*" $r "*) started=${started#*" $r "} ;;
	*) echo "FAIL: $r not reached in turn"; exit 1 ;;
	esac
	echo "reached $r"
	lastroom=$r
done
grep -q "\[MENU\] returned from totSub $lastroom" "$dir/out.txt" || { echo "FAIL: $lastroom not solved"; exit 1; }
if [ "$lastroom" = FINAL ]; then
	grep -q "\[MENU\] game ended" "$dir/out.txt" || { echo "FAIL: the ending didn't end the game"; exit 1; }
	echo "OK: Goblins 3 played from the intro through the ending"
	exit 0
fi
echo "OK: Goblins 3 played from the intro through $lastroom, which leads on to $(echo "$started" | cut -d' ' -f1)"
