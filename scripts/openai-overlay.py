#!/usr/bin/env python3
# gob2native - repaint animation parts in place on a remastered background
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   openai-overlay.py --name wizard --tot GOB00 --layers "anim30004/layer0 anim30004/layer7 ..."
#                     [--background 30007] [--describe TEXT] [--pack openai-gob01]
#                     [--max-frames 30] [--frames DIR] [--model M] [--quality Q] [--dry-run]
#
# Some characters are painted into a room's background and only their moving
# parts (a hand, eyes, a mouth) are sprites drawn over it. Repainted on their
# own, those parts never match the remastered body around them. This puts
# every distinct frame, enlarged, onto a crop of the pack's remastered
# background exactly where the game draws it, lays the crops out in one
# picture, and asks the model to repaint only the pixel-art parts to match the
# painting around them. Each frame is then cut back out with the game's own
# outline (smoothed), so in the game it sits on the same painting it was
# painted on, and imported with gobexport --import-frames --alpha.
# Everything is kept in ../upscale-openai/overlays/NAME/.

import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageFilter

HERE = Path(__file__).resolve().parent.parent
ROOT = HERE.parent
SCALE = 4
MARGIN = 6  # original pixels of painted surroundings around each frame
GAP = 4

PROMPT = """This image is a contact sheet of crops from one remastered, hand-painted background of the 1990s adventure game Gobliins 2. In every crop, small parts of a character are drawn on top of the painting in old low-resolution pixel art (for example a hand, eyes, a mouth or part of a beard): they are frames of the character's animation. The second image is the whole remastered background, for context.

Repaint only those pixel-art parts in each crop so they match the painted character around them exactly: the same hand-painted style, colours, skin tone, lighting, shading and level of detail, with no pixel steps and no outlines. Keep each part's shape, size, pose and position as the pixel art shows it, so the animation still plays the same. {describe}

Leave everything else in every crop exactly as it is: the painted background around the parts must not change. Keep the grey gaps between the crops. Do not add new elements or text."""


