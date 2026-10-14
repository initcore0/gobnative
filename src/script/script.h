// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gob {

// Cursor over a TOT file's bytecode. Offsets are absolute file offsets, so
// function entry points from the TOT header can be used directly.
class Script {
public:
	Script() = default;
	explicit Script(const std::vector<uint8_t> *bytes) : _bytes(bytes) {}

	void attach(const std::vector<uint8_t> *bytes) { _bytes = bytes; _pos = 0; _finished = false; _calls.clear(); }
	bool loaded() const { return _bytes != nullptr; }
	size_t size() const { return _bytes ? _bytes->size() : 0; }

	uint32_t pos() const { return _pos; }

	// Matches the original's semantics: a successful seek means execution
	// continues (clears `finished`); seeking into the 128-byte header marks
	// the script finished; out-of-range seeks are ignored.
	bool seek(int64_t pos) {
		if (!_bytes || pos < 0 || uint64_t(pos) >= _bytes->size())
			return false;
		if (pos < 128) {
			_finished = true;
			return false;
		}
		_finished = false;
		_pos = uint32_t(pos);
		return true;
	}
	bool skip(int32_t n) { return seek(int64_t(_pos) + n); }
	bool skipBlock() { return skip(peekUint16(2) + 2); }

	uint8_t peekByte(int32_t off = 0) const {
		uint32_t p = uint32_t(int32_t(_pos) + off);
		return (_bytes && p < _bytes->size()) ? (*_bytes)[p] : 0;
	}
	uint16_t peekUint16(int32_t off = 0) const { return uint16_t(peekByte(off) | (peekByte(off + 1) << 8)); }
	int16_t peekInt16(int32_t off = 0) const { return int16_t(peekUint16(off)); }
	uint32_t peekUint32(int32_t off = 0) const { return peekUint16(off) | (uint32_t(peekUint16(off + 2)) << 16); }

	uint8_t readByte() { uint8_t v = peekByte(); _pos += 1; return v; }
	int8_t readInt8() { return int8_t(readByte()); }
	uint16_t readUint16() { uint16_t v = peekUint16(); _pos += 2; return v; }
	int16_t readInt16() { return int16_t(readUint16()); }
	uint32_t readUint32() { uint32_t v = peekUint32(); _pos += 4; return v; }
	int32_t readInt32() { return int32_t(readUint32()); }

	std::string peekString(int32_t off = 0) const {
		std::string s;
		for (int32_t i = off; uint8_t c = peekByte(i); i++)
			s.push_back(char(c));
		return s;
	}
	std::string readString() {
		std::string s = peekString();
		_pos += uint32_t(s.size()) + 1;
		return s;
	}

	// Block finished flag, used by the interpreter's block loop.
	bool finished() const { return _finished; }
	void setFinished(bool f) { _finished = f; }

	// Call stack for sub-function calls within the same file.
	void push() { _calls.push_back({_pos, _finished}); }
	void pop(bool restore) {
		if (_calls.empty())
			return;
		Frame f = _calls.back();
		_calls.pop_back();
		if (restore) {
			_pos = f.pos;
			_finished = f.finished;
		}
	}
	void call(uint32_t offset) { push(); seek(offset); }
	size_t callDepth() const { return _calls.size(); }

private:
	struct Frame { uint32_t pos; bool finished; };

	const std::vector<uint8_t> *_bytes = nullptr;
	uint32_t _pos = 0;
	bool _finished = false;
	std::vector<Frame> _calls;
};

} // namespace gob
