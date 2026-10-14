#!/usr/bin/env python3
# gob2native - repaint pictures with the OpenAI Image API into an HD pack
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   openai-remaster.py [--tots "GOB01 ..."] [--ids "30007 ..."] [--kinds background]
#                      [--scale 4|8] [--model gpt-image-2.5-sunburst] [--quality high]
#                      [--prompt TEXT | --prompt-file FILE] [--pack NAME] [--force] [--dry-run]
#                      [--fix X0 Y0 X1 Y1 --prompt "what to fix"]
#   openai-remaster.py --image IN.png --out OUT.png [--ref STYLE.png ...] --prompt-file FILE
#
# Picks pictures from the gobexport folder (default: the room backgrounds of
# GOB01), sends each one to /v1/images/edits enlarged with nearest-neighbour
# to SCALE times its size, asks for exactly that size back (the GPT Image
# models take custom sizes: multiples of 16, 655,360 to 8,294,400 pixels), and
# installs the answer pixel-exact into HDPacks/NAME, so it lines up with the
# game. Every answer is also kept, with the prompt and the token usage, in
# ../upscale-openai/NAME/ for comparing and redoing.
#
# The API key comes from OPENAI_API_KEY in the environment or in .env at the
# repository root (never printed). Each picture is billed by OpenAI and the
# game picture is sent to OpenAI: use your own copy, keep the pack to yourself.
# Touch-ups: --fix X0 Y0 X1 Y1 (in the original picture's pixels) redoes
# only that rectangle of a picture already in the pack. The pack's picture is
# sent with the original as a second reference and a mask over the
# rectangle, --prompt says what is wrong, and only the rectangle (with soft
# edges) is pasted back, so the rest stays exactly as it was.
#
# --image edits any one picture at its own size (it must be a size the API
# takes) and writes OUT; --ref adds reference pictures after it (e.g. a
# remastered background, for the style). scripts/openai-frames.sh uses this.
#
# Standard library and Pillow only.

import argparse
import base64
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid
from io import BytesIO
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent.parent
ROOT = HERE.parent
API = "https://api.openai.com/v1/images/edits"
# Token prices of the GPT Image 2.5 models (USD per million), for the estimate.
PRICE = {"image_in": 8.0, "text_in": 5.0, "image_out": 30.0}

PROMPT = """Remaster this background from a 1990s point-and-click adventure game (Gobliins 2) as a detailed, high-resolution hand-painted illustration. It is a faithful remaster of the same picture, not a new picture.

Keep exactly: the composition, framing, camera angle, perspective, proportions and the position, size and outline of every building, wall, path, step, door, plant, tree and object. Characters walk on this picture and objects are drawn on top of it at fixed positions, so nothing may move, grow, shrink or disappear. Keep the colour palette, the time of day and the overall mood. Characters painted into the picture keep their exact design: head shape, ears, nose, eyes, mouth, hair, beard, age, clothes, colours and pose.

Change only the rendering: replace the low-resolution pixels with painterly brushwork, natural textures (stone, wood, leaves, roof tiles, earth), soft lighting and fine detail, in the style of a lavish hand-painted cartoon adventure game.

Do not add new elements, characters, text, logos, borders or watermarks. Do not change anything else."""


def gob_paths():
	"""Gobliins 2's HD pack folder and the export cache, as scripts/gobpaths.sh finds them."""
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


def api_key():
	key = os.environ.get("OPENAI_API_KEY")
	env = HERE / ".env"
	if not key and env.exists():
		for line in env.read_text().splitlines():
			line = line.strip()
			if line.startswith("export "):
				line = line[7:]
			if line.startswith("OPENAI_API_KEY="):
				key = line.split("=", 1)[1].strip().strip("'\"")
	if not key:
		sys.exit("No OPENAI_API_KEY in the environment or in .env")
	return key


