#!/usr/bin/env bash
# gob2native - repaint animation frames with the OpenAI Image API into an HD pack
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   scripts/openai-frames.sh --layers "GOB01/anim0/layer0 GOB01/anim0/layer1" --name green-goblin
#                            [--describe "what the character looks like"] [--prompt-file FILE]
#                            [--style PNG] [--pack NAME] [--from-pack NAME]
#                            [--model M] [--quality Q] [--frames DIR] [--dry-run]
#
# All frames of the given layers (from a gobexport --frames folder) go into
# one picture, a row per animation, so one request paints the same character
# in all of them (scripts/frames-grid.py --api --combine). It is sent with a
# remastered background as a style reference (default: GOB01's background in
# the --from-pack pack), cut back into frames with the magenta key made
# transparent, and imported with gobexport --import-frames --alpha into PACK.
# A new PACK starts as a copy of --from-pack (default openai-gob01), so it
# holds the remastered rooms plus the repainted sprites; pick it in the
# overlay with pack scope "all" to see the sprites.
#
# Everything sent and received is kept in ../upscale-openai/sprites/NAME/.

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
. "$HERE/scripts/gobpaths.sh"
LAYERS=""
NAME=""
DESCRIBE=""
PROMPT_FILE=""
STYLE=""
PACK="openai-sprites"
FROM_PACK="openai-gob01"
MODEL="gpt-image-2.5-sunburst"
QUALITY="high"
FRAMES="$ROOT/upscale-gob01/frames"
DRY=""

while [ $# -gt 0 ]; do
	case "$1" in
	--layers) LAYERS="$2"; shift 2 ;;
	--name) NAME="$2"; shift 2 ;;
	--describe) DESCRIBE="$2"; shift 2 ;;
	--prompt-file) PROMPT_FILE="$2"; shift 2 ;;
	--style) STYLE="$2"; shift 2 ;;
	--pack) PACK="$2"; shift 2 ;;
	--from-pack) FROM_PACK="$2"; shift 2 ;;
	--model) MODEL="$2"; shift 2 ;;
	--quality) QUALITY="$2"; shift 2 ;;
	--frames) FRAMES="$2"; shift 2 ;;
	--dry-run) DRY="--dry-run"; shift ;;
	-h|--help) sed -n '2,21p' "$0"; exit 0 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ -n "$LAYERS" ] && [ -n "$NAME" ] || { echo "--layers and --name are required" >&2; exit 2; }
STYLE="${STYLE:-$PACKS_DIR/$FROM_PACK/GOB01/30007.png}"
[ -f "$STYLE" ] || { echo "no style picture $STYLE (give --style)" >&2; exit 1; }
KEEP="$ROOT/upscale-openai/sprites/$NAME"
STAMP="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$KEEP"

python3 "$HERE/scripts/frames-grid.py" pack --frames "$FRAMES" --layers "$LAYERS" --out "$KEEP/grid" --api --combine "$NAME"

if [ -n "$PROMPT_FILE" ]; then
	cp "$PROMPT_FILE" "$KEEP/prompt-$STAMP.txt"
else
	cat > "$KEEP/prompt-$STAMP.txt" <<EOF
The first image is a sprite sheet: animation frames of one cartoon character from the 1990s point-and-click adventure game Gobliins 2, enlarged from low-resolution pixel art, on a flat magenta background. Each row is one animation of the character. The second image is a remastered background from the same game: use its hand-painted style, lighting and level of detail.

Redraw every frame of the first image as high-quality hand-painted cartoon art in that style.

Keep exactly: the image size; every frame's position, size and silhouette; the pose in every frame, so the animation still plays the same; and the character's design and colours: head shape, eyes, nose, ears, mouth, hair, clothes, proportions. It must be the same character in every frame. ${DESCRIBE}

Keep the background pure flat magenta (#FF00FF) everywhere outside the character, with no shadows, ground, outlines or gradients on it. Do not add new elements, text or frame borders.
EOF
fi

python3 "$HERE/scripts/openai-remaster.py" --image "$KEEP/grid/$NAME.png" --out "$KEEP/answer-$STAMP.png" \
	--ref "$STYLE" --prompt-file "$KEEP/prompt-$STAMP.txt" --model "$MODEL" --quality "$QUALITY" $DRY
[ -z "$DRY" ] || exit 0

# Cut the answer into frames.
mkdir -p "$KEEP/answer"
cp "$KEEP/answer-$STAMP.png" "$KEEP/answer/$NAME.png"
rm -rf "$KEEP/frames"
python3 "$HERE/scripts/frames-grid.py" unpack --grids "$KEEP/grid" --repainted "$KEEP/answer" --out "$KEEP/frames" --frames "$FRAMES"

DEST="$PACKS_DIR/$PACK"
if [ ! -d "$DEST" ] && [ -d "$PACKS_DIR/$FROM_PACK" ]; then
	cp -R "$PACKS_DIR/$FROM_PACK" "$DEST"
	echo "new pack $PACK, started from $FROM_PACK"
fi
mkdir -p "$DEST"
"$HERE/build/gobexport" "$ROOT/gobliins2" --import-frames "$KEEP/frames" "$CACHE_DIR/export" "$DEST" --alpha --scale 4

printf '\nIn the game: overlay (`) > Video > HD pack "%s", pack scope "all".\n' "$PACK"
