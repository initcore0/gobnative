// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/hdrender.h"

#include "gfx/hdpack.h"
#include "gfx/pixelfilter.h"
#include "gfx/smooth.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace gob {

namespace {

uint8_t conv6(uint8_t v) {
	v &= 0x3F;
	return uint8_t((v << 2) | (v >> 4));
}

bool allBlack(const VgaPalette &p) {
	for (uint8_t v : p)
		if (v & 0x3F)
			return false;
	return true;
}

// Scale2x (EPX) on palette indices: every output pixel is a copy of the
// pixel or one of its neighbours, so no new colours (and no new
// transparency) appear.
std::vector<uint8_t> scale2x(const std::vector<uint8_t> &src, int w, int h) {
	std::vector<uint8_t> out(size_t(w) * h * 4);
	auto at = [&](int x, int y) { return src[size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
	const int ow = w * 2;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const uint8_t b = at(x, y - 1), d = at(x - 1, y), e = at(x, y), f = at(x + 1, y), hh = at(x, y + 1);
			uint8_t e0 = e, e1 = e, e2 = e, e3 = e;
			if (b != hh && d != f) {
				e0 = d == b ? d : e;
				e1 = b == f ? f : e;
				e2 = d == hh ? d : e;
				e3 = hh == f ? f : e;
			}
			uint8_t *o = &out[size_t(y * 2) * ow + x * 2];
			o[0] = e0;
			o[1] = e1;
			o[ow] = e2;
			o[ow + 1] = e3;
		}
	return out;
}

// Scale3x (AdvMAME3x) on palette indices.
std::vector<uint8_t> scale3x(const std::vector<uint8_t> &src, int w, int h) {
	std::vector<uint8_t> out(size_t(w) * h * 9);
	auto at = [&](int x, int y) { return src[size_t(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)]; };
	const int ow = w * 3;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const uint8_t a = at(x - 1, y - 1), b = at(x, y - 1), c = at(x + 1, y - 1);
			const uint8_t d = at(x - 1, y), e = at(x, y), f = at(x + 1, y);
			const uint8_t g = at(x - 1, y + 1), hh = at(x, y + 1), i = at(x + 1, y + 1);
			uint8_t r[9] = {e, e, e, e, e, e, e, e, e};
			if (b != hh && d != f) {
				r[0] = d == b ? d : e;
				r[1] = (d == b && e != c) || (b == f && e != a) ? b : e;
				r[2] = b == f ? f : e;
				r[3] = (d == b && e != g) || (d == hh && e != a) ? d : e;
				r[5] = (b == f && e != i) || (hh == f && e != c) ? f : e;
				r[6] = d == hh ? d : e;
				r[7] = (d == hh && e != i) || (hh == f && e != g) ? hh : e;
				r[8] = hh == f ? f : e;
			}
			for (int j = 0; j < 3; j++)
				for (int k = 0; k < 3; k++)
					out[size_t(y * 3 + j) * ow + x * 3 + k] = r[j * 3 + k];
		}
	return out;
}

} // namespace

void HdRenderer::attach(Draw &draw, HdPack *pack, int scale, std::function<std::string()> totName) {
	detach();
	_draw = &draw;
	_pack = pack;
	_scale = std::max(1, scale);
	_totName = std::move(totName);
	_stats = Stats();
	_fw = Draw::kScreenW * _scale;
	_fh = Draw::kScreenH * _scale;
	_frame.assign(size_t(_fw) * _fh, 0xFF000000u);
	const size_t tiles = size_t((Draw::kScreenW + HdLayer::kTileW - 1) / HdLayer::kTileW) *
	                     size_t((Draw::kScreenH + HdLayer::kTileH - 1) / HdLayer::kTileH);
	_pending.assign(tiles, 0);
	_upload.assign(tiles, 0);
	_full = true;
	// The reference palette: the room's, also when attached during a fade.
	setReference(draw.litPalette);
	_haveRef = !allBlack(_ref);
	_paletteDependent.clear();
	_lastTot.clear();
	{
		std::lock_guard<std::mutex> lock(_filteredLock); // made for another scale
		_filtered.clear();
		_filteredBytes = 0;
	}
	draw.pictureHook = [this](Surface &pic, int resId, bool transparent, PictureUse use) {
		decorate(pic, resId, transparent, use);
	};
	rebuild();
}

