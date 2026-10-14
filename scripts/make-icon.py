#!/usr/bin/env python3
# gob2native - draw the app icon ("GOB" on an orange tile) in every format
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   scripts/make-icon.py [outdir]   (default: assets/icon)
#
# Writes icon-1024.png (the tile filling the square), gobnative.icns (macOS
# layout: the tile inset with a soft shadow; made with iconutil, so on macOS
# only), gobnative.ico (16-256 px), favicon-32.png, apple-touch-icon.png and
# window-icon.png (the window's icon at runtime). The lettering is Nunito
# Bold (third_party/fonts/nunito, SIL OFL). Needs Pillow.

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent.parent
FONT = HERE / "third_party/fonts/nunito/Nunito-Bold.ttf"
TOP, BOTTOM = (255, 176, 64), (214, 84, 32)
TEXT, SHADOW = (255, 255, 255, 255), (110, 30, 0, 170)


def tile(size, radius=0.225, highlight=22):
	"""The orange rounded tile with "GOB", filling a size x size square."""
	grad = Image.new("RGB", (1, size))
	for y in range(size):
		t = y / max(1, size - 1)
		grad.putpixel((0, y), tuple(int(TOP[i] + (BOTTOM[i] - TOP[i]) * t) for i in range(3)))
	grad = grad.resize((size, size)).convert("RGBA")
	mask = Image.new("L", (size, size), 0)
	ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), int(size * radius), fill=255)
	img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	img.paste(grad, (0, 0), mask)
	hl = Image.new("L", (size, size), 0)
	ImageDraw.Draw(hl).ellipse((-size * 0.3, -size * 0.75, size * 1.3, size * 0.45), fill=highlight)
	img.paste(Image.new("RGBA", (size, size), (255, 255, 255, 255)), (0, 0), Image.composite(hl, Image.new("L", (size, size), 0), mask))
	font = ImageFont.truetype(str(FONT), int(size * 0.40))
	d = ImageDraw.Draw(img)
	l, t, r, b = d.textbbox((0, 0), "GOB", font=font)
	x, y = (size - (r - l)) / 2 - l, (size - (b - t)) / 2 - t + size * 0.01
	sh = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	ImageDraw.Draw(sh).text((x, y + size * 0.025), "GOB", font=font, fill=SHADOW)
	img = Image.alpha_composite(img, sh.filter(ImageFilter.GaussianBlur(size * 0.012)))
	ImageDraw.Draw(img).text((x, y), "GOB", font=font, fill=TEXT)
	return img


def mac_layout(size):
	"""Apple's icon grid: the tile at 824/1024 of the canvas, centred, with a soft drop shadow."""
	inner = round(size * 824 / 1024)
	canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	art = tile(inner)
	shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
	off = (size - inner) // 2
	shadow.paste((0, 0, 0, 90), (off, off + round(size * 0.012)), art.split()[3])
	canvas = Image.alpha_composite(canvas, shadow.filter(ImageFilter.GaussianBlur(size * 0.012)))
	canvas.paste(art, (off, off), art)
	return canvas


def main():
	out = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "assets/icon"
	out.mkdir(parents=True, exist_ok=True)
	master = tile(1024)
	master.save(out / "icon-1024.png")
	master.resize((256, 256), Image.LANCZOS).save(out / "window-icon.png")
	master.resize((32, 32), Image.LANCZOS).save(out / "favicon-32.png")
	master.resize((180, 180), Image.LANCZOS).save(out / "apple-touch-icon.png")
	sizes = [16, 24, 32, 48, 64, 128, 256]
	icos = [tile(s * 4).resize((s, s), Image.LANCZOS) if s < 64 else tile(s) for s in sizes]
	icos[-1].save(out / "gobnative.ico", sizes=[(s, s) for s in sizes], append_images=icos[:-1])
	if shutil.which("iconutil"):
		with tempfile.TemporaryDirectory() as tmp:
			iconset = Path(tmp) / "gobnative.iconset"
			iconset.mkdir()
			for s in (16, 32, 128, 256, 512):
				mac_layout(s).save(iconset / f"icon_{s}x{s}.png")
				mac_layout(s * 2).save(iconset / f"icon_{s}x{s}@2x.png")
			subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(out / "gobnative.icns")], check=True)
	else:
		print("iconutil not found (macOS only): gobnative.icns not written")
	print("wrote", ", ".join(sorted(p.name for p in out.iterdir())))


if __name__ == "__main__":
	main()
