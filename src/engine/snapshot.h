// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Little-endian byte streams for engine snapshots (Gobliiins' quick save,
// engine/levels_v1.cpp). A serializer is written once as a template over
// the stream type: with a Writer it appends the fields, with a Reader it
// reads them back in the same order (Reader::kReading tells which). A
// Reader that runs past its end stops reading, leaves the remaining fields
// zero and reports !ok().

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace gob::snap {

class Writer {
public:
	static constexpr bool kReading = false;

	template <typename T>
	void operator()(const T &v) {
		static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
		if constexpr (std::is_same_v<T, bool>) {
			_data.push_back(v ? 1 : 0);
		} else {
			const uint64_t u = uint64_t(v);
			for (size_t i = 0; i < sizeof(T); i++)
				_data.push_back(uint8_t(u >> (8 * i)));
		}
	}
	void bytes(const uint8_t *p, size_t n) { _data.insert(_data.end(), p, p + n); }
	// A byte block with its size (u32) in front.
	void block(const std::vector<uint8_t> &b) {
		(*this)(uint32_t(b.size()));
		bytes(b.data(), b.size());
	}
	void str(const std::string &s) {
		(*this)(uint16_t(s.size()));
		bytes(reinterpret_cast<const uint8_t *>(s.data()), s.size());
	}

	const std::vector<uint8_t> &data() const { return _data; }
	std::vector<uint8_t> take() { return std::move(_data); }

private:
	std::vector<uint8_t> _data;
};

class Reader {
public:
	static constexpr bool kReading = true;

	Reader(const uint8_t *p, size_t n) : _p(p), _end(p + n) {}
	explicit Reader(const std::vector<uint8_t> &d) : Reader(d.data(), d.size()) {}

	template <typename T>
	void operator()(T &v) {
		static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
		if (!take(sizeof(T))) {
			v = T{};
			return;
		}
		if constexpr (std::is_same_v<T, bool>) {
			v = _p[-1] != 0;
		} else {
			uint64_t u = 0;
			const uint8_t *b = _p - sizeof(T);
			for (size_t i = 0; i < sizeof(T); i++)
				u |= uint64_t(b[i]) << (8 * i);
			v = T(u);
		}
	}
	bool bytes(uint8_t *dst, size_t n) {
		if (!take(n))
			return false;
		std::memcpy(dst, _p - n, n);
		return true;
	}
	bool block(std::vector<uint8_t> &b, size_t maxSize = size_t(1) << 26) {
		uint32_t n = 0;
		(*this)(n);
		if (!_ok || n > maxSize || size_t(_end - _p) < n) {
			_ok = false;
			return false;
		}
		b.assign(_p, _p + n);
		_p += n;
		return true;
	}
	bool str(std::string &s) {
		uint16_t n = 0;
		(*this)(n);
		if (!_ok || size_t(_end - _p) < n) {
			_ok = false;
			return false;
		}
		s.assign(reinterpret_cast<const char *>(_p), n);
		_p += n;
		return true;
	}

	bool ok() const { return _ok; }
	bool atEnd() const { return _p == _end; }

private:
	bool take(size_t n) {
		if (!_ok || size_t(_end - _p) < n) {
			_ok = false;
			return false;
		}
		_p += n;
		return true;
	}

	const uint8_t *_p, *_end;
	bool _ok = true;
};

} // namespace gob::snap
