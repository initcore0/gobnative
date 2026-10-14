#!/bin/sh
# gob2native - headless check: Goblins 3 saves and loads through its own menu.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_saves_gob3.sh <gob3 gamedir> [build dir]
#
# 1. From the first room (EMAP1000, after the intro), the menu (pointer at the
#    top edge, the disk icon, SAVE, the first line, Return for the proposed
#    name BOAT, I CONFIRM) saves slot 0: gob3-slot00.sav holds the variables
#    and the 118x160 screenshot.
# 2. A new run (a restart) shows BOAT and its screenshot in the LOAD list
#    (the screen differs from the same list with no saves).
# 3. Loading it (the line, I CONFIRM) restores every variable exactly as
#    saved, and the room starts again with Blount where he stood.

set -eu
game=${1:?usage: $0 <gob3 gamedir> [build dir]}
build=${2:-build}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
run() { "$build/gobrun" "$game" --press-every 5 --press-stop 60 "$@" 2>&1; }
menu="--move 90 160 0 --click 92 28 8"

out=$(run --save-dir "$dir/saves" $menu --click 94 50 13 --click 96 85 45 --key 98 0x1C0D --click 100 128 96 \
	--state-on-trace "saved slot" "$dir/saved" --print-objects --frames 6600)
echo "$out" | grep -q '\[MENU\] saved slot 0' && [ -s "$dir/saves/gob3-slot00.sav" ] || { echo "FAIL: save: $out"; exit 1; }
size=$(wc -c <"$dir/saves/gob3-slot00.sav")
[ "$size" -eq $((52 + 31664 + 9 + 768 + 118 * 160)) ] || { echo "FAIL: slot file of $size bytes (no screenshot?)"; exit 1; }
echo "$out" | grep -q '^obj  0 .* cell 6,21 ' || { echo "FAIL: Blount isn't at 6,21 after the save"; exit 1; }

mkdir -p "$dir/shots" "$dir/none" "$dir/empty"
run --save-dir "$dir/saves" $menu --click 94 50 25 --move 95 85 45 --snap-every 96 "$dir/shots" --frames 6000 >/dev/null
run --save-dir "$dir/empty" $menu --click 94 50 25 --move 95 85 45 --snap-every 96 "$dir/none" --frames 6000 >/dev/null
cmp -s "$dir/shots/frame_000_0096.00s.bmp" "$dir/none/frame_000_0096.00s.bmp" &&
	{ echo "FAIL: the load list looks the same without saves"; exit 1; }

out=$(run --save-dir "$dir/saves" $menu --click 94 50 25 --click 95 85 45 --click 97 55 96 \
	--state-on-trace "loaded save slot" "$dir/loaded" --print-objects --frames 6500)
echo "$out" | grep -q '\[MENU\] loaded save slot 0' || { echo "FAIL: load: $out"; exit 1; }
cmp -s "$dir/saved-1.vars" "$dir/loaded-1.vars" || { echo "FAIL: variables differ after the load"; exit 1; }
echo "$out" | grep -q '^obj  0 .* cell 6,21 ' || { echo "FAIL: Blount isn't back at 6,21 after the load"; exit 1; }
if echo "$out" | grep -q stub; then echo "FAIL: stubs reached"; exit 1; fi
echo "OK: Goblins 3 saves from its menu with a screenshot, lists it, and loads every variable exactly"
