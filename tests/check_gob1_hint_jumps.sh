#!/bin/sh
# gob2native - headless check: Gobliiins' hints start again from a level's
# first step when the level does (gobrun --script with --hints).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_gob1_hint_jumps.sh <gob1 gamedir> [build dir]
#
# For each level of tests/gob1: jump to it (the Levels tab's jump), play
# its walkthrough up to its last step (checking the hints on the way, as
# check_gob1_walkthrough.sh does), jump to it again and check that the hint
# is its first step. A level's script doesn't clear every variable it uses
# when it starts again (as after AGAIN), so the hints must not take what the
# run before left behind for this run's progress (engine/hints.h,
# HintTracker).

set -eu
game=${1:?usage: $0 <gob1 gamedir> [build dir]}
build=${2:-build}
here=$(dirname "$0")
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

for f in "$here"/gob1/[0-9][0-9]-AVT*.txt; do
	level=$(basename "$f" | cut -d- -f2)
	awk -v level="$level" '
		{ line[NR] = $0 }
		/^hint / && $0 != "hint end" { if (!first) first = $2; last = NR }
		END {
			print "level " level
			for (i = 1; i < last; i++)
				if (line[i] !~ /^#/)
					print line[i]
			print "level " level
			print "room " level
			print "hint " first
		}' "$f" >>"$dir/script.txt"
done
echo end >>"$dir/script.txt"
"$build/gobrun" "$game" --save-dir "$dir/saves" --hints "$here/../assets/hints/gob1.txt" \
	--script "$dir/script.txt" --frames 3000000 >"$dir/out.txt" 2>&1 ||
	{ grep -- '--' "$dir/out.txt" | tail -3; echo "FAIL: the script didn't finish"; exit 1; }
if grep -q 'FAILED' "$dir/out.txt"; then
	grep 'FAILED' "$dir/out.txt" | head -3
	echo "FAIL: a hint was wrong"
	exit 1
fi
echo "OK: the hints start again with each level, in every level"
