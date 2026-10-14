# gob2native - where gobnative keeps Gobliins 2's HD packs, and the scripts' export cache
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by the scripts (POSIX sh). Sets PACKS_DIR and CACHE_DIR.
#
# Since the rename, Gobliins 2's folder is gobnative/Gobliins2; the first
# start of gobnative moves the old gob2native/Gobliins2 there and leaves a
# link behind (docs/port-plan.md 5.4). Until then (or if the move failed and
# the app uses the old folder in place) the old folder is the one in use, so
# take whichever holds the files, and never make gobnative/Gobliins2 while
# the old folder is still a real one (the app would then use the new, empty
# folder and leave the old one alone).
# PACKS_DIR or GOB_PREF_ROOT (instead of ~/Library/Application Support/gobnative)
# in the environment override this.

_gob_support="$HOME/Library/Application Support"
_gob_root="${GOB_PREF_ROOT:-$_gob_support/gobnative}"
_gob_old="$(dirname "$_gob_root")/gob2native/Gobliins2"
if [ -z "${PACKS_DIR:-}" ]; then
	if [ -d "$_gob_root/Gobliins2" ] || [ ! -d "$_gob_old" ]; then
		PACKS_DIR="$_gob_root/Gobliins2/HDPacks"
	else
		PACKS_DIR="$_gob_old/HDPacks"
	fi
fi
# The export cache only belongs to these scripts: keep using an existing
# gob2native one rather than exporting again.
_gob_cache="${XDG_CACHE_HOME:-$HOME/Library/Caches}"
if [ -d "$_gob_cache/gobnative" ] || [ ! -d "$_gob_cache/gob2native" ]; then
	CACHE_DIR="$_gob_cache/gobnative"
else
	CACHE_DIR="$_gob_cache/gob2native"
fi
unset _gob_support _gob_root _gob_old _gob_cache
