// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/hdlayer.h"
#include "gfx/pixelsource.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace gob {

// 8-bit indexed drawing surface. Coordinates follow the original engine's
// conventions: rectangles are inclusive (left, top, right, bottom). A
// surface can carry an HdLayer (gfx/hdlayer.h) that every drawing operation
// keeps in step, and can track where its pixels came from (PixelSources,
// gfx/pixelsource.h) so that the layer can be rebuilt for another pack.
class Surface {
public:
	Surface() = default;
	Surface(int width, int height) : _w(width), _h(height), _px(size_t(width) * height, 0) {}

	int width() const { return _w; }
	int height() const { return _h; }
	uint8_t *data() { return _px.data(); }
	const uint8_t *data() const { return _px.data(); }

	uint8_t get(int x, int y) const { return (x >= 0 && y >= 0 && x < _w && y < _h) ? _px[size_t(y) * _w + x] : 0; }
	void putPixel(int x, int y, uint8_t c) {
		if (x >= 0 && y >= 0 && x < _w && y < _h) {
			_px[size_t(y) * _w + x] = c;
			if (_src)
				_src->top[size_t(y) * _w + x] = _src->under[size_t(y) * _w + x] = 0;
			if (_hd)
				fillHd(x, y, x, y, c);
		}
	}

	void clear(uint8_t c = 0);

	// Copies the inclusive source rectangle to (x, y), clipped to both
	// surfaces. `transp` is a color index to skip, or -1 for an opaque copy.
	void blit(const Surface &from, int left, int top, int right, int bottom, int x, int y, int transp = -1,
	          bool mirror = false);
	void blit(const Surface &from) { blit(from, 0, 0, from._w - 1, from._h - 1, 0, 0); }

	void fillRect(int left, int top, int right, int bottom, uint8_t c);
	void drawLine(int x0, int y0, int x1, int y1, uint8_t c);
	void drawCircle(int x0, int y0, int radius, uint8_t c);

	// --- HD layer
	HdLayer *hd() { return _hd.get(); }
	const HdLayer *hd() const { return _hd.get(); }
	// A layer at `scale` holding the indexed pixels as live pixels.
	void attachHd(int scale);
	// Takes a prepared layer (its size must match; a wrong one is dropped).
	void setHd(std::unique_ptr<HdLayer> layer);
	void detachHd() { _hd.reset(); }

	// --- pixel sources
	const PixelSources *sources() const { return _src.get(); }
	// Starts tracking (every pixel live) or stops.
	void trackSources(bool on);
	// Marks every pixel as pixel (x, y) of registered picture `pic`.
	void setPictureSources(int pic);
	// Makes the layer again at `scale` from the pixel sources: a pixel from
	// a picture takes its block from `picture(id)`, the picture's layer at
	// the same scale (its `nat` holds the original pixels; null if it can't
	// be had). Pixels whose sources don't explain their value (changed
	// behind the operations' back, or a picture missing) become live.
	// Without sources, the same as attachHd. Returns the pixels that came
	// from a picture but had to be made live. With `only` (indexed by
	// picture id), only the pixels from those pictures are made again and
	// the rest of the layer (which must be at `scale`) is kept.
	size_t rebuildHd(int scale, const std::function<const HdLayer *(int pic)> &picture,
	                 const std::vector<uint8_t> *only = nullptr);

private:
	void fillHd(int left, int top, int right, int bottom, uint8_t c);
	void blitHd(const Surface &from, int left, int top, int width, int height, int x, int y, int transp,
	            bool mirror);
	void blitSources(const Surface &from, int left, int top, int width, int height, int x, int y, int transp,
	                 bool mirror);
	void clearSources(int left, int top, int right, int bottom);

	int _w = 0;
	int _h = 0;
	std::vector<uint8_t> _px;
	std::unique_ptr<HdLayer> _hd;
	std::unique_ptr<PixelSources> _src;
};

} // namespace gob
