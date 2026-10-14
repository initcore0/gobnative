#!/usr/bin/env python3
# gob2native - batch AI upscaler for exported game images
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs an OpenModelDB-style super-resolution model (loaded with spandrel)
# over every colour image of a gobexport folder, writing results with the
# same relative paths. Masks (*.mask.png) and metadata are left alone: the
# game rebuilds transparency from the original masks.
#
# Usage: upscale.py --model MODEL.pth --input EXPORT --output OUT
#                   [--target-scale N] [--tile N|auto] [--force]
#
# A target scale above the model's own runs the model several times and
# resamples the result down to the exact target (Lanczos).

import argparse
import math
import sys
import time
from pathlib import Path

import numpy as np
import spandrel
import torch
from PIL import Image


def pick_tile(model_scale, requested):
	"""Tile edge in input pixels, sized to the free VRAM (other processes, such
	as a resident language model, may hold most of the card)."""
	if requested != "auto":
		return int(requested)
	free, _ = torch.cuda.mem_get_info()
	free_gb = free / 1e9
	# ESRGAN in fp16 needs roughly 0.15 GB per 64x64 input tile at 4x.
	budget = max(0.3, free_gb - 0.5)
	edge = int(64 * math.sqrt(budget / (0.15 * (model_scale / 4) ** 2)))
	return max(64, min(512, edge // 16 * 16))


def upscale_tiled(model, img, tile, overlap=16):
	"""Upscales an HxWx3 float array in overlapping tiles, feathering the
	seams. Returns the (H*s)x(W*s)x3 result."""
	s = model.scale
	h, w, _ = img.shape
	out = np.zeros((h * s, w * s, 3), dtype=np.float32)
	weight = np.zeros((h * s, w * s, 1), dtype=np.float32)
	step = tile - overlap
	dtype = torch.float16 if model.supports_half else torch.float32

	for y0 in range(0, max(h - overlap, 1), step):
		for x0 in range(0, max(w - overlap, 1), step):
			y1, x1 = min(y0 + tile, h), min(x0 + tile, w)
			y0c, x0c = max(0, y1 - tile), max(0, x1 - tile)
			patch = torch.from_numpy(img[y0c:y1, x0c:x1].transpose(2, 0, 1)).unsqueeze(0)
			with torch.inference_mode():
				res = model(patch.to("cuda", dtype)).float().clamp(0, 1)[0].cpu().numpy().transpose(1, 2, 0)
			ph, pw = res.shape[:2]
			# Linear ramps on inner edges so overlapping tiles blend smoothly.
			ramp_y = np.ones(ph, np.float32)
			ramp_x = np.ones(pw, np.float32)
			o = overlap * s
			if y0c > 0:
				ramp_y[:o] = np.linspace(0, 1, o, dtype=np.float32)
			if y1 < h:
				ramp_y[-o:] = np.minimum(ramp_y[-o:], np.linspace(1, 0, o, dtype=np.float32))
			if x0c > 0:
				ramp_x[:o] = np.linspace(0, 1, o, dtype=np.float32)
			if x1 < w:
				ramp_x[-o:] = np.minimum(ramp_x[-o:], np.linspace(1, 0, o, dtype=np.float32))
			wgt = (ramp_y[:, None] * ramp_x[None, :])[:, :, None] + 1e-6
			out[y0c * s:y0c * s + ph, x0c * s:x0c * s + pw] += res * wgt
			weight[y0c * s:y0c * s + ph, x0c * s:x0c * s + pw] += wgt
	return out / weight


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--model", required=True)
	ap.add_argument("--input", required=True)
	ap.add_argument("--output", required=True)
	ap.add_argument("--target-scale", type=int, default=0,
	                help="resize results to this factor (e.g. 4 for an 8x model)")
	ap.add_argument("--tile", default="auto")
	ap.add_argument("--force", action="store_true", help="redo images that are already done")
	args = ap.parse_args()

	if not torch.cuda.is_available():
		sys.exit("CUDA is not available")

	model = spandrel.ModelLoader().load_from_file(args.model).cuda().eval()
	if model.supports_half:
		model.half()
	tile = pick_tile(model.scale, args.tile)
	target = args.target_scale or model.scale
	passes = 1
	while model.scale ** passes < target:
		passes += 1
	src, dst = Path(args.input), Path(args.output)

	images = sorted(p for p in src.rglob("*.png") if not p.name.endswith(".mask.png"))
	print(f"{Path(args.model).name}: x{model.scale} x {passes} pass(es) -> x{target}, tile {tile}, {len(images)} images",
	      flush=True)

	done = skipped = 0
	t0 = time.time()
	for p in images:
		out = dst / p.relative_to(src)
		if out.exists() and not args.force and out.stat().st_mtime >= p.stat().st_mtime:
			skipped += 1
			continue
		im = Image.open(p).convert("RGB")
		arr = np.asarray(im, dtype=np.float32) / 255.0
		# Targets above the model's factor run the model repeatedly (e.g. 4x
		# then 4x for 8x), then resample down to the exact target.
		for _ in range(passes):
			arr = upscale_tiled(model, arr, tile)
		res_im = Image.fromarray((arr * 255.0 + 0.5).clip(0, 255).astype(np.uint8))
		if res_im.width != im.width * target:
			res_im = res_im.resize((im.width * target, im.height * target), Image.LANCZOS)
		out.parent.mkdir(parents=True, exist_ok=True)
		res_im.save(out, optimize=False)
		done += 1
		if done % 25 == 0:
			print(f"  {done}/{len(images) - skipped} ({time.time() - t0:.0f}s)", flush=True)

	peak = torch.cuda.max_memory_allocated() / 1e9
	print(f"done: {done} upscaled, {skipped} already up to date, {time.time() - t0:.1f}s, peak VRAM {peak:.2f} GB", flush=True)


if __name__ == "__main__":
	main()
