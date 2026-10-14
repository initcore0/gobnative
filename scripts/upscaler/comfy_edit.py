#!/usr/bin/env python3
# gob2native - repaint pictures with an image-editing model through ComfyUI
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs on the GPU machine next to ComfyUI and talks to its HTTP API (standard
# library only). Every .png in INPUT is enlarged with nearest-neighbour to
# SCALE times its size, padded to a multiple of 32, sent as the reference
# image of the editing model (Qwen-Image 2.1 or FLUX.2 [dev]) with PROMPT,
# and the result cropped back to exactly SCALE times the original, so it
# lines up with the game's pixel grid.
#
# Usage: comfy_edit.py --input DIR --output DIR --prompt TEXT [--model qwen|flux2]
#                      [--scale 4] [--steps 20] [--cfg 4] [--seed N] [--server URL]

import argparse
import json
import sys
import time
import urllib.parse
import urllib.request
import uuid
from pathlib import Path

from PIL import Image

MODEL = "qwen-image-2.1-Q4_K_M.gguf"
TEXT_ENCODER = "qwen3vl_8b_int8_convrot.safetensors"
VAE = "qwen_image_2.1_vae_bf16.safetensors"
FLUX2_MODEL = "flux2-dev-Q4_K_M.gguf"
FLUX2_TEXT_ENCODER = "mistral_3_small_flux2_fp8.safetensors"
FLUX2_VAE = "flux2-vae.safetensors"
NEGATIVE = "blurry, low quality, deformed, watermark, text, signature, frame, border"


def workflow(image_name, prompt, negative, steps, cfg, seed, prefix, denoise=1.0):
	# Below 1.0, sampling starts from the reference itself (img2img), which
	# keeps its colours and shapes and only adds detail.
	g = {
		"1": {"class_type": "UnetLoaderGGUF", "inputs": {"unet_name": MODEL}},
		"2": {"class_type": "CLIPLoader", "inputs": {"clip_name": TEXT_ENCODER, "type": "qwen_image", "device": "default"}},
		"3": {"class_type": "VAELoader", "inputs": {"vae_name": VAE}},
		"4": {"class_type": "LoadImage", "inputs": {"image": image_name}},
		"5": {"class_type": "TextEncodeQwenImage21", "inputs": {
			"clip": ["2", 0], "vae": ["3", 0], "prompt": prompt, "negative_prompt": negative,
			"resolution": 0, "images.image_1": ["4", 0]}},
		"6": {"class_type": "KSampler", "inputs": {
			"model": ["1", 0], "positive": ["5", 0], "negative": ["5", 1], "latent_image": ["5", 2],
			"seed": seed, "steps": steps, "cfg": cfg, "sampler_name": "euler", "scheduler": "simple",
			"denoise": denoise}},
		"7": {"class_type": "VAEDecode", "inputs": {"samples": ["6", 0], "vae": ["3", 0]}},
		"8": {"class_type": "SaveImage", "inputs": {"images": ["7", 0], "filename_prefix": prefix}},
	}
	if denoise < 1.0:
		g["9"] = {"class_type": "VAEEncode", "inputs": {"pixels": ["4", 0], "vae": ["3", 0]}}
		g["6"]["inputs"]["latent_image"] = ["9", 0]
	return g


def workflow_flux2(image_name, prompt, size, steps, guidance, seed, prefix):
	# FLUX.2 [dev] is guidance-distilled: no negative prompt, guidance instead of CFG.
	w, h = size
	return {
		"1": {"class_type": "UnetLoaderGGUF", "inputs": {"unet_name": FLUX2_MODEL}},
		"2": {"class_type": "CLIPLoader", "inputs": {"clip_name": FLUX2_TEXT_ENCODER, "type": "flux2", "device": "default"}},
		"3": {"class_type": "VAELoader", "inputs": {"vae_name": FLUX2_VAE}},
		"4": {"class_type": "LoadImage", "inputs": {"image": image_name}},
		"5": {"class_type": "VAEEncode", "inputs": {"pixels": ["4", 0], "vae": ["3", 0]}},
		"6": {"class_type": "CLIPTextEncode", "inputs": {"clip": ["2", 0], "text": prompt}},
		"7": {"class_type": "FluxGuidance", "inputs": {"conditioning": ["6", 0], "guidance": guidance}},
		"8": {"class_type": "ReferenceLatent", "inputs": {"conditioning": ["7", 0], "latent": ["5", 0]}},
		"9": {"class_type": "BasicGuider", "inputs": {"model": ["1", 0], "conditioning": ["8", 0]}},
		"10": {"class_type": "RandomNoise", "inputs": {"noise_seed": seed}},
		"11": {"class_type": "KSamplerSelect", "inputs": {"sampler_name": "euler"}},
		"12": {"class_type": "Flux2Scheduler", "inputs": {"steps": steps, "width": w, "height": h}},
		"13": {"class_type": "EmptyFlux2LatentImage", "inputs": {"width": w, "height": h, "batch_size": 1}},
		"14": {"class_type": "SamplerCustomAdvanced", "inputs": {
			"noise": ["10", 0], "guider": ["9", 0], "sampler": ["11", 0], "sigmas": ["12", 0], "latent_image": ["13", 0]}},
		"15": {"class_type": "VAEDecode", "inputs": {"samples": ["14", 0], "vae": ["3", 0]}},
		"16": {"class_type": "SaveImage", "inputs": {"images": ["15", 0], "filename_prefix": prefix}},
	}


def api(server, path, data=None, headers=None):
	req = urllib.request.Request(server + path, data=data, headers=headers or {})
	with urllib.request.urlopen(req, timeout=600) as r:
		return r.read()


def upload(server, path):
	boundary = uuid.uuid4().hex
	body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"image\"; filename=\"{path.name}\"\r\n"
	        "Content-Type: image/png\r\n\r\n").encode() + path.read_bytes() + \
	       f"\r\n--{boundary}\r\nContent-Disposition: form-data; name=\"overwrite\"\r\n\r\ntrue\r\n--{boundary}--\r\n".encode()
	res = json.loads(api(server, "/upload/image", body, {"Content-Type": f"multipart/form-data; boundary={boundary}"}))
	return res["name"]


