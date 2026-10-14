// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/surface.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace gob {

void Surface::blit(const Surface &from, int left, int top, int right, int bottom, int x, int y, int transp,
                   bool mirror) {
	if (x >= _w || y >= _h)
		return;
	if (left > right)
		std::swap(left, right);
	if (top > bottom)
		std::swap(top, bottom);
	if (left >= from._w || top >= from._h || right < 0 || bottom < 0)
		return;

	// Clip against the source, then the destination.
	if (left < 0) { x -= left; left = 0; }
	if (top < 0) { y -= top; top = 0; }
	if (x < 0) { left -= x; x = 0; }
	if (y < 0) { top -= y; y = 0; }
	right = std::min(right, std::min(from._w, _w - x + left) - 1);
	bottom = std::min(bottom, std::min(from._h, _h - y + top) - 1);
	if (right < left || bottom < top)
		return;

	int width = right - left + 1;
	int height = bottom - top + 1;

	if (_src) // first: it looks at the pixels being replaced
		blitSources(from, left, top, width, height, x, y, transp, mirror);

	// Overlapping self-blits must behave like memmove, row by row.
	std::vector<uint8_t> tmp;
	const uint8_t *src = from._px.data();
	int srcPitch = from._w;
	if (&from == this) {
		tmp.assign(from._px.begin(), from._px.end());
		src = tmp.data();
	}

	for (int row = 0; row < height; row++) {
		const uint8_t *s = src + size_t(top + row) * srcPitch + left;
		uint8_t *d = _px.data() + size_t(y + row) * _w + x;
		if (transp < 0 && !mirror) {
			std::memmove(d, s, size_t(width));
			continue;
		}
		for (int i = 0; i < width; i++) {
			uint8_t p = mirror ? s[width - 1 - i] : s[i];
			if (transp >= 0 && p == transp)
				continue;
			d[i] = p;
		}
	}

	if (_hd)
		blitHd(from, left, top, width, height, x, y, transp, mirror);
}

void Surface::blitHd(const Surface &from, int left, int top, int width, int height, int x, int y, int transp,
                     bool mirror) {
	HdLayer &d = *_hd;
	const int n = d.scale;
	const HdLayer *s = from._hd.get();
	if (s && s->scale != n)
		s = nullptr; // never happens: every layer uses the compositor's scale

	// The indexed picture the mirrored operations produced: the source
	// layer's own when it has one (so a surface changed behind the layer's
	// back stays detectable after copies), else the source's pixels.
	const uint8_t *snat = s ? s->nat.data() : from._px.data();
	std::vector<uint8_t> natTmp;
	if (&from == this) {
		natTmp.assign(snat, snat + size_t(_w) * _h);
		snat = natTmp.data();
	}
	for (int row = 0; row < height; row++) {
		const uint8_t *sr = snat + size_t(top + row) * from._w + left;
		uint8_t *dr = d.nat.data() + size_t(y + row) * _w + x;
		if (transp < 0 && !mirror) {
			std::memmove(dr, sr, size_t(width));
			continue;
		}
		for (int i = 0; i < width; i++) {
			uint8_t p = mirror ? sr[width - 1 - i] : sr[i];
			if (transp >= 0 && p == transp)
				continue;
			dr[i] = p;
		}
	}

	if (s) {
		const int sw = width * n, sh = height * n;
		const int sx = left * n, sy = top * n, dx = x * n, dy = y * n;
		// A copy of the source rectangle when blitting within one surface.
		std::vector<uint32_t> pxTmp;
		std::vector<uint8_t> idxTmp;
		const uint32_t *spx = s->px.data() + size_t(sy) * s->w + sx;
		const uint8_t *sidx = s->idx.data() + size_t(sy) * s->w + sx;
		size_t pitch = size_t(s->w);
		if (&from == this) {
			pxTmp.resize(size_t(sw) * sh);
			idxTmp.resize(size_t(sw) * sh);
			for (int r = 0; r < sh; r++) {
				std::memcpy(&pxTmp[size_t(r) * sw], spx + size_t(r) * pitch, size_t(sw) * 4);
				std::memcpy(&idxTmp[size_t(r) * sw], sidx + size_t(r) * pitch, size_t(sw));
			}
			spx = pxTmp.data();
			sidx = idxTmp.data();
			pitch = size_t(sw);
		}
		for (int r = 0; r < sh; r++) {
			const uint32_t *sp = spx + size_t(r) * pitch;
			const uint8_t *si = sidx + size_t(r) * pitch;
			uint32_t *dp = d.px.data() + size_t(dy + r) * d.w + dx;
			uint8_t *di = d.idx.data() + size_t(dy + r) * d.w + dx;
			if (transp < 0 && !mirror) {
				std::memcpy(dp, sp, size_t(sw) * 4);
				std::memcpy(di, si, size_t(sw));
				continue;
			}
			for (int i = 0; i < sw; i++) {
				const int j = mirror ? sw - 1 - i : i;
				const uint8_t ix = si[j];
				if (transp >= 0 && ix == transp)
					continue;
				dp[i] = sp[j];
				di[i] = ix;
			}
		}
	} else {
		// No layer: the source's indexed pixels as live blocks.
		for (int row = 0; row < height; row++) {
			const uint8_t *sr = from._px.data() + size_t(top + row) * from._w + left;
			for (int i = 0; i < width; i++) {
				uint8_t p = mirror ? sr[width - 1 - i] : sr[i];
				if (transp >= 0 && p == transp)
					continue;
				for (int by = 0; by < n; by++) {
					size_t o = size_t((y + row) * n + by) * d.w + size_t((x + i) * n);
					std::fill_n(d.px.data() + o, n, HdLayer::pack(0, 0, 0, HdLayer::Live));
					std::fill_n(d.idx.data() + o, n, p);
				}
			}
		}
	}
	d.markDirty(x, y, x + width - 1, y + height - 1);
}

