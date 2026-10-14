// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/sharptext.h"

#include "gfx/smooth.h"

#include <algorithm>
#include <cstring>

namespace gob {

void SharpText::clear() {
	std::fill(_map.begin(), _map.end(), 0);
	_count = 0;
}

int SharpText::slotFor(const Draw::TextCell &c) {
	const auto key = std::make_tuple(c.ink, int(c.w), int(c.h));
	auto it = _slots.find(key);
	if (it != _slots.end())
		return it->second;
	if (_atlas.empty())
		_atlas.assign(size_t(kAtlasSize) * size_t(kAtlasSize), 0);
	const int slot = int(_slots.size());
	if (slot >= kSlotsPerRow * kSlotsPerRow)
		return -1; // full: build() starts over next frame

	uint8_t mask[Draw::kCellW * Draw::kCellH];
	for (int y = 0; y < c.h; y++)
		for (int x = 0; x < c.w; x++)
			mask[y * c.w + x] = c.inked(x, y);
	const std::vector<uint8_t> sdf = glyphDistanceField(mask, c.w, c.h, kSamples, kSpread);
	const int sw = c.w * kSamples, sh = c.h * kSamples;
	const int ox = (slot % kSlotsPerRow) * kCellStride, oy = (slot / kSlotsPerRow) * kCellStride;
	// Copy with a one-texel gutter that repeats the edge, so linear
	// filtering at the cell's border never reads a neighbouring glyph.
	for (int y = -1; y <= sh; y++)
		for (int x = -1; x <= sw; x++) {
			const int sx = std::clamp(x, 0, sw - 1), sy = std::clamp(y, 0, sh - 1);
			_atlas[size_t(oy + 1 + y) * size_t(kAtlasSize) + size_t(ox + 1 + x)] = sdf[size_t(sy) * size_t(sw) + size_t(sx)];
		}
	_atlasDirty = true;
	_slots.emplace(key, slot);
	return slot;
}

void SharpText::build(const std::vector<Draw::TextCell> &cells, const Palette &pal, uint8_t *rgba, int frameW,
                      int frameH) {
	if (_w != frameW || _h != frameH) {
		_w = frameW;
		_h = frameH;
		_map.assign(size_t(frameW) * size_t(frameH) * 4, 0);
		_colors.assign(size_t(frameW) * size_t(frameH) * 4, 0);
	}
	clear();
	if (_slots.size() + cells.size() > size_t(kSlotsPerRow * kSlotsPerRow)) {
		_slots.clear(); // very many different glyphs: start the atlas over
		_atlasDirty = true;
	}
	for (const Draw::TextCell &c : cells) {
		const int slot = slotFor(c);
		if (slot < 0)
			continue;
		_count++;
		const Rgb &ink = pal[c.front];
		for (int j = 0; j < c.h; j++)
			for (int i = 0; i < c.w; i++) {
				const int x = c.x + i, y = c.y + j;
				if (x < 0 || y < 0 || x >= frameW || y >= frameH)
					continue;
				const size_t p = (size_t(y) * size_t(frameW) + size_t(x)) * 4;
				_map[p] = uint8_t(slot);
				_map[p + 1] = uint8_t(slot >> 8);
				_map[p + 2] = uint8_t(i | (j << 4));
				_map[p + 3] = 255;
				_colors[p] = ink.r;
				_colors[p + 1] = ink.g;
				_colors[p + 2] = ink.b;
				_colors[p + 3] = 255;
				if (c.inked(i, j)) { // native ink: the background instead
					const Rgb &bg = pal[c.clean[size_t(j * 8 + i)]];
					rgba[p] = bg.r;
					rgba[p + 1] = bg.g;
					rgba[p + 2] = bg.b;
				}
			}
	}
}

void SharpText::maskOut(int x, int y) {
	if (x < 0 || y < 0 || x >= _w || y >= _h)
		return;
	_map[(size_t(y) * size_t(_w) + size_t(x)) * 4 + 3] = 0;
}

} // namespace gob
