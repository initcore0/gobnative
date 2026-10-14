// gob2native - GOG installer import
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-1 (FIPS 180-4) and MD5 (RFC 1321), for the checksums Inno Setup and
// GOG keep of every file.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace gob {

class Sha1 {
public:
	Sha1() { reset(); }
	void reset();
	void update(const uint8_t *data, size_t size);
	std::array<uint8_t, 20> finish();

private:
	void block(const uint8_t *p);

	uint32_t _h[5];
	uint8_t _buf[64];
	size_t _bufLen = 0;
	uint64_t _length = 0; // bytes
};

class Md5 {
public:
	Md5() { reset(); }
	void reset();
	void update(const uint8_t *data, size_t size);
	std::array<uint8_t, 16> finish();

private:
	void block(const uint8_t *p);

	uint32_t _h[4];
	uint8_t _buf[64];
	size_t _bufLen = 0;
	uint64_t _length = 0;
};

// Lower-case hex digits of a digest.
template <size_t N>
std::string hexDigest(const std::array<uint8_t, N> &digest) {
	static const char digits[] = "0123456789abcdef";
	std::string s;
	for (uint8_t b : digest) {
		s += digits[b >> 4];
		s += digits[b & 15];
	}
	return s;
}

} // namespace gob
