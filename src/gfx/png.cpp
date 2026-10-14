// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/png.h"

#include <array>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <fstream>

namespace gob {

namespace {

// Deflate length and distance code bases and extra bit counts (RFC 1951 3.2.5).
constexpr uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

constexpr int kWindow = 32768, kMinMatch = 3, kMaxMatch = 258;
constexpr int kHashBits = 15, kMaxChain = 32, kGoodEnough = 64;

// Deflate's bit order: values LSB first, Huffman codes MSB first.
class BitWriter {
public:
	explicit BitWriter(std::vector<uint8_t> &out) : _out(out) {}

	void bits(uint32_t value, int count) {
		_acc |= uint64_t(value) << _n;
		_n += count;
		while (_n >= 8) {
			_out.push_back(uint8_t(_acc));
			_acc >>= 8;
			_n -= 8;
		}
	}
	void code(uint32_t code, int length) {
		uint32_t rev = 0;
		for (int i = 0; i < length; i++)
			rev |= ((code >> i) & 1) << (length - 1 - i);
		bits(rev, length);
	}
	void flush() {
		if (_n > 0)
			_out.push_back(uint8_t(_acc));
		_acc = 0;
		_n = 0;
	}

private:
	std::vector<uint8_t> &_out;
	uint64_t _acc = 0;
	int _n = 0;
};

// Fixed Huffman code of a literal/length symbol (RFC 1951 3.2.6).
void putSymbol(BitWriter &w, int sym) {
	if (sym < 144)
		w.code(0x30 + sym, 8);
	else if (sym < 256)
		w.code(0x190 + (sym - 144), 9);
	else if (sym < 280)
		w.code(sym - 256, 7);
	else
		w.code(0xC0 + (sym - 280), 8);
}

void putMatch(BitWriter &w, int length, int dist) {
	int l = 28;
	while (kLengthBase[l] > length)
		l--;
	putSymbol(w, 257 + l);
	w.bits(uint32_t(length - kLengthBase[l]), kLengthExtra[l]);
	int d = 29;
	while (kDistBase[d] > dist)
		d--;
	w.code(uint32_t(d), 5);
	w.bits(uint32_t(dist - kDistBase[d]), kDistExtra[d]);
}

uint32_t hash3(const uint8_t *p) {
	return ((uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]) * 2654435761u) >> (32 - kHashBits);
}

void put32be(std::vector<uint8_t> &b, uint32_t v) {
	for (int s = 24; s >= 0; s -= 8)
		b.push_back(uint8_t(v >> s));
}

void chunk(std::vector<uint8_t> &png, const char *type, const std::vector<uint8_t> &data) {
	put32be(png, uint32_t(data.size()));
	size_t start = png.size();
	png.insert(png.end(), type, type + 4);
	png.insert(png.end(), data.begin(), data.end());
	put32be(png, crc32(&png[start], png.size() - start));
}

uint8_t paeth(int a, int b, int c) {
	int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
	return uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

} // namespace

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc) {
	static const std::array<uint32_t, 256> table = [] {
		std::array<uint32_t, 256> t{};
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[n] = c;
		}
		return t;
	}();
	crc = ~crc;
	for (size_t i = 0; i < size; i++)
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

uint32_t adler32(const uint8_t *data, size_t size, uint32_t adler) {
	uint32_t a = adler & 0xFFFF, b = adler >> 16;
	while (size > 0) {
		size_t n = size < 5552 ? size : 5552; // no overflow before the modulo
		size -= n;
		while (n--) {
			a += *data++;
			b += a;
		}
		a %= 65521;
		b %= 65521;
	}
	return b << 16 | a;
}

