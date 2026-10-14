// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/resample.h"

#include <algorithm>
#include <vector>

namespace gob::resample {

namespace {

constexpr int kResolution = 512; // table entries per zero crossing
constexpr double kBeta = 8.0;

// Modified Bessel function of the first kind, order 0 (power series).
double besselI0(double x) {
	double sum = 1.0, term = 1.0, q = x * x / 4.0;
	for (int k = 1; k < 64 && term > sum * 1e-12; k++) {
		term *= q / (double(k) * k);
		sum += term;
	}
	return sum;
}

std::vector<float> buildTable() {
	const int n = kZeroCrossings * kResolution;
	std::vector<float> t(size_t(n) + 2, 0.0f);
	const double norm = besselI0(kBeta);
	for (int i = 0; i <= n; i++) {
		double x = double(i) / kResolution;
		double sinc = i == 0 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
		double r = x / kZeroCrossings;
		double w = besselI0(kBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / norm;
		t[size_t(i)] = float(sinc * w);
	}
	return t; // t[n] (and the guard entry after it) is the window's zero end
}

} // namespace

float kernel(double x) {
	static const std::vector<float> table = buildTable();
	double u = std::fabs(x) * kResolution;
	if (u >= double(kZeroCrossings * kResolution))
		return 0.0f;
	size_t i = size_t(u);
	float f = float(u - double(i));
	return table[i] + (table[i + 1] - table[i]) * f;
}

} // namespace gob::resample
