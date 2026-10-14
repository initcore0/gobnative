#!/bin/sh
# gob2native - headless check: Gobliiins through the HD compositor.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_hd_gob1.sh <gob1 gamedir> [build dir]
#
# Exports every picture (gobexport), makes a plain 4x pack of them
# (gobexport --scale-export: nearest neighbour), and runs the game with it
# (gobrun --hd-pack). Each HD frame, reduced to 320x200 by averaging, must
# equal the 320x200 frame exactly, which it only does when every picture got
# the palette the game shows it in and the compositor keeps its layers in
# step: the intro and the title, the first level while a click script solves
# it, INTER, the game over and access code screens, the ending, a level's
# story text, a quick load, and every level's room.

set -eu
game=${1:?usage: $0 <gob1 gamedir> [build dir]}
build=${2:-build}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

"$build/gobexport" "$game" "$dir/export" --quiet >/dev/null 2>&1 || { echo "FAIL: gobexport"; exit 1; }
"$build/gobexport" "$game" --scale-export "$dir/export" "$dir/pack" 4 >/dev/null 2>&1 || { echo "FAIL: --scale-export"; exit 1; }
mkdir -p "$dir/dumps" "$dir/saves"
run() { "$build/gobrun" "$game" --save-dir "$dir/saves" --hd-pack "$dir/pack" --quiet "$@" 2>&1; }
d=$dir/dumps
out=$(
	run --hd-dump-at 20 "$d/intro" --hd-dump-at 60 "$d/doll" --hd-dump-at 140 "$d/title" --frames 8800
	run --start INTER --set-var 0 286 100 --set-var 0 296 10 \
		--click 2 165 182 --rclick 3 150 100 --click 4 180 56 \
		--click 14 165 182 --click 15 165 182 --rclick 16 150 100 --click 17 156 92 \
		--rclick 23 150 100 --rclick 24 150 100 --click 25 93 86 \
		--click 34 165 182 --click 35 165 182 --click 36 84 122 \
		--click 46 165 182 --rclick 47 150 100 --click 48 85 118 \
		--click 56 16 85 --click 62 160 100 --quicksave-at 20 \
		--hd-dump-at 5 "$d/l5" --hd-dump-at 10.5 "$d/l10" --hd-dump-at 17.3 "$d/l17" --hd-dump-at 26 "$d/l26" \
		--hd-dump-at 37 "$d/l37" --hd-dump-at 50 "$d/l50" --hd-dump-at 59 "$d/code" --hd-dump-at 63 "$d/inter" \
		--hd-dump-at 72 "$d/next" --frames 4600
	run --start INTER --set-var 0 286 3 --set-var 0 296 4 --quickload-at 6 --hd-dump-at 8 "$d/loaded" --frames 600
	run --start INTER --set-var 0 286 -1 --hd-dump-at 1.5 "$d/gameover" --key 2 0x1C0D --click 4 130 115 \
		--hd-dump-at 5 "$d/password" --frames 400
	run --start FINAL --hd-dump-at 10 "$d/final10" --hd-dump-at 50 "$d/final50" --frames 3200
	run --level-at 3 5 --hd-dump-at 15 "$d/story" --frames 1000
	for n in $(seq 1 22); do
		run --level-at 3 "$n" --skip-at 12 --hd-dump-at 20 "$d/level$n" --frames 1300
	done
)
dumps=$(echo "$out" | grep -c 'hd dump' || true)
bad=$(echo "$out" | grep 'hd dump' | grep -v 'mean difference 0.000/255, 0 values off' || true)
if [ "$dumps" != 40 ] || [ -n "$bad" ] || echo "$out" | grep -q 'ERROR\|stub'; then
	echo "FAIL: $dumps of 40 dumps"
	echo "$bad"
	echo "$out" | grep 'ERROR\|stub' || true
	exit 1
fi
echo "OK: 40 HD frames equal the 320x200 frames (4x nearest pack from gobexport)"
