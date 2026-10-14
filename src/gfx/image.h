// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "formats/tot.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace gob {

struct Rgb { uint8_t r, g, b; };
using Palette = std::array<Rgb, 256>;

// 8-bit indexed image; index 0 is transparent when blitted with transparency.
struct Image {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> pixels;

	uint8_t at(int x, int y) const { return pixels[size_t(y) * width + x]; }
};

// Decodes a sprite resource as drawn by the DRAW_LOADSPRITE operation:
// "01 02 02" raw pixels, "01 02 01" LZSS, otherwise 4-bit run-length data.
std::optional<Image> decodeSprite(const Resource &res);

// Converts a 768-byte VGA palette resource (6 bits per channel).
std::optional<Palette> decodeVgaPalette(const std::vector<uint8_t> &data);

Palette grayscalePalette();

} // namespace gob
