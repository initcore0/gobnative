#!/usr/bin/env bash
# gob2native - build an HD pack with an AI upscaler on a remote CUDA machine
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One command: export the game's pictures, send them to the GPU box, upscale
# them there, fetch the results and install them as an HD pack.
#
#   scripts/hdpack-remote.sh [--model nxbrz|deviance|rebout|unpainter|
#                                    ultrasharp|janai|janai-esrgan|hfa2k]
#                            [--scale 2|3|4|8] [--pack NAME] [--host USER@HOST]
#                            [--gamedir DIR] [--force] [--setup]
#
# --setup (re)creates the remote environment (Python venv with PyTorch CUDA
# and spandrel, plus the four models) in $REMOTE_DIR; run it once per box.
# Everything on the remote side lives in that one directory.

set -euo pipefail

# Your own machine's settings (GPU box address, ComfyUI folder) can go into
# scripts/local.env (git-ignored) or the environment: GOB_GPU_HOST,
# GOB_COMFY_DIR, GOB_COMFY_USER.
[ -f "$(dirname "$0")/local.env" ] && . "$(dirname "$0")/local.env"

HOST="${GOB_GPU_HOST:-root@gpu-box}"
REMOTE_DIR="/root/gob2-upscaler"
MODEL="nxbrz"
SCALE=4
PACK=""
FORCE=""
SETUP=0
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GAMEDIR="$(cd "$HERE/.." && pwd)/gobliins2"
. "$HERE/scripts/gobpaths.sh" # PACKS_DIR (Gobliins 2's HD packs), CACHE_DIR

while [ $# -gt 0 ]; do
	case "$1" in
	--model) MODEL="$2"; shift 2 ;;
	--scale) SCALE="$2"; shift 2 ;;
	--pack) PACK="$2"; shift 2 ;;
	--host) HOST="$2"; shift 2 ;;
	--gamedir) GAMEDIR="$2"; shift 2 ;;
	--force) FORCE="--force"; shift ;;
	--setup) SETUP=1; shift ;;
	-h|--help) sed -n '2,15p' "$0"; exit 0 ;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done

case "$MODEL" in
nxbrz) MODEL_FILE="4x-NXbrz.pth" ;;          # pixel-art sprites
deviance) MODEL_FILE="4x-BS-Deviance.pth" ;; # painted art: backgrounds
rebout) MODEL_FILE="4x-Rebout-Blend.pth" ;;  # fighting-game sprite style
unpainter) MODEL_FILE="8x-MS-Unpainter.pth" ;; # low-res, dithered art (8x)
ultrasharp) MODEL_FILE="4x-UltraSharpV2.safetensors" ;;      # DAT2: illustration, general
janai) MODEL_FILE="4x_IllustrationJaNai_V1_DAT2_190k.pth" ;;  # DAT2: 2D illustration
janai-esrgan) MODEL_FILE="4x_IllustrationJaNai_V1_ESRGAN_135k.pth" ;; # lighter variant
hfa2k) MODEL_FILE="4xHFA2k_ludvae_realplksr_dysample.pth" ;; # RealPLKSR: anime-style art
*) echo "unknown model: $MODEL (nxbrz, deviance, rebout, unpainter, ultrasharp, janai, janai-esrgan, hfa2k)" >&2; exit 2 ;;
esac
case "$SCALE" in 2|3|4|8) ;; *) echo "--scale must be 2, 3, 4 or 8" >&2; exit 2 ;; esac
PACK="${PACK:-$MODEL-${SCALE}x}"

SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")
step() { printf '\n== %s\n' "$*"; }

