#!/usr/bin/env bash
# gob2native - repaint room backgrounds with an image-editing model on the GPU box
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One command: pick pictures from the gobexport folder, send them to the GPU
# box, repaint them with Qwen-Image 2.1 or FLUX.2 [dev] in the ComfyUI install there, fetch
# the results and install them as an HD pack (pixel-exact, so live switching
# and the alignment guide work as with any other pack).
#
#   scripts/remaster-remote.sh [--model qwen|flux2] [--tots "GOB01 GOB02"] [--kinds background]
#                              [--ids "30000 30006"] [--prompt TEXT | --prompt-file FILE]
#                              [--scale 2|3|4] [--steps N] [--cfg X] [--seed N] [--denoise X]
#                              [--base PACK] [--pack NAME] [--host USER@HOST] [--keep] [--force]
#
# Pictures with transparency (sprites, animation sheets) are sent on a flat
# magenta background; the game cuts it away again with the original mask.
#
# ComfyUI is started for the run (as its owner, on 127.0.0.1 only) and
# stopped afterwards unless it was already running or --keep is given.

set -euo pipefail

# Your own machine's settings (GPU box address, ComfyUI folder) can go into
# scripts/local.env (git-ignored) or the environment: GOB_GPU_HOST,
# GOB_COMFY_DIR, GOB_COMFY_USER.
[ -f "$(dirname "$0")/local.env" ] && . "$(dirname "$0")/local.env"

HOST="${GOB_GPU_HOST:-root@gpu-box}"
REMOTE_DIR="/root/gob2-upscaler"
COMFY_DIR="${GOB_COMFY_DIR:-/home/user/ComfyUI}"
COMFY_USER="${GOB_COMFY_USER:-user}"
TOTS="GOB01"
IDS=""
KINDS="background"
SCALE=4
STEPS=20
CFG=4
SEED=1
DENOISE=1.0
BASE=""
MODEL="qwen"
PACK=""
KEEP=0
FORCE=""
PROMPT="Remaster this 1990s point-and-click adventure game background as a detailed hand-painted illustration. \
Keep the exact composition, perspective, every object, its position, size and outline, and the colour palette. \
Add painterly texture, soft lighting and fine detail. No new objects, no text."
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GAMEDIR="$(cd "$HERE/.." && pwd)/gobliins2"
. "$HERE/scripts/gobpaths.sh" # PACKS_DIR (Gobliins 2's HD packs), CACHE_DIR
EXPORT="$CACHE_DIR/export"

while [ $# -gt 0 ]; do
	case "$1" in
	--model) MODEL="$2"; shift 2 ;;
	--tots) TOTS="$2"; shift 2 ;;
	--kinds) KINDS="$2"; shift 2 ;;
	--ids) IDS="$2"; shift 2 ;;
	--prompt) PROMPT="$2"; shift 2 ;;
	--prompt-file) PROMPT="$(cat "$2")"; shift 2 ;;
	--scale) SCALE="$2"; shift 2 ;;
	--steps) STEPS="$2"; shift 2 ;;
	--cfg) CFG="$2"; shift 2 ;;
	--seed) SEED="$2"; shift 2 ;;
	--denoise) DENOISE="$2"; shift 2 ;;
	--base) BASE="$2"; shift 2 ;;
	--pack) PACK="$2"; shift 2 ;;
	--host) HOST="$2"; shift 2 ;;
	--keep) KEEP=1; shift ;;
	--force) FORCE="--force"; shift ;;
	-h|--help) sed -n '2,16p' "$0"; exit 0 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
case "$SCALE" in 2|3|4) ;; *) echo "--scale must be 2, 3 or 4" >&2; exit 2 ;; esac
case "$MODEL" in qwen|flux2) ;; *) echo "--model must be qwen or flux2" >&2; exit 2 ;; esac
PACK="${PACK:-remaster-$MODEL}"

SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")
step() { printf '\n== %s\n' "$*"; }
JOB="$REMOTE_DIR/jobs/remaster/$PACK"

if [ ! -f "$EXPORT/index.json" ]; then
	step "Exporting pictures from $GAMEDIR"
	[ -x "$HERE/build/gobexport" ] || cmake --build "$HERE/build" --target gobexport -j8 >/dev/null
	mkdir -p "$EXPORT"
	"$HERE/build/gobexport" "$GAMEDIR" "$EXPORT" --quiet
