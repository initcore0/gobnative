#!/bin/sh
# gob2native - headless check: Goblins Quest 3 through the HD compositor.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_hd_gob3.sh <gob3 gamedir> [build dir]
#
# Exports every picture (gobexport), makes a plain 4x pack of them
# (gobexport --scale-export: nearest neighbour), and runs the game with it
# (gobrun --hd-pack). Each HD frame, reduced to 320x200 by averaging, must
# equal the 320x200 frame exactly: the intro video and the title, the first
# room (472 pixels wide) before and after it scrolls to its right end, a
# talking head over it, the icon bar, an intermission with its mult videos,
# and the rooms of every world reached with the level select (EMAP1012 is
# 320x240).

set -eu
game=${1:?usage: $0 <gob3 gamedir> [build dir]}
build=${2:-build}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

"$build/gobexport" "$game" "$dir/export" --quiet >/dev/null 2>&1 || { echo "FAIL: gobexport"; exit 1; }
"$build/gobexport" "$game" --scale-export "$dir/export" "$dir/pack" 4 >/dev/null 2>&1 || { echo "FAIL: --scale-export"; exit 1; }
mkdir -p "$dir/dumps" "$dir/saves"
run() { "$build/gobrun" "$game" --save-dir "$dir/saves" --hd-pack "$dir/pack" --quiet --press-every 5 "$@" 2>&1; }
d=$dir/dumps
out=$(
	run --press-stop 60 --hd-dump-at 20 "$d/intro" --hd-dump-at 70 "$d/title" --hd-dump-at 88 "$d/room" \
		--move 90 319 100 --hd-dump-at 93 "$d/scrolled" --move 95 160 0 --hd-dump-at 97 "$d/iconbar" --frames 6200
	run --press-stop 60 --set-var 90 60 1002 --hd-dump-at 92 "$d/talk" --frames 5800
	run --press-stop 70 --set-str 78.2 580 INTER3 --hd-dump-at 80 "$d/inter3" --hd-dump-at 130 "$d/comic" --frames 8200
	for r in EMAP1005 EMAP1010 EMAP1012 EMAP1015 EMAP1017 EMAP1019; do
		run --level-at 3 $r --hd-dump-at 50 "$d/$r" --frames 3200
	done
)
dumps=$(echo "$out" | grep -c 'hd dump' || true)
bad=$(echo "$out" | grep 'hd dump' | grep -v 'mean difference 0.000/255, 0 values off' || true)
if [ "$dumps" != 14 ] || [ -n "$bad" ] || echo "$out" | grep -q 'ERROR\|stub'; then
	echo "FAIL: $dumps of 14 dumps"
	echo "$bad"
	echo "$out" | grep 'ERROR\|stub' || true
	exit 1
fi
echo "OK: 14 HD frames equal the 320x200 frames (4x nearest pack from gobexport), scrolled rooms included"