void HdRenderer::rebuild() {
	if (!_draw)
		return;
	const auto t0 = std::chrono::steady_clock::now();
	_full = true;
	_stats.rebuilds++;
	if (!_draw->trackingSources())
		_draw->setHdScale(_scale);
	else
		rebuildPictures(false);
	_stats.rebuildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

void HdRenderer::rebuildPictures(bool paletteDependentOnly) {
	// The pictures the surfaces show (and hold for later copies). For the
	// palette-dependent ones only: those, and the pictures sharing a pixel
	// with them (two entries), whose layers the blocks need too.
	_paletteDependent.resize(PixelSources::kMaxPictures + 1, 0);
	std::vector<uint8_t> used(PixelSources::kMaxPictures + 1, 0), only;
	auto dependent = [&](uint32_t v) {
		return PixelSources::isRef(v) && _paletteDependent[size_t(PixelSources::picture(v))];
	};
	if (paletteDependentOnly)
		only.assign(PixelSources::kMaxPictures + 1, 0);
	for (Surface *s : _draw->surfaces())
		if (const PixelSources *src = s->sources())
			for (size_t o = 0; o < src->top.size(); o++) {
				const uint32_t t = src->top[o], u = src->under[o];
				if (paletteDependentOnly) {
					if (!dependent(t) && !dependent(u))
						continue;
					for (uint32_t v : {t, u})
						if (dependent(v))
							only[size_t(PixelSources::picture(v))] = 1;
				}
				if (PixelSources::isRef(t))
					used[size_t(PixelSources::picture(t))] = 1;
				if (PixelSources::isRef(u))
					used[size_t(PixelSources::picture(u))] = 1;
			}
	std::vector<int> ids;
	for (int id = 1; id <= PixelSources::kMaxPictures; id++)
		if (used[size_t(id)] && _draw->pictureSource(id))
			ids.push_back(id);
	if (paletteDependentOnly && ids.empty())
		return;

	// Decorated again, on up to four threads (the pack decodes outside its lock).
	std::vector<std::unique_ptr<HdLayer>> layers(ids.size());
	std::vector<uint8_t> fromPack(ids.size(), 0);
	std::atomic<size_t> next{0};
	auto work = [&] {
		for (size_t i; (i = next++) < ids.size();) {
			const Draw::PictureSource *p = _draw->pictureSource(ids[i]);
			Surface pic(p->width, p->height);
			std::copy(p->pixels.begin(), p->pixels.end(), pic.data());
			bool hd = false;
			layers[i] = makeLayer(pic, p->tot, p->resId, p->transparent, p->use, hd);
			fromPack[i] = hd;
		}
	};
#ifdef __EMSCRIPTEN__
	const size_t threads = 1; // the web build has no threads
#else
	const size_t threads = std::min<size_t>(ids.size(), 4);
#endif
	std::vector<std::thread> pool;
	for (size_t t = 1; t < threads; t++)
		pool.emplace_back(work);
	work();
	for (std::thread &t : pool)
		t.join();

	std::vector<const HdLayer *> byId(PixelSources::kMaxPictures + 1, nullptr);
	for (size_t i = 0; i < ids.size(); i++) {
		byId[size_t(ids[i])] = layers[i].get();
		if (!paletteDependentOnly) {
			_paletteDependent[size_t(ids[i])] = !fromPack[i] && paletteDependent();
			(fromPack[i] ? _stats.hdPictures : _stats.nativePictures)++;
		}
	}
	const size_t lost = _draw->rebuildHd(
		_scale, [&](int id) { return id >= 0 && size_t(id) < byId.size() ? byId[size_t(id)] : nullptr; },
		paletteDependentOnly ? &only : nullptr);
	if (!paletteDependentOnly) {
		_stats.rebuildLost = lost;
		_stats.rebuildPictures = ids.size();
	}
}

void HdRenderer::setReference(const VgaPalette &ref) {
	_ref = ref;
	_haveRef = true;
	for (int i = 0; i < 256; i++)
		_refRgb[size_t(i)] = uint32_t(conv6(ref[size_t(i) * 3])) | uint32_t(conv6(ref[size_t(i) * 3 + 1])) << 8 |
		                     uint32_t(conv6(ref[size_t(i) * 3 + 2])) << 16;
	_haveRefRgb = !allBlack(ref);
}

void HdRenderer::detach() {
	if (_draw) {
		_draw->pictureHook = nullptr;
		_draw->setHdScale(0);
	}
	_draw = nullptr;
	_pack = nullptr;
	_scale = 0;
	_frame.clear();
	_fw = _fh = 0;
}

void HdRenderer::setOptions(const HdRenderOptions &o) {
	if (o.tint != _options.tint)
		_full = true;
	const bool redecorate = o.filter != _options.filter || o.roomsOnly != _options.roomsOnly ||
	                        o.smoothSilhouettes != _options.smoothSilhouettes;
	_options = o;
	if (redecorate) {
		{
			std::lock_guard<std::mutex> lock(_filteredLock);
			_filtered.clear();
			_filteredBytes = 0;
		}
		rebuild(); // pictures decorated differently
	}
}

void HdRenderer::decorate(Surface &pic, int resId, bool transparent, PictureUse use) {
	std::string tot;
	if (_pack && _totName) {
		tot = _totName();
		if (tot != _lastTot) {
			// A new script file starts drawing: decode the rest of its
			// pictures on other threads meanwhile.
			_lastTot = tot;
			_pack->prefetch(tot);
		}
	}
	bool fromPack = false;
	if (auto layer = makeLayer(pic, tot, resId, transparent, use, fromPack)) {
		(fromPack ? _stats.hdPictures : _stats.nativePictures)++;
		if (const PixelSources *src = pic.sources(); src && !src->top.empty() && PixelSources::isRef(src->top[0])) {
			_paletteDependent.resize(PixelSources::kMaxPictures + 1, 0);
			_paletteDependent[size_t(PixelSources::picture(src->top[0]))] = !fromPack && paletteDependent();
		}
		pic.setHd(std::move(layer));
	}
}

std::unique_ptr<HdLayer> HdRenderer::makeLayer(const Surface &pic, const std::string &tot, int resId,
                                               bool transparent, PictureUse use, bool &fromPack) const {
	fromPack = false;
	const int n = _scale, w = pic.width(), h = pic.height();
	if (n < 1 || w <= 0 || h <= 0)
		return nullptr;
	auto layer = std::make_unique<HdLayer>();
	layer->init(w, h, n);
	const uint8_t *nat = pic.data();
	layer->nat.assign(nat, nat + size_t(w) * h);
	const int lw = layer->w, lh = layer->h;

	std::shared_ptr<const HdImage> img;
	if (_pack && !tot.empty() && (!_options.roomsOnly || use == PictureUse::Screen))
		img = _pack->find(tot, resId);
	if (img && (img->origWidth != w || img->origHeight != h || img->scale < 1))
		img = nullptr;

	if (img) {
		const int m = img->scale;
		const uint8_t *rgba = img->rgba.data();
		const int iw = img->width;
		const bool nearest = m == n || n % m == 0; // the same scale, or enlarged by nearest neighbour
		std::vector<int> srcCol(size_t(lw), 0), natCol(size_t(lw), 0);
		for (int x = 0; x < lw; x++) {
			srcCol[size_t(x)] = x * m / n;
			natCol[size_t(x)] = x / n;
		}
		for (int y = 0; y < lh; y++) {
			const uint8_t *srcRow = rgba + size_t(y * m / n) * iw * 4;
			const uint8_t *natRow = nat + size_t(y / n) * w;
			if (nearest && !transparent) { // backgrounds: the fast path
				uint32_t *dp = layer->px.data() + size_t(y) * lw;
				uint8_t *di = layer->idx.data() + size_t(y) * lw;
				for (int x = 0; x < lw; x++) {
					const uint8_t *p = srcRow + size_t(srcCol[size_t(x)]) * 4;
					dp[x] = HdLayer::pack(p[0], p[1], p[2], HdLayer::Hd);
					di[x] = natRow[natCol[size_t(x)]];
				}
				continue;
			}
			for (int x = 0; x < lw; x++) {
				int r, g, b, a;
				if (nearest) {
					const uint8_t *p = srcRow + size_t(srcCol[size_t(x)]) * 4;
					r = p[0], g = p[1], b = p[2], a = p[3];
				} else if (m % n == 0) { // reduced by averaging
					const int k = m / n;
					int sr = 0, sg = 0, sb = 0;
					for (int j = 0; j < k; j++)
						for (int i = 0; i < k; i++) {
							const uint8_t *p = rgba + (size_t(y * k + j) * iw + size_t(x * k + i)) * 4;
							sr += p[0], sg += p[1], sb += p[2];
						}
					r = sr / (k * k), g = sg / (k * k), b = sb / (k * k);
					a = rgba[(size_t(y * k + k / 2) * iw + size_t(x * k + k / 2)) * 4 + 3];
				} else { // bilinear (3x pictures in a 2x/4x/8x compositor)
					const double fx = std::clamp((x + 0.5) * m / n - 0.5, 0.0, double(iw - 1));
					const double fy = std::clamp((y + 0.5) * m / n - 0.5, 0.0, double(img->height - 1));
					const int x0 = int(fx), y0 = int(fy);
					const int x1 = std::min(x0 + 1, iw - 1), y1 = std::min(y0 + 1, img->height - 1);
					const double tx = fx - x0, ty = fy - y0;
					auto px = [&](int xx, int yy, int c) { return double(rgba[(size_t(yy) * iw + xx) * 4 + c]); };
					int ch[3];
					for (int c = 0; c < 3; c++)
						ch[c] = int((px(x0, y0, c) * (1 - tx) + px(x1, y0, c) * tx) * (1 - ty) +
						            (px(x0, y1, c) * (1 - tx) + px(x1, y1, c) * tx) * ty + 0.5);
					r = ch[0], g = ch[1], b = ch[2];
					a = rgba[(size_t(std::lround(fy)) * iw + size_t(std::lround(fx))) * 4 + 3];
				}
				const int ox = x / n, oy = y / n;
				uint8_t p = nat[size_t(oy) * w + ox];
				if (transparent) {
					// Transparency follows the pack picture's alpha (the
					// original silhouette unless edge smoothing or a pack mask
					// changed it): an HD pixel outside it gets index 0; one
					// inside it over an originally transparent pixel takes a
					// neighbouring original index (for fades and re-tinting).
					if (a < 128) {
						p = 0;
					} else if (p == 0) {
						for (int j = -1; j <= 1 && !p; j++)
							for (int i = -1; i <= 1 && !p; i++) {
								const int xx = ox + i, yy = oy + j;
								if (xx >= 0 && yy >= 0 && xx < w && yy < h)
									p = nat[size_t(yy) * w + xx];
							}
					}
				}
				const size_t o = size_t(y) * lw + x;
				layer->px[o] = HdLayer::pack(uint8_t(r), uint8_t(g), uint8_t(b), HdLayer::Hd);
				layer->idx[o] = p;
			}
		}
		fromPack = true;
	} else {
		enlarge(pic, transparent, *layer);
	}
	return layer;
}

// A picture without HD art (or not taken from the pack), enlarged with the
// chosen filter.
void HdRenderer::enlarge(const Surface &pic, bool transparent, HdLayer &layer) const {
	const int n = layer.scale, w = pic.width(), h = pic.height(), lw = layer.w, lh = layer.h;
	const uint8_t *nat = pic.data();
	HdSpriteFilter filter = n > 1 ? _options.filter : HdSpriteFilter::Original;
	if (filter == HdSpriteFilter::Scale2x && n != 2 && n != 3 && n != 4 && n != 8)
		filter = HdSpriteFilter::Original;
	if (filter == HdSpriteFilter::Original) {
		for (int y = 0; y < lh; y++)
			for (int x = 0; x < lw; x++)
				layer.idx[size_t(y) * lw + x] = nat[size_t(y / n) * w + x / n];
		return;
	}

	// The smoothing filters: cached by content.
	uint64_t key = 1469598103934665603ull;
	auto hash = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
	hash(uint64_t(w) << 32 | uint64_t(h));
	const bool smoothOutline = transparent && _options.smoothSilhouettes &&
	                           (filter == HdSpriteFilter::SmoothEdges || filter == HdSpriteFilter::Smooth);
	hash(uint64_t(n) << 8 | uint64_t(filter) << 2 | (smoothOutline ? 2 : 0) | (transparent ? 1 : 0));
	const bool withPalette = filter == HdSpriteFilter::SmoothEdges && _haveRefRgb;
	if (withPalette)
		for (uint32_t c : _refRgb)
			hash(c);
	for (size_t i = 0; i < size_t(w) * h; i++)
		hash(nat[i]);
	{
		std::lock_guard<std::mutex> lock(_filteredLock);
		auto it = _filtered.find(key);
		if (it != _filtered.end() && it->second->w == lw && it->second->nat.size() == layer.nat.size() &&
		    std::equal(layer.nat.begin(), layer.nat.end(), it->second->nat.begin())) {
			layer.px = it->second->px;
			layer.idx = it->second->idx;
			return;
		}
	}

	if (filter == HdSpriteFilter::Scale2x) {
		std::vector<uint8_t> idx(nat, nat + size_t(w) * h);
		int s = 1;
		if (n == 3) {
			idx = scale3x(idx, w, h);
		} else {
			while (s < n) {
				idx = scale2x(idx, w * s, h * s);
				s *= 2;
			}
		}
		layer.idx = std::move(idx);
	} else {
		const std::vector<uint32_t> mix = filter == HdSpriteFilter::SmoothEdges ? smoothEdgesMix(nat, w, h, n, transparent, withPalette ? _refRgb.data() : nullptr)
		                                                                       : bilinearMix(nat, w, h, n, transparent);
		for (int y = 0; y < lh; y++)
			for (int x = 0; x < lw; x++) {
				const size_t o = size_t(y) * lw + x;
				const uint32_t m = mix[o];
				// The index is the original pixel's (transparency, fades of
				// the dominant colour); the colour mixes two indices.
				layer.idx[o] = nat[size_t(y / n) * w + x / n];
				if (m >> 16)
					layer.px[o] = m | uint32_t(HdLayer::Mix) << 24;
				else if ((m & 0xFF) != layer.idx[o])
					layer.px[o] = (m & 0xFF) | (m & 0xFF) << 8 | uint32_t(HdLayer::Mix) << 24; // another index, unmixed
			}
		if (smoothOutline) {
			// The outline smoothed (gfx/smooth.h): HD pixels outside it
			// become colour 0; inside it over an originally clear pixel they
			// take a neighbouring original index.
			std::vector<uint8_t> mask(size_t(w) * h);
			for (size_t i = 0; i < mask.size(); i++)
				mask[i] = nat[i] != 0;
			const std::vector<uint8_t> shape = smoothUpscaleMask(mask.data(), w, h, n);
			for (int y = 0; y < lh; y++)
				for (int x = 0; x < lw; x++) {
					const size_t o = size_t(y) * lw + x;
					const int ox = x / n, oy = y / n;
					if (!shape[o]) {
						layer.idx[o] = 0;
						layer.px[o] = HdLayer::pack(0, 0, 0, HdLayer::Live);
					} else if (layer.idx[o] == 0) {
						uint8_t p = 0;
						for (int j = -1; j <= 1 && !p; j++)
							for (int i = -1; i <= 1 && !p; i++) {
								const int xx = ox + i, yy = oy + j;
								if (xx >= 0 && yy >= 0 && xx < w && yy < h)
									p = nat[size_t(yy) * w + xx];
							}
						layer.idx[o] = p;
						layer.px[o] = HdLayer::pack(0, 0, 0, HdLayer::Live);
					}
				}
		}
	}
	auto copy = std::make_shared<HdLayer>(layer);
	std::lock_guard<std::mutex> lock(_filteredLock);
	const size_t bytes = copy->px.size() * 5 + copy->nat.size();
	if (_filteredBytes + bytes > (size_t(128) << 20)) {
		_filtered.clear();
		_filteredBytes = 0;
	}
	if (_filtered.emplace(key, std::move(copy)).second)
		_filteredBytes += bytes;
}

void HdRenderer::computeMode(const VgaPalette &s) {
	const VgaPalette &r = _ref;
	_mode = Retint;
	if (allBlack(s)) {
		// A black screen (also before any palette was loaded, when the
		// reference is black too): everything black.
		_mode = FadeOut;
		_lut.fill(0);
	} else if (s == r) {
		_mode = Identity;
	} else {
		bool below = true;
		int d = 0, e = 0;
		for (size_t j = 0; j < s.size(); j++) {
			const int sv = s[j] & 0x3F, rv = r[j] & 0x3F;
			below &= sv <= rv;
			d = std::max(d, rv - sv);
			e = std::max(e, sv);
		}
		if (below) {
			bool out = true, in = true;
			for (size_t j = 0; j < s.size(); j++) {
				const int sv = s[j] & 0x3F, rv = r[j] & 0x3F;
				out &= sv == std::max(rv - d, 0);
				in &= sv == std::min(rv, e);
			}
			// The game's fades step every channel by the same amount toward
			// black or up from black: map HD colours the same way.
			if (out) {
				_mode = FadeOut;
				const int sub = conv6(uint8_t(d));
				for (int v = 0; v < 256; v++)
					_lut[size_t(v)] = uint8_t(std::max(v - sub, 0));
			} else if (in) {
				_mode = FadeIn;
				const int top = conv6(uint8_t(e));
				for (int v = 0; v < 256; v++)
					_lut[size_t(v)] = uint8_t(std::min(v, top));
			}
		}
	}
	for (int i = 0; i < 256; i++) {
		const uint8_t sr = conv6(s[size_t(i) * 3]), sg = conv6(s[size_t(i) * 3 + 1]), sb = conv6(s[size_t(i) * 3 + 2]);
		_off[size_t(i)] = {int16_t(sr - conv6(r[size_t(i) * 3])), int16_t(sg - conv6(r[size_t(i) * 3 + 1])),
		                   int16_t(sb - conv6(r[size_t(i) * 3 + 2]))};
		_live[size_t(i)] = uint32_t(sr) | uint32_t(sg) << 8 | uint32_t(sb) << 16 | 0xFF000000u;
		_repairedColour[size_t(i)] = _live[size_t(i)];
		if (_options.tint) {
			// Live pixels (no HD picture there) half blue, repaired ones half red.
			_live[size_t(i)] = uint32_t(sr / 2) | uint32_t(sg / 2) << 8 | uint32_t(sb / 2 + 128) << 16 | 0xFF000000u;
			_repairedColour[size_t(i)] = uint32_t(sr / 2 + 128) | uint32_t(sg / 2) << 8 | uint32_t(sb / 2) << 16 |
			                             0xFF000000u;
		}
	}
	_stats.mode = _mode;
}

uint32_t HdRenderer::resolve(uint32_t p, uint8_t idx) const {
	switch (HdLayer::kind(p)) {
	case HdLayer::Hd:
		break;
	case HdLayer::Repaired:
		return _repairedColour[idx];
	case HdLayer::Mix:
		return mixColour(p);
	default:
		return _live[idx];
	}
	const int r = int(p & 0xFF), g = int((p >> 8) & 0xFF), b = int((p >> 16) & 0xFF);
	switch (_mode) {
	case Identity:
		return (p & 0xFFFFFFu) | 0xFF000000u;
	case FadeOut:
	case FadeIn:
		return uint32_t(_lut[size_t(r)]) | uint32_t(_lut[size_t(g)]) << 8 | uint32_t(_lut[size_t(b)]) << 16 |
		       0xFF000000u;
	default: {
		const auto &o = _off[idx];
		return uint32_t(std::clamp(r + o[0], 0, 255)) | uint32_t(std::clamp(g + o[1], 0, 255)) << 8 |
		       uint32_t(std::clamp(b + o[2], 0, 255)) << 16 | 0xFF000000u;
	}
	}
}

void HdRenderer::repair(const Surface &front, HdLayer &layer) {
	const int w = front.width(), h = front.height(), n = layer.scale;
	const uint8_t *src = front.data();
	size_t repaired = 0;
	for (int y = 0; y < h; y++) {
		const uint8_t *s = src + size_t(y) * w;
		uint8_t *d = layer.nat.data() + size_t(y) * w;
		if (std::memcmp(s, d, size_t(w)) == 0)
			continue;
		for (int x = 0; x < w; x++) {
			if (s[x] == d[x])
				continue;
			d[x] = s[x];
			const uint32_t v = HdLayer::pack(0, 0, 0, HdLayer::Repaired);
			for (int j = 0; j < n; j++) {
				const size_t o = size_t(y * n + j) * layer.w + size_t(x) * n;
				std::fill_n(layer.px.data() + o, n, v);
				std::fill_n(layer.idx.data() + o, n, s[x]);
			}
			layer.markDirty(x, y, x, y);
			repaired++;
		}
	}
	_stats.lastRepaired = repaired;
	_stats.repairedPixels += repaired;
}

void HdRenderer::resolveRect(const HdLayer &layer, int left, int top, int right, int bottom) {
	const int n = layer.scale;
	const int x0 = left * n, x1 = (right + 1) * n;
	for (int y = top * n; y < (bottom + 1) * n; y++) {
		const uint32_t *s = layer.px.data() + size_t(y) * layer.w;
		const uint8_t *ix = layer.idx.data() + size_t(y) * layer.w;
		uint32_t *d = _frame.data() + size_t(y) * _fw;
		if (_mode == Identity) {
			for (int x = x0; x < x1; x++) {
				const uint32_t p = s[x];
				const uint32_t k = p >> 24;
				d[x] = k == HdLayer::Hd     ? (p | 0xFF000000u)
				       : k == HdLayer::Live ? _live[ix[x]]
				       : k == HdLayer::Mix  ? mixColour(p)
				                            : _repairedColour[ix[x]];
			}
		} else if (_mode == FadeOut || _mode == FadeIn) {
			const uint8_t *lut = _lut.data();
			for (int x = x0; x < x1; x++) {
				const uint32_t p = s[x];
				const uint32_t k = p >> 24;
				d[x] = k == HdLayer::Hd ? (uint32_t(lut[p & 0xFF]) | uint32_t(lut[(p >> 8) & 0xFF]) << 8 |
				                           uint32_t(lut[(p >> 16) & 0xFF]) << 16 | 0xFF000000u)
				       : k == HdLayer::Live ? _live[ix[x]]
				       : k == HdLayer::Mix  ? mixColour(p)
				                            : _repairedColour[ix[x]];
			}
		} else {
			for (int x = x0; x < x1; x++)
				d[x] = resolve(s[x], ix[x]);
		}
	}
	_stats.resolvedPixels += size_t(x1 - x0) * size_t((bottom - top + 1) * n);
}

void HdRenderer::markTiles(std::vector<uint8_t> &tiles, int left, int top, int right, int bottom) const {
	const int tx = (Draw::kScreenW + HdLayer::kTileW - 1) / HdLayer::kTileW;
	const int ty = (Draw::kScreenH + HdLayer::kTileH - 1) / HdLayer::kTileH;
	left = std::max(left, 0);
	top = std::max(top, 0);
	right = std::min(right, Draw::kScreenW - 1);
	bottom = std::min(bottom, Draw::kScreenH - 1);
	if (left > right || top > bottom || tiles.size() != size_t(tx) * ty)
		return;
	for (int y = top / HdLayer::kTileH; y <= bottom / HdLayer::kTileH; y++)
		for (int x = left / HdLayer::kTileW; x <= right / HdLayer::kTileW; x++)
			tiles[size_t(y) * tx + x] = 1;
}

void HdRenderer::overlay(int left, int top, int right, int bottom) {
	markTiles(_pending, left, top, right, bottom);
	markTiles(_upload, left, top, right, bottom);
}

bool HdRenderer::update() {
	if (!_draw)
		return false;
	Surface *shown = &_draw->screenSurface(); // the front surface or the view composed from it
	HdLayer *layer = shown->hd();
	if (!layer || layer->scale != _scale || shown->width() != Draw::kScreenW || shown->height() != Draw::kScreenH) {
		_draw->setHdScale(_scale); // a surface replaced behind our back: start it live
		shown = &_draw->screenSurface();
		layer = shown->hd();
		if (!layer)
			return false;
		_full = true;
	}
	_stats.resolvedPixels = 0;

	// The reference palette: what the room's pictures look like at full
	// brightness. Rooms fade to black (palette 54), draw, load their palette
	// and fade it in, so the last non-black staged palette is the one.
	const VgaPalette &staged = _draw->vgaPalette;
	if (!allBlack(staged) && (!_haveRef || staged != _ref)) {
		setReference(staged);
		_full = true;
		// Smoothed edges are found with the room's colours: the pictures
		// decorated under another palette are made again.
		if (paletteDependent() && _draw->trackingSources()) {
			const auto t0 = std::chrono::steady_clock::now();
			rebuildPictures(true);
			_stats.paletteRebuildMs =
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		}
	} else if (!_haveRef) {
		setReference(staged);
	}
	const VgaPalette &screen = _draw->screenVga();
	if (_full || screen != _lastScreen || _options.tint != _lastTint) {
		computeMode(screen);
		_lastScreen = screen;
		_lastTint = _options.tint;
		_full = true;
	}

	repair(*shown, *layer);

	std::fill(_upload.begin(), _upload.end(), 0);
	const int tx = layer->tilesX, ty = layer->tilesY;
	for (int y = 0; y < ty; y++)
		for (int x = 0; x < tx; x++) {
			const size_t t = size_t(y) * tx + x;
			if (!_full && !layer->dirty[t] && !_pending[t])
				continue;
			const int l = x * HdLayer::kTileW, tp = y * HdLayer::kTileH;
			resolveRect(*layer, l, tp, std::min(l + HdLayer::kTileW, Draw::kScreenW) - 1,
			            std::min(tp + HdLayer::kTileH, Draw::kScreenH) - 1);
			_upload[t] = 1;
		}
	std::fill(layer->dirty.begin(), layer->dirty.end(), 0);
	std::fill(_pending.begin(), _pending.end(), 0);
	_full = false;
	return true;
}

HdRenderer::Coverage HdRenderer::coverage() const {
	Coverage c;
	const HdLayer *layer = _draw ? _draw->screenSurface().hd() : nullptr;
	if (!layer)
		return c;
	const int n = layer->scale;
	for (int y = 0; y < layer->h; y += n)
		for (int x = 0; x < layer->w; x += n) {
			switch (HdLayer::kind(layer->px[size_t(y) * layer->w + x])) {
			case HdLayer::Hd: c.hd++; break; // mixes count as live: original pixels
			case HdLayer::Repaired: c.repaired++; break;
			default: c.live++; break;
			}
		}
	return c;
}

std::vector<HdRenderer::Rect> HdRenderer::changedRects() const {
	std::vector<Rect> out;
	const int tx = (Draw::kScreenW + HdLayer::kTileW - 1) / HdLayer::kTileW;
	const int ty = (Draw::kScreenH + HdLayer::kTileH - 1) / HdLayer::kTileH;
	if (_upload.size() != size_t(tx) * ty)
		return out;
	// Runs of changed tiles per tile row, merged with the same run in the
	// rows below.
	std::vector<std::pair<int, int>> open; // run start, end (tiles) -> index in out
	std::vector<size_t> openIdx;
	for (int y = 0; y < ty; y++) {
		std::vector<std::pair<int, int>> runs;
		for (int x = 0; x < tx; x++) {
			if (!_upload[size_t(y) * tx + x])
				continue;
			int e = x;
			while (e + 1 < tx && _upload[size_t(y) * tx + e + 1])
				e++;
			runs.push_back({x, e});
			x = e;
		}
		std::vector<std::pair<int, int>> nextOpen;
		std::vector<size_t> nextIdx;
		for (auto run : runs) {
			auto it = std::find(open.begin(), open.end(), run);
			size_t i;
			if (it != open.end()) {
				i = openIdx[size_t(it - open.begin())];
				out[i].h += HdLayer::kTileH * _scale;
			} else {
				Rect r;
				r.x = run.first * HdLayer::kTileW * _scale;
				r.y = y * HdLayer::kTileH * _scale;
				r.w = (run.second - run.first + 1) * HdLayer::kTileW * _scale;
				r.h = HdLayer::kTileH * _scale;
				i = out.size();
				out.push_back(r);
			}
			nextOpen.push_back(run);
			nextIdx.push_back(i);
		}
		open = std::move(nextOpen);
		openIdx = std::move(nextIdx);
	}
	for (Rect &r : out) { // clip to the frame (the last tile row/column may stick out)
		r.w = std::min(r.w, _fw - r.x);
		r.h = std::min(r.h, _fh - r.y);
	}
	return out;
}

} // namespace gob
