// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/surface.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gob {

// Bitmap font in the Coktel .LET format:
//   u8 width (bit 7: per-glyph width table follows), u8 height,
//   u8 firstChar, u8 lastChar, then one bitmap per glyph (rows top to
//   bottom, MSB = leftmost pixel), then the optional width table.
class Font {
public:
	static std::optional<Font> load(const std::vector<uint8_t> &data);

	int width() const { return _width; }
	int height() const { return _height; }
	bool monospaced() const { return _widths.empty(); }
	int charWidth(uint8_t c) const;

	// Draws one glyph. Ink pixels use `front`; background pixels use `back`
	// unless `transparent`. Clipped per pixel.
	void drawChar(Surface &dst, uint8_t c, int x, int y, uint8_t front, uint8_t back, bool transparent) const;

	// Draws a string as the engine's text operation does and returns the x
	// position after it. Monospaced fonts skip characters whose whole cell
	// doesn't fit on the surface (but still advance).
	int drawString(Surface &dst, const std::string &s, int x, int y, uint8_t front, uint8_t back,
	               bool transparent) const;

	bool hasChar(uint8_t c) const { return glyph(c) != nullptr; }
	// The ink of glyph `c` as 64 bits, bit (y * 8 + x) for pixel (x, y), for
	// fonts of at most 8x8 pixels (0 otherwise or when the glyph is missing).
	uint64_t glyphBits8(uint8_t c) const;
	// The ink of glyph `c` by rows, bit x of row y for pixel (x, y), for fonts
	// of at most 8x16 pixels (all zero otherwise or when it is missing).
	using Rows = std::array<uint8_t, 16>;
	Rows glyphRows(uint8_t c) const;

private:
	const uint8_t *glyph(uint8_t c) const;

	int _width = 0, _height = 0;
	int _first = 0, _last = -1;
	int _rowBytes = 0, _itemSize = 0;
	std::vector<uint8_t> _bitmaps;
	std::vector<uint8_t> _widths;
};

} // namespace gob
