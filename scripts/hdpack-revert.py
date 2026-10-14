#!/usr/bin/env python3
# gob2native - put sheet pieces of chosen animations in an HD pack back to the original art
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   hdpack-revert.py --pack NAME --tot GOB00 --layers "anim30004/layer36 anim30004/layer37"
#                    [--keep-layers "anim30004/layer35:0-5"] [--frames DIR] [--dry-run]
#
# For every piece the frames of the given layers are made of, the pack's sheet
# gets the original pixels back there (enlarged with nearest-neighbour to the
# pack's scale) and its mask the original outline, so those animations look as
# in the original game while the rest of the pack stays as it is. Pieces also
# used by --keep-layers (optionally only some frames: layer:first-last) are
# left alone. A piece shared with another animation changes there too; the
# script lists those animations.

import argparse
import json
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent.parent
ROOT = HERE.parent


def gob_paths():
	import importlib.util
	spec = importlib.util.spec_from_file_location("orm", HERE / "scripts" / "openai-remaster.py")
	mod = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(mod)
	return mod.gob_paths()


def pieces_of(frames_root, spec):
	name, _, rng = spec.partition(":")
	L = json.load(open(frames_root / name / "layer.json"))
	lo, hi = (int(x) for x in rng.split("-")) if rng else (0, 10 ** 9)
	out = {}
	for f in L["frames"]:
		if lo <= f["frame"] <= hi:
			for p in f["pieces"]:
				out[(p["sheet"], p["piece"])] = tuple(p["source"])
	return out


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--pack", required=True)
	ap.add_argument("--tot", required=True)
	ap.add_argument("--layers", required=True)
	ap.add_argument("--keep-layers", default="")
	ap.add_argument("--frames", default=str(ROOT / "upscale-frames"))
	ap.add_argument("--dry-run", action="store_true")
	args = ap.parse_args()

	packs, cache = gob_paths()
	pack = packs / args.pack / args.tot
	export = cache / "export" / args.tot
	frames_root = Path(args.frames) / args.tot
	revert = {}
	for spec in args.layers.split():
		revert.update(pieces_of(frames_root, spec))
	for spec in args.keep_layers.split():
		for k in pieces_of(frames_root, spec):
			revert.pop(k, None)
	# Which other animations use these pieces (they change too).
	others = set()
	targets = {s.partition(":")[0] for s in args.layers.split()}
	for lj in frames_root.glob("anim*/layer*/layer.json"):
		name = str(lj.parent.relative_to(frames_root))
		if name in targets:
			continue
		used = {(p["sheet"], p["piece"]) for f in json.load(open(lj))["frames"] for p in f["pieces"]}
		if used & set(revert):
			others.add(name)

	by_sheet = {}
	for (sheet, piece), src in revert.items():
		by_sheet.setdefault(sheet, []).append((piece, src))
	for sheet, items in sorted(by_sheet.items()):
		dst = pack / f"{sheet}.png"
		if not dst.exists():
			print(f"{args.tot}/{sheet}: not in the pack, nothing to revert")
			continue
		hd = Image.open(dst).convert("RGB")
		orig = Image.open(export / f"{sheet}.png").convert("RGB")
		scale = hd.width // orig.width
		big = orig.resize((orig.width * scale, orig.height * scale), Image.NEAREST)
		mask_path = dst.with_name(f"{sheet}.mask.png")
		omask = export / f"{sheet}.mask.png"
		mask = Image.open(mask_path).convert("L") if mask_path.exists() else None
		bigmask = Image.open(omask).convert("L").resize(big.size, Image.NEAREST) if omask.exists() else None
		for piece, (l, t, r, b) in items:
			box = (l * scale, t * scale, (r + 1) * scale, (b + 1) * scale)
			hd.paste(big.crop(box), box[:2])
			if mask is not None and bigmask is not None:
				mask.paste(bigmask.crop(box), box[:2])
		print(f"{args.tot}/{sheet}: {len(items)} piece(s) back to the original")
		if not args.dry_run:
			hd.save(dst)
			if mask is not None:
				mask.save(mask_path)
	if others:
		print("also used (and so changed) by: " + " ".join(sorted(others)))


if __name__ == "__main__":
	main()
