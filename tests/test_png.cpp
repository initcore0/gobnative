// gob2native - PNG writer tests: checksums and a deflate round trip
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/png.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace gob;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Just enough of an inflater for what zlibCompress writes (fixed Huffman blocks).
static bool inflateFixed(const std::vector<uint8_t> &z, std::vector<uint8_t> &out) {
	static const int lenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
	                                31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
	static const int lenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
	static const int distBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
	                                 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
	static const int distExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
	if (z.size() < 6 || ((z[0] << 8) | z[1]) % 31 != 0 || (z[0] & 0x0F) != 8)
		return false;
	size_t bitPos = 16, end = (z.size() - 4) * 8;
	auto bit = [&]() -> int {
		if (bitPos >= end)
			throw 0;
		int b = (z[bitPos / 8] >> (bitPos % 8)) & 1;
		bitPos++;
		return b;
	};
	auto bits = [&](int n) {
		int v = 0;
		for (int i = 0; i < n; i++)
			v |= bit() << i;
		return v;
	};
	try {
		bool last;
		do {
			last = bit();
			if (bits(2) != 1)
				return false;
			for (;;) {
				int code = 0, len = 0, sym = -1;
				while (sym < 0 && len < 9) {
					code = code << 1 | bit();
					len++;
					if (len == 7 && code <= 0x17)
						sym = 256 + code;
					else if (len == 8 && code >= 0x30 && code <= 0xBF)
						sym = code - 0x30;
					else if (len == 8 && code >= 0xC0 && code <= 0xC7)
						sym = 280 + code - 0xC0;
					else if (len == 9 && code >= 0x190)
						sym = 144 + code - 0x190;
				}
				if (sym < 0 || sym > 285)
					return false;
				if (sym < 256) {
					out.push_back(uint8_t(sym));
					continue;
				}
				if (sym == 256)
					break;
				int length = lenBase[sym - 257] + bits(lenExtra[sym - 257]);
				int d = 0;
				for (int i = 0; i < 5; i++)
					d = d << 1 | bit();
				if (d > 29)
					return false;
				size_t dist = size_t(distBase[d] + bits(distExtra[d]));
				if (dist > out.size())
					return false;
				for (int i = 0; i < length; i++)
					out.push_back(out[out.size() - dist]);
			}
		} while (!last);
	} catch (int) {
		return false;
	}
	size_t n = z.size();
	uint32_t adler = uint32_t(z[n - 4]) << 24 | z[n - 3] << 16 | z[n - 2] << 8 | z[n - 1];
	return adler == adler32(out.data(), out.size());
}

static void roundTrip(const std::vector<uint8_t> &data) {
	std::vector<uint8_t> z = zlibCompress(data.data(), data.size()), back;
	CHECK(inflateFixed(z, back));
	CHECK(back == data);
}

int main() {
	const char *digits = "123456789";
	CHECK(crc32(reinterpret_cast<const uint8_t *>(digits), 9) == 0xCBF43926u);
	const char *wiki = "Wikipedia";
	CHECK(adler32(reinterpret_cast<const uint8_t *>(wiki), 9) == 0x11E60398u);

	roundTrip({});
	roundTrip({42});
	std::vector<uint8_t> runs(100000, 7), noise(70000), mixed;
	uint32_t seed = 1;
	for (uint8_t &b : noise) {
		seed = seed * 1103515245u + 12345u;
		b = uint8_t(seed >> 24);
	}
	for (int i = 0; i < 50000; i++)
		mixed.push_back(uint8_t((i % 300) < 150 ? i % 7 : (i * 31) >> 3));
	roundTrip(runs);
	roundTrip(noise);
	roundTrip(mixed);
	CHECK(zlibCompress(runs.data(), runs.size()).size() < 1000);

	// A 3x2 RGB image: signature, IHDR first, IEND last.
	const uint8_t px[18] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 1, 2, 3, 4, 5, 6, 7, 8, 9};
	std::vector<uint8_t> png = encodePng(px, 3, 2, 3);
	CHECK(png.size() > 8 + 25 + 12 + 12);
	CHECK(std::memcmp(png.data(), "\x89PNG\r\n\x1a\n", 8) == 0);
	CHECK(std::memcmp(png.data() + 12, "IHDR", 4) == 0);
	CHECK(std::memcmp(png.data() + png.size() - 8, "IEND", 4) == 0);

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("all PNG tests passed\n");
	return 0;
}
