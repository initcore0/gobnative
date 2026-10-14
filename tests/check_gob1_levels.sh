#!/bin/sh
# gob2native - headless check: Gobliiins' level select and access codes.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_gob1_levels.sh <gob1 gamedir> [build dir]
#
# For each of the 22 levels: the level select (gobrun --level-at, as the
# overlay's Levels tab does), requested during the intro, starts the level
# with full energy; and the level's access code as the Levels tab lists it,
# typed on INTER's password screen (after the game over screen), starts the
# same level with full energy. No stubs anywhere.

set -eu
game=${1:?usage: $0 <gob1 gamedir> [build dir]}
build=${2:-build}
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

energy() { # v[296] from a --dump-vars file
	od -An -tu4 -j 1184 -N 4 "$1" | tr -d ' '
}

# The level list with codes, as the Levels tab shows it.
"$build/gobrun" "$game" --list-levels --frames 0 >"$dir/levels.txt" 2>/dev/null
[ "$(grep -c '^level' "$dir/levels.txt")" = 22 ] || { echo "FAIL: --list-levels"; cat "$dir/levels.txt"; exit 1; }

fail=0
while read -r _ n tot code; do
	out=$("$build/gobrun" "$game" --save-dir "$dir" --level-at 3 "$n" --frames 4000 --dump-vars "$dir/v.bin" 2>&1)
	if ! echo "$out" | grep -q "level jump: leaving AVT003 for $tot\$" ||
		! echo "$out" | grep -q "\[$tot\] loadObjects $tot.AVO" || echo "$out" | grep -q stub ||
		[ "$(energy "$dir/v.bin")" != 10 ]; then
		echo "FAIL: level select $n ($tot)"
		fail=1
	fi
	[ "$code" = "-" ] && continue
	out=$("$build/gobrun" "$game" --start INTER --set-var 0 286 -1 --save-dir "$dir" --key 2 0x1C0D \
		--click 4 130 115 --type 6 "$code" --key 8 0x1C0D --frames 3500 --dump-vars "$dir/v.bin" 2>&1)
	if ! echo "$out" | grep -q "\[INTER\] loadTot $tot\$" || ! echo "$out" | grep -q "\[$tot\] loadObjects $tot.AVO" ||
		echo "$out" | grep -q stub || [ "$(energy "$dir/v.bin")" != 10 ]; then
		echo "FAIL: access code $code for level $n ($tot)"
		fail=1
	fi
done <"$dir/levels.txt"
[ $fail = 0 ] || exit 1
echo "OK: the 22 levels start from the level select and from their access codes, with full energy"