SHEET_PROMPT = """The first image is a sprite sheet from the 1990s point-and-click adventure game Gobliins 2: the pieces the game cuts out and assembles into its animation frames (heads, bodies, arms, legs, whole figures and objects), enlarged from low-resolution pixel art, on a flat magenta background.{style}

Redraw every piece as high-quality hand-painted cartoon art. Keep exactly: the image size and every piece's position, size and outline, so the game can still cut it out and combine it with the others; each piece's colours; and the design of every character and object (head shape, eyes, nose, ears, mouth, hair, clothes, proportions). Pieces of the same character must keep the same look.

Keep the background pure flat magenta (#FF00FF) everywhere between the pieces, with no shadows, outlines, glow or gradients on it. Do not add new elements, text or borders, and do not move, merge or join pieces."""

SHEET_STYLE = """ The second image is a remastered background from the same game: use its hand-painted style, lighting and level of detail."""

KEY = (255, 0, 255)


def api_size(w, h):
	"""The nearest size at least w x h that the Image API takes (multiples of 16, >= 655,360 pixels)."""
	W, H = -(-w // 16) * 16, -(-h // 16) * 16
	while W * H < 655_360:  # grow the shorter side, keeping the aspect within 3:1
		if W < H:
			W += 16
		else:
			H += 16
	return W, H


def unkey(im):
	"""Replace the magenta background of an answer with the nearest painted colours, so a piece
	the model drew a little smaller still shows paint, not magenta, inside the game's outline."""
	import numpy as np
	a = np.asarray(im, dtype=np.int16).copy()
	dist = np.max(np.abs(a - np.array(KEY)), axis=2)
	empty = dist <= 90
	for _ in range(256):
		if not empty.any():
			break
		for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
			src = np.roll(empty, (dy, dx), axis=(0, 1))
			take = empty & ~src
			if take.any():
				a[take] = np.roll(a, (dy, dx), axis=(0, 1))[take]
				empty = empty & ~take
	return Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGB")


def despill(im, silhouette, band=8):
	"""Remove the magenta cast the model leaves on soft edges: within `band` pixels inside
	the game's outline (silhouette: L image, 255 = drawn), pull red and blue down to green
	where both clearly exceed it and match each other (magenta, not the game's purples)."""
	import numpy as np
	from PIL import ImageFilter
	inside = np.asarray(silhouette) > 127
	outside = Image.fromarray(((~inside) * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(2 * band + 1))
	edge = inside & (np.asarray(outside) > 127)
	a = np.asarray(im, dtype=np.int16).copy()
	r, g, b = a[..., 0], a[..., 1], a[..., 2]
	spill = np.minimum(r, b) - g
	fix = edge & (spill > 30) & (np.abs(r - b) < 70)
	a[..., 0] = np.where(fix, r - spill, r)
	a[..., 2] = np.where(fix, b - spill, b)
	return Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGB")


def smooth_mask(mask, scale):
	"""The game's outline enlarged without its pixel steps: nearest-neighbour, blurred, thresholded."""
	from PIL import ImageFilter
	big = mask.convert("L").resize((mask.width * scale, mask.height * scale), Image.NEAREST)
	return big.filter(ImageFilter.GaussianBlur(scale * 0.6)).point(lambda v: 255 if v >= 128 else 0)


FIX_PROMPT = """The first image is a remastered background from a 1990s adventure game (Gobliins 2); the second image is the original low-resolution picture it was made from. Repaint only the masked area of the first image: {what}

Match the original's design in the second image exactly for that area (shapes, proportions, features, colours, pose and position), rendered in the same hand-painted style, lighting and detail as the rest of the first image. Do not change anything outside the masked area. Do not add new elements or text."""


class Blocked(Exception):
	"""The safety system refused this request; retrying it unchanged won't help."""


def edit(key, png, prompt, model, size, quality, extra=(), mask=None):
	boundary = uuid.uuid4().hex
	parts = []
	for name, value in (("model", model), ("prompt", prompt), ("size", size), ("quality", quality), ("n", "1")):
		parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"\r\n\r\n{value}\r\n'.encode())
	for i, data in enumerate((png, *extra)):
		parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="image[]"; filename="input{i}.png"\r\n'
		             'Content-Type: image/png\r\n\r\n'.encode() + data + b"\r\n")
	if mask:
		parts.append(f'--{boundary}\r\nContent-Disposition: form-data; name="mask"; filename="mask.png"\r\n'
		             'Content-Type: image/png\r\n\r\n'.encode() + mask + b"\r\n")
	parts.append(f"--{boundary}--\r\n".encode())
	req = urllib.request.Request(API, data=b"".join(parts), headers={
		"Authorization": f"Bearer {key}", "Content-Type": f"multipart/form-data; boundary={boundary}"})
	for attempt in range(4):
		try:
			with urllib.request.urlopen(req, timeout=900) as r:
				return json.load(r)
		except urllib.error.HTTPError as e:
			body = e.read().decode(errors="replace")
			try:
				err = json.loads(body).get("error", {})
			except ValueError:
				err = {"message": body[:500]}
			if err.get("code") == "moderation_blocked":
				raise Blocked(err.get("message", ""))
			retry = e.code == 429 and err.get("code") != "insufficient_quota" or e.code >= 500
			if not retry or attempt == 3:
				sys.exit(f"OpenAI error {e.code}: {err.get('code') or err.get('type')}: {err.get('message')}")
			wait = 10 * 2 ** attempt
			print(f"  {e.code} {err.get('message', '')[:120]}; retrying in {wait}s", flush=True)
			time.sleep(wait)


def cost(usage):
	if not usage:
		return None
	details = usage.get("input_tokens_details", {})
	img_in = details.get("image_tokens", 0)
	txt_in = details.get("text_tokens", usage.get("input_tokens", 0) - img_in)
	out = usage.get("output_tokens", 0)
	return (img_in * PRICE["image_in"] + txt_in * PRICE["text_in"] + out * PRICE["image_out"]) / 1e6


def main():
	ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("--tots", default="GOB01")
	ap.add_argument("--ids", default="", help="only these picture ids")
	ap.add_argument("--kinds", default="background")
	ap.add_argument("--scale", type=int, default=4, choices=(4, 8))
	ap.add_argument("--model", default="gpt-image-2.5-sunburst")
	ap.add_argument("--quality", default="high", choices=("low", "medium", "high", "xhigh", "max", "auto"))
	ap.add_argument("--prompt")
	ap.add_argument("--prompt-file")
	ap.add_argument("--pack", default="openai")
	ap.add_argument("--force", action="store_true", help="redo pictures already in the pack")
	ap.add_argument("--dry-run", action="store_true", help="show what would be sent, send nothing")
	ap.add_argument("--image", help="edit this one picture at its own size and write --out")
	ap.add_argument("--out")
	ap.add_argument("--ref", action="append", default=[], help="reference picture sent after --image (repeatable)")
	ap.add_argument("--ref-pack", metavar="PACK",
	                help="send the picture's own room background from this pack as the style reference")
	ap.add_argument("--fix", nargs=4, type=int, metavar=("X0", "Y0", "X1", "Y1"),
	                help="redo only this rectangle (original pixels) of the pack's picture; --prompt says what to fix")
	args = ap.parse_args()
	prompt = Path(args.prompt_file).read_text() if args.prompt_file else args.prompt or PROMPT
	if args.image:
		if not args.out or not (args.prompt or args.prompt_file):
			sys.exit("--image needs --out and --prompt (or --prompt-file)")
		im = Image.open(args.image)
		size = f"{im.width}x{im.height}"
		if im.width % 16 or im.height % 16 or not 655_360 <= im.width * im.height <= 8_294_400:
			sys.exit(f"{args.image}: {size} is not a size the API takes")
		if args.dry_run:
			print(f"would send {args.image} ({size}) with {len(args.ref)} reference(s) to {args.model} ({args.quality})")
			return
		print(f"{Path(args.image).name}: {args.model}, {size}, {args.quality}, {len(args.ref)} reference(s) ...", flush=True)
		t = time.time()
		res = edit(api_key(), Path(args.image).read_bytes(), prompt, args.model, size, args.quality,
		           tuple(Path(r).read_bytes() for r in args.ref))
		out = Image.open(BytesIO(base64.b64decode(res["data"][0]["b64_json"]))).convert("RGB")
		got = out.size
		if out.size != im.size:
			out = out.resize(im.size, Image.LANCZOS)
		Path(args.out).parent.mkdir(parents=True, exist_ok=True)
		out.save(args.out)
		c = cost(res.get("usage"))
		Path(args.out).with_suffix(".json").write_text(json.dumps({"image": args.image, "refs": args.ref, "model": args.model,
			"quality": args.quality, "size": size, "returned": list(got), "usage": res.get("usage"),
			"cost_usd_estimate": c, "prompt": prompt}, indent=1))
		print(f"  done in {time.time() - t:.0f}s" + (f", about ${c:.3f}" if c is not None else ""), flush=True)
		return
	if args.fix:
		if not (args.prompt or args.prompt_file):
			sys.exit("--fix needs --prompt (or --prompt-file) saying what to fix")
		prompt = FIX_PROMPT.format(what=prompt.strip())
		args.force = True

	packs, cache = gob_paths()
	export = cache / "export"
	if not (export / "index.json").exists():
		sys.exit(f"No picture export at {export}: run build/gobexport ../gobliins2 {export}")
	index = json.load(open(export / "index.json"))
	tots, ids = set(args.tots.split()), {int(i) for i in args.ids.split()}
	kinds = set(args.kinds.replace(",", " ").split())
	todo = [e for e in index["images"] if e["tot"] in tots and (e["resource"] in ids if ids else e["kind"] in kinds)
	        and ("same_as" not in e or ids)]
	if not todo:
		sys.exit("Nothing selected")

	pack, keep = packs / args.pack, ROOT / "upscale-openai" / args.pack
	key = None if args.dry_run else api_key()
	total = 0.0
	blocked = []
	for i, e in enumerate(todo, 1):
		w, h = e["width"] * args.scale, e["height"] * args.scale
		W, H = api_size(w, h)  # asked for at this size; the padding is cut off again
		if W * H > 8_294_400 or max(W, H) > 3840 or max(W, H) > 3 * min(W, H):
			print(f"[{i}/{len(todo)}] {e['image']}: {w}x{h} can't be sent at this scale; skipped (try the other --scale)")
			continue
		sheet = e["kind"] in ("animation", "sprite") or e.get("transparent", False)
		dst = pack / e["image"]
		if args.fix and not dst.exists():
			print(f"[{i}/{len(todo)}] {e['image']}: not in {args.pack} yet, nothing to touch up")
			continue
		if dst.exists() and not args.force:
			print(f"[{i}/{len(todo)}] {e['image']}: already in {args.pack} (--force redoes it)")
			continue
		src = Image.open(export / e["image"]).convert("RGB")
		mask_path = export / e["image"].replace(".png", ".mask.png")
		big = src.resize((w, h), Image.NEAREST)
		if sheet and mask_path.exists():  # pieces on flat magenta
			flat = Image.new("RGB", (w, h), KEY)
			flat.paste(big, (0, 0), Image.open(mask_path).convert("L").resize((w, h), Image.NEAREST))
			big = flat
		canvas = Image.new("RGB", (W, H), KEY if sheet else (0, 0, 0))
		canvas.paste(big, (0, 0))
		buf = BytesIO()
		canvas.save(buf, "PNG")
		this_prompt = prompt
		refs = list(args.ref)
		if args.ref_pack:  # this room's remastered background (or the first room's)
			for cand in [b for b in index["images"] if b["tot"] == e["tot"] and b["kind"] == "background"] + \
			            [{"image": "GOB01/30007.png"}]:
				if (packs / args.ref_pack / cand["image"]).exists():
					refs.append(str(packs / args.ref_pack / cand["image"]))
					break
		if sheet and not (args.prompt or args.prompt_file or args.fix):
			this_prompt = SHEET_PROMPT.format(style=SHEET_STYLE if refs else "")
		extra, mask, box = tuple(Path(r).read_bytes() for r in refs), None, None
		if args.fix:
			if (W, H) != (w, h):
				sys.exit("--fix works on pictures sent at their own size")
			box = tuple(v * args.scale for v in args.fix)
			ref = buf.getvalue()  # the original, as the second image
			buf = BytesIO()
			current = Image.open(dst).convert("RGB")
			if current.size != (w, h):
				sys.exit(f"{dst} is {current.size[0]}x{current.size[1]}, not {w}x{h}: use the pack's --scale")
			current.save(buf, "PNG")
			extra = (ref,)
			m = Image.new("RGBA", (w, h), (0, 0, 0, 255))  # opaque: keep; transparent: repaint
			m.paste((0, 0, 0, 0), box)
			mbuf = BytesIO()
			m.save(mbuf, "PNG")
			mask = mbuf.getvalue()
		size = f"{W}x{H}"
		if args.dry_run:
			print(f"[{i}/{len(todo)}] {e['image']}: would send {size} ({e['kind']}) with {len(extra)} reference(s) "
			      f"to {args.model} ({args.quality})")
			continue
		print(f"[{i}/{len(todo)}] {e['image']}: {args.model}, {size}, {args.quality} ...", flush=True)
		t = time.time()
		try:
			res = edit(key, buf.getvalue(), this_prompt, args.model, size, args.quality, extra, mask)
		except Blocked as b:
			blocked.append(e["image"])
			print(f"  refused by OpenAI's safety system, skipped ({str(b)[-80:]})", flush=True)
			continue
		out = Image.open(BytesIO(base64.b64decode(res["data"][0]["b64_json"]))).convert("RGB")
		got = out.size
		if out.size != (W, H):
			out = out.resize((W, H), Image.LANCZOS)
		raw = out
		out = out.crop((0, 0, w, h))
		if sheet:
			out = unkey(out)
			if mask_path.exists():
				out = despill(out, Image.open(mask_path).convert("L").resize((w, h), Image.NEAREST), 2 * args.scale)
		if box:  # paste back only the rectangle, its edges blended over a few pixels
			from PIL import ImageFilter
			feather = 3 * args.scale
			alpha = Image.new("L", (w, h), 0)
			alpha.paste(255, (box[0] + feather, box[1] + feather, box[2] - feather, box[3] - feather))
			alpha = alpha.filter(ImageFilter.GaussianBlur(feather / 2))
			merged = Image.open(dst).convert("RGB")
			merged.paste(out, (0, 0), alpha)
			out = merged
		dst.parent.mkdir(parents=True, exist_ok=True)
		out.save(dst)
		if sheet and mask_path.exists():  # smooth outlines instead of the enlarged pixel steps
			smooth_mask(Image.open(mask_path), args.scale).save(dst.with_name(dst.stem + ".mask.png"))
		stem = keep / e["image"].replace("/", "_")[:-4]
		keep.mkdir(parents=True, exist_ok=True)
		stamp = time.strftime("%Y%m%d-%H%M%S")
		out.save(f"{stem}-{stamp}.png")
		if sheet:
			raw.save(f"{stem}-{stamp}-raw.png")
		c = cost(res.get("usage"))
		json.dump({"image": e["image"], "model": args.model, "quality": args.quality, "size": size,
		           "returned": list(got), "usage": res.get("usage"), "cost_usd_estimate": c, "prompt": this_prompt,
		           "refs": refs},
		          open(f"{stem}-{stamp}.json", "w"), indent=1)
		total += c or 0
		note = "" if got == (W, H) else f" (returned {got[0]}x{got[1]}, resized)"
		print(f"  done in {time.time() - t:.0f}s{note}" + (f", about ${c:.3f}" if c is not None else ""), flush=True)

	if total:
		print(f"Estimated cost: ${total:.2f}")
	if blocked:
		print(f"Refused by OpenAI's safety system (left as they were): {' '.join(blocked)}")
	gobexport = HERE / "build" / "gobexport"
	if pack.exists() and gobexport.exists() and not args.dry_run:
		subprocess.run([str(gobexport), str(ROOT / "gobliins2"), "--check-pack", str(pack)])
	print(f'In the game: overlay (`) > Video > HD pack "{args.pack}" (pack scope: rooms keeps the original sprites).')


if __name__ == "__main__":
	main()
