#!/usr/bin/env python3
# gob2native - fit externally upscaled pictures into an HD pack
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Image models return whatever size they like. An HD pack needs every picture
# at exactly 1x, 2x, 3x, 4x or 8x its original size. For each image in OUTPUT
# (same relative path as in the gobexport folder), this picks the nearest
# valid scale, resizes to the exact size and writes it into the pack.
#
# Usage: fit-to-pack.py --export EXPORT --output OUTPUT --pack PACK [--scale N]
#
# Needs Pillow (python3 -m pip install pillow).

import argparse
import sys
from pathlib import Path

try:
	from PIL import Image
except ImportError:
	sys.exit("Pillow is required: python3 -m pip install pillow")

SCALES = (1, 2, 3, 4, 8)


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--export", required=True, help="gobexport folder with the originals")
	ap.add_argument("--output", required=True, help="folder with your upscaled images")
	ap.add_argument("--pack", required=True, help="HD pack folder to write into")
	ap.add_argument("--scale", type=int, choices=SCALES, help="force this scale for every image")
	args = ap.parse_args()

	export, output, pack = Path(args.export), Path(args.output), Path(args.pack)
	done = 0
	for src in sorted(output.rglob("*")):
		if src.suffix.lower() not in (".png", ".jpg", ".jpeg", ".webp") or src.name.endswith(".mask.png"):
			continue
		rel = src.relative_to(output).with_suffix(".png")
		orig_path = export / rel
		if not orig_path.exists():
			print(f"skip {rel}: no original of that name in the export")
			continue
		orig = Image.open(orig_path)
		im = Image.open(src).convert("RGB")
		scale = args.scale or min(SCALES, key=lambda s: abs(orig.width * s - im.width))
		target = (orig.width * scale, orig.height * scale)

		aspect_in = im.width / im.height
		aspect_orig = orig.width / orig.height
		note = ""
		if abs(aspect_in / aspect_orig - 1) > 0.03:
			note = f"  (aspect {aspect_in:.2f} vs original {aspect_orig:.2f}: stretched to fit; " \
			       "check the result lines up)"
		if im.size != target:
			im = im.resize(target, Image.LANCZOS)
		dst = pack / rel
		dst.parent.mkdir(parents=True, exist_ok=True)
		im.save(dst)
		print(f"{rel}: {src.name} {Image.open(src).size[0]}x{Image.open(src).size[1]} -> {scale}x "
		      f"{target[0]}x{target[1]}{note}")
		done += 1
	print(f"{done} picture(s) written to {pack}")


if __name__ == "__main__":
	main()