std::vector<uint8_t> zlibCompress(const uint8_t *data, size_t size) {
	std::vector<uint8_t> out = {0x78, 0x01}; // deflate, 32K window, no dictionary
	out.reserve(size / 2 + 64);
	BitWriter w(out);
	w.bits(1, 1); // final block
	w.bits(1, 2); // fixed Huffman codes

	std::vector<int32_t> head(size_t(1) << kHashBits, -1), prev(kWindow, -1);
	auto insert = [&](size_t pos) {
		uint32_t h = hash3(data + pos);
		prev[pos & (kWindow - 1)] = head[h];
		head[h] = int32_t(pos);
	};
	size_t pos = 0;
	while (pos < size) {
		int bestLen = 0, bestDist = 0;
		if (pos + kMinMatch <= size) {
			const int maxLen = int(std::min<size_t>(kMaxMatch, size - pos));
			int32_t cand = head[hash3(data + pos)];
			for (int chain = 0; cand >= 0 && chain < kMaxChain; chain++) {
				size_t c = size_t(cand);
				if (pos - c > size_t(kWindow - 1))
					break;
				if (data[c + size_t(bestLen)] == data[pos + size_t(bestLen)] || bestLen == 0) {
					int len = 0;
					while (len < maxLen && data[c + size_t(len)] == data[pos + size_t(len)])
						len++;
					if (len > bestLen) {
						bestLen = len;
						bestDist = int(pos - c);
						if (len >= kGoodEnough || len == maxLen)
							break;
					}
				}
				int32_t next = prev[c & (kWindow - 1)];
				if (next >= cand) // the slot was reused by a newer position
					break;
				cand = next;
			}
		}
		if (bestLen >= kMinMatch) {
			putMatch(w, bestLen, bestDist);
			for (int i = 0; i < bestLen; i++, pos++)
				if (pos + kMinMatch <= size)
					insert(pos);
		} else {
			putSymbol(w, data[pos]);
			if (pos + kMinMatch <= size)
				insert(pos);
			pos++;
		}
	}
	putSymbol(w, 256); // end of block
	w.flush();
	put32be(out, adler32(data, size));
	return out;
}

std::vector<uint8_t> encodePng(const uint8_t *pixels, int width, int height, int channels) {
	const size_t stride = size_t(width) * size_t(channels);
	std::vector<uint8_t> raw;
	raw.reserve((stride + 1) * size_t(height));
	std::vector<uint8_t> zero(stride, 0), cand[5];
	for (auto &c : cand)
		c.resize(stride);
	for (int y = 0; y < height; y++) {
		const uint8_t *row = pixels + size_t(y) * stride;
		const uint8_t *up = y > 0 ? row - stride : zero.data();
		// Filter types 0-4 (none, sub, up, average, Paeth); keep the one with
		// the smallest sum of absolute (signed) residuals.
		int best = 0;
		long bestSum = -1;
		for (int f = 0; f < 5; f++) {
			long sum = 0;
			for (size_t i = 0; i < stride; i++) {
				int a = i >= size_t(channels) ? row[i - size_t(channels)] : 0;
				int b = up[i];
				int c = i >= size_t(channels) ? up[i - size_t(channels)] : 0;
				int pred = f == 0 ? 0 : f == 1 ? a : f == 2 ? b : f == 3 ? (a + b) / 2 : paeth(a, b, c);
				uint8_t v = uint8_t(row[i] - pred);
				cand[f][i] = v;
				sum += v < 128 ? v : 256 - v;
			}
			if (bestSum < 0 || sum < bestSum) {
				bestSum = sum;
				best = f;
			}
		}
		raw.push_back(uint8_t(best));
		raw.insert(raw.end(), cand[best].begin(), cand[best].end());
	}

	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	std::vector<uint8_t> ihdr;
	put32be(ihdr, uint32_t(width));
	put32be(ihdr, uint32_t(height));
	ihdr.push_back(8);                       // bits per channel
	ihdr.push_back(channels == 4 ? 6 : channels == 3 ? 2 : channels == 2 ? 4 : 0); // RGBA, RGB, gray+alpha, gray
	ihdr.push_back(0);                       // deflate
	ihdr.push_back(0);                       // adaptive filtering
	ihdr.push_back(0);                       // not interlaced
	chunk(png, "IHDR", ihdr);
	chunk(png, "IDAT", zlibCompress(raw.data(), raw.size()));
	chunk(png, "IEND", {});
	return png;
}

bool writePng(const std::filesystem::path &path, const uint8_t *pixels, int width, int height, int channels) {
	std::vector<uint8_t> png = encodePng(pixels, width, height, channels);
	std::ofstream f(path, std::ios::binary);
	if (!f)
		return false;
	f.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
	return bool(f);
}

// --- reading -------------------------------------------------------------