void Surface::fillHd(int left, int top, int right, int bottom, uint8_t c) {
	HdLayer &d = *_hd;
	const int n = d.scale;
	for (int yy = top; yy <= bottom; yy++)
		std::memset(d.nat.data() + size_t(yy) * _w + left, c, size_t(right - left + 1));
	const uint32_t live = HdLayer::pack(0, 0, 0, HdLayer::Live);
	for (int yy = top * n; yy < (bottom + 1) * n; yy++) {
		size_t o = size_t(yy) * d.w + size_t(left) * n;
		size_t len = size_t(right - left + 1) * n;
		std::fill_n(d.px.data() + o, len, live);
		std::memset(d.idx.data() + o, c, len);
	}
	d.markDirty(left, top, right, bottom);
}

void Surface::blitSources(const Surface &from, int left, int top, int width, int height, int x, int y, int transp,
                          bool mirror) {
	PixelSources &d = *_src;
	const PixelSources *s = from._src.get();
	std::vector<uint32_t> topTmp, underTmp;
	const uint32_t *st = nullptr, *su = nullptr;
	if (s) {
		st = s->top.data();
		su = s->under.data();
		if (&from == this) {
			topTmp = s->top;
			underTmp = s->under;
			st = topTmp.data();
			su = underTmp.data();
		}
	}
	if (transp < 0 && !mirror) {
		for (int row = 0; row < height; row++) {
			const size_t so = size_t(top + row) * from._w + left, o = size_t(y + row) * _w + x;
			if (st) {
				std::memmove(&d.top[o], st + so, size_t(width) * 4);
				std::memmove(&d.under[o], su + so, size_t(width) * 4);
			} else {
				std::fill_n(&d.top[o], width, PixelSources::kLive);
				std::fill_n(&d.under[o], width, PixelSources::kNone);
			}
		}
		return;
	}
	const uint8_t *spx = from._px.data();
	const int fw = from._w, fh = from._h;
	auto clearAt = [&](int xx, int yy) {
		return xx < 0 || yy < 0 || xx >= fw || yy >= fh || spx[size_t(yy) * fw + xx] == transp;
	};
	for (int row = 0; row < height; row++) {
		const int sy = top + row;
		for (int i = 0; i < width; i++) {
			const int sx = mirror ? left + width - 1 - i : left + i;
			const size_t so = size_t(sy) * fw + size_t(sx), o = size_t(y + row) * _w + size_t(x + i);
			uint32_t t = st ? st[so] : PixelSources::kLive, u = su ? su[so] : PixelSources::kNone;
			if (mirror) {
				t = PixelSources::flip(t);
				u = PixelSources::flip(u);
			}
			if (transp < 0) {
				d.top[o] = t;
				d.under[o] = u;
				continue;
			}
			const bool clear = spx[so] == transp;
			if (!PixelSources::isRef(t) || transp != 0) {
				if (!clear) { // a plain pixel: its block is all this index
					d.top[o] = PixelSources::kLive;
					d.under[o] = PixelSources::kNone;
				}
				continue; // a clear one is skipped, in the HD layer too
			}
			// Only edge pixels of the picture need two entries: the HD
			// version of an opaque one may leave parts of its block clear,
			// that of a clear one may fill parts of its block.
			bool edge = false;
			for (int j = -1; j <= 1 && !edge; j++)
				for (int k = -1; k <= 1 && !edge; k++)
					edge = clearAt(sx + k, sy + j) != clear;
			if (clear) {
				if (!edge || d.under[o] != PixelSources::kNone)
					continue; // nothing of it shows (or one entry too many: dropped)
				d.under[o] = PixelSources::isRef(d.top[o]) ? d.top[o] : PixelSources::kIndex | _px[o];
				d.top[o] = t;
				continue;
			}
			uint32_t below = PixelSources::kNone;
			if (edge) {
				// What shows here now: the top entry, or below it when the
				// top is a clear pixel's fill.
				const uint32_t ot = d.top[o], ou = d.under[o];
				if (ou != PixelSources::kNone && PixelSources::isClear(ot))
					below = ou;
				else
					below = PixelSources::isRef(ot) ? ot : PixelSources::kIndex | _px[o];
			}
			d.top[o] = t;
			d.under[o] = below;
		}
	}
}

