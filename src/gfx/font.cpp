// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/font.h"

namespace gob {

std::optional<Font> Font::load(const std::vector<uint8_t> &data) {
	if (data.size() < 4)
		return std::nullopt;

	Font f;
	bool hasWidths = data[0] & 0x80;
	f._width = data[0] & 0x7F;
	f._height = data[1];
	f._first = data[2];
	f._last = data[3];
	if (f._width == 0 || f._height == 0 || f._last < f._first)
		return std::nullopt;

	f._rowBytes = (f._width - 1) / 8 + 1;
	f._itemSize = f._rowBytes * f._height;
	size_t count = size_t(f._last - f._first + 1);
	size_t bitmapBytes = count * size_t(f._itemSize);
	if (data.size() < 4 + bitmapBytes)
		return std::nullopt;

	f._bitmaps.assign(data.begin() + 4, data.begin() + 4 + long(bitmapBytes));
	if (hasWidths && data.size() >= 4 + bitmapBytes + count)
		f._widths.assign(data.begin() + 4 + long(bitmapBytes), data.begin() + 4 + long(bitmapBytes + count));
	return f;
}

uint64_t Font::glyphBits8(uint8_t c) const {
	const uint8_t *g = glyph(c);
	if (!g || _width > 8 || _height > 8)
		return 0;
	uint64_t bits = 0;
	for (int y = 0; y < _height; y++)
		for (int x = 0; x < _width; x++)
			if (g[size_t(y) * size_t(_rowBytes)] & (0x80 >> x))
				bits |= uint64_t(1) << (y * 8 + x);
	return bits;
}

Font::Rows Font::glyphRows(uint8_t c) const {
	Rows rows{};
	const uint8_t *g = glyph(c);
	if (!g || _width > 8 || _height > 16)
		return rows;
	for (int y = 0; y < _height; y++)
		for (int x = 0; x < _width; x++)
			if (g[size_t(y) * size_t(_rowBytes)] & (0x80 >> x))
				rows[size_t(y)] |= uint8_t(1 << x);
	return rows;
}

const uint8_t *Font::glyph(uint8_t c) const {
	if (c < _first || c > _last)
		return nullptr;
	return _bitmaps.data() + size_t(c - _first) * size_t(_itemSize);
}

int Font::charWidth(uint8_t c) const {
	if (!_widths.empty() && c >= _first && c <= _last)
		return _widths[c - _first];
	return _width;
}

void Font::drawChar(Surface &dst, uint8_t c, int x, int y, uint8_t front, uint8_t back, bool transparent) const {
	if (c == '\r' || c == '\n')
		return;
	if (x < 0 || y < 0 || x >= dst.width() || y >= dst.height())
		return;
	const uint8_t *g = glyph(c);
	if (!g)
		return;

	for (int row = 0; row < _height; row++) {
		const uint8_t *bits = g + row * _rowBytes;
		for (int col = 0; col < _width; col++) {
			bool ink = bits[col / 8] & (0x80 >> (col % 8));
			if (ink)
				dst.putPixel(x + col, y + row, front);
			else if (!transparent)
				dst.putPixel(x + col, y + row, back);
		}
	}
}

int Font::drawString(Surface &dst, const std::string &s, int x, int y, uint8_t front, uint8_t back,
                     bool transparent) const {
	for (unsigned char c : s) {
		if (monospaced()) {
			if (x >= 0 && y >= 0 && x + _width <= dst.width() && y + _height <= dst.height())
				drawChar(dst, c, x, y, front, back, transparent);
			x += _width;
		} else {
			drawChar(dst, c, x, y, front, back, transparent);
			x += charWidth(c);
		}
	}
	return x;
}

} // namespace gob
