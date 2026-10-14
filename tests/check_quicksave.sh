#!/bin/sh
# gob2native - headless check: a quick save never overwrites a manual save,
# and a quick load only loads a quick save.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_quicksave.sh <gamedir> [build dir] [gob2|gob3]
#
# Quick saves into an empty save folder (creates slot 14 "QUICKSAVE";
# Goblins 3: slot 29), quick saves again (overwrites its own slot), then
# renames the slot as if it had been saved from the game's menu and checks
# that a third quick save is refused and leaves the file untouched. Quick
# load must load the slot while it is named QUICKSAVE, and refuse (without
# loading) once it holds the manual save. Gobliins 2 is checked in its first
# room after the intro; Goblins 3 in EMAP1000.

set -eu
game=${1:?usage: $0 <gamedir> [build dir] [gob2|gob3]}
build=${2:-build}
key=${3:-gob2}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
if [ "$key" = gob3 ]; then
	n=29
	run="--press-every 5 --press-stop 60"
	at=90
	frames=6000
else
	n=14
	run="--press-every 3 --press-stop 100"
	at=115
	frames=7500
fi
slot="$dir/$key-slot$n.sav"

quicksave() {
	"$build/gobrun" "$game" --game "$key" $run --save-dir "$dir" --quicksave-at $at \
		--frames $frames --quiet 2>&1 | grep '(notify)' || true
}
quickload() {
	"$build/gobrun" "$game" --game "$key" $run --save-dir "$dir" --quickload-at $at \
		--frames $frames --quiet 2>&1 | grep -E '\(notify\)|quick load|loaded save slot' || true
}

out=$(quicksave)
echo "$out" | grep -q 'Saved' || { echo "FAIL: first quick save: $out"; exit 1; }
out=$(quicksave)
echo "$out" | grep -q 'Saved' || { echo "FAIL: quick save over its own slot: $out"; exit 1; }
out=$(quickload)
echo "$out" | grep -q "loaded save slot $n" && echo "$out" | grep -q 'Loaded' ||
	{ echo "FAIL: quick load of the quick save: $out"; exit 1; }

# Name the slot "MYGAME" (the 40-byte name starts at offset 12).
printf 'MYGAME\000\000\000' | dd of="$slot" bs=1 seek=12 conv=notrunc 2>/dev/null
cp "$slot" "$dir/before.sav"
out=$(quicksave)
echo "$out" | grep -q 'holds a manual save' || { echo "FAIL: quick save not refused: $out"; exit 1; }
cmp -s "$slot" "$dir/before.sav" || { echo "FAIL: manual save was modified"; exit 1; }
out=$(quickload)
echo "$out" | grep -q 'No quick save to load' || { echo "FAIL: quick load of a manual save not refused: $out"; exit 1; }
echo "$out" | grep -q 'loaded save slot\|quick load: leaving' && { echo "FAIL: quick load loaded a manual save: $out"; exit 1; }
echo "OK: quick save refuses to overwrite a manual save; quick load only loads a quick save"
