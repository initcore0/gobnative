#!/usr/bin/env python3
# gob2native - repaint all animations of one character with the OpenAI Image API
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   openai-character.py --name green-goblin --layers "GOB01/anim0/layer0 GOB01/anim0/layer1 ..."
#                       [--objects]  (props and secondary characters: no character reference)
#                       [--describe "what the character looks like"] [--pack openai-goblins]
#                       [--from-pack openai-gob01] [--max-frames 20] [--frames DIR]
#                       [--model M] [--quality Q] [--dry-run]
#
# Takes every frame of the given layers (from a gobexport --frames folder),
# paints each distinct frame once (identical frames in other animations get
# the same result), in batches of about --max-frames frames laid out in one
# picture. Every batch goes out with the room's remastered background (style)
# and, from the second batch on, the first batch's answer as the character
# reference, so all batches show the same character. Each answer is keyed
# (magenta transparent, edges despilled), every frame realigned to its
# original outline, and all frames are imported with gobexport --import-frames
# --alpha into PACK (a new PACK starts as a copy of --from-pack). Requests
# already answered are not sent again (delete the batch's answer to redo it).
# Everything is kept in ../upscale-openai/characters/NAME/.

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

from PIL import Image

HERE = Path(__file__).resolve().parent.parent
ROOT = HERE.parent


def load(name):
	spec = importlib.util.spec_from_file_location(name.replace("-", "_"), HERE / "scripts" / f"{name}.py")
	mod = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(mod)
	return mod


PROMPT = """The first image is a sprite sheet: animation frames of one cartoon character from the 1990s point-and-click adventure game Gobliins 2, enlarged from low-resolution pixel art, on a flat magenta background. The second image is a remastered background from the same game: use its hand-painted style, lighting and level of detail.{charref}

Redraw every frame of the first image as high-quality hand-painted cartoon art in that style.

Keep exactly: the image size; every frame's position, size and silhouette; the pose in every frame, so the animations still play the same; and the character's design and colours: head shape, eyes, nose, ears, mouth, hair, clothes, proportions. It must be the same character in every frame. {describe}

Keep the background pure flat magenta (#FF00FF) everywhere outside the character, with no shadows, ground, outlines or gradients on it. Do not add new elements, text or frame borders."""

OBJECTS_PROMPT = """The first image is a sprite sheet: animation frames of the props and secondary characters of the 1990s point-and-click adventure game Gobliins 2 (objects such as bottles, keys, food, flowers, fire, animals and townsfolk), enlarged from low-resolution pixel art, on a flat magenta background. The second image is a remastered background from the same game: use its hand-painted style, lighting and level of detail.

Redraw every frame of the first image as high-quality hand-painted cartoon art in that style.

Keep exactly: the image size; every frame's position, size and silhouette; the pose in every frame, so the animations still play the same; and the design and colours of every object and character as the original shows them: shapes, faces (eyes, nose, ears, mouth), hair, clothes, proportions. Do not reinterpret details: an ear stays an ear, a small nose stays small, an object keeps its exact shape. Frames of the same object or character must match each other. {describe}

Keep the background pure flat magenta (#FF00FF) everywhere outside the objects, with no shadows, ground, outlines or gradients on it. Do not add new elements, text or frame borders."""

ITEMS_PROMPT = """The first image is a sprite sheet: animation frames of small items from the 1990s point-and-click adventure game Gobliins 2 (bottles, keys, food, flowers, fire, stones), enlarged from low-resolution pixel art, on a flat magenta background. The second image is a remastered background from the same game: paint the items so they look like part of that painting.

Redraw every frame of the first image as a hand-painted item in the background's style: soft painterly shading, natural highlights and the same soft edges as the objects painted in the background. Do not draw black outlines or dark ink lines around the items: the dark border in the pixel art is only a pixel-art edge, not part of the design.

Keep exactly: the image size; every frame's position, size and silhouette; each item's shape, proportions and colours as the original shows them (the same hues, not brighter or different ones); and the animation, so frames of the same item still play the same. {describe}

Keep the background pure flat magenta (#FF00FF) everywhere outside the items, with no shadows, ground, glow or gradients on it. Do not add new elements, text or frame borders."""