void Surface::clearSources(int left, int top, int right, int bottom) {
	for (int yy = top; yy <= bottom; yy++) {
		const size_t o = size_t(yy) * _w + size_t(left);
		std::fill_n(&_src->top[o], right - left + 1, PixelSources::kLive);
		std::fill_n(&_src->under[o], right - left + 1, PixelSources::kNone);
	}
}

void Surface::trackSources(bool on) {
	if (!on) {
		_src.reset();
	} else if (!_src) {
		_src = std::make_unique<PixelSources>();
		_src->init(_px.size());
	}
}

void Surface::setPictureSources(int pic) {
	if (!_src || pic <= 0 || pic > PixelSources::kMaxPictures || _w > PixelSources::kMaxSide ||
	    _h > PixelSources::kMaxSide)
		return;
	for (int yy = 0; yy < _h; yy++)
		for (int xx = 0; xx < _w; xx++) {
			_src->top[size_t(yy) * _w + xx] = PixelSources::ref(pic, xx, yy, false, _px[size_t(yy) * _w + xx] == 0);
			_src->under[size_t(yy) * _w + xx] = PixelSources::kNone;
		}
}

size_t Surface::rebuildHd(int scale, const std::function<const HdLayer *(int pic)> &picture,
                          const std::vector<uint8_t> *only) {
	if (only && (!_src || !_hd || _hd->scale != scale))
		return 0;
	if (!only)
		attachHd(scale); // every pixel live; the pixels from pictures follow
	if (!_src)
		return 0;
	HdLayer &d = *_hd;
	auto selected = [&](uint32_t v) {
		return PixelSources::isRef(v) && size_t(PixelSources::picture(v)) < only->size() &&
		       (*only)[size_t(PixelSources::picture(v))];
	};
	const int n = d.scale;
	int lastId = -1;
	const HdLayer *last = nullptr;
	auto layerOf = [&](uint32_t v) -> const HdLayer * {
		const int id = PixelSources::picture(v);
		if (id != lastId) {
			lastId = id;
			last = picture(id);
			if (last && (last->scale != n || last->w % n || last->h % n ||
			             last->nat.size() != size_t(last->w / n) * size_t(last->h / n)))
				last = nullptr;
		}
		return last;
	};
	// The original index under a reference, or false if out of the picture.
	auto natAt = [&](const HdLayer *l, uint32_t v, uint8_t &out) {
		const int pw = l->w / n, ph = l->h / n, px = PixelSources::x(v), py = PixelSources::y(v);
		if (px >= pw || py >= ph)
			return false;
		out = l->nat[size_t(py) * pw + px];
		return true;
	};
	auto copyBlock = [&](const HdLayer &l, uint32_t v, int x, int y, bool transparent) {
		const int sx = PixelSources::x(v) * n, sy = PixelSources::y(v) * n;
		const bool mirror = PixelSources::mirrored(v);
		for (int j = 0; j < n; j++) {
			const size_t so = size_t(sy + j) * l.w + size_t(sx);
			const size_t o = size_t(y * n + j) * d.w + size_t(x) * n;
			for (int i = 0; i < n; i++) {
				const size_t s = so + size_t(mirror ? n - 1 - i : i);
				if (transparent && l.idx[s] == 0)
					continue;
				d.px[o + size_t(i)] = l.px[s];
				d.idx[o + size_t(i)] = l.idx[s];
			}
		}
	};
	size_t lost = 0;
	for (int y = 0; y < _h; y++)
		for (int x = 0; x < _w; x++) {
			const size_t o = size_t(y) * _w + x;
			const uint32_t t = _src->top[o], u = _src->under[o];
			if (only) {
				if (!selected(t) && !selected(u))
					continue;
				// Start the block live, as attachHd does.
				for (int j = 0; j < n; j++) {
					const size_t b = size_t(y * n + j) * d.w + size_t(x) * n;
					std::fill_n(d.px.data() + b, n, HdLayer::pack(0, 0, 0, HdLayer::Live));
					std::fill_n(d.idx.data() + b, n, _px[o]);
				}
				d.nat[o] = _px[o];
				d.markDirty(x, y, x, y);
			}
			if (!PixelSources::isRef(t))
				continue; // live: attachHd made it
			const HdLayer *tl = layerOf(t);
			uint8_t tv = 0, uv = 0;
			if (!tl || !natAt(tl, t, tv)) {
				lost++;
				continue;
			}
			const HdLayer *ul = nullptr;
			if (PixelSources::isRef(u)) {
				ul = layerOf(u);
				if (!ul || !natAt(ul, u, uv)) {
					lost++;
					continue;
				}
			} else if (u != PixelSources::kNone) {
				uv = uint8_t(u & 0xFF);
			}
			const uint8_t expect = u == PixelSources::kNone || tv != 0 ? tv : uv;
			if (expect != _px[o]) {
				lost++;
				continue;
			}
			if (u != PixelSources::kNone) {
				if (ul) {
					copyBlock(*ul, u, x, y, false);
				} else {
					for (int j = 0; j < n; j++) {
						const size_t b = size_t(y * n + j) * d.w + size_t(x) * n;
						std::fill_n(d.px.data() + b, n, HdLayer::pack(0, 0, 0, HdLayer::Live));
						std::fill_n(d.idx.data() + b, n, uv);
					}
				}
			}
			copyBlock(*tl, t, x, y, u != PixelSources::kNone);
		}
	return lost;
}

