#!/bin/sh
# gob2native - headless check: Gobliiins' quick save and quick load.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_quicksave_gob1.sh <gob1 gamedir> [build dir]
#
# 1. Quick load with no quick save, and quick save on a screen that isn't a
#    level (INTER's game over screen), are refused.
# 2. In the first level (AVT003), after the fighter has kicked the horn off
#    the gate and the picker has picked it up (the clicks of
#    tests/check_gob1_level1.sh), a quick save writes gob1-quick.sav.
# 3. Another run, playing the next level (AVT00) with less energy, quick
#    loads: it goes back to AVT003, and the variables (apart from VAR 0-15:
#    input, time and configuration) and every goblin and object (positions,
#    states, frames, the pocket) equal those at the save.
# 4. After the load, the rest of the walkthrough solves the level and the
#    game goes on to AVT00.
# 5. A damaged quick save, or one from another format version, is not
#    loaded.

set -eu
game=${1:?usage: $0 <gob1 gamedir> [build dir]}
build=${2:-build}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
run() { "$build/gobrun" "$game" --save-dir "$dir/saves" "$@" 2>&1; }

out=$(run --start INTER --set-var 0 286 100 --set-var 0 296 10 --quickload-at 3 --frames 300)
echo "$out" | grep -q '(notify) No quick save to load' || { echo "FAIL: quick load without a quick save: $out"; exit 1; }
out=$(run --start INTER --set-var 0 286 -1 --quicksave-at 2 --frames 200)
echo "$out" | grep -q "(notify) Can't save here" || { echo "FAIL: quick save on the game over screen: $out"; exit 1; }
[ ! -e "$dir/saves/gob1-quick.sav" ] || { echo "FAIL: a quick save was written"; exit 1; }

out=$(run --start INTER --set-var 0 286 100 --set-var 0 296 10 \
	--click 2 165 182 --rclick 3 150 100 --click 4 180 56 \
	--click 14 165 182 --click 15 165 182 --rclick 16 150 100 --click 17 156 92 \
	--quicksave-at 21 --state-on-notify "$dir/saved" --frames 1400)
echo "$out" | grep -q '(notify) Saved' && [ -s "$dir/saves/gob1-quick.sav" ] ||
	{ echo "FAIL: quick save: $out"; exit 1; }
grep -q 'pocket: object 0, item 2' "$dir/saved-1.objects" || { echo "FAIL: the horn isn't in the pocket"; exit 1; }

out=$(run --start INTER --set-var 0 286 3 --set-var 0 296 4 --quickload-at 6 --state-on-notify "$dir/loaded" \
	--rclick 8 150 100 --rclick 9 150 100 --click 10 93 86 \
	--click 19 165 182 --click 20 165 182 --click 21 84 122 \
	--click 31 165 182 --rclick 32 150 100 --click 33 85 118 \
	--click 41 16 85 --click 47 160 100 --frames 3800)
echo "$out" | grep -q '(notify) Loaded' || { echo "FAIL: quick load: $out"; exit 1; }
echo "$out" | grep -q 'quick load: leaving AVT00 for AVT003' || { echo "FAIL: quick load didn't go back to AVT003"; exit 1; }
cmp -s "$dir/saved-1.vars" "$dir/loaded-1.vars" 64 64 || { echo "FAIL: variables differ after the load"; exit 1; }
cmp -s "$dir/saved-1.objects" "$dir/loaded-1.objects" || {
	echo "FAIL: goblins and objects differ after the load"
	diff "$dir/saved-1.objects" "$dir/loaded-1.objects" || true
	exit 1
}
if echo "$out" | grep -q stub; then echo "FAIL: stubs reached"; exit 1; fi
echo "$out" | grep -q '\[AVT003\] loadTot INTER' && echo "$out" | grep -q '\[INTER\] loadTot AVT00$' ||
	{ echo "FAIL: level 1 not solved after the quick load"; exit 1; }

cp "$dir/saves/gob1-quick.sav" "$dir/good.sav"
head -c 60000 "$dir/good.sav" >"$dir/saves/gob1-quick.sav"
out=$(run --start INTER --set-var 0 286 3 --set-var 0 296 10 --quickload-at 6 --frames 800)
echo "$out" | grep -q 'quick load: leaving' && { echo "FAIL: a damaged quick save was loaded"; exit 1; }
cp "$dir/good.sav" "$dir/saves/gob1-quick.sav"
printf '\377' | dd of="$dir/saves/gob1-quick.sav" bs=1 seek=4 conv=notrunc 2>/dev/null # format version
out=$(run --start INTER --set-var 0 286 3 --set-var 0 296 10 --quickload-at 6 --frames 800)
echo "$out" | grep -q '(notify) No quick save to load' || { echo "FAIL: a quick save of another version was loaded"; exit 1; }
echo "OK: Gobliiins quick save restores the level, its goblins and objects exactly; bad files are refused"
