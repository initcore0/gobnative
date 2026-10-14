#!/usr/bin/env bash
# gob2native - repaint animation frames with an image-editing model on the GPU box
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One command per animation: takes layers from a gobexport --frames folder,
# lays each layer's frames out in one picture (scripts/frames-grid.py),
# repaints it on the GPU box through ComfyUI (Qwen-Image 2.1 or FLUX.2 [dev]),
# cuts the result back into frames and imports them into an HD pack with
# gobexport --import-frames.
#
#   scripts/remaster-frames.sh --layers "GOB01/anim0/layer0 ..." [--model flux2|qwen]
#                              [--prompt TEXT | --prompt-file FILE] [--steps N] [--cfg X]
#                              [--seed N] [--pack NAME] [--base PACK] [--frames DIR]
#                              [--host USER@HOST] [--keep]
#
# --base PACK seeds a new pack's sheets from an installed pack (default
# nxbrz-4x), so pieces no repainted frame covers still look upscaled.
# The repainted frames are kept next to the frames folder, in
# <frames>-repainted/<pack>/, for a look or touch-ups.

set -euo pipefail

# Your own machine's settings (GPU box address, ComfyUI folder) can go into
# scripts/local.env (git-ignored) or the environment: GOB_GPU_HOST,
# GOB_COMFY_DIR, GOB_COMFY_USER.
[ -f "$(dirname "$0")/local.env" ] && . "$(dirname "$0")/local.env"

HOST="${GOB_GPU_HOST:-root@gpu-box}"
REMOTE_DIR="/root/gob2-upscaler"
COMFY_DIR="${GOB_COMFY_DIR:-/home/user/ComfyUI}"
COMFY_USER="${GOB_COMFY_USER:-user}"
LAYERS=""
MODEL="flux2"
STEPS=20
CFG=4
SEED=1
PACK=""
BASE="nxbrz-4x"
KEEP=0
PROMPT="Redraw these animation frames of a cartoon character from a 1990s point-and-click adventure game as \
high-quality hand-drawn cartoon art with clean ink outlines, soft cel shading and fine detail. Every frame stays \
exactly where it is, with the same pose, proportions, size and colours, so the frames still animate together. \
Keep the flat magenta background. No new objects, no text."
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GAMEDIR="$(cd "$HERE/.." && pwd)/gobliins2"
FRAMES="$(cd "$HERE/.." && pwd)/upscale-gob01/frames"
. "$HERE/scripts/gobpaths.sh" # PACKS_DIR (Gobliins 2's HD packs), CACHE_DIR
EXPORT="$CACHE_DIR/export"

while [ $# -gt 0 ]; do
	case "$1" in
	--layers) LAYERS="$2"; shift 2 ;;
	--model) MODEL="$2"; shift 2 ;;
	--prompt) PROMPT="$2"; shift 2 ;;
	--prompt-file) PROMPT="$(cat "$2")"; shift 2 ;;
	--steps) STEPS="$2"; shift 2 ;;
	--cfg) CFG="$2"; shift 2 ;;
	--seed) SEED="$2"; shift 2 ;;
	--pack) PACK="$2"; shift 2 ;;
	--base) BASE="$2"; shift 2 ;;
	--frames) FRAMES="$2"; shift 2 ;;
	--host) HOST="$2"; shift 2 ;;
	--keep) KEEP=1; shift ;;
	-h|--help) sed -n '2,20p' "$0"; exit 0 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ -n "$LAYERS" ] || { echo "--layers is required (e.g. GOB01/anim0/layer0)" >&2; exit 2; }
case "$MODEL" in qwen|flux2) ;; *) echo "--model must be qwen or flux2" >&2; exit 2 ;; esac
PACK="${PACK:-frames-$MODEL}"

SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")
step() { printf '\n== %s\n' "$*"; }
JOB="$REMOTE_DIR/jobs/frames/$PACK"
WORK="$(mktemp -d)"
STARTED=no
cleanup() {
	rm -rf "$WORK"
	if [ "$STARTED" = yes ] && [ "$KEEP" = 0 ]; then
		"${SSH[@]}" "systemctl stop gob2-comfyui" || true
		echo "ComfyUI stopped"
	fi
}
trap cleanup EXIT

