#!/usr/bin/env python3
# gob2native - lay out animation frames in one picture for an image model, and back
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Repainting each frame on its own makes them disagree (the animation
# flickers). This puts all frames of a layer into one grid picture, each a
# whole character enlarged with nearest-neighbour on a flat key colour, so
# the model repaints them together and consistently; "unpack" cuts the
# repainted grid back into frame PNGs for gobexport --import-frames ... --alpha:
# the key colour becomes transparent and its tint is removed from the edges.
#
#   frames-grid.py pack --frames FRAMES --layers "GOB01/anim0/layer0 ..." --out GRIDS
#                       [--chatgpt | --api] [--combine NAME]
#   frames-grid.py unpack --grids GRIDS --repainted REPAINTED_GRIDS --out FRAMES_OUT
#
# FRAMES is a gobexport --frames folder. Each grid is GRIDS/<TOT>_anim<A>_layer<L>.png
# with a .json describing the cells. --chatgpt pads each grid to the nearest
# shape ChatGPT's image model returns (1:1, 3:2 or 2:3), so its answer can be
# scaled back without stretching; --api pads it to a size the OpenAI Image API
# takes. --combine NAME puts all the layers into one picture (a row each), so
# one request paints the same character in all of its animations.

import argparse
import json
import math
import re
import sys
from pathlib import Path

from PIL import Image

KEY = (255, 0, 255)
KEY_TOLERANCE = 90  # max channel distance from the key that counts as background
EDGE = 3            # pixels next to the background whose magenta cast is removed
TARGET_PIXELS = 1_100_000  # the editing models work best around one megapixel


def load_layer(frames_root, spec):
	layer_dir = frames_root / spec
	files = sorted(layer_dir.glob("frame*.png"), key=lambda p: int(re.sub(r"\D", "", p.stem)))
	if not files:
		sys.exit(f"{layer_dir}: no frames")
	return [(spec, f.name, Image.open(f).convert("RGBA")) for f in files]


def pad_to(grid, cells, W, H):
	ox, oy = (W - grid.width) // 2, (H - grid.height) // 2
	padded = Image.new("RGB", (W, H), KEY)
	padded.paste(grid, (ox, oy))
	for c in cells:
		c["x"] += ox
		c["y"] += oy
	return padded


def build(frames, rows_of, name, args, out):
	"""frames: (layer, file, image); rows_of: list of lists of indexes, one per grid row (None: auto)."""
	fw, fh = max(im.width for _, _, im in frames), max(im.height for _, _, im in frames)
	gap = 4  # in frame pixels
	cw, ch = fw + gap, fh + gap
	if rows_of is None:
		cols = max(1, round(math.sqrt(len(frames) * fh / fw)))
		rows_of = [list(range(i, min(i + cols, len(frames)))) for i in range(0, len(frames), cols)]
	cols, rows = max(len(r) for r in rows_of), len(rows_of)
	scale = max(2, int(math.sqrt(TARGET_PIXELS / (cols * cw * rows * ch))))
	grid = Image.new("RGB", (cols * cw * scale, rows * ch * scale), KEY)
	cells = []
	for r, row in enumerate(rows_of):
		for c, i in enumerate(row):
			layer, fname, im = frames[i]
			x = c * cw * scale + gap // 2 * scale
			y = r * ch * scale + gap // 2 * scale
			big = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
			grid.paste(big, (x, y), big)
			cells.append({"layer": layer, "file": fname, "x": x, "y": y, "w": big.width, "h": big.height})
	if args.chatgpt:
		ratio = grid.width / grid.height
		aw, ah = min(((1, 1), (3, 2), (2, 3)), key=lambda a: abs(math.log(ratio * a[1] / a[0])))
		grid = pad_to(grid, cells, max(grid.width, -(-grid.height * aw // ah)), max(grid.height, -(-grid.width * ah // aw)))
	if args.api:
		# OpenAI Image API custom sizes: multiples of 16, at least 655,360 pixels.
		W, H = -(-grid.width // 16) * 16, -(-grid.height // 16) * 16
		while W * H < 655_360:
			W, H = W + 16, H + 16
		grid = pad_to(grid, cells, W, H)
	grid.save(out / f"{name}.png")
	json.dump({"layer": frames[0][0], "scale": scale, "size": grid.size, "cells": cells},
	          open(out / f"{name}.json", "w"), indent=1)
	print(f"{name}: {len(frames)} frames, {cols}x{rows} grid at {scale}x -> {grid.width}x{grid.height}")


def pack(args):
	frames_root, out = Path(args.frames), Path(args.out)
	out.mkdir(parents=True, exist_ok=True)
	specs = args.layers.split()
	if args.combine:  # all layers in one picture, one row per layer
		frames, rows_of = [], []
		for spec in specs:
			layer = load_layer(frames_root, spec)
			rows_of.append(list(range(len(frames), len(frames) + len(layer))))
			frames += layer
		build(frames, rows_of, args.combine, args, out)
	else:
		for spec in specs:
			build(load_layer(frames_root, spec), None, spec.replace("/", "_"), args, out)


def key_out(im):
	"""RGB grid -> RGBA: magenta background transparent. Edge pixels (next to the background)
	are blends of paint and magenta: those that are mostly paint stay, but take the colour of
	the nearest solid pixel inside, so no pink, dark or green rim is left."""
	import numpy as np
	a = np.asarray(im, dtype=np.int16)
	dist = np.max(np.abs(a - np.array(KEY, dtype=np.int16)), axis=2)
	bg = dist <= KEY_TOLERANCE
	near = bg.copy()
	for _ in range(EDGE):  # grow the background by EDGE pixels
		grown = near.copy()
		grown[1:] |= near[:-1]; grown[:-1] |= near[1:]
		grown[:, 1:] |= near[:, :-1]; grown[:, :-1] |= near[:, 1:]
		near = grown
	edge = near & ~bg
	opacity = np.clip((dist - KEY_TOLERANCE) / (255.0 - KEY_TOLERANCE) * 1.6, 0, 1)
	gone = bg | (edge & (opacity < 0.5))
	out = a.copy()
	todo = edge & ~gone  # recolour from the solid inside, ring by ring
	solid = ~near
	for _ in range(EDGE + 2):
		if not todo.any():
			break
		for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0), (1, 1), (1, -1), (-1, 1), (-1, -1)):
			src = np.roll(solid, (dy, dx), axis=(0, 1))
			take = todo & src
			if take.any():
				out[take] = np.roll(out, (dy, dx), axis=(0, 1))[take]
				solid = solid | take
				todo = todo & ~take
	alpha = np.where(gone, 0, 255)
	rgba = np.dstack([np.clip(out, 0, 255), alpha]).astype(np.uint8)
	return Image.fromarray(rgba, "RGBA")


def best_shift(alpha, orig, x, y, w, h, reach):
	"""The offset (dx, dy) within reach whose crop of the keyed answer's alpha
	overlaps the original frame's outline best (intersection over union)."""
	import numpy as np
	target = np.asarray(orig.split()[3].resize((w, h), Image.NEAREST)) > 127
	best, best_score = (0, 0), -1.0
	H, W = alpha.shape
	for dy in range(-reach, reach + 1):
		for dx in range(-reach, reach + 1):
			x0, y0 = x + dx, y + dy
			if x0 < 0 or y0 < 0 or x0 + w > W or y0 + h > H:
				continue
			got = alpha[y0:y0 + h, x0:x0 + w]
			union = np.count_nonzero(got | target)
			score = np.count_nonzero(got & target) / union if union else 0
			if score > best_score:
				best, best_score = (dx, dy), score
	return best


def unpack(args):
	grids, repainted, out = Path(args.grids), Path(args.repainted), Path(args.out)
	for meta_path in sorted(grids.glob("*.json")):
		meta = json.load(open(meta_path))
		src = repainted / (meta_path.stem + ".png")
		if not src.exists():
			print(f"{meta['layer']}: no repainted grid ({src.name}); skipped")
			continue
		im = Image.open(src).convert("RGB")
		if im.size != tuple(meta["size"]):
			im = im.resize(tuple(meta["size"]), Image.LANCZOS)
		im = key_out(im)
		if args.frames:  # realign each frame to the original's outline (models drift by a pixel or two)
			import numpy as np
			alpha = np.asarray(im.split()[3]) > 127
			reach = 2 * meta.get("scale", 4)
		moved = 0
		for c in meta["cells"]:
			layer = c.get("layer", meta["layer"])
			dst = out / layer
			dst.mkdir(parents=True, exist_ok=True)
			x, y = c["x"], c["y"]
			if args.frames:
				orig = Image.open(Path(args.frames) / layer / c["file"]).convert("RGBA")
				dx, dy = best_shift(alpha, orig, x, y, c["w"], c["h"], reach)
				x, y = x + dx, y + dy
				moved += (dx, dy) != (0, 0)
			im.crop((x, y, x + c["w"], y + c["h"])).save(dst / c["file"])
		if args.frames:
			print(f"{meta_path.stem}: {moved} of {len(meta['cells'])} frames realigned to the original outline")
		print(f"{meta_path.stem}: {len(meta['cells'])} frames -> {out}")


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	sub = ap.add_subparsers(dest="cmd", required=True)
	p = sub.add_parser("pack")
	p.add_argument("--frames", required=True)
	p.add_argument("--layers", required=True)
	p.add_argument("--out", required=True)
	p.add_argument("--chatgpt", action="store_true", help="pad to 1:1, 3:2 or 2:3")
	p.add_argument("--api", action="store_true", help="pad to a size the OpenAI Image API takes")
	p.add_argument("--combine", metavar="NAME", help="all layers in one picture NAME.png, a row per layer")
	u = sub.add_parser("unpack")
	u.add_argument("--grids", required=True)
	u.add_argument("--repainted", required=True)
	u.add_argument("--out", required=True)
	u.add_argument("--frames", help="the original frames folder: realign each frame to its outline")
	args = ap.parse_args()
	pack(args) if args.cmd == "pack" else unpack(args)


if __name__ == "__main__":
	main()
