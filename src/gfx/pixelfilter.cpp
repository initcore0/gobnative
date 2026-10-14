// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/pixelfilter.h"

#include <algorithm>
#include <cmath>

namespace gob {

namespace {

uint32_t mix(uint8_t a, uint8_t b, double wb) {
	const int w = std::clamp(int(std::lround(wb * 255.0)), 0, 255);
	if (w == 0 || a == b)
		return a;
	if (w == 255)
		return b;
	return uint32_t(a) | uint32_t(b) << 8 | uint32_t(w) << 16;
}

// One corner of a source pixel, seen in the frame where it is the
// bottom-right one: whether it is rounded, with which index, and the
// slope of the line.
struct Corner {
	bool on = false, shallow = false, steep = false;
	uint8_t px = 0;
};

} // namespace

std::vector<uint32_t> smoothEdgesMix(const uint8_t *src, int w, int h, int n, bool transparent,
                                     const uint32_t *palette) {
	std::vector<uint32_t> out(size_t(w) * h * n * n);
	// Luma-weighted colour difference between two indices (the shader's).
	std::vector<float> table;
	if (palette) {
		table.resize(256 * 256);
		for (int a = 0; a < 256; a++)
			for (int b = 0; b < 256; b++) {
				float d[3];
				for (int c = 0; c < 3; c++)
					d[c] = (float((palette[a] >> (8 * c)) & 0xFF) - float((palette[b] >> (8 * c)) & 0xFF)) / 255.0f;
				const float y = 0.299f * d[0] + 0.587f * d[1] + 0.114f * d[2];
				table[size_t(a) * 256 + b] = 2.0f * std::fabs(y) + 0.5f * std::fabs(d[0] - y) + 0.4f * std::fabs(d[2] - y);
			}
	}
	auto diff = [&](uint8_t a, uint8_t b) -> float {
		return palette ? table[size_t(a) * 256 + b] : (a != b ? 1.0f : 0.0f);
	};
	auto same = [&](uint8_t a, uint8_t b) { return palette ? diff(a, b) < 0.05f : a == b; };
	auto at = [&](int x, int y) { return src[size_t(std::clamp(y, 0, h - 1)) * w + size_t(std::clamp(x, 0, w - 1))]; };
	const double aa = 1.0 / n; // one HD pixel, in source pixels
	const int ow = w * n;
	Corner corners[4];
	static constexpr int kMx[4] = {1, -1, 1, -1}, kMy[4] = {1, 1, -1, -1};
	for (int cy = 0; cy < h; cy++)
		for (int cx = 0; cx < w; cx++) {
			const uint8_t e = src[size_t(cy) * w + cx];
			for (int k = 0; k < 4; k++) {
				Corner &c = corners[k];
				c = Corner();
				const int mx = kMx[k], my = kMy[k];
				auto p = [&](int x, int y) { return at(cx + x * mx, cy + y * my); };
				const uint8_t B = p(0, -1), C = p(1, -1), D = p(-1, 0), F = p(1, 0), F4 = p(2, 0);
				const uint8_t G = p(-1, 1), H = p(0, 1), I = p(1, 1), I4 = p(2, 1), H5 = p(0, 2), I5 = p(1, 2);
				if (same(e, F) || same(e, H))
					continue;
				const float slash = diff(e, C) + diff(e, G) + diff(I, F4) + diff(I, H5) + 4 * diff(F, H);
				const float backslash = diff(D, H) + diff(H, I5) + diff(B, F) + diff(F, I4) + 4 * diff(e, I);
				if (slash >= backslash)
					continue;
				const bool carriesOn = (!same(F, B) && !same(F, C)) || (!same(H, D) && !same(H, G)) ||
				                       (same(e, I) && ((!same(F, F4) && !same(F, I4)) || (!same(H, H5) && !same(H, I5)))) ||
				                       same(e, G) || same(e, C);
				if (!carriesOn)
					continue;
				// The neighbour whose index fills the corner: the closer
				// colour (with index equality: the one the edge continues with).
				const uint8_t px = palette ? (diff(e, F) <= diff(e, H) ? F : H) : (F == I || H != I ? F : H);
				if (transparent && (e == 0 || px == 0))
					continue; // the silhouette stays exact
				c.on = true;
				c.px = px;
				c.shallow = same(G, H) && !same(D, G);
				c.steep = same(C, F) && !same(B, C);
			}
			for (int j = 0; j < n; j++)
				for (int i = 0; i < n; i++) {
					const double fx = (i + 0.5) / n, fy = (j + 0.5) / n;
					double best = 0;
					uint8_t bestPx = e;
					for (int k = 0; k < 4; k++) {
						const Corner &c = corners[k];
						if (!c.on)
							continue;
						const double x = kMx[k] > 0 ? fx : 1.0 - fx, y = kMy[k] > 0 ? fy : 1.0 - fy;
						double dist = (x + y - 1.5) * 0.7071;
						if (c.shallow)
							dist = std::max(dist, (0.5 * x + y - 1.0) * 0.8944);
						if (c.steep)
							dist = std::max(dist, (x + 0.5 * y - 1.0) * 0.8944);
						const double a = std::clamp(dist / aa + 0.5, 0.0, 1.0);
						if (a > best) {
							best = a;
							bestPx = c.px;
						}
					}
					out[size_t(cy * n + j) * ow + size_t(cx * n + i)] = mix(e, bestPx, best);
				}
		}
	return out;
}

std::vector<uint32_t> bilinearMix(const uint8_t *src, int w, int h, int n, bool transparent) {
	std::vector<uint32_t> out(size_t(w) * h * n * n);
	const int ow = w * n;
	// Sub-pixel i of a source pixel samples at (i + 0.5) / n - 0.5: between
	// the pixel and its left neighbour (d = -1) or right one (d = 0), with
	// the second sample's weight t (of 256). The same for every pixel.
	std::vector<int> d(static_cast<size_t>(n)), t(static_cast<size_t>(n));
	for (int i = 0; i < n; i++) {
		const double f = (i + 0.5) / n - 0.5;
		d[size_t(i)] = f < 0 ? -1 : 0;
		t[size_t(i)] = int(std::lround((f < 0 ? f + 1.0 : f) * 256.0));
	}
	auto at = [&](int x, int y) { return src[size_t(std::clamp(y, 0, h - 1)) * w + size_t(std::clamp(x, 0, w - 1))]; };
	for (int cy = 0; cy < h; cy++)
		for (int cx = 0; cx < w; cx++) {
			const uint8_t nearest = src[size_t(cy) * w + cx];
			for (int j = 0; j < n; j++) {
				uint32_t *row = &out[size_t(cy * n + j) * ow + size_t(cx * n)];
				if (transparent && nearest == 0) {
					std::fill_n(row, n, 0u);
					continue;
				}
				const int y0 = cy + d[size_t(j)], ty = t[size_t(j)];
				for (int i = 0; i < n; i++) {
					const int x0 = cx + d[size_t(i)], tx = t[size_t(i)];
					const uint8_t v[4] = {at(x0, y0), at(x0 + 1, y0), at(x0, y0 + 1), at(x0 + 1, y0 + 1)};
					const int wt4[4] = {(256 - tx) * (256 - ty), tx * (256 - ty), (256 - tx) * ty, tx * ty};
					uint8_t idx[4];
					int wt[4], count = 0;
					for (int k = 0; k < 4; k++) {
						if ((transparent && v[k] == 0) || wt4[k] <= 0)
							continue;
						int m = 0;
						while (m < count && idx[m] != v[k])
							m++;
						if (m == count) {
							idx[count] = v[k];
							wt[count++] = 0;
						}
						wt[m] += wt4[k];
					}
					if (count == 0) {
						row[i] = nearest;
						continue;
					}
					int a = 0;
					for (int m = 1; m < count; m++)
						if (wt[m] > wt[a])
							a = m;
					int b = -1;
					for (int m = 0; m < count; m++)
						if (m != a && (b < 0 || wt[m] > wt[b]))
							b = m;
					row[i] = b < 0 ? idx[a] : mix(idx[a], idx[b], double(wt[b]) / double(wt[a] + wt[b]));
				}
			}
		}
	return out;
}

} // namespace gob
