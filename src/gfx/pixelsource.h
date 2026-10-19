// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the pixels of a Surface came from, so that HD layers can be rebuilt
// at any time (docs/hdpacks.md, "Switching packs"): when a pack is chosen,
// changed or dropped while a room is on screen, every surface's layer is
// made again from the pictures its pixels were copied from, as if the new
// pack had been in use when they were drawn.
//
// A surface that tracks sources keeps two entries per pixel. `top` is
// either kLive (the pixel isn't from a picture: a fill, text, a line, or a
// copy of such a pixel; its HD block is the surface's own index) or a
// reference to pixel (x, y) of a registered picture, possibly mirrored,
// with a flag telling whether that picture pixel is colour 0 ("clear").
// `under` is kNone, except where a transparent blit put a picture's edge
// pixel over something else: the HD version of an edge pixel may leave
// parts of its block transparent, or fill parts of a clear pixel's block
// (smoothed edges, pack masks, Scale2x), and the rest shows what was
// there before: `under` (a picture reference, or kIndex | index for a
// plain pixel). Copies carry both entries along, so
// sources follow pictures through the back surface, the animation snapshot,
// the captures and the front surface. Pictures are numbered by Draw
// (Draw::registerPicture); id 0 means none.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gob {

struct PixelSources {
	static constexpr uint32_t kLive = 0;               // top: the surface's own index
	static constexpr uint32_t kNone = 0;               // under: nothing below the top
	static constexpr uint32_t kIndex = 0x80000000u;    // under: | palette index
	static constexpr uint32_t kClear = 0x40000000u;    // a reference to a colour 0 pixel
	static constexpr int kMaxPictures = 2047;          // picture ids 1..2047
	static constexpr int kMaxSide = 512;               // pictures up to 512x512

	std::vector<uint32_t> top, under; // per surface pixel

	static uint32_t ref(int pic, int x, int y, bool mirror, bool clear) {
		return uint32_t(pic) << 19 | (clear ? kClear : 0u) | (mirror ? 1u << 18 : 0u) | uint32_t(y) << 9 |
		       uint32_t(x);
	}
	static bool isRef(uint32_t v) { return v != 0 && !(v & kIndex); }
	static bool isClear(uint32_t v) { return isRef(v) && (v & kClear); }
	static int picture(uint32_t v) { return int((v >> 19) & 0x7FF); }
	static int x(uint32_t v) { return int(v & 0x1FF); }
	static int y(uint32_t v) { return int((v >> 9) & 0x1FF); }
	static bool mirrored(uint32_t v) { return (v >> 18) & 1; }
	// The same reference seen through a mirrored blit.
	static uint32_t flip(uint32_t v) { return isRef(v) ? v ^ (1u << 18) : v; }

	void init(size_t pixels) {
		top.assign(pixels, kLive);
		under.assign(pixels, kNone);
	}
};

} // namespace gob
