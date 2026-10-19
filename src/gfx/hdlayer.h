// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// High-resolution shadow of an indexed Surface, for HD packs
// (docs/hdpacks.md, "The HD compositor"). While a Surface carries a layer,
// every one of its pixel operations (blit, fill, pixel, line, circle, clear)
// does the same to the layer at `scale` times the size: a blit from a
// surface that has a layer copies its HD pixels, a blit from one without
// enlarges the source's indexed pixels, fills and pixels fill blocks.
//
// An HD pixel is either "live" (a palette index, coloured with the screen
// palette when the frame is made) or "HD" (a full colour from a pack
// picture, mapped through the palette's fades relative to the palette the
// picture was made for), or a "mix" of two palette indices (pictures
// without HD art, smoothed: gfx/pixelfilter.h). `idx` keeps every HD pixel's palette index (for HD
// pixels the index of the original pixel under it): transparent blits skip
// index 0 as the game does. `nat` is the indexed picture the mirrored
// operations produced, at surface resolution: wherever it differs from the
// surface itself something changed the surface without going through the
// operations above, and the compositor repairs the layer from the surface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gob {

struct HdLayer {
	enum Kind : uint8_t {
		Live = 0,     // colour = palette[idx]
		Hd = 1,       // colour = the stored RGB (mapped through fades)
		Repaired = 2, // live, restored from the indexed surface after a mismatch
		Mix = 3,      // two palette indices a, b and b's weight w: R = a, G = b, B = w (0-255)
	};

	int scale = 1;
	int w = 0, h = 0;          // HD size: the surface's size times scale
	std::vector<uint32_t> px;  // per HD pixel: R | G << 8 | B << 16 | kind << 24
	std::vector<uint8_t> idx;  // per HD pixel: palette index
	std::vector<uint8_t> nat;  // per surface pixel: the index the operations left

	// Dirty tracking (the front surface only): tiles of kTileW x kTileH
	// surface pixels changed since the compositor last looked.
	static constexpr int kTileW = 16, kTileH = 8;
	bool trackDirty = false;
	int tilesX = 0, tilesY = 0;
	std::vector<uint8_t> dirty;

	static uint32_t pack(uint8_t r, uint8_t g, uint8_t b, Kind k) {
		return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(k) << 24;
	}
	static Kind kind(uint32_t p) { return Kind(p >> 24); }

	// An empty (all live index 0) layer for a `sw` x `sh` surface.
	void init(int sw, int sh, int s) {
		scale = s;
		w = sw * s;
		h = sh * s;
		px.assign(size_t(w) * size_t(h), 0);
		idx.assign(size_t(w) * size_t(h), 0);
		nat.assign(size_t(sw) * size_t(sh), 0);
		tilesX = (sw + kTileW - 1) / kTileW;
		tilesY = (sh + kTileH - 1) / kTileH;
		dirty.assign(size_t(tilesX) * size_t(tilesY), 1);
	}

	// Inclusive rectangle in surface pixels (already clipped).
	void markDirty(int left, int top, int right, int bottom) {
		if (!trackDirty)
			return;
		for (int ty = top / kTileH; ty <= bottom / kTileH && ty < tilesY; ty++)
			for (int tx = left / kTileW; tx <= right / kTileW && tx < tilesX; tx++)
				dirty[size_t(ty) * size_t(tilesX) + size_t(tx)] = 1;
	}
};

} // namespace gob
