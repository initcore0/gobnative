#!/usr/bin/env bash
# gob2native - repaint animation frames by hand in ChatGPT (or any image app)
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   scripts/chatgpt-frames.sh prepare "GOB01/anim0/layer0 ..." [DIR]
#     writes one sheet per animation into DIR/to-chatgpt/ (all frames of the
#     layer on flat magenta, padded to a shape ChatGPT returns) and DIR/PROMPT.txt
#
#   scripts/chatgpt-frames.sh import [DIR] [PACK]
#     takes the pictures you saved into DIR/from-chatgpt/ under the same file
#     names, cuts them into frames and imports them into HD pack PACK
#     (default chatgpt), seeded from nxbrz-4x
#
# DIR defaults to ../upscale-gob01/chatgpt, the frames come from
# ../upscale-gob01/frames (gobexport --frames).

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
FRAMES="$ROOT/upscale-gob01/frames"
GAMEDIR="$ROOT/gobliins2"
. "$HERE/scripts/gobpaths.sh" # PACKS_DIR (Gobliins 2's HD packs), CACHE_DIR
EXPORT="$CACHE_DIR/export"

cmd="${1:-}"
case "$cmd" in
prepare)
	LAYERS="${2:?layers, e.g. GOB01/anim0/layer0}"
	DIR="${3:-$ROOT/upscale-gob01/chatgpt}"
	mkdir -p "$DIR/to-chatgpt" "$DIR/from-chatgpt" "$DIR/.grids"
	python3 "$HERE/scripts/frames-grid.py" pack --frames "$FRAMES" --layers "$LAYERS" --out "$DIR/.grids" --chatgpt
	cp "$DIR/.grids/"*.png "$DIR/to-chatgpt/"
	cat > "$DIR/PROMPT.txt" <<'TXT'
This is a sprite sheet of animation frames of one cartoon character from a 1990s point-and-click adventure game, on a flat magenta background. Redraw it as high-quality hand-drawn cartoon art with clean ink outlines, soft shading and fine detail.

Rules:
- Same image size and framing. Every frame stays exactly where it is, with the same pose, proportions and size, so the frames still animate together.
- Keep the character's colours (skin, clothes, shoes).
- The same character design in every frame.
- Keep the background pure flat magenta (#FF00FF), with no shadows, gradients or ground on it.
- No new objects, no text, no frame borders.
TXT
	echo
	echo "Upload the PNG(s) from $DIR/to-chatgpt with the text of PROMPT.txt,"
	echo "save ChatGPT's picture into $DIR/from-chatgpt under the same file name, then run:"
	echo "  scripts/chatgpt-frames.sh import"
	;;
import)
	DIR="${2:-$ROOT/upscale-gob01/chatgpt}"
	PACK="${3:-chatgpt}"
	WORK="$(mktemp -d)"
	trap 'rm -rf "$WORK"' EXIT
	python3 "$HERE/scripts/frames-grid.py" unpack --grids "$DIR/.grids" --repainted "$DIR/from-chatgpt" --out "$WORK/frames"
	DEST="$PACKS_DIR/$PACK"
	for meta in "$DIR/.grids/"*.json; do
		python3 - "$meta" "$FRAMES" <<'PY' | while read -r tot sheet; do
import json, sys
layer = json.load(open(sys.argv[1]))["layer"]
for s in json.load(open(f"{sys.argv[2]}/{layer}/layer.json"))["sheets"]:
	print(layer.split("/")[0], s)
PY
			if [ ! -f "$DEST/$tot/$sheet.png" ] && [ -f "$PACKS_DIR/nxbrz-4x/$tot/$sheet.png" ]; then
				mkdir -p "$DEST/$tot"
				cp "$PACKS_DIR/nxbrz-4x/$tot/$sheet.png" "$DEST/$tot/"
			fi
		done
	done
	mkdir -p "$DEST"
	"$HERE/build/gobexport" "$GAMEDIR" --import-frames "$WORK/frames" "$EXPORT" "$DEST" --alpha
	printf '\nIn the game, open the overlay (`) > Video > HD pack and pick "%s" (pack scope: all).\n' "$PACK"
	;;
*) sed -n '2,17p' "$0"; exit 2 ;;
esac
