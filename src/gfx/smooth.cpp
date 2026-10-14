// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/smooth.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gob {

namespace {

constexpr double kRound = 0.3;  // corner rounding of a lone corner (sum of the two side distances)
constexpr double kFillet = 0.2; // fillet in an inner corner

inline bool at(const uint8_t *m, int w, int h, int x, int y) {
	return x >= 0 && y >= 0 && x < w && y < h && m[size_t(y) * size_t(w) + size_t(x)] != 0;
}

// 1D squared Euclidean distance transform (Felzenszwalb and Huttenlocher):
// the lower envelope of the parabolas rooted at each sample.
void edt1d(const float *f, int n, float *d, int *v, float *z) {
	auto cross = [&](int q, int p) {
		return ((f[q] - f[p]) + float(q * q - p * p)) / float(2 * (q - p));
	};
	int k = 0;
	v[0] = 0;
	z[0] = -std::numeric_limits<float>::infinity();
	z[1] = std::numeric_limits<float>::infinity();
	for (int q = 1; q < n; q++) {
		float s = cross(q, v[k]);
		while (s <= z[k]) {
			k--;
			s = cross(q, v[k]);
		}
		k++;
		v[k] = q;
		z[k] = s;
		z[k + 1] = std::numeric_limits<float>::infinity();
	}
	k = 0;
	for (int q = 0; q < n; q++) {
		while (z[k + 1] < float(q))
			k++;
		float dq = float(q - v[k]);
		d[q] = dq * dq + f[v[k]];
	}
}

// Squared distance from each pixel to the nearest pixel where `target` is
// true.
std::vector<float> edt2d(const std::vector<uint8_t> &img, int w, int h, bool target) {
	const float inf = 1e20f;
	std::vector<float> grid(img.size());
	for (size_t i = 0; i < img.size(); i++)
		grid[i] = (img[i] != 0) == target ? 0.0f : inf;
	const int n = std::max(w, h);
	const size_t len = size_t(n);
	std::vector<float> f(len), d(len), z(len + 1);
	std::vector<int> v(len);
	for (int x = 0; x < w; x++) {
		for (int y = 0; y < h; y++)
			f[size_t(y)] = grid[size_t(y) * size_t(w) + size_t(x)];
		edt1d(f.data(), h, d.data(), v.data(), z.data());
		for (int y = 0; y < h; y++)
			grid[size_t(y) * size_t(w) + size_t(x)] = d[size_t(y)];
	}
	for (int y = 0; y < h; y++) {
		float *row = &grid[size_t(y) * size_t(w)];
		std::copy(row, row + w, f.begin());
		edt1d(f.data(), w, d.data(), v.data(), z.data());
		std::copy(d.begin(), d.begin() + w, row);
	}
	return grid;
}

} // namespace

bool smoothShapeCovers(const uint8_t *m, int w, int h, int x, int y, double u, double v) {
	const int dx = u < 0.5 ? -1 : 1, dy = v < 0.5 ? -1 : 1;
	// Distances from the two pixel sides that meet at this quadrant's corner.
	const double a = dx < 0 ? u : 1.0 - u, b = dy < 0 ? v : 1.0 - v;
	const double s = a + b;
	const bool H = at(m, w, h, x + dx, y), V = at(m, w, h, x, y + dy), D = at(m, w, h, x + dx, y + dy);
	if (at(m, w, h, x, y)) {
		if (H || V || D)
			return true;
		// A corner sticking out. Does a diagonal stroke run past it (a set
		// pixel diagonally across one of the two clear side neighbours)?
		const bool diagonal = (at(m, w, h, x + dx, y - dy) && !at(m, w, h, x, y - dy)) ||
		                      (at(m, w, h, x - dx, y + dy) && !at(m, w, h, x - dx, y));
		return s >= (diagonal ? 0.5 : kRound);
	}
	if (H && V)
		return s < (D ? kFillet : 0.5);
	return false;
}

std::vector<uint8_t> smoothUpscaleMask(const uint8_t *mask, int w, int h, int scale) {
	const int ow = w * scale, oh = h * scale;
	std::vector<uint8_t> out(size_t(ow) * size_t(oh));
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			const int px = x / scale, py = y / scale;
			const double u = ((x % scale) + 0.5) / scale, v = ((y % scale) + 0.5) / scale;
			out[size_t(y) * size_t(ow) + size_t(x)] = smoothShapeCovers(mask, w, h, px, py, u, v) ? 255 : 0;
		}
	return out;
}

std::vector<uint8_t> nearestUpscale(const uint8_t *src, int w, int h, int scale) {
	const int ow = w * scale, oh = h * scale;
	std::vector<uint8_t> out(size_t(ow) * size_t(oh));
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++)
			out[size_t(y) * size_t(ow) + size_t(x)] = src[size_t(y / scale) * size_t(w) + size_t(x / scale)];
	return out;
}

std::vector<uint8_t> glyphDistanceField(const uint8_t *mask, int w, int h, int samples, double spread) {
	constexpr int kHi = 16; // high-resolution samples per glyph pixel
	const int pad = kHi;    // one glyph pixel of clear border around the glyph
	const int hw = w * kHi + 2 * pad, hh = h * kHi + 2 * pad;
	std::vector<uint8_t> hi(size_t(hw) * size_t(hh), 0);
	for (int y = 0; y < h * kHi; y++)
		for (int x = 0; x < w * kHi; x++)
			hi[size_t(y + pad) * size_t(hw) + size_t(x + pad)] =
			    smoothShapeCovers(mask, w, h, x / kHi, y / kHi, ((x % kHi) + 0.5) / kHi, ((y % kHi) + 0.5) / kHi);
	const std::vector<float> toInside = edt2d(hi, hw, hh, true), toOutside = edt2d(hi, hw, hh, false);

	const int ow = w * samples, oh = h * samples;
	std::vector<uint8_t> out(size_t(ow) * size_t(oh));
	for (int y = 0; y < oh; y++)
		for (int x = 0; x < ow; x++) {
			// The high-resolution pixel nearest to the sample's centre.
			const int hx = pad + std::clamp(int((x + 0.5) * kHi / samples), 0, w * kHi - 1);
			const int hy = pad + std::clamp(int((y + 0.5) * kHi / samples), 0, h * kHi - 1);
			const size_t i = size_t(hy) * size_t(hw) + size_t(hx);
			// The outline lies half-way between a set and a clear pixel.
			double d = hi[i] ? std::sqrt(double(toOutside[i])) - 0.5 : -(std::sqrt(double(toInside[i])) - 0.5);
			d /= kHi;
			const double v = 128.0 + d / spread * 127.0;
			out[size_t(y) * size_t(ow) + size_t(x)] = uint8_t(std::clamp(std::lround(v), 0L, 255L));
		}
	return out;
}

} // namespace gob
