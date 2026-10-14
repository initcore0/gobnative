// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sharp text: the game's text (glyphs up to 8x16) drawn at window resolution. Per frame,
// the glyph cells the engine reports as still on screen exactly as drawn
// (Draw::visibleText) are taken out of the 320x200 frame (their ink pixels
// get the background back) and described in two frame-sized maps that the
// presenter's text pass reads: which glyph (atlas slot and position inside
// the cell) covers each game pixel, and its ink colour. The glyphs
// themselves are signed distance fields of the game's own glyph bitmaps
// after edge smoothing (gfx/smooth.h), made once per distinct bitmap and
// kept in an atlas.

#pragma once

#include "engine/draw.h"
#include "gfx/image.h"

#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace gob {

class SharpText {
public:
	static constexpr int kSamples = 8;        // distance field samples per glyph pixel
	static constexpr int kCellStride = Draw::kCellH * kSamples + 2; // square cells, one-texel gutter on each side
	static constexpr int kSlotsPerRow = 32;
	static constexpr int kAtlasSize = kCellStride * kSlotsPerRow;
	static constexpr double kSpread = 1.0;    // glyph pixels from the outline to +-127

	// Fills the maps for `cells` and erases their native ink from `rgba`
	// (frameW x frameH RGBA, colours from `pal`). Call before the cursor is
	// drawn into the frame; then maskOut() each cursor pixel.
	void build(const std::vector<Draw::TextCell> &cells, const Palette &pal, uint8_t *rgba, int frameW, int frameH);
	void maskOut(int x, int y);
	void clear();

	bool empty() const { return _count == 0; }
	int cellCount() const { return _count; }
	int glyphCount() const { return int(_slots.size()); }

	// frameW x frameH RGBA each: map (R, G = atlas slot, B = x | y << 4 in
	// the cell, A = 255 where text is) and ink colours.
	const uint8_t *map() const { return _map.data(); }
	const uint8_t *colors() const { return _colors.data(); }
	// kAtlasSize^2 single-channel distance fields; dirty when slots were added.
	const uint8_t *atlas() const { return _atlas.data(); }
	bool atlasDirty() const { return _atlasDirty; }
	void atlasUploaded() { _atlasDirty = false; }

private:
	int slotFor(const Draw::TextCell &c);

	int _w = 0, _h = 0, _count = 0;
	std::vector<uint8_t> _map, _colors;
	std::vector<uint8_t> _atlas;
	bool _atlasDirty = false;
	std::map<std::tuple<Font::Rows, int, int>, int> _slots; // (ink, w, h) -> slot
};

} // namespace gob
