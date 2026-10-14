#!/bin/sh
# gob2native - check: the GOG installer import against innoextract.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: tests/check_install.sh <folder with the setup_*.exe files> [build dir]
#
# For each of the three GOG installers found in the folder (the others are
# skipped): extracts every file with innoextract and with `gobinstall
# --all`, and the trees must be identical, byte for byte (this covers the
# LZMA2 chunks, the GOG Galaxy parts, the x86 call filter of the DLLs and
# the file names); then installs the game with gobinstall (with --floppy),
# and every installed file must equal innoextract's. Runs test_install
# with the folder too. Skips (exit 0) without innoextract or installers.
# Everything is written to a temporary folder, which is removed.

set -eu
setups=${1:?usage: $0 <folder with the setup_*.exe files> [build dir]}
build=${2:-build}
if ! command -v innoextract >/dev/null 2>&1; then
	echo "SKIP: innoextract not found (it is the reference)"
	exit 0
fi
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

"$build/test_install" "$setups" >"$dir/unit.txt" 2>&1 || { cat "$dir/unit.txt"; echo "FAIL: test_install"; exit 1; }

fail=0 checked=0
for pair in "gob1:setup_gobliiins_1.02_(20270).exe" "gob2:setup_gobliins_2_-_the_prince_buffoon_1.02_(20270).exe" \
	"gob3:setup_goblins_quest_3_1.02_(20270).exe"; do
	key=${pair%%:*}
	setup=$setups/${pair#*:}
	if [ ! -f "$setup" ]; then
		echo "skipped $key: $setup not found"
		continue
	fi
	checked=$((checked + 1))
	mkdir -p "$dir/$key/inno"
	innoextract -q -d "$dir/$key/inno" "$setup" >/dev/null 2>&1 || { echo "FAIL $key: innoextract"; fail=1; continue; }
	if ! "$build/gobinstall" "$setup" "$dir/$key/all" --all >"$dir/$key/all.txt" 2>&1; then
		cat "$dir/$key/all.txt"
		echo "FAIL $key: gobinstall --all"
		fail=1
	elif ! diff -r "$dir/$key/inno" "$dir/$key/all" >"$dir/$key/diff.txt" 2>&1; then
		head -20 "$dir/$key/diff.txt"
		echo "FAIL $key: gobinstall --all differs from innoextract"
		fail=1
	fi
	n=$(cd "$dir/$key/inno" && find . -type f | wc -l | tr -d ' ')
	if ! "$build/gobinstall" "$setup" "$dir/$key/game" --floppy >"$dir/$key/game.txt" 2>&1; then
		cat "$dir/$key/game.txt"
		echo "FAIL $key: gobinstall"
		fail=1
		continue
	fi
	installed=0
	for f in $(cd "$dir/$key/game" && find . -type f | sort); do
		installed=$((installed + 1))
		cmp -s "$dir/$key/game/$f" "$dir/$key/inno/$f" || { echo "FAIL $key: $f differs"; fail=1; }
	done
	game=$(grep '^game:' "$dir/$key/game.txt")
	echo "$key: $n files identical to innoextract's; installed $installed files ($game)"
	rm -rf "$dir/$key"
done
if [ "$fail" != 0 ]; then
	exit 1
fi
if [ "$checked" = 0 ]; then
	echo "SKIP: no GOG installers in $setups"
	exit 0
fi
echo "PASS: $checked installer(s)"
