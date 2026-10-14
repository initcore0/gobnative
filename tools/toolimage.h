// gob2native - small RGBA image helpers shared by the art tools
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An 8-bit RGBA canvas with the few operations the guide and frame tools
// need: nearest-neighbour enlargement, outlines, filled boxes, bitmap-font
// labels and alpha blending. Nothing here is performance critical.

#pragma once

#include "gfx/png.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gob::tool {

struct Color {
	uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct Canvas {
	int w = 0, h = 0;
	std::vector<uint8_t> px; // RGBA, top row first

	Canvas() = default;
	Canvas(int width, int height, Color fill = {0, 0, 0, 0}) : w(width), h(height), px(size_t(width) * size_t(height) * 4) {
		for (size_t i = 0; i < px.size(); i += 4) {
			px[i] = fill.r;
			px[i + 1] = fill.g;
			px[i + 2] = fill.b;
			px[i + 3] = fill.a;
		}
	}
	bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
	uint8_t *at(int x, int y) { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
	const uint8_t *at(int x, int y) const { return &px[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
	void set(int x, int y, Color c) {
		if (!inside(x, y))
			return;
		uint8_t *p = at(x, y);
		p[0] = c.r, p[1] = c.g, p[2] = c.b, p[3] = c.a;
	}
	// Source-over blend of `c` (its alpha) onto the pixel.
	void blend(int x, int y, Color c) {
		if (!inside(x, y) || c.a == 0)
			return;
		uint8_t *p = at(x, y);
		const int a = c.a, da = p[3];
		const int oa = a + da * (255 - a) / 255;
		if (oa == 0)
			return;
		auto mix = [&](int s, int d) { return uint8_t((s * a + d * da * (255 - a) / 255) / oa); };
		p[0] = mix(c.r, p[0]);
		p[1] = mix(c.g, p[1]);
		p[2] = mix(c.b, p[2]);
		p[3] = uint8_t(oa);
	}
	void fill(int l, int t, int r, int b, Color c, bool blended = false) { // inclusive
		for (int y = std::max(t, 0); y <= std::min(b, h - 1); y++)
			for (int x = std::max(l, 0); x <= std::min(r, w - 1); x++)
				blended ? blend(x, y, c) : set(x, y, c);
	}
	// Outline of the inclusive rectangle, `thick` pixels inward.
	void outline(int l, int t, int r, int b, Color c, int thick = 1, int dash = 0) {
		for (int k = 0; k < thick; k++) {
			const int L = l + k, T = t + k, R = r - k, B = b - k;
			if (L > R || T > B)
				break;
			for (int x = L; x <= R; x++)
				if (!dash || ((x - L) / dash) % 2 == 0) {
					set(x, T, c);
					set(x, B, c);
				}
			for (int y = T; y <= B; y++)
				if (!dash || ((y - T) / dash) % 2 == 0) {
					set(L, y, c);
					set(R, y, c);
				}
		}
	}
	bool save(const std::filesystem::path &p) const { return writePng(p, px.data(), w, h, 4); }
};

// The canvas enlarged `n` times by nearest neighbour.
inline Canvas enlarge(const Canvas &src, int n) {
	Canvas out(src.w * n, src.h * n);
	for (int y = 0; y < out.h; y++)
		for (int x = 0; x < out.w; x++)
			std::copy_n(src.at(x / n, y / n), 4, out.at(x, y));
	return out;
}

// An 8x8-or-smaller bitmap font as 64-bit glyphs (bit y * 8 + x), e.g. the
// game's own font; characters it lacks are drawn as boxes.
struct LabelFont {
	int w = 0, h = 0;
	std::map<uint8_t, uint64_t> glyphs;
	bool valid() const { return w > 0 && h > 0 && !glyphs.empty(); }

	// Width in canvas pixels of `text` at `scale`.
	int textWidth(const std::string &text, int scale) const { return int(text.size()) * w * scale; }

	// Draws `text` at (x, y) with each font pixel `scale` x `scale`, on a
	// box of `bg` with a margin of `scale` pixels.
	void draw(Canvas &c, int x, int y, const std::string &text, int scale, Color fg, Color bg) const {
		if (!valid() || text.empty())
			return;
		c.fill(x - scale, y - scale, x + textWidth(text, scale) + scale - 1, y + h * scale + scale - 1, bg, true);
		for (size_t i = 0; i < text.size(); i++) {
			auto it = glyphs.find(uint8_t(text[i]));
			const uint64_t bits = it != glyphs.end() ? it->second : (text[i] == ' ' ? 0 : ~uint64_t(0));
			for (int gy = 0; gy < h; gy++)
				for (int gx = 0; gx < w; gx++)
					if ((bits >> (gy * 8 + gx)) & 1)
						c.fill(x + (int(i) * w + gx) * scale, y + gy * scale, x + (int(i) * w + gx + 1) * scale - 1,
						       y + (gy + 1) * scale - 1, fg);
		}
	}
};

// Distinct outline colours for numbered objects.
inline Color distinctColor(int i) {
	static const std::array<Color, 12> k = {{{255, 64, 64, 255},
	                                         {64, 220, 64, 255},
	                                         {64, 160, 255, 255},
	                                         {255, 220, 0, 255},
	                                         {255, 64, 255, 255},
	                                         {0, 230, 230, 255},
	                                         {255, 140, 0, 255},
	                                         {170, 110, 255, 255},
	                                         {150, 255, 120, 255},
	                                         {255, 150, 180, 255},
	                                         {255, 255, 255, 255},
	                                         {120, 200, 255, 255}}};
	return k[size_t(i) % k.size()];
}

// Digits 0-9 in a 3x5 pixel font (rows top to bottom, bit 2 = left), for
// frame numbers on contact sheets.
inline void drawNumber(Canvas &c, int x, int y, int value, int scale, Color fg) {
	static const uint8_t k[10][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
	                                 {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};
	const std::string s = std::to_string(value);
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] < '0' || s[i] > '9')
			continue;
		for (int gy = 0; gy < 5; gy++)
			for (int gx = 0; gx < 3; gx++)
				if ((k[s[i] - '0'][gy] >> (2 - gx)) & 1)
					c.fill(x + (int(i) * 4 + gx) * scale, y + gy * scale, x + (int(i) * 4 + gx + 1) * scale - 1,
					       y + (gy + 1) * scale - 1, fg);
	}
}

// Resizes to exactly `nw` x `nh` with a separable Lanczos-3 filter (its
// support widened when shrinking, so it averages), on premultiplied alpha.
// A same-size request returns a copy.
inline Canvas resample(const Canvas &src, int nw, int nh) {
	if (src.w == nw && src.h == nh)
		return src;
	auto lanczos = [](double x) {
		x = std::fabs(x);
		if (x < 1e-9)
			return 1.0;
		if (x >= 3)
			return 0.0;
		const double px = M_PI * x;
		return 3 * std::sin(px) * std::sin(px / 3) / (px * px);
	};
	// One pass along one axis: `len` source samples -> `nlen`.
	auto pass = [&](const std::vector<double> &in, int w, int h, bool horizontal, int nlen) {
		const int len = horizontal ? w : h, other = horizontal ? h : w;
		const double scale = double(nlen) / len, support = scale < 1 ? 3 / scale : 3;
		const int ow = horizontal ? nlen : w, oh = horizontal ? h : nlen;
		std::vector<double> out(size_t(ow) * size_t(oh) * 4);
		for (int i = 0; i < nlen; i++) {
			const double center = (i + 0.5) / scale - 0.5;
			const int lo = int(std::floor(center - support)), hi = int(std::ceil(center + support));
			std::vector<std::pair<int, double>> taps;
			double sum = 0;
			for (int j = lo; j <= hi; j++) {
				const double wt = lanczos((j - center) * (scale < 1 ? scale : 1));
				if (wt == 0)
					continue;
				taps.push_back({std::clamp(j, 0, len - 1), wt});
				sum += wt;
			}
			for (int o = 0; o < other; o++)
				for (int ch = 0; ch < 4; ch++) {
					double acc = 0;
					for (const auto &[j, wt] : taps)
						acc += wt * in[((horizontal ? size_t(o) * size_t(w) + size_t(j) : size_t(j) * size_t(w) + size_t(o))) * 4 + size_t(ch)];
					out[((horizontal ? size_t(o) * size_t(ow) + size_t(i) : size_t(i) * size_t(ow) + size_t(o))) * 4 + size_t(ch)] = acc / sum;
				}
		}
		return out;
	};
	std::vector<double> buf(src.px.size());
	for (size_t i = 0; i < src.px.size(); i += 4) {
		const double a = src.px[i + 3] / 255.0;
		buf[i] = src.px[i] * a;
		buf[i + 1] = src.px[i + 1] * a;
		buf[i + 2] = src.px[i + 2] * a;
		buf[i + 3] = src.px[i + 3];
	}
	std::vector<double> h = pass(buf, src.w, src.h, true, nw);
	std::vector<double> v = pass(h, nw, src.h, false, nh);
	Canvas out(nw, nh);
	for (size_t i = 0; i < out.px.size(); i += 4) {
		const double a = std::clamp(v[i + 3], 0.0, 255.0);
		for (int ch = 0; ch < 3; ch++)
			out.px[i + size_t(ch)] = a > 0 ? uint8_t(std::clamp(v[i + size_t(ch)] / (a / 255.0), 0.0, 255.0) + 0.5) : 0;
		out.px[i + 3] = uint8_t(a + 0.5);
	}
	return out;
}

// Gives pixels with alpha below 128 the colours of their opaque
// neighbours, spreading outward one pixel per pass (sides weigh twice);
// after `passes` the rest get the mean opaque colour. Their alpha is kept.
// Used where clear pixels of a repaint end up inside an opaque outline.
inline void bleedClear(Canvas &c, int passes) {
	std::vector<uint8_t> filled(size_t(c.w) * size_t(c.h));
	double sum[3] = {0, 0, 0};
	size_t solid = 0;
	for (int y = 0; y < c.h; y++)
		for (int x = 0; x < c.w; x++)
			if (c.at(x, y)[3] >= 128) {
				filled[size_t(y) * size_t(c.w) + size_t(x)] = 1;
				for (int k = 0; k < 3; k++)
					sum[k] += c.at(x, y)[k];
				solid++;
			}
	if (solid == 0 || solid == filled.size())
		return;
	for (int pass = 0; pass < passes; pass++) {
		std::vector<uint8_t> next = filled;
		bool any = false;
		for (int y = 0; y < c.h; y++)
			for (int x = 0; x < c.w; x++) {
				if (filled[size_t(y) * size_t(c.w) + size_t(x)])
					continue;
				int acc[3] = {0, 0, 0}, wsum = 0;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++) {
						const int nx = x + dx, ny = y + dy;
						if ((!dx && !dy) || !c.inside(nx, ny) || !filled[size_t(ny) * size_t(c.w) + size_t(nx)])
							continue;
						const int wt = (dx == 0 || dy == 0) ? 2 : 1;
						for (int k = 0; k < 3; k++)
							acc[k] += c.at(nx, ny)[k] * wt;
						wsum += wt;
					}
				if (wsum) {
					for (int k = 0; k < 3; k++)
						c.at(x, y)[k] = uint8_t((acc[k] + wsum / 2) / wsum);
					next[size_t(y) * size_t(c.w) + size_t(x)] = 1;
					any = true;
				}
			}
		filled.swap(next);
		if (!any)
			break;
	}
	for (int y = 0; y < c.h; y++)
		for (int x = 0; x < c.w; x++)
			if (!filled[size_t(y) * size_t(c.w) + size_t(x)])
				for (int k = 0; k < 3; k++)
					c.at(x, y)[k] = uint8_t(sum[k] / double(solid) + 0.5);
}

// Reads any PNG as a canvas.
inline bool loadCanvas(const std::filesystem::path &p, Canvas &out, std::string *err = nullptr) {
	PngImage img;
	if (!readPng(p, img, err))
		return false;
	out.w = img.info.width;
	out.h = img.info.height;
	out.px = std::move(img.rgba);
	return true;
}

inline std::string jsonString(const std::string &s) {
	std::string o = "\"";
	for (unsigned char c : s) {
		if (c == '"' || c == '\\')
			o += '\\', o += char(c);
		else if (c < 0x20 || c >= 0x7F) {
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\u%04x", c);
			o += buf;
		} else
			o += char(c);
	}
	return o + "\"";
}

} // namespace gob::tool