def run(server, graph):
	client = uuid.uuid4().hex
	res = json.loads(api(server, "/prompt", json.dumps({"prompt": graph, "client_id": client}).encode(),
	                     {"Content-Type": "application/json"}))
	if res.get("node_errors"):
		sys.exit(f"ComfyUI rejected the workflow: {res['node_errors']}")
	pid = res["prompt_id"]
	while True:
		hist = json.loads(api(server, f"/history/{pid}"))
		if pid in hist:
			entry = hist[pid]
			if entry.get("status", {}).get("status_str") == "error":
				sys.exit(f"ComfyUI error: {json.dumps(entry['status'].get('messages'))[:2000]}")
			for out in entry.get("outputs", {}).values():
				for img in out.get("images", []):
					q = urllib.parse.urlencode({k: img[k] for k in ("filename", "subfolder", "type")})
					return api(server, f"/view?{q}")
			sys.exit("ComfyUI finished without an image")
		time.sleep(1)


def main():
	ap = argparse.ArgumentParser(description=__doc__)
	ap.add_argument("--input", required=True)
	ap.add_argument("--output", required=True)
	ap.add_argument("--prompt", required=True)
	ap.add_argument("--model", default="qwen", choices=("qwen", "flux2"))
	ap.add_argument("--negative", default=NEGATIVE, help="qwen only")
	ap.add_argument("--scale", type=int, default=4, choices=(1, 2, 3, 4))
	ap.add_argument("--steps", type=int, default=20)
	ap.add_argument("--cfg", type=float, default=4.0, help="CFG for qwen, guidance for flux2")
	ap.add_argument("--seed", type=int, default=1)
	ap.add_argument("--server", default="http://127.0.0.1:8188")
	ap.add_argument("--denoise", type=float, default=1.0,
	                help="qwen: below 1.0 start from the picture itself (0.4-0.6 keeps colours and shapes)")
	ap.add_argument("--base", metavar="DIR",
	                help="start from these pre-upscaled pictures (same layout, SCALE times the size) "
	                     "instead of a nearest-neighbour enlargement")
	ap.add_argument("--flat-bg", metavar="RRGGBB",
	                help="fill transparent pixels (from <id>.mask.png) with this colour before sending")
	ap.add_argument("--force", action="store_true")
	args = ap.parse_args()

	src_dir, out_dir = Path(args.input), Path(args.output)
	work = out_dir / ".work"
	work.mkdir(parents=True, exist_ok=True)
	files = [p for p in sorted(src_dir.rglob("*.png")) if not p.name.endswith(".mask.png") and ".work" not in p.parts]
	for i, src in enumerate(files, 1):
		rel = src.relative_to(src_dir)
		dst = out_dir / rel
		if dst.exists() and not args.force and dst.stat().st_mtime >= src.stat().st_mtime:
			continue
		orig = Image.open(src).convert("RGB")
		mask_path = src.with_name(src.stem + ".mask.png")
		if args.flat_bg and mask_path.exists():
			flat = Image.new("RGB", orig.size, "#" + args.flat_bg)
			flat.paste(orig, (0, 0), Image.open(mask_path).convert("L"))
			orig = flat
		w, h = orig.width * args.scale, orig.height * args.scale
		pw, ph = -(-w // 32) * 32, -(-h // 32) * 32
		big = orig.resize((w, h), Image.NEAREST)
		base = Path(args.base) / rel if args.base else None
		if base and base.exists():
			pre = Image.open(base).convert("RGB")
			if pre.size != (w, h):
				pre = pre.resize((w, h), Image.LANCZOS)
			if args.flat_bg and mask_path.exists():
				flat = Image.new("RGB", (w, h), "#" + args.flat_bg)
				flat.paste(pre, (0, 0), Image.open(mask_path).convert("L").resize((w, h), Image.NEAREST))
				pre = flat
			big = pre
		ref = Image.new("RGB", (pw, ph))
		ref.paste(big, (0, 0))
		if ph > h:  # repeat the edge rows/columns into the padding
			ref.paste(big.crop((0, h - 1, w, h)).resize((w, ph - h)), (0, h))
		if pw > w:
			ref.paste(ref.crop((w - 1, 0, w, ph)).resize((pw - w, ph)), (w, 0))
		tmp = work / (rel.as_posix().replace("/", "_"))
		ref.save(tmp)
		t = time.time()
		name = upload(args.server, tmp)
		if args.model == "flux2":
			graph = workflow_flux2(name, args.prompt, (pw, ph), args.steps, args.cfg, args.seed, "gob2-remaster")
		else:
			graph = workflow(name, args.prompt, args.negative, args.steps, args.cfg, args.seed, "gob2-remaster", args.denoise)
		png = run(args.server, graph)
		res_path = work / ("out_" + tmp.name)
		res_path.write_bytes(png)
		res = Image.open(res_path).convert("RGB")
		if res.size != (pw, ph):
			res = res.resize((pw, ph), Image.LANCZOS)
		dst.parent.mkdir(parents=True, exist_ok=True)
		res.crop((0, 0, w, h)).save(dst)
		print(f"[{i}/{len(files)}] {rel} {orig.width}x{orig.height} -> {w}x{h} ({time.time() - t:.0f}s)", flush=True)


if __name__ == "__main__":
	main()
