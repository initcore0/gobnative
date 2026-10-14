// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Enlargers for indexed pictures that keep palette indices instead of
// colours, so that palettes and fades still apply to the result
// (docs/hdpacks.md, "Pictures without HD art"). Each HD pixel becomes a
// mix of at most two palette indices: a | b << 8 | weight of b (0-255) << 16.
// For pictures drawn transparently, colour 0 is never mixed in and an HD
// pixel is colour 0 exactly where the original pixel under it is: the
// silhouette stays the original one.

#pragma once

#include <cstdint>
#include <vector>

namespace gob {

// Edge-directed smoothing in the spirit of xBR (the presenter's "Smooth
// edges" shader), at any factor `n`: corners of a pixel where an edge runs
// diagonally are filled with the neighbour's index up to a 45-degree (or
// shallower / steeper) line, anti-aliased over one HD pixel. Edges are
// found from the colours of `palette` (256 entries R | G << 8 | B << 16)
// as the shader does, or from index equality when it is null.
std::vector<uint32_t> smoothEdgesMix(const uint8_t *src, int w, int h, int n, bool transparent,
                                     const uint32_t *palette = nullptr);

// Bilinear interpolation, reduced to the two strongest indices of the four
// samples (exact wherever at most two indices meet).
std::vector<uint32_t> bilinearMix(const uint8_t *src, int w, int h, int n, bool transparent);

} // namespace gob
