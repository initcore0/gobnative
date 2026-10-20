#!/bin/sh
# gob2native - headless check: the fixes for bugs of the original Gobliiins
# (engine/rules.h, GameRules::pickAliases).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_gob1_fixes.sh <gob1 gamedir> [build dir]
#
# The spiders (AVT001): once the small spider is shot, a pick on the ladder
# under it takes its pillow without climbing; the original put the pillow
# in the pocket under the ladder's item id, and the pillow laid on the path
# then never caught the flask. The level must be solved from there.

set -eu
game=${1:?usage: $0 <gob1 gamedir> [build dir]}
build=${2:-build}
here=$(dirname "$0")
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

{
	echo "level AVT001"
	sed -n '/^room AVT001/,/^use 54 69/p' "$here/gob1/06-AVT001-spiders.txt"
	cat <<'EOF'
pick 54 52      # the pillow, picked on the ladder below it, from the ladder's foot
hint pillow-place
pick 174 135
goblin 2
act 174 81
goblin 0
walk 150 135
walk 174 129
exit
end
EOF
} >"$dir/script.txt"
"$build/gobrun" "$game" --save-dir "$dir/saves" --hints "$here/../assets/hints/gob1.txt" \
	--script "$dir/script.txt" --frames 900000 >"$dir/out.txt" 2>&1 ||
	{ grep -- '--' "$dir/out.txt" | tail -3; echo "FAIL: the script didn't finish"; exit 1; }
grep -q 'pick on item 16 takes item 2' "$dir/out.txt" || { echo "FAIL: the pick on the ladder wasn't taken as the pillow"; exit 1; }
grep -q '(AVT001 solved' "$dir/out.txt" || { echo "FAIL: the spiders level wasn't solved"; exit 1; }
echo "OK: the pillow picked from below catches the flask"