namespace {

// Inflater after the canonical-Huffman scheme of zlib's "puff": codes are
// decoded bit by bit against per-length counts, which is small and plenty
// fast for images of a few megabytes.
class Inflater {
public:
	Inflater(const uint8_t *data, size_t size, std::vector<uint8_t> &out, size_t maxSize)
	    : _in(data), _size(size), _out(out), _max(maxSize) {}

	bool run() {
		try {
			int last;
			do {
				last = bits(1);
				int type = bits(2);
				if (type == 0)
					stored();
				else if (type == 1)
					fixed();
				else if (type == 2)
					dynamic();
				else
					return false;
			} while (!last);
			return true;
		} catch (const Error &) {
			return false;
		}
	}
	size_t consumed() const { return _pos; }

private:
	struct Error {};
	struct Huffman {
		uint16_t count[16] = {};
		uint16_t symbol[320] = {};
	};

	int bits(int need) {
		uint32_t val = _bitBuf;
		while (_bitCount < need) {
			if (_pos >= _size)
				throw Error();
			val |= uint32_t(_in[_pos++]) << _bitCount;
			_bitCount += 8;
		}
		_bitBuf = val >> need;
		_bitCount -= need;
		return int(val & ((1u << need) - 1));
	}

	void put(uint8_t b) {
		if (_out.size() >= _max)
			throw Error();
		_out.push_back(b);
	}

	void stored() {
		_bitBuf = 0;
		_bitCount = 0;
		if (_pos + 4 > _size)
			throw Error();
		unsigned len = _in[_pos] | (_in[_pos + 1] << 8);
		unsigned nlen = _in[_pos + 2] | (_in[_pos + 3] << 8);
		_pos += 4;
		if (len != (~nlen & 0xFFFF) || _pos + len > _size)
			throw Error();
		for (unsigned i = 0; i < len; i++)
			put(_in[_pos++]);
	}

	int decode(const Huffman &h) {
		int code = 0, first = 0, index = 0;
		for (int len = 1; len < 16; len++) {
			code |= bits(1);
			int count = h.count[len];
			if (code - count < first)
				return h.symbol[index + (code - first)];
			index += count;
			first += count;
			first <<= 1;
			code <<= 1;
		}
		throw Error();
	}

	// Returns false for an over-subscribed set of lengths; incomplete sets
	// are allowed (a single distance code is legal).
	static bool build(Huffman &h, const uint8_t *lengths, int n) {
		std::fill(std::begin(h.count), std::end(h.count), 0);
		for (int s = 0; s < n; s++)
			h.count[lengths[s]]++;
		if (h.count[0] == n)
			return true;
		int left = 1;
		for (int len = 1; len < 16; len++) {
			left <<= 1;
			left -= h.count[len];
			if (left < 0)
				return false;
		}
		uint16_t offs[16];
		offs[1] = 0;
		for (int len = 1; len < 15; len++)
			offs[len + 1] = uint16_t(offs[len] + h.count[len]);
		for (int s = 0; s < n; s++)
			if (lengths[s])
				h.symbol[offs[lengths[s]]++] = uint16_t(s);
		return true;
	}

	void codes(const Huffman &lit, const Huffman &dist) {
		static const uint16_t lenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
		                                     31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
		static const uint8_t lenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
		                                     2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
		static const uint16_t distBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
		                                      33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
		                                      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
		static const uint8_t distExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
		                                      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
		for (;;) {
			int sym = decode(lit);
			if (sym < 256) {
				put(uint8_t(sym));
			} else if (sym == 256) {
				return;
			} else {
				sym -= 257;
				if (sym >= 29)
					throw Error();
				int len = lenBase[sym] + bits(lenExtra[sym]);
				int d = decode(dist);
				if (d >= 30)
					throw Error();
				size_t back = size_t(distBase[d] + bits(distExtra[d]));
				if (back > _out.size())
					throw Error();
				for (int i = 0; i < len; i++)
					put(_out[_out.size() - back]);
			}
		}
	}

