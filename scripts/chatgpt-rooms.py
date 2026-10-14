#!/usr/bin/env python3
# gob2native - repaint room backgrounds by hand in ChatGPT (or any image app)
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   chatgpt-rooms.py prepare [--tots "GOB01 GOB02"] [--dir DIR]
#     writes every room background (or those of the given script files) to
#     DIR/to-chatgpt/<TOT>_<id>.png, enlarged 4x and letterboxed to 3:2 with
#     black bars (ChatGPT returns 1536x1024, so nothing gets stretched), plus
#     DIR/PROMPT.txt
#
#   chatgpt-rooms.py import [--dir DIR] [--pack NAME]
#     takes the pictures saved into DIR/from-chatgpt/ under the same names,
#     scales them back, cuts the padding off and installs them at exactly 4x
#     into HD pack NAME (default chatgpt-rooms). Leave out rooms you don't
#     like; import again any time to add or replace some.
#
# The pack holds only backgrounds, so sprites keep the original art (or a
# sprite filter: overlay > Video). Needs Pillow.

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageOps

HERE = Path(__file__).resolve().parent.parent
ROOT = HERE.parent


def gob_paths():
	"""Gobliins 2's HD pack folder and the export cache, as scripts/gobpaths.sh
	finds them: gobnative/Gobliins2 since the rename, the old gob2native one
	while it is still a real folder (before gobnative's first start moves it)."""
	root = Path(os.environ.get("GOB_PREF_ROOT") or Path.home() / "Library/Application Support/gobnative")
	old = root.parent / "gob2native/Gobliins2"
	if os.environ.get("PACKS_DIR"):
		packs = Path(os.environ["PACKS_DIR"])
	elif (root / "Gobliins2").is_dir() or not old.is_dir():
		packs = root / "Gobliins2/HDPacks"
	else:
		packs = old / "HDPacks"
	cache = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / "Library/Caches")
	cache = cache / ("gobnative" if (cache / "gobnative").is_dir() or not (cache / "gob2native").is_dir() else "gob2native")
	return packs, cache


PACKS, _CACHE = gob_paths()
EXPORT = _CACHE / "export"
SCALE = 4

PROMPT = """This is a background from a 1990s point-and-click adventure game, at low resolution. Repaint it as a detailed, high-resolution hand-painted illustration: a faithful remaster, not a new picture.

Rules:
- Same framing and size. Keep the exact composition and perspective: every building, path, step, wall, plant and object stays exactly where it is, with the same size and outline. Characters walk on this picture, so nothing may move.
- Keep the colours and the time of day.
- Add painterly texture, soft lighting and fine detail.
- No new objects, no people or characters, no text.
- Keep the black letterbox bars at the top and bottom exactly as they are, plain black.
"""


def prepare(args):
	out = Path(args.dir)
	(out / "to-chatgpt").mkdir(parents=True, exist_ok=True)
	(out / "from-chatgpt").mkdir(parents=True, exist_ok=True)
	index = json.load(open(EXPORT / "index.json"))
	tots = set(args.tots.split()) if args.tots else None
	layout = {}
	for e in index["images"]:
		if e["kind"] != "background" or "same_as" in e or (tots and e["tot"] not in tots):
			continue
		im = Image.open(EXPORT / e["image"]).convert("RGB")
		w, h = im.width * SCALE, im.height * SCALE
		big = im.resize((w, h), Image.NEAREST)
		W, H = max(w, h * 3 // 2), max(h, -(-w * 2 // 3))
		ox, oy = (W - w) // 2, (H - h) // 2
		padded = ImageOps.expand(big, (ox, oy, W - w - ox, H - h - oy), fill=(0, 0, 0))
		name = f"{e['tot']}_{e['resource']}.png"
		padded.save(out / "to-chatgpt" / name)
		layout[name] = {"image": e["image"], "size": [W, H], "crop": [ox, oy, ox + w, oy + h]}
	json.dump(layout, open(out / "layout.json", "w"), indent=1)
	(out / "PROMPT.txt").write_text(PROMPT)
	print(f"{len(layout)} background(s) in {out / 'to-chatgpt'}")
	print(f"Upload each with the text of PROMPT.txt, save the answer into {out / 'from-chatgpt'} "
	      "under the same file name, then run: scripts/chatgpt-rooms.py import")


def do_import(args):
	d = Path(args.dir)
	layout = json.load(open(d / "layout.json"))
	pack = PACKS / args.pack
	done = 0
	for src in sorted((d / "from-chatgpt").iterdir()):
		if src.suffix.lower() not in (".png", ".jpg", ".jpeg", ".webp"):
			continue
		entry = layout.get(src.stem + ".png")
		if not entry:
			print(f"skip {src.name}: not one of the prepared pictures")
			continue
		im = Image.open(src).convert("RGB")
		W, H = entry["size"]
		if abs(im.width / im.height - W / H) > 0.03:
			print(f"  {src.name}: aspect {im.width / im.height:.2f}, expected {W / H:.2f}: stretched, check it lines up")
		im = im.resize((W, H), Image.LANCZOS).crop(tuple(entry["crop"]))
		dst = pack / entry["image"]
		dst.parent.mkdir(parents=True, exist_ok=True)
		im.save(dst)
		print(f"{src.name} -> {entry['image']} at {SCALE}x ({im.width}x{im.height})")
		done += 1
	print(f"{done} background(s) in {pack}")
	gobexport = HERE / "build" / "gobexport"
	if done and gobexport.exists():
		subprocess.run([str(gobexport), str(ROOT / "gobliins2"), "--check-pack", str(pack)])
	print(f'In the game, open the overlay (`) > Video > HD pack and pick "{args.pack}".')


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	sub = ap.add_subparsers(dest="cmd", required=True)
	p = sub.add_parser("prepare")
	p.add_argument("--tots")
	p.add_argument("--dir", default=str(ROOT / "upscale-rooms"))
	i = sub.add_parser("import")
	i.add_argument("--dir", default=str(ROOT / "upscale-rooms"))
	i.add_argument("--pack", default="chatgpt-rooms")
	args = ap.parse_args()
	prepare(args) if args.cmd == "prepare" else do_import(args)


if __name__ == "__main__":
	sys.exit(main())