if [ "$SETUP" = 1 ]; then
	step "Setting up $HOST:$REMOTE_DIR"
	"${SSH[@]}" "set -e; mkdir -p $REMOTE_DIR/models; cd $REMOTE_DIR
		[ -x venv/bin/python ] || python3 -m venv venv
		venv/bin/pip install --quiet --upgrade pip
		venv/bin/pip install --quiet torch torchvision --index-url https://download.pytorch.org/whl/cu128
		venv/bin/pip install --quiet spandrel pillow numpy gdown
		S=https://objectstorage.us-phoenix-1.oraclecloud.com/n/ax6ygfvpvzka/b/open-modeldb-files/o
		cd models
		[ -s 4x-NXbrz.pth ] || curl -fsSL -o 4x-NXbrz.pth \$S/4x-NXbrz.pth
		[ -s 8x-MS-Unpainter.pth ] || curl -fsSL -o 8x-MS-Unpainter.pth \$S/8x-MS-Unpainter.pth
		[ -s 4x-BS-Deviance.pth ] || ../venv/bin/gdown -q -O 4x-BS-Deviance.pth 1hLhsp3aKurLys4cALQiEkYxyxGWNf1ET
		[ -s 4x-Rebout-Blend.pth ] || ../venv/bin/gdown -q -O 4x-Rebout-Blend.pth 1h-bY4gq_61sRF_xK3F4OiVK1cWRX0Q9w
		[ -s 4x-UltraSharpV2.safetensors ] || curl -fsSL -o 4x-UltraSharpV2.safetensors \
			'https://huggingface.co/Kim2091/UltraSharpV2/resolve/main/4x-UltraSharpV2.safetensors?download=true'
		[ -s 4xHFA2k_ludvae_realplksr_dysample.pth ] || curl -fsSL -o 4xHFA2k_ludvae_realplksr_dysample.pth \
			https://github.com/Phhofm/models/releases/download/4xHFA2k_ludvae_realplksr_dysample/4xHFA2k_ludvae_realplksr_dysample.pth
		if [ ! -s 4x_IllustrationJaNai_V1_DAT2_190k.pth ]; then
			../venv/bin/gdown -q -O janai.zip 1qpioSqBkB_IkSBhEAewSSNFt6qgkBimP
			python3 -c 'import zipfile; z = zipfile.ZipFile(\"janai.zip\"); [open(n.split(\"/\")[-1], \"wb\").write(z.read(n)) for n in z.namelist() if n.endswith(\".pth\")]'
			rm -f janai.zip
		fi
		../venv/bin/python -c 'import torch; print(\"CUDA:\", torch.cuda.get_device_name(0))'"
fi

step "Checking $HOST"
if ! "${SSH[@]}" "test -x $REMOTE_DIR/venv/bin/python && test -s $REMOTE_DIR/models/$MODEL_FILE"; then
	echo "The remote environment or model $MODEL_FILE is missing; run once with --setup." >&2
	exit 1
fi
rsync -q -e "ssh -o BatchMode=yes" "$HERE/scripts/upscaler/upscale.py" "$HOST:$REMOTE_DIR/upscale.py"

step "Exporting pictures from $GAMEDIR"
[ -x "$HERE/build/gobexport" ] || cmake --build "$HERE/build" --target gobexport -j8 >/dev/null
EXPORT="$CACHE_DIR/export"
rm -rf "$EXPORT"
mkdir -p "$EXPORT"
"$HERE/build/gobexport" "$GAMEDIR" "$EXPORT" --quiet
echo "$(find "$EXPORT" -name '*.png' ! -name '*.mask.png' | wc -l | tr -d ' ') pictures"

step "Sending pictures (colour images only)"
"${SSH[@]}" "mkdir -p $REMOTE_DIR/jobs/export"
rsync -a --delete -e "ssh -o BatchMode=yes" --include='*/' --exclude='*.mask.png' --include='*.png' --exclude='*' \
	"$EXPORT/" "$HOST:$REMOTE_DIR/jobs/export/"

step "Upscaling with $MODEL_FILE to ${SCALE}x"
"${SSH[@]}" "cd $REMOTE_DIR && venv/bin/python upscale.py --model models/$MODEL_FILE \
	--input jobs/export --output jobs/out/$PACK --target-scale $SCALE $FORCE"

step "Installing pack \"$PACK\""
DEST="$PACKS_DIR/$PACK"
mkdir -p "$DEST"
rsync -a --delete -e "ssh -o BatchMode=yes" "$HOST:$REMOTE_DIR/jobs/out/$PACK/" "$DEST/"
echo "$(find "$DEST" -name '*.png' | wc -l | tr -d ' ') pictures in $DEST"

step "Validating"
"$HERE/build/gobexport" "$GAMEDIR" --check-pack "$DEST" | tail -5

printf '\nDone. In the game, open the overlay (`) > Video > HD pack and pick "%s".\n' "$PACK"