	void fixed() {
		// Built once, thread-safely (pictures are decoded on several threads).
		struct Tables {
			Huffman lit, dist;
			Tables() {
				uint8_t l[288];
				for (int i = 0; i < 288; i++)
					l[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
				build(lit, l, 288);
				uint8_t d[30];
				std::fill(std::begin(d), std::end(d), 5);
				build(dist, d, 30);
			}
		};
		static const Tables tables;
		codes(tables.lit, tables.dist);
	}

	void dynamic() {
		static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
		int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
		if (nlen > 286 || ndist > 30)
			throw Error();
		uint8_t lengths[320] = {};
		for (int i = 0; i < ncode; i++)
			lengths[order[i]] = uint8_t(bits(3));
		Huffman lencode, lit, dist;
		if (!build(lencode, lengths, 19))
			throw Error();
		std::fill(std::begin(lengths), std::end(lengths), 0);
		for (int i = 0; i < nlen + ndist;) {
			int sym = decode(lencode);
			if (sym < 16) {
				lengths[i++] = uint8_t(sym);
				continue;
			}
			int len = 0, rep;
			if (sym == 16) {
				if (i == 0)
					throw Error();
				len = lengths[i - 1];
				rep = 3 + bits(2);
			} else if (sym == 17) {
				rep = 3 + bits(3);
			} else {
				rep = 11 + bits(7);
			}
			if (i + rep > nlen + ndist)
				throw Error();
			while (rep--)
				lengths[i++] = uint8_t(len);
		}
		if (lengths[256] == 0)
			throw Error();
		if (!build(lit, lengths, nlen) || !build(dist, lengths + nlen, ndist))
			throw Error();
		codes(lit, dist);
	}

	const uint8_t *_in;
	size_t _size, _pos = 0;
	uint32_t _bitBuf = 0;
	int _bitCount = 0;
	std::vector<uint8_t> &_out;
	size_t _max;
};

uint32_t be32(const uint8_t *p) {
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool fail(std::string *error, const std::string &what) {
	if (error)
		*error = what;
	return false;
}

bool readFile(const std::filesystem::path &path, std::vector<uint8_t> &data, std::string *error) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return fail(error, "cannot open the file");
	data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

int channelsOf(int colorType) {
	switch (colorType) {
	case 0: return 1;
	case 2: return 3;
	case 3: return 1;
	case 4: return 2;
	case 6: return 4;
	default: return 0;
	}
}

bool parseIhdr(const uint8_t *data, size_t size, PngInfo &info, std::string *error) {
	static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	if (size < 33 || std::memcmp(data, sig, 8) != 0)
		return fail(error, "not a PNG file");
	if (be32(data + 8) != 13 || std::memcmp(data + 12, "IHDR", 4) != 0)
		return fail(error, "no IHDR chunk");
	const uint8_t *h = data + 16;
	info.width = int(std::min<uint32_t>(be32(h), 0x7FFFFFFF));
	info.height = int(std::min<uint32_t>(be32(h + 4), 0x7FFFFFFF));
	info.bitDepth = h[8];
	info.colorType = h[9];
	info.interlaced = h[12] == 1;
	info.hasAlpha = info.colorType == 4 || info.colorType == 6;
	const int d = info.bitDepth;
	bool depthOk = false;
	switch (info.colorType) {
	case 0: depthOk = d == 1 || d == 2 || d == 4 || d == 8 || d == 16; break;
	case 3: depthOk = d == 1 || d == 2 || d == 4 || d == 8; break;
	case 2: case 4: case 6: depthOk = d == 8 || d == 16; break;
	default: return fail(error, "unknown colour type " + std::to_string(info.colorType));
	}
	if (!depthOk)
		return fail(error, "bad bit depth " + std::to_string(d));
	if (info.width <= 0 || info.height <= 0)
		return fail(error, "empty image");
	if (h[10] != 0 || h[11] != 0 || h[12] > 1)
		return fail(error, "unknown compression, filter or interlace method");
	return true;
}

} // namespace

bool zlibDecompress(const uint8_t *data, size_t size, std::vector<uint8_t> &out, size_t maxSize) {
	if (size < 6 || (data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20))
		return false;
	out.clear();
	Inflater inf(data + 2, size - 2, out, maxSize);
	if (!inf.run())
		return false;
	size_t end = 2 + inf.consumed();
	if (end + 4 > size)
		return false;
	return be32(data + end) == adler32(out.data(), out.size());
}

bool inflateRaw(const uint8_t *data, size_t size, std::vector<uint8_t> &out, size_t maxSize) {
	out.clear();
	Inflater inf(data, size, out, maxSize);
	return inf.run();
}

bool readPngInfo(const uint8_t *data, size_t size, PngInfo &info, std::string *error) {
	return parseIhdr(data, size, info, error);
}

bool readPngInfo(const std::filesystem::path &path, PngInfo &info, std::string *error) {
	// The IHDR is in the first 33 bytes.
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return fail(error, "cannot open the file");
	uint8_t head[33] = {};
	f.read(reinterpret_cast<char *>(head), sizeof(head));
	return parseIhdr(head, size_t(f.gcount()), info, error);
}

bool decodePng(const uint8_t *data, size_t size, PngImage &out, std::string *error, size_t maxPixels) {
	PngInfo info;
	if (!parseIhdr(data, size, info, error))
		return false;
	if (size_t(info.width) * size_t(info.height) > maxPixels)
		return fail(error, "image too large");

	// Gather the chunks we need; check CRCs of the critical ones.
	std::vector<uint8_t> idat;
	std::array<uint8_t, 1024> palette{}; // RGBA
	int paletteSize = 0;
	bool haveTrns = false;
	int trnsGray = -1, trnsR = -1, trnsG = -1, trnsB = -1;
	bool sawEnd = false;
	for (size_t pos = 8; pos + 12 <= size;) {
		uint32_t len = be32(data + pos);
		if (len > size - pos - 12)
			return fail(error, "truncated chunk");
		const uint8_t *type = data + pos + 4, *body = data + pos + 8;
		bool critical = !(type[0] & 0x20);
		if (critical && crc32(type, len + 4) != be32(body + len))
			return fail(error, std::string("bad CRC in ") + std::string(reinterpret_cast<const char *>(type), 4));
		if (std::memcmp(type, "IHDR", 4) == 0) {
			// parsed above
		} else if (std::memcmp(type, "IDAT", 4) == 0) {
			idat.insert(idat.end(), body, body + len);
		} else if (std::memcmp(type, "PLTE", 4) == 0) {
			if (len % 3 || len > 768)
				return fail(error, "bad palette");
			paletteSize = int(len / 3);
			for (int i = 0; i < paletteSize; i++) {
				palette[size_t(i) * 4] = body[i * 3];
				palette[size_t(i) * 4 + 1] = body[i * 3 + 1];
				palette[size_t(i) * 4 + 2] = body[i * 3 + 2];
				palette[size_t(i) * 4 + 3] = 255;
			}
		} else if (std::memcmp(type, "tRNS", 4) == 0) {
			haveTrns = true;
			if (info.colorType == 3) {
				for (uint32_t i = 0; i < len && i < 256; i++)
					palette[size_t(i) * 4 + 3] = body[i];
			} else if (info.colorType == 0 && len >= 2) {
				trnsGray = (body[0] << 8) | body[1];
			} else if (info.colorType == 2 && len >= 6) {
				trnsR = (body[0] << 8) | body[1];
				trnsG = (body[2] << 8) | body[3];
				trnsB = (body[4] << 8) | body[5];
			}
		} else if (std::memcmp(type, "IEND", 4) == 0) {
			sawEnd = true;
			break;
		} else if (critical) {
			return fail(error, "unknown critical chunk");
		}
		pos += 12 + len;
	}
	if (!sawEnd && idat.empty())
		return fail(error, "no image data");
	if (info.colorType == 3 && paletteSize == 0)
		return fail(error, "palette image without a palette");
	info.hasAlpha |= haveTrns;

	const int channels = channelsOf(info.colorType);
	const int bitsPerPixel = channels * info.bitDepth;
	const int bpp = std::max(1, bitsPerPixel / 8); // filter byte distance
	auto strideOf = [&](int w) { return (size_t(w) * size_t(bitsPerPixel) + 7) / 8; };

	// The seven Adam7 passes, or one pass over the whole image.
	struct Pass { int x0, y0, dx, dy; };
	static const Pass adam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
	                              {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
	static const Pass single[1] = {{0, 0, 1, 1}};
	const Pass *passes = info.interlaced ? adam7 : single;
	const int passCount = info.interlaced ? 7 : 1;

	size_t expected = 0;
	for (int p = 0; p < passCount; p++) {
		int pw = (info.width - passes[p].x0 + passes[p].dx - 1) / passes[p].dx;
		int ph = (info.height - passes[p].y0 + passes[p].dy - 1) / passes[p].dy;
		if (pw > 0 && ph > 0)
			expected += (strideOf(pw) + 1) * size_t(ph);
	}
	std::vector<uint8_t> raw;
	if (!zlibDecompress(idat.data(), idat.size(), raw, expected))
		return fail(error, "corrupt image data");
	if (raw.size() < expected)
		return fail(error, "image data too short");

	out.info = info;
	out.rgba.assign(size_t(info.width) * size_t(info.height) * 4, 0);
	const int maxVal = (1 << info.bitDepth) - 1;

	size_t at = 0;
	std::vector<uint8_t> prev, cur;
	for (int p = 0; p < passCount; p++) {
		const Pass &ps = passes[p];
		int pw = (info.width - ps.x0 + ps.dx - 1) / ps.dx;
		int ph = (info.height - ps.y0 + ps.dy - 1) / ps.dy;
		if (pw <= 0 || ph <= 0)
			continue;
		const size_t stride = strideOf(pw);
		prev.assign(stride, 0);
		cur.resize(stride);
		for (int y = 0; y < ph; y++) {
			uint8_t filter = raw[at++];
			const uint8_t *src = &raw[at];
			at += stride;
			for (size_t i = 0; i < stride; i++) {
				int a = i >= size_t(bpp) ? cur[i - size_t(bpp)] : 0;
				int b = prev[i];
				int c = i >= size_t(bpp) ? prev[i - size_t(bpp)] : 0;
				int pred;
				switch (filter) {
				case 0: pred = 0; break;
				case 1: pred = a; break;
				case 2: pred = b; break;
				case 3: pred = (a + b) / 2; break;
				case 4: pred = paeth(a, b, c); break;
				default: return fail(error, "bad filter type");
				}
				cur[i] = uint8_t(src[i] + pred);
			}
			// Unpack samples of this row.
			auto sample = [&](int x, int ch) -> int {
				if (info.bitDepth == 8)
					return cur[size_t(x) * size_t(channels) + size_t(ch)];
				if (info.bitDepth == 16) {
					size_t o = (size_t(x) * size_t(channels) + size_t(ch)) * 2;
					return (cur[o] << 8) | cur[o + 1];
				}
				size_t bit = size_t(x) * size_t(info.bitDepth); // one channel
				int shift = 8 - info.bitDepth - int(bit % 8);
				return (cur[bit / 8] >> shift) & maxVal;
			};
			auto to8 = [&](int v) -> uint8_t {
				if (info.bitDepth == 16)
					return uint8_t(v >> 8);
				if (info.bitDepth == 8)
					return uint8_t(v);
				return uint8_t(v * 255 / maxVal);
			};
			for (int x = 0; x < pw; x++) {
				uint8_t *o = &out.rgba[(size_t(ps.y0 + y * ps.dy) * size_t(info.width) + size_t(ps.x0 + x * ps.dx)) * 4];
				switch (info.colorType) {
				case 0: {
					int g = sample(x, 0);
					o[0] = o[1] = o[2] = to8(g);
					o[3] = g == trnsGray ? 0 : 255;
					break;
				}
				case 2: {
					int r = sample(x, 0), g = sample(x, 1), b = sample(x, 2);
					o[0] = to8(r);
					o[1] = to8(g);
					o[2] = to8(b);
					o[3] = (r == trnsR && g == trnsG && b == trnsB) ? 0 : 255;
					break;
				}
				case 3: {
					int idx = sample(x, 0);
					if (idx >= paletteSize)
						return fail(error, "palette index out of range");
					std::memcpy(o, &palette[size_t(idx) * 4], 4);
					break;
				}
				case 4:
					o[0] = o[1] = o[2] = to8(sample(x, 0));
					o[3] = to8(sample(x, 1));
					break;
				case 6:
					for (int ch = 0; ch < 4; ch++)
						o[ch] = to8(sample(x, ch));
					break;
				}
			}
			std::swap(prev, cur);
			cur.resize(stride);
		}
	}
	return true;
}

bool readPng(const std::filesystem::path &path, PngImage &out, std::string *error, size_t maxPixels) {
	std::vector<uint8_t> data;
	if (!readFile(path, data, error))
		return false;
	return decodePng(data.data(), data.size(), out, error, maxPixels);
}

} // namespace gob
