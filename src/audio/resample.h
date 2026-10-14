// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Band-limited (windowed-sinc) interpolation, used by the mixer's enhanced
// sound effects and to convert replacement music files to the output rate.

#pragma once

#include <cmath>

namespace gob::resample {

// Half-width of the prototype kernel in zero crossings.
constexpr int kZeroCrossings = 16;

// The prototype low-pass kernel: sin(pi x) / (pi x) under a Kaiser window
// (beta 8, about 80 dB stopband) that is zero for |x| >= kZeroCrossings.
// Looked up from a table with linear interpolation.
float kernel(double x);

// Taps for interpolating at a fractional position with cutoff `cutoff`
// (in cycles per input sample, at most 0.5): the kernel stretched to
// scale = 2 * cutoff spans this many input samples on each side.
inline int halfTaps(double scale) { return int(std::ceil(kZeroCrossings / scale)); }

// Interpolates x(pos) from samples get(k), k = integer input index, through a
// low-pass at `scale` / 2 cycles per input sample. `get` handles indices
// outside the data (silence, or the next/previous loop pass).
template <class Get>
inline float interpolate(double pos, double scale, int taps, Get &&get) {
	long base = long(std::floor(pos));
	float acc = 0.0f;
	for (long k = base - taps + 1; k <= base + taps; k++)
		acc += get(k) * kernel((pos - double(k)) * scale);
	return acc * float(scale);
}

} // namespace gob::resample
