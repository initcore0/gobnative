// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ring buffer of the engine's trace lines (Host::trace), for the Tools tab's
// trace viewer and the diagnostics report. Lines are numbered from 0 as
// they arrive; the oldest are dropped once the buffer is full.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gob {

class TraceLog {
public:
	struct Line {
		uint32_t ms = 0; // wall-clock milliseconds since start
		std::string text;
	};

	explicit TraceLog(size_t capacity = 20000) : _lines(capacity) {}

	void add(uint32_t ms, std::string text) {
		Line &l = _lines[size_t(_end % _lines.size())];
		l.ms = ms;
		l.text = std::move(text);
		_end++;
	}

	// Numbers of the oldest line held and one past the newest.
	uint64_t begin() const { return _end > _lines.size() ? _end - _lines.size() : 0; }
	uint64_t end() const { return _end; }
	// Line `n`, for begin() <= n < end().
	const Line &at(uint64_t n) const { return _lines[size_t(n % _lines.size())]; }

private:
	std::vector<Line> _lines;
	uint64_t _end = 0;
};

} // namespace gob
