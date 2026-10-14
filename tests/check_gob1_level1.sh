#!/bin/sh
# gob2native - headless check: a click script solves Gobliiins' first level
# (AVT003) and the game goes on through INTER to the next one (AVT00).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_gob1_level1.sh <gob1 gamedir> [build dir]
#
# The run starts INTER as the intro leaves it (level 100 = AVT003, full
# energy). Times are simulated seconds; the runner is deterministic, so the
# goblins are where the script expects them. The walkthrough:
#   2  select the fighter (portrait)      3  right click: "act" cursor
#   4  kick the gate: the horn falls      14, 15  select the picker
#   16 right click: "pick" cursor         17 pick up the horn
#   23, 24 right clicks: "act" cursor     25 blow the horn: a branch falls
#   34, 35 select the wizard              36 cast on the branch: a pickaxe
#   46 select the picker                  47 right click: "pick" cursor
#   48 swap the horn for the pickaxe      56 click the exit sign ("GO")
#   62 click past the level code

set -eu
game=${1:?usage: $0 <gob1 gamedir> [build dir]}
build=${2:-build}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

out=$("$build/gobrun" "$game" --start INTER --set-var 0 286 100 --set-var 0 296 10 --save-dir "$dir" \
	--click 2 165 182 --rclick 3 150 100 --click 4 180 56 \
	--click 14 165 182 --click 15 165 182 --rclick 16 150 100 --click 17 156 92 \
	--rclick 23 150 100 --rclick 24 150 100 --click 25 93 86 \
	--click 34 165 182 --click 35 165 182 --click 36 84 122 \
	--click 46 165 182 --rclick 47 150 100 --click 48 85 118 \
	--click 56 16 85 --click 62 160 100 --frames 4500 --print-objects 2>&1)

echo "$out" | grep -q '\[AVT003\] loadObjects AVT003.AVO' || { echo "FAIL: AVT003 did not start"; exit 1; }
if echo "$out" | grep -q 'stub'; then
	echo "FAIL: stubs reached:"
	echo "$out" | grep stub | sort | uniq -c
	exit 1
fi
echo "$out" | grep -q '\[AVT003\] loadTot INTER' || {
	echo "FAIL: level 1 not solved"
	echo "$out" | grep -E '^(gob|item|pocket)'
	exit 1
}
echo "$out" | grep -q '\[INTER\] loadTot AVT00$' || { echo "FAIL: INTER did not start AVT00"; exit 1; }
echo "$out" | grep -q '\[AVT00\] loadObjects AVT00.AVO' || { echo "FAIL: AVT00 did not load its objects"; exit 1; }
echo "OK: level 1 solved, AVT00 started"