CHARREF = """ The third image shows this same character already redrawn in that style: draw him exactly the same way (same face, proportions, colours, shading and level of detail)."""


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--name", required=True)
	ap.add_argument("--layers", required=True)
	ap.add_argument("--describe", default="")
	ap.add_argument("--pack", default="openai-goblins")
	ap.add_argument("--from-pack", default="openai-gob01")
	ap.add_argument("--style", help="style reference (default: the room's background in --from-pack)")
	ap.add_argument("--max-frames", type=int, default=20)
	ap.add_argument("--frames", default=str(ROOT / "upscale-gob01" / "frames"))
	ap.add_argument("--model", default="gpt-image-2.5-sunburst")
	ap.add_argument("--quality", default="high")
	ap.add_argument("--objects", action="store_true",
	                help="the layers are props and secondary characters, not one character: no character reference")
	ap.add_argument("--items", action="store_true",
	                help="the layers are small items: painted like the background, without outlines")
	ap.add_argument("--dry-run", action="store_true")
	args = ap.parse_args()

	grid = load("frames-grid")
	remaster = load("openai-remaster")
	packs, cache = remaster.gob_paths()
	frames_root = Path(args.frames)
	keep = ROOT / "upscale-openai" / "characters" / args.name
	keep.mkdir(parents=True, exist_ok=True)

	# Distinct frames, in layer order; each with every place it is used.
	unique, places = [], {}
	for spec in args.layers.split():
		for layer, fname, im in grid.load_layer(frames_root, spec):
			h = hashlib.md5(im.tobytes() + str(im.size).encode()).hexdigest()
			if h not in places:
				places[h] = []
				unique.append((h, layer, fname, im))
			places[h].append((layer, fname))
	total = sum(len(v) for v in places.values())
	batches = [unique[i:i + args.max_frames] for i in range(0, len(unique), args.max_frames)]
	print(f"{args.name}: {total} frames in {len(args.layers.split())} layers, {len(unique)} distinct, "
	      f"{len(batches)} request(s)")

	tot = args.layers.split()[0].split("/")[0]
	style = Path(args.style) if args.style else None
	if not style:
		index = json.load(open(cache / "export" / "index.json"))
		for e in index["images"]:
			if e["tot"] == tot and e["kind"] == "background" and (packs / args.from_pack / e["image"]).exists():
				style = packs / args.from_pack / e["image"]
				break
	if not style or not style.exists():
		sys.exit("no style reference: give --style")

	out_frames = keep / "frames"
	first_answer = None
	for b, batch in enumerate(batches, 1):
		name = f"batch{b:02d}"
		bdir = keep / name
		bdir.mkdir(exist_ok=True)
		ns = SimpleNamespace(chatgpt=False, api=True)
		grid.build([(layer, fname, im) for _, layer, fname, im in batch], None, name, ns, bdir)
		answer = bdir / "answer.png"
		if args.items:
			refs = [str(style)]
			prompt = ITEMS_PROMPT.format(describe=args.describe)
		elif args.objects:
			refs = [str(style)]
			prompt = OBJECTS_PROMPT.format(describe=args.describe)
		else:
			refs = [str(style)] + ([str(first_answer)] if first_answer else [])
			prompt = PROMPT.format(charref=CHARREF if first_answer else "", describe=args.describe)
		(bdir / "prompt.txt").write_text(prompt)
		if not answer.exists():
			cmd = [sys.executable, str(HERE / "scripts" / "openai-remaster.py"), "--image", str(bdir / f"{name}.png"),
			       "--out", str(answer), "--prompt-file", str(bdir / "prompt.txt"), "--model", args.model,
			       "--quality", args.quality] + sum([["--ref", r] for r in refs], []) + (["--dry-run"] if args.dry_run else [])
			if subprocess.run(cmd).returncode != 0:
				sys.exit(f"{name}: request failed")
			if args.dry_run:
				continue
		else:
			print(f"{name}: already answered")
		if first_answer is None:
			first_answer = answer
		# Cut, key and realign; copy each frame to every place it is used.
		meta = json.load(open(bdir / f"{name}.json"))
		im = Image.open(answer).convert("RGB")
		if im.size != tuple(meta["size"]):
			im = im.resize(tuple(meta["size"]), Image.LANCZOS)
		keyed = grid.key_out(im)
		import numpy as np
		alpha = np.asarray(keyed.split()[3]) > 127
		reach = 2 * meta.get("scale", 4)
		by_place = {(layer, fname): h for h, layer, fname, _ in batch}
		for c in meta["cells"]:
			orig = Image.open(frames_root / c["layer"] / c["file"]).convert("RGBA")
			dx, dy = grid.best_shift(alpha, orig, c["x"], c["y"], c["w"], c["h"], reach)
			x, y = c["x"] + dx, c["y"] + dy
			frame = keyed.crop((x, y, x + c["w"], y + c["h"]))
			for layer, fname in places[by_place[(c["layer"], c["file"])]]:
				(out_frames / layer).mkdir(parents=True, exist_ok=True)
				frame.save(out_frames / layer / fname)
	if args.dry_run:
		return

	dest = packs / args.pack
	if not dest.exists() and (packs / args.from_pack).exists():
		shutil.copytree(packs / args.from_pack, dest)
		print(f"new pack {args.pack}, started from {args.from_pack}")
	dest.mkdir(parents=True, exist_ok=True)
	subprocess.run([str(HERE / "build" / "gobexport"), str(ROOT / "gobliins2"), "--import-frames", str(out_frames),
	                str(cache / "export"), str(dest), "--alpha", "--scale", "4"])
	print(f'In the game: overlay (`) > Video > HD pack "{args.pack}", pack scope "all".')


if __name__ == "__main__":
	main()
