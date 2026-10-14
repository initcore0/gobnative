#!/usr/bin/env python3
# gob2native - side-by-side comparison sheets of HD packs
# SPDX-License-Identifier: GPL-3.0-or-later
#
# For a few pictures of a gobexport folder, cuts the same region out of every
# pack (plus a nearest-neighbour baseline of the original) and lays them out
# as a labelled grid, one sheet per picture.
#
# Usage: compare.py --export EXPORT --packs DIR [--pick KIND:N ...] [--crop W H]
#                   --out OUTDIR [--scale 8]

import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw


def detail_crop(img, w, h):
	"""Top-left corner of the w x h window with the most edges, so the crop
	shows outlines and texture rather than flat sky."""
	gray = img.convert("L")
	px = gray.load()
	best, best_xy = -1, (0, 0)
	for y in range(0, max(1, img.height - h + 1), max(1, h // 4)):
		for x in range(0, max(1, img.width - w + 1), max(1, w // 4)):
			score = 0
			for yy in range(y, min(y + h, img.height - 1), 2):
				for xx in range(x, min(x + w, img.width - 1), 2):
					score += abs(px[xx, yy] - px[xx + 1, yy]) + abs(px[xx, yy] - px[xx, yy + 1])
			if score > best:
				best, best_xy = score, (x, y)
	return best_xy


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--export", required=True)
	ap.add_argument("--packs", required=True, help="directory containing one folder per pack")
	ap.add_argument("--pick", nargs="*", default=["background:2", "animation:2"],
	                help="KIND:N = the N largest pictures of that kind")
	ap.add_argument("--crop", nargs=2, type=int, default=[64, 40], help="crop size in original pixels")
	ap.add_argument("--scale", type=int, default=8)
	ap.add_argument("--out", required=True)
	args = ap.parse_args()

	export, packs_dir, out = Path(args.export), Path(args.packs), Path(args.out)
	out.mkdir(parents=True, exist_ok=True)
	index = json.load(open(export / "index.json"))
	packs = sorted(p for p in packs_dir.iterdir() if p.is_dir() and not p.name.startswith("_"))
	cw, ch = args.crop
	s = args.scale

	chosen = []
	for spec in args.pick:
		kind, n = spec.split(":")
		cands = [e for e in index["images"] if e.get("kind") == kind]
		cands.sort(key=lambda e: e["width"] * e["height"], reverse=True)
		chosen += cands[: int(n)]

	for entry in chosen:
		rel = entry["image"]
		orig = Image.open(export / rel).convert("RGB")
		x, y = detail_crop(orig, min(cw, orig.width), min(ch, orig.height))
		box = (x, y, x + min(cw, orig.width), y + min(ch, orig.height))

		tiles = [("original (nearest)", orig.crop(box).resize(((box[2] - box[0]) * s, (box[3] - box[1]) * s),
		                                                       Image.NEAREST))]
		for pack in packs:
			p = pack / rel
			if not p.exists():
				continue
			im = Image.open(p).convert("RGB")
			f = im.width // orig.width
			tile = im.crop(tuple(v * f for v in box))
			if f != s:
				tile = tile.resize(((box[2] - box[0]) * s, (box[3] - box[1]) * s), Image.LANCZOS)
			tiles.append((pack.name, tile))

		tw, th = tiles[0][1].size
		cols = min(3, len(tiles))
		rows = math.ceil(len(tiles) / cols)
		label_h = 22
		sheet = Image.new("RGB", (cols * tw + (cols + 1) * 8, rows * (th + label_h) + (rows + 1) * 8), (24, 24, 28))
		draw = ImageDraw.Draw(sheet)
		for i, (name, tile) in enumerate(tiles):
			cx = 8 + (i % cols) * (tw + 8)
			cy = 8 + (i // cols) * (th + label_h + 8)
			draw.text((cx + 4, cy + 4), name, fill=(235, 235, 235))
			sheet.paste(tile, (cx, cy + label_h))
		name = rel.replace("/", "_").replace(".png", "")
		sheet.save(out / f"compare_{name}.png")
		print("wrote", out / f"compare_{name}.png", f"({len(tiles)} tiles)")


if __name__ == "__main__":
	main()