def load(name):
	spec = importlib.util.spec_from_file_location(name.replace("-", "_"), HERE / "scripts" / f"{name}.py")
	mod = importlib.util.module_from_spec(spec)
	spec.loader.exec_module(mod)
	return mod


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--name", required=True)
	ap.add_argument("--tot", required=True)
	ap.add_argument("--layers", required=True, help="layer folders inside the TOT's frames folder")
	ap.add_argument("--background", type=int, help="background picture id (default: the TOT's first background)")
	ap.add_argument("--describe", default="")
	ap.add_argument("--pack", default="openai-gob01")
	ap.add_argument("--max-frames", type=int, default=30)
	ap.add_argument("--frames", default=str(ROOT / "upscale-frames"))
	ap.add_argument("--model", default="gpt-image-2.5-sunburst")
	ap.add_argument("--quality", default="high")
	ap.add_argument("--only", metavar="DIR",
	                help="only the frames present in DIR/<TOT>/<layer>/ (a hand-picked subset)")
	ap.add_argument("--box", nargs=4, type=int, metavar=("X0", "Y0", "X1", "Y1"),
	                help="piece mode: only frames with pieces inside this screen box, and only those pieces are "
	                     "written (straight into the pack's sheets); frames may contain goblins")
	ap.add_argument("--frame-list", default="", help="only these frames: layer:frame,frame ... (with --box)")
	ap.add_argument("--dry-run", action="store_true")
	args = ap.parse_args()

	remaster = load("openai-remaster")
	packs, cache = remaster.gob_paths()
	pack = packs / args.pack
	index = json.load(open(cache / "export" / "index.json"))
	bgs = [e for e in index["images"] if e["tot"] == args.tot and e["kind"] == "background"
	       and (args.background is None or e["resource"] == args.background)]
	bg_path = next((pack / e["image"] for e in bgs if (pack / e["image"]).exists()), None)
	if not bg_path:
		sys.exit(f"no remastered background of {args.tot} in {pack}")
	bg = Image.open(bg_path).convert("RGB")
	frames_root = Path(args.frames) / args.tot
	keep = ROOT / "upscale-openai" / "overlays" / args.name
	keep.mkdir(parents=True, exist_ok=True)

	# Goblin body pieces (everything in anim0): frames using any are left alone, so the
	# goblins stay untouched (the character's pieces in them are mostly shared anyway).
	goblin = set()
	for lj in (frames_root / "anim0").glob("layer*/layer.json"):
		for f in json.load(open(lj))["frames"]:
			goblin |= {(p["sheet"], p["piece"]) for p in f["pieces"]}
	skipped = 0
	frame_list = {}
	for item in args.frame_list.split():
		layer, _, nums = item.partition(":")
		frame_list[layer] = {int(n) for n in nums.split(",") if n}
	# Distinct frames with their screen rectangles; every place each is used.
	unique, places = [], {}
	for spec in args.layers.split():
		L = json.load(open(frames_root / spec / "layer.json"))
		px, py = L["position"]
		for f in L["frames"]:
			fp = frames_root / spec / f["file"]
			if not fp.exists() or (args.only and not (Path(args.only) / args.tot / spec / f["file"]).exists()):
				continue
			im = Image.open(fp).convert("RGBA")
			if im.getbbox() is None:
				continue
			if args.box:
				bx0, by0, bx1, by1 = args.box
				fx, fy = px + f.get("offset", [0, 0])[0], py + f.get("offset", [0, 0])[1]
				inside = [p for p in f["pieces"] if bx0 <= fx + p["dest"][0] and fx + p["dest"][0] + p["source"][2] - p["source"][0] < bx1
				          and by0 <= fy + p["dest"][1] and fy + p["dest"][1] + p["source"][3] - p["source"][1] < by1]
				wanted = frame_list.get(spec)
				if not inside or (wanted is not None and f["frame"] not in wanted):
					continue
			elif {(p["sheet"], p["piece"]) for p in f["pieces"]} & goblin:
				skipped += 1
				continue
			ox, oy = f.get("offset", [0, 0])
			rect = (px + ox, py + oy)
			h = hashlib.md5(im.tobytes() + str(im.size).encode() + str(rect).encode()).hexdigest()
			if h not in places:
				places[h] = []
				unique.append((h, spec, f["file"], im, rect, f))
			places[h].append((spec, f["file"]))
	batches = [unique[i:i + args.max_frames] for i in range(0, len(unique), args.max_frames)]
	print(f"{args.name}: {sum(len(v) for v in places.values())} frames, {len(unique)} distinct, {len(batches)} request(s); "
	      f"{skipped} frame(s) with goblins left alone")

	out_dir = keep / "frames" / args.tot
	for b, batch in enumerate(batches, 1):
		name = f"batch{b:02d}"
		# Cells: background crop around the frame with the pixel-art frame on top, at 4x.
		cells = []
		for h, spec, fname, im, (sx, sy), _ in batch:
			x0, y0 = max(0, sx - MARGIN), max(0, sy - MARGIN)
			x1 = min(bg.width // SCALE, sx + im.width + MARGIN)
			y1 = min(bg.height // SCALE, sy + im.height + MARGIN)
			crop = bg.crop((x0 * SCALE, y0 * SCALE, x1 * SCALE, y1 * SCALE))
			big = im.resize((im.width * SCALE, im.height * SCALE), Image.NEAREST)
			crop.paste(big, ((sx - x0) * SCALE, (sy - y0) * SCALE), big)
			cells.append((h, crop, ((sx - x0) * SCALE, (sy - y0) * SCALE), big.size))
		cw = max(c[1].width for c in cells) + GAP * SCALE
		ch = max(c[1].height for c in cells) + GAP * SCALE
		cols = max(1, round((len(cells) * ch / cw) ** 0.5 * 1.2))
		rows = -(-len(cells) // cols)
		W, H = remaster.api_size(cols * cw, rows * ch)
		sheet = Image.new("RGB", (W, H), (128, 128, 128))
		layout = []
		for i, (h, crop, (fx, fy), (fw, fh)) in enumerate(cells):
			x, y = (i % cols) * cw + GAP * SCALE // 2, (i // cols) * ch + GAP * SCALE // 2
			sheet.paste(crop, (x, y))
			layout.append({"hash": h, "x": x + fx, "y": y + fy, "w": fw, "h": fh})
		bdir = keep / name
		bdir.mkdir(exist_ok=True)
		sheet.save(bdir / f"{name}.png")
		(bdir / "prompt.txt").write_text(PROMPT.format(describe=args.describe))
		answer = bdir / "answer.png"
		if not answer.exists():
			cmd = [sys.executable, str(HERE / "scripts" / "openai-remaster.py"), "--image", str(bdir / f"{name}.png"),
			       "--out", str(answer), "--prompt-file", str(bdir / "prompt.txt"), "--ref", str(bg_path),
			       "--model", args.model, "--quality", args.quality] + (["--dry-run"] if args.dry_run else [])
			if subprocess.run(cmd).returncode != 0:
				sys.exit(f"{name}: request failed")
			if args.dry_run:
				continue
		else:
			print(f"{name}: already answered")
		res = Image.open(answer).convert("RGB")
		if res.size != sheet.size:
			res = res.resize(sheet.size, Image.LANCZOS)
		by_hash = {u[0]: u for u in batch}
		if args.box:  # write the pieces inside the box straight into the pack's sheets
			sheets = {}
			bx0, by0, bx1, by1 = args.box
			for c in layout:
				_, spec, fname, im, (sx, sy), f = by_hash[c["hash"]]
				painted = res.crop((c["x"], c["y"], c["x"] + c["w"], c["y"] + c["h"]))
				ox, oy = f.get("offset", [0, 0])
				L = json.load(open(frames_root / spec / "layer.json"))
				for p in f["pieces"]:
					l, t, r, b = p["source"]
					dx, dy = L["position"][0] + ox + p["dest"][0], L["position"][1] + oy + p["dest"][1]
					if not (bx0 <= dx and dx + r - l < bx1 and by0 <= dy and dy + b - t < by1):
						continue
					sid = p["sheet"]
					if sid not in sheets:
						sp = pack / args.tot / f"{sid}.png"
						if not sp.exists():
							print(f"  {args.tot}/{sid} not in the pack; piece {p['piece']} skipped")
							continue
						sheets[sid] = (Image.open(sp).convert("RGB"), Image.open(sp.with_name(f"{sid}.mask.png")).convert("L"),
						               Image.open(cache / "export" / args.tot / f"{sid}.mask.png").convert("L"))
					sheet, smask, omask = sheets[sid]
					px0, py0 = (dx - sx) * SCALE, (dy - sy) * SCALE  # the piece's place in the painted crop
					piece_img = painted.crop((px0, py0, px0 + (r - l + 1) * SCALE, py0 + (b - t + 1) * SCALE))
					sheet.paste(piece_img, (l * SCALE, t * SCALE))
					m = omask.crop((l, t, r + 1, b + 1)).resize(((r - l + 1) * SCALE, (b - t + 1) * SCALE), Image.NEAREST)
					m = m.filter(ImageFilter.GaussianBlur(SCALE * 0.6)).point(lambda v: 255 if v >= 128 else 0)
					smask.paste(m, (l * SCALE, t * SCALE))
			for sid, (sheet, smask, _) in sheets.items():
				sheet.save(pack / args.tot / f"{sid}.png")
				smask.save(pack / args.tot / f"{sid}.mask.png")
				print(f"  {args.tot}/{sid}: pieces inside the box written")
			continue
		for c in layout:
			_, spec, fname, im, _, _ = by_hash[c["hash"]]
			painted = res.crop((c["x"], c["y"], c["x"] + c["w"], c["y"] + c["h"]))
			# The game's outline, smoothed (as for sprite sheets), decides what is drawn.
			mask = im.split()[3].resize((c["w"], c["h"]), Image.NEAREST).filter(ImageFilter.GaussianBlur(SCALE * 0.6))
			mask = mask.point(lambda v: 255 if v >= 128 else 0)
			frame = painted.convert("RGBA")
			frame.putalpha(mask)
			for pspec, pname in places[c["hash"]]:
				(out_dir / pspec).mkdir(parents=True, exist_ok=True)
				frame.save(out_dir / pspec / pname)
	if args.dry_run or args.box:
		return
	subprocess.run([str(HERE / "build" / "gobexport"), str(ROOT / "gobliins2"), "--import-frames", str(keep / "frames"),
	                str(cache / "export"), str(pack), "--alpha", "--scale", str(SCALE)])


if __name__ == "__main__":
	main()