step "Laying out frames"
python3 "$HERE/scripts/frames-grid.py" pack --frames "$FRAMES" --layers "$LAYERS" --out "$WORK/grids"

step "Sending grids and script"
"${SSH[@]}" "rm -rf $JOB/in && mkdir -p $JOB/in $JOB/out"
rsync -a -e "ssh -o BatchMode=yes" --include='*.png' --exclude='*' "$WORK/grids/" "$HOST:$JOB/in/"
rsync -q -e "ssh -o BatchMode=yes" "$HERE/scripts/upscaler/comfy_edit.py" "$HOST:$REMOTE_DIR/comfy_edit.py"

step "Starting ComfyUI"
comfy_up() { "${SSH[@]}" "curl -sf http://127.0.0.1:8188/system_stats >/dev/null"; }
if ! comfy_up; then
	STARTED=yes
	"${SSH[@]}" "systemctl reset-failed gob2-comfyui 2>/dev/null; systemd-run --quiet --unit gob2-comfyui \
		--uid $COMFY_USER --working-directory $COMFY_DIR -p StandardOutput=file:/tmp/gob2-comfyui.log \
		-p StandardError=file:/tmp/gob2-comfyui.log $COMFY_DIR/venv/bin/python main.py --listen 127.0.0.1 --port 8188"
	for i in $(seq 60); do comfy_up && break; sleep 2; done
	comfy_up || { "${SSH[@]}" "tail -20 /tmp/gob2-comfyui.log" >&2; exit 1; }
fi

step "Repainting with $MODEL"
PROMPT_B64="$(printf '%s' "$PROMPT" | base64 | tr -d '\n')"
"${SSH[@]}" "cd $REMOTE_DIR && $COMFY_DIR/venv/bin/python comfy_edit.py --input $JOB/in --output $JOB/out \
	--model $MODEL --prompt \"\$(echo $PROMPT_B64 | base64 -d)\" --scale 1 --steps $STEPS --cfg $CFG --seed $SEED --force"
rsync -a -e "ssh -o BatchMode=yes" --exclude='.work' "$HOST:$JOB/out/" "$WORK/repainted/"

step "Cutting frames"
OUT="$FRAMES-repainted/$PACK"
mkdir -p "$OUT"
python3 "$HERE/scripts/frames-grid.py" unpack --grids "$WORK/grids" --repainted "$WORK/repainted" --out "$OUT"
mkdir -p "$OUT/_grids"
cp "$WORK/repainted/"*.png "$OUT/_grids/"

step "Importing into pack \"$PACK\""
DEST="$PACKS_DIR/$PACK"
mkdir -p "$DEST"
if [ -n "$BASE" ] && [ -d "$PACKS_DIR/$BASE" ]; then
	for spec in $LAYERS; do
		tot="${spec%%/*}"
		python3 - "$FRAMES/$spec/layer.json" <<'EOF' | while read -r sheet; do
import json, sys
print("\n".join(str(s) for s in json.load(open(sys.argv[1]))["sheets"]))
EOF
			if [ ! -f "$DEST/$tot/$sheet.png" ] && [ -f "$PACKS_DIR/$BASE/$tot/$sheet.png" ]; then
				mkdir -p "$DEST/$tot"
				cp "$PACKS_DIR/$BASE/$tot/$sheet.png" "$DEST/$tot/"
				echo "seeded $tot/$sheet.png from $BASE"
			fi
		done
	done
fi
# The import picks files by path, so hand it only the frames (not _grids).
IMPORT="$WORK/import"
mkdir -p "$IMPORT"
rsync -a --exclude='_grids' "$OUT/" "$IMPORT/"
# --alpha: the outline comes from the repaint (frames-grid.py keyed the background out).
"$HERE/build/gobexport" "$GAMEDIR" --import-frames "$IMPORT" "$EXPORT" "$DEST" --alpha
printf '%s\n' "$PROMPT" > "$DEST/prompt.txt"

printf '\nDone. Repainted frames: %s\nIn the game, open the overlay (`) > Video > HD pack and pick "%s" (pack scope: all).\n' "$OUT" "$PACK"
