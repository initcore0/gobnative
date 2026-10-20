#!/bin/sh
# gob2native - headless check: click scripts play Gobliiins from the intro,
# level by level (tests/gob1/NN-TOT-name.txt, gobrun --script).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_gob1_walkthrough.sh <gob1 gamedir> [build dir] [last file number]
#
# The level files are played in order as one script from the start of the
# game: the intro leads to the first level, and each solved level shows the
# access code of the next one and goes on through INTER, as a player sees it
# (no level select, no access codes typed). Each file begins with `room TOT`
# and ends with `exit`, which clicks the exit sign and traces the code shown.
# A run passes when every level is reached in turn and solved with full
# energy, each code is the one the overlay's Levels tab lists for the next
# level (gobrun --list-levels), the script ends without a failed step and no
# stub or error shows up. The last file watches FINAL to the end of the game.
# The `hint ID` lines check the hints (assets/hints/gob1.txt) along the way:
# before each step's actions, the step the hints give is that one.

set -eu
game=${1:?usage: $0 <gob1 gamedir> [build dir] [last file number]}
build=${2:-build}
last=${3:-99}
here=$(dirname "$0")
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

"$build/gobrun" "$game" --list-levels >"$dir/levels.txt" 2>/dev/null
levels=""
for f in "$here"/gob1/[0-9][0-9]-*.txt; do
	n=$(basename "$f" | cut -c1-2)
	[ "$n" -le "$last" ] || break
	cat "$f" >>"$dir/script.txt"
	levels="$levels $(basename "$f" | cut -d- -f2)"
done
printf 'wait 30\nend\n' >>"$dir/script.txt"
"$build/gobrun" "$game" --save-dir "$dir/saves" --hints "$here/../assets/hints/gob1.txt" \
	--script "$dir/script.txt" --frames 2000000 >"$dir/out.txt" 2>&1 ||
	{ grep -- '--' "$dir/out.txt" | tail -3; echo "FAIL: the script didn't finish"; exit 1; }
if grep -q 'stub\|ERROR' "$dir/out.txt"; then
	grep 'stub\|ERROR' "$dir/out.txt" | head -3
	echo "FAIL: stubs or errors"
	exit 1
fi
started=$(sed -n 's/.*\] loadTot \([A-Za-z0-9]*\)$/\1/p' "$dir/out.txt" | tr 'a-z' 'A-Z' | grep -v '^INTER$' | tr '\n' ' ')
n=0
for t in $levels; do
	n=$((n + 1))
	if [ "$t" = FINAL ]; then
		case " $started " in
		*" FINAL INTRO "*) ;;
		*) echo "FAIL: the ending didn't play to the start of the game again"; exit 1 ;;
		esac
		echo "OK: Gobliiins played from the intro through the ending"
		exit 0
	fi
	started=" $started "
	case "$started" in
	*" $t "*) started=${started#*" $t "} ;;
	*) echo "FAIL: level $n ($t) not reached in turn"; exit 1 ;;
	esac
	solved=$(sed -n "s/.*($t solved, \(code \)*\([a-zA-Z]*\), energy \([0-9]*\)).*/\2 \3/p" "$dir/out.txt" | head -1)
	expected=$(awk -v n=$((n + 1)) '$2 == n { print $4 }' "$dir/levels.txt")
	[ -n "$expected" ] || expected=ending # the last level leads to the ending
	[ -n "$solved" ] || { echo "FAIL: level $n ($t) not solved"; exit 1; }
	if [ "$solved" != "$expected 10" ]; then
		echo "FAIL: level $n ($t): code and energy $solved (expected $expected 10)"
		exit 1
	fi
	echo "level $n ($t) solved: ${solved% *}"
done
echo "OK: Gobliiins played from the intro through level $n"
