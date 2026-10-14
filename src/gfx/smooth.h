// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Edge smoothing for 1-bit pixel shapes (the game's 8x8 font glyphs and the
// transparency masks of pictures), after the idea of the pixel-art
// upscalers (EPX, xBR): every pixel is split into four corner quadrants, and
// each quadrant is decided from the pixel and its neighbours around that
// corner:
//
//  - a set pixel whose corner sticks out (both side neighbours clear) loses
//    a triangle there: a big one (cut through the middle of both sides, 45
//    degrees) where a diagonal stroke continues past the corner, a small one
//    (rounding) otherwise. A corner touching a set diagonal neighbour stays,
//    so diagonal strokes keep their thickness;
//  - a clear pixel between two set side neighbours gets a triangle filled:
//    the big one where the diagonal neighbour is clear (that is a staircase:
//    it becomes a straight 45 degree edge), a small fillet in an inner
//    corner.
//
// The cuts and fills of neighbouring pixels meet on the same lines, so a
// one-pixel staircase turns into a straight diagonal band. The result is a
// shape in continuous coordinates, sampled at any resolution.

#pragma once

#include <cstdint>
#include <vector>

namespace gob {

// `mask` is w x h bytes, non-zero = set. True if the point (u, v) in
// [0, 1) x [0, 1) inside pixel (x, y) belongs to the smoothed shape.
bool smoothShapeCovers(const uint8_t *mask, int w, int h, int x, int y, double u, double v);

// The smoothed shape sampled at pixel centres of a `scale` times larger
// image: (w * scale) x (h * scale) bytes, 255 = set, 0 = clear.
std::vector<uint8_t> smoothUpscaleMask(const uint8_t *mask, int w, int h, int scale);

// Nearest-neighbour enlargement of a mask (or any 8-bit image), for
// comparison and as the plain alternative.
std::vector<uint8_t> nearestUpscale(const uint8_t *src, int w, int h, int scale);

// Signed distance field of the smoothed shape of a w x h glyph:
// (w * samples) x (h * samples) bytes, 128 on the outline, +-127 at
// `spread` glyph pixels inside / outside (clamped). Computed from a
// 16-samples-per-pixel rendering with an exact Euclidean distance transform.
std::vector<uint8_t> glyphDistanceField(const uint8_t *mask, int w, int h, int samples, double spread);

} // namespace gob