void Surface::clear(uint8_t c) {
	std::fill(_px.begin(), _px.end(), c);
	if (_src)
		_src->init(_px.size());
	if (_hd && _w > 0 && _h > 0)
		fillHd(0, 0, _w - 1, _h - 1, c);
}

void Surface::attachHd(int scale) {
	auto layer = std::make_unique<HdLayer>();
	layer->init(_w, _h, std::max(1, scale));
	const int n = layer->scale;
	layer->nat = _px;
	for (int yy = 0; yy < layer->h; yy++)
		for (int xx = 0; xx < layer->w; xx++)
			layer->idx[size_t(yy) * layer->w + xx] = _px[size_t(yy / n) * _w + xx / n];
	_hd = std::move(layer);
}

void Surface::setHd(std::unique_ptr<HdLayer> layer) {
	if (layer && (layer->w != _w * layer->scale || layer->h != _h * layer->scale ||
	              layer->px.size() != size_t(layer->w) * layer->h || layer->idx.size() != layer->px.size() ||
	              layer->nat.size() != _px.size()))
		layer.reset();
	_hd = std::move(layer);
}

void Surface::fillRect(int left, int top, int right, int bottom, uint8_t c) {
	if (left > right)
		std::swap(left, right);
	if (top > bottom)
		std::swap(top, bottom);
	if (left >= _w || top >= _h || right < 0 || bottom < 0)
		return;
	left = std::clamp(left, 0, _w - 1);
	top = std::clamp(top, 0, _h - 1);
	right = std::clamp(right, 0, _w - 1);
	bottom = std::clamp(bottom, 0, _h - 1);
	for (int yy = top; yy <= bottom; yy++)
		std::memset(_px.data() + size_t(yy) * _w + left, c, size_t(right - left + 1));
	if (_src)
		clearSources(left, top, right, bottom);
	if (_hd)
		fillHd(left, top, right, bottom, c);
}

void Surface::drawLine(int x0, int y0, int x1, int y1, uint8_t c) {
	int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
	int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	while (true) {
		putPixel(x0, y0, c);
		if (x0 == x1 && y0 == y1)
			break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

void Surface::drawCircle(int x0, int y0, int radius, uint8_t c) {
	int f = 1 - radius, ddFx = 0, ddFy = -2 * radius, x = 0, y = radius;
	putPixel(x0, y0 + radius, c);
	putPixel(x0, y0 - radius, c);
	putPixel(x0 + radius, y0, c);
	putPixel(x0 - radius, y0, c);
	while (x < y) {
		if (f >= 0) { y--; ddFy += 2; f += ddFy; }
		x++;
		ddFx += 2;
		f += ddFx + 1;
		putPixel(x0 + x, y0 + y, c); putPixel(x0 - x, y0 + y, c);
		putPixel(x0 + x, y0 - y, c); putPixel(x0 - x, y0 - y, c);
		putPixel(x0 + y, y0 + x, c); putPixel(x0 - y, y0 + x, c);
		putPixel(x0 + y, y0 - x, c); putPixel(x0 - y, y0 - x, c);
	}
}

} // namespace gob
