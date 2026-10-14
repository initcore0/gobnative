// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/bmp.h"

#include <fstream>
#include <vector>

namespace gob {

namespace {

void put16(std::ofstream &f, uint16_t v) { f.put(char(v & 0xFF)); f.put(char(v >> 8)); }
void put32(std::ofstream &f, uint32_t v) { put16(f, uint16_t(v)); put16(f, uint16_t(v >> 16)); }

} // namespace

bool writeBmp(const std::filesystem::path &path, const uint8_t *pixels, int width, int height, const Palette &pal) {
	int stride = (width + 3) & ~3;
	uint32_t dataSize = uint32_t(stride) * uint32_t(height);
	std::ofstream f(path, std::ios::binary);
	if (!f)
		return false;
	f.write("BM", 2);
	put32(f, 14 + 40 + 1024 + dataSize);
	put32(f, 0);
	put32(f, 14 + 40 + 1024);
	put32(f, 40);
	put32(f, uint32_t(width));
	put32(f, uint32_t(height));
	put16(f, 1);
	put16(f, 8);
	put32(f, 0);
	put32(f, dataSize);
	put32(f, 2835);
	put32(f, 2835);
	put32(f, 256);
	put32(f, 0);
	for (const Rgb &c : pal) {
		f.put(char(c.b)); f.put(char(c.g)); f.put(char(c.r)); f.put(0);
	}
	std::vector<char> row(size_t(stride), 0);
	for (int y = height - 1; y >= 0; y--) {
		for (int x = 0; x < width; x++)
			row[size_t(x)] = char(pixels[size_t(y) * width + x]);
		f.write(row.data(), stride);
	}
	return bool(f);
}

} // namespace gob
