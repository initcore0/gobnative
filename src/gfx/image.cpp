// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/image.h"

#include "formats/stk.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace gob {

namespace {

uint32_t le32(const uint8_t *p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

// Run-length format: each control byte holds a 4-bit color in the high
// nibble; bit 3 set means a short run (low 3 bits), else an 11-bit run
// using the next byte.
bool decodeRle4(const std::vector<uint8_t> &src, Image &img) {
	size_t total = size_t(img.width) * img.height;
	size_t pos = 0;
	size_t out = 0;
	while (out < total) {
		if (pos >= src.size())
			return false;
		uint8_t val = src[pos++];
		unsigned repeat = val & 7;
		if (!(val & 8)) {
			if (pos >= src.size())
				return false;
			repeat = (repeat << 8) | src[pos++];
		}
		repeat++;
		uint8_t color = val >> 4;
		for (unsigned i = 0; i < repeat && out < total; i++)
			img.pixels[out++] = color;
	}
	return true;
}

} // namespace

std::optional<Image> decodeSprite(const Resource &res) {
	if (res.width <= 0 || res.height <= 0 || res.width > 4096 || res.height > 4096)
		return std::nullopt;

	Image img;
	img.width = res.width;
	img.height = res.height;
	size_t total = size_t(img.width) * img.height;
	img.pixels.assign(total, 0);

	const std::vector<uint8_t> &d = res.data;
	try {
		if (d.size() >= 3 && d[0] == 1 && d[1] == 2) {
			if (d[2] == 2) {
				size_t n = std::min(total, d.size() - 3);
				std::copy_n(d.begin() + 3, n, img.pixels.begin());
				return img;
			}
			if (d[2] == 1 && d.size() >= 7) {
				uint32_t size = le32(&d[3]);
				std::vector<uint8_t> px = unpackLzss(&d[7], d.size() - 7, std::min<uint32_t>(size, uint32_t(total)));
				std::copy(px.begin(), px.end(), img.pixels.begin());
				return img;
			}
		}
		if (decodeRle4(d, img))
			return img;
	} catch (const std::exception &) {
	}
	return std::nullopt;
}

std::optional<Palette> decodeVgaPalette(const std::vector<uint8_t> &data) {
	if (data.size() < 768)
		return std::nullopt;
	Palette pal;
	for (int i = 0; i < 256; i++) {
		auto conv = [](uint8_t v) { v &= 0x3F; return uint8_t((v << 2) | (v >> 4)); };
		pal[i] = {conv(data[i * 3]), conv(data[i * 3 + 1]), conv(data[i * 3 + 2])};
	}
	return pal;
}

Palette grayscalePalette() {
	Palette pal;
	for (int i = 0; i < 256; i++)
		pal[i] = {uint8_t(i), uint8_t(i), uint8_t(i)};
	return pal;
}

} // namespace gob