fi

step "Selecting pictures ($KINDS in $TOTS)"
LIST="$(mktemp)"
trap 'rm -f "$LIST"' EXIT
python3 - "$EXPORT/index.json" "$TOTS" "$KINDS" "$IDS" > "$LIST" <<'EOF'
import json, sys
index, tots, kinds = json.load(open(sys.argv[1])), set(sys.argv[2].split()), set(sys.argv[3].replace(",", " ").split())
ids = {int(i) for i in sys.argv[4].split()}
for e in index["images"]:
	if e["tot"] in tots and (e["resource"] in ids if ids else e["kind"] in kinds and "same_as" not in e):
		print(e["image"])
		if e.get("transparent"):
			print(e["image"][:-4] + ".mask.png")
EOF
echo "$(grep -vc '\.mask\.png$' "$LIST") picture(s)"
[ -s "$LIST" ] || exit 1

step "Sending pictures and script"
"${SSH[@]}" "mkdir -p $JOB/in $JOB/out"
rsync -a --delete -e "ssh -o BatchMode=yes" --files-from="$LIST" "$EXPORT/" "$HOST:$JOB/in/"
BASE_ARG=""
if [ -n "$BASE" ]; then
	# --base: an installed HD pack name, used as the starting point (with --denoise below 1)
	rsync -a --delete -e "ssh -o BatchMode=yes" --files-from=<(grep -v '\.mask\.png$' "$LIST") \
		"$PACKS_DIR/$BASE/" "$HOST:$JOB/base/"
	BASE_ARG="--base $JOB/base"
fi
rsync -q -e "ssh -o BatchMode=yes" "$HERE/scripts/upscaler/comfy_edit.py" "$HOST:$REMOTE_DIR/comfy_edit.py"

step "Starting ComfyUI"
comfy_up() { "${SSH[@]}" "curl -sf http://127.0.0.1:8188/system_stats >/dev/null"; }
STARTED=no
if ! comfy_up; then
	STARTED=yes
	"${SSH[@]}" "systemctl reset-failed gob2-comfyui 2>/dev/null; systemd-run --quiet --unit gob2-comfyui \
		--uid $COMFY_USER --working-directory $COMFY_DIR -p StandardOutput=file:/tmp/gob2-comfyui.log \
		-p StandardError=file:/tmp/gob2-comfyui.log $COMFY_DIR/venv/bin/python main.py --listen 127.0.0.1 --port 8188"
	for i in $(seq 60); do comfy_up && break; sleep 2; done
	comfy_up || { "${SSH[@]}" "tail -20 /tmp/gob2-comfyui.log" >&2; exit 1; }
fi
stop_comfy() {
	if [ "$STARTED" = yes ] && [ "$KEEP" = 0 ]; then
		"${SSH[@]}" "systemctl stop gob2-comfyui" || true
		echo "ComfyUI stopped"
	fi
}
trap 'rm -f "$LIST"; stop_comfy' EXIT

step "Repainting with $MODEL at ${SCALE}x"
PROMPT_B64="$(printf '%s' "$PROMPT" | base64 | tr -d '\n')"
"${SSH[@]}" "cd $REMOTE_DIR && $COMFY_DIR/venv/bin/python comfy_edit.py --input $JOB/in --output $JOB/out \
	--model $MODEL --prompt \"\$(echo $PROMPT_B64 | base64 -d)\" --scale $SCALE --steps $STEPS --cfg $CFG --seed $SEED --denoise $DENOISE --flat-bg ff00ff $BASE_ARG $FORCE"

step "Installing pack \"$PACK\""
DEST="$PACKS_DIR/$PACK"
mkdir -p "$DEST"
rsync -a -e "ssh -o BatchMode=yes" --exclude='.work' "$HOST:$JOB/out/" "$DEST/"
printf '%s\n' "$PROMPT" > "$DEST/prompt.txt"
echo "$(find "$DEST" -name '*.png' | wc -l | tr -d ' ') pictures in $DEST"
"$HERE/build/gobexport" "$GAMEDIR" --check-pack "$DEST" | tail -3

printf '\nDone. In the game, open the overlay (`) > Video > HD pack and pick "%s".\n' "$PACK"
