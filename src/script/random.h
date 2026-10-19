// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace gob {

// The engine's own random numbers (the script's rand(), goblin boredom,
// Gobliiins' relax timers and speaker pitches). std::rand() differs
// between libcs, so headless traces and click-script walkthroughs drifted
// apart across platforms; this generator gives every build the same
// sequence for the same seed.
//
// It is the Park-Miller "minimal standard" generator
// (x = 16807 * x mod 2^31-1), which is what macOS's rand() is; seeded with 1,
// as an unseeded rand() is, it reproduces the sequence the walkthroughs were
// recorded with on the Mac.
class Random {
public:
	static constexpr uint32_t kDefaultSeed = 1;

	explicit Random(uint32_t seed = kDefaultSeed) { setSeed(seed); }

	// 0 and multiples of 2^31-1 would stick at 0; they fall back to 1.
	void setSeed(uint32_t seed) {
		_state = seed % kModulus;
		if (_state == 0)
			_state = 1;
	}
	uint32_t state() const { return _state; }

	// The next value in [1, 2^31-2].
	uint32_t next() {
		_state = uint32_t(uint64_t(_state) * 16807 % kModulus);
		return _state;
	}
	// A value in [0, n), or 0 when n <= 0 (rand() % n, as the engine used it).
	int32_t below(int32_t n) { return n > 0 ? int32_t(next() % uint32_t(n)) : 0; }

private:
	static constexpr uint32_t kModulus = 2147483647;
	uint32_t _state = 1;
};

} // namespace gob
