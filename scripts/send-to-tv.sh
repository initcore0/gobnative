#!/bin/sh
# gob2native - sends GOG installers or game folders to gobnative on a rooted LG TV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: scripts/send-to-tv.sh root@ADDRESS setup_....exe [more installers or game folders...]
#
# For a TV without a USB stick at hand: copies each file or folder over SSH
# into the app's Installers folder on the TV, where the game library finds
# it as it finds a USB stick's (within a few seconds, also while it is
# open). An installer is unpacked with Install and then removed from the TV;
# a game's folder (already installed on a computer) is moved into place.
#
# Needs gobnative installed on the TV and started once (it makes its data
# folder, which the copies are given to: the app runs as its own user).
# Each item goes to a hidden name first and is renamed when complete, so
# the library never sees half a copy.

set -eu

if [ $# -lt 2 ]; then
	echo "usage: $0 root@ADDRESS setup_....exe [more installers or game folders...]" >&2
	exit 2
fi
tv=$1
shift

app=/media/developer/apps/usr/palm/applications/io.github.initcore0.gobnative
dest=$app/data/Installers

ssh "$tv" "test -d $app/data || { echo 'gobnative is not installed on the TV, or has never been started: start it once' >&2; exit 1; }
	mkdir -p $dest && chown \$(stat -c %u:%g $app/data) $dest"

no_xattrs=
if tar --no-xattrs -cf - /dev/null >/dev/null 2>&1; then
	no_xattrs=--no-xattrs
fi

for item in "$@"; do
	if [ ! -e "$item" ]; then
		echo "$item: not found" >&2
		exit 1
	fi
	name=$(basename "$item")
	case $name in
	*\'*) echo "$name: names with ' aren't supported; rename it first" >&2; exit 1 ;;
	esac
	parent=$(cd "$(dirname "$item")" && pwd)
	echo "sending $name ($(du -sh "$item" | cut -f1))"
	# tar keeps any name intact (GOG's have parentheses); COPYFILE_DISABLE
	# and --no-xattrs keep macOS's extra metadata out.
	COPYFILE_DISABLE=1 tar $no_xattrs -C "$parent" -cf - "$name" |
		ssh "$tv" "rm -rf '$dest/.incoming' && mkdir -p '$dest/.incoming' && tar -xf - -C '$dest/.incoming' &&
			chown -R \$(stat -c %u:%g $app/data) '$dest/.incoming' &&
			rm -rf '$dest/$name' && mv '$dest/.incoming/$name' '$dest/$name' && rmdir '$dest/.incoming'"
done
echo "done: open gobnative on the TV and press Install on the game's tile"
