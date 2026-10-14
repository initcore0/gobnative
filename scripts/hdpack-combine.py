#!/usr/bin/env python3
# gob2native - combine two HD packs by picture kind
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Painted-art models tend to suit the room backgrounds, pixel-art models the
# sprites and animation sheets. This builds a pack taking backgrounds and
# overlays from one pack and everything else from another, using the kinds
# recorded in gobexport's index.json.
#
# Usage: hdpack-combine.py --index EXPORT/index.json --backgrounds PACK_A
#                          [--sprites PACK_B] --out PACK_C
#
# Without --sprites the result holds only backgrounds and overlays; the game
# then draws sprites, animations and cursors from the original pictures.

import argparse
import json
import shutil
import sys
from pathlib import Path

BACKGROUND_KINDS = {"background", "overlay"}


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--index", required=True, help="index.json of a gobexport folder")
	ap.add_argument("--backgrounds", required=True, help="pack used for backgrounds and overlays")
	ap.add_argument("--sprites", help="pack used for everything else (omit for a backgrounds-only pack)")
	ap.add_argument("--out", required=True)
	args = ap.parse_args()

	index = json.load(open(args.index))
	bg, out = Path(args.backgrounds), Path(args.out)
	sp = Path(args.sprites) if args.sprites else None
	if out.exists():
		shutil.rmtree(out)

	counts = {"backgrounds": 0, "sprites": 0, "missing": 0}
	for entry in index["images"]:
		rel = entry["image"]
		from_bg = entry.get("kind") in BACKGROUND_KINDS
		if not from_bg and sp is None:
			continue
		src = (bg if from_bg else sp) / rel
		if not src.exists():
			counts["missing"] += 1
			continue
		dst = out / rel
		dst.parent.mkdir(parents=True, exist_ok=True)
		shutil.copy2(src, dst)
		counts["backgrounds" if from_bg else "sprites"] += 1

	print(f"{out}: {counts['backgrounds']} from {bg.name}, "
	      f"{counts['sprites']} from {sp.name if sp else '(originals)'}, {counts['missing']} missing")
	return 0 if counts["missing"] == 0 else 1


if __name__ == "__main__":
	sys.exit(main())
