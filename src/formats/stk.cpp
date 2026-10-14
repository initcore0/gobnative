// gob2native - native reimplementation of the Coktel Vision Gob engine
// Copyright (C) 2026  gob2native contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formats/stk.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>

namespace gob {

namespace {

uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t *p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	return s;
}

// Bounds-checked byte reader over a packed buffer.
struct Reader {
	const uint8_t *data;
	size_t size;
	size_t pos = 0;

	uint8_t byte() {
		if (pos >= size)
			throw std::runtime_error("LZSS: unexpected end of packed data");
		return data[pos++];
	}
};

} // namespace

std::vector<uint8_t> unpackLzss(const uint8_t *src, size_t srcSize, uint32_t outSize) {
	std::vector<uint8_t> out;
	out.reserve(outSize);
	if (outSize == 0)
		return out;

	Reader in{src, srcSize};

	// Two variants: the "extended" one is tagged with 0x1234 0x5678 and allows
	// back-references of up to 18 + 255 bytes; the standard one caps at 18.
	int extendedLen = -1;
	int winPos = 4078;
	if (srcSize >= 4 && le16(src) == 0x1234 && le16(src + 2) == 0x5678) {
		in.pos = 4;
		extendedLen = 18;
		winPos = 273;
	}

	uint8_t window[4096];
	std::fill(std::begin(window), std::end(window), uint8_t(0x20));

	auto emit = [&](uint8_t b) {
		out.push_back(b);
		window[winPos] = b;
		winPos = (winPos + 1) & 0xFFF;
	};

	unsigned flags = 0;
	while (out.size() < outSize) {
		flags >>= 1;
		if ((flags & 0x100) == 0)
			flags = in.byte() | 0xFF00u;

		if (flags & 1) {
			emit(in.byte());
		} else {
			uint8_t lo = in.byte();
			uint8_t hi = in.byte();
			int off = lo | ((hi & 0xF0) << 4);
			int len = (hi & 0x0F) + 3;
			if (len == extendedLen)
				len = in.byte() + 18;

			for (int i = 0; i < len && out.size() < outSize; i++)
				emit(window[(off + i) & 0xFFF]);
		}
	}

	return out;
}

std::vector<uint8_t> unpack(const std::vector<uint8_t> &packed, uint8_t compression) {
	if (compression == 0)
		return packed;

	if (compression == 1) {
		if (packed.size() < 4)
			throw std::runtime_error("unpack: truncated header");
		return unpackLzss(packed.data() + 4, packed.size() - 4, le32(packed.data()));
	}

	if (compression == 2) {
		// Sequence of chunks: uint16 chunkSize, uint16 realSize, 2 skipped
		// bytes, then LZSS data. A chunkSize of 0xFFFF marks the last chunk.
		// The archive stores an extra 4-byte header before the first chunk.
		std::vector<uint8_t> out;
		size_t pos = 4;
		while (true) {
			if (pos + 6 > packed.size())
				throw std::runtime_error("unpack: truncated chunk header");
			uint16_t chunkSize = le16(&packed[pos]);
			uint16_t realSize = le16(&packed[pos + 2]);
			size_t dataPos = pos + 6;
			std::vector<uint8_t> chunk = unpackLzss(&packed[dataPos], packed.size() - dataPos, realSize);
			out.insert(out.end(), chunk.begin(), chunk.end());
			if (chunkSize == 0xFFFF)
				break;
			pos += size_t(chunkSize) + 2;
		}
		return out;
	}

	throw std::runtime_error("unpack: unknown compression " + std::to_string(compression));
}

bool StkArchive::open(const std::string &path) {
	_path = path;
	_entries.clear();
	_file.close();
	_file.open(path, std::ios::binary);
	if (!_file)
		return false;

	uint8_t countBuf[2];
	if (!_file.read(reinterpret_cast<char *>(countBuf), 2))
		return false;

	uint16_t count = le16(countBuf);
	_entries.reserve(count);
	for (uint16_t i = 0; i < count; i++) {
		uint8_t rec[22];
		if (!_file.read(reinterpret_cast<char *>(rec), sizeof(rec)))
			return false;

		StkEntry e;
		const char *name = reinterpret_cast<const char *>(rec);
		e.name = upper(std::string(name, strnlen(name, 13)));
		e.size = le32(rec + 13);
		e.offset = le32(rec + 17);
		e.compression = rec[21] ? 1 : 0;
		_entries.push_back(std::move(e));
	}
	return true;
}

const StkEntry *StkArchive::find(const std::string &name) const {
	std::string key = upper(name);
	for (const StkEntry &e : _entries)
		if (e.name == key)
			return &e;
	return nullptr;
}

std::vector<uint8_t> StkArchive::read(const StkEntry &entry) {
	std::vector<uint8_t> raw(entry.size);
	_file.clear();
	_file.seekg(entry.offset);
	if (!_file.read(reinterpret_cast<char *>(raw.data()), raw.size()))
		throw std::runtime_error("STK: short read for " + entry.name);
	return unpack(raw, entry.compression);
}

std::optional<std::vector<uint8_t>> StkArchive::read(const std::string &name) {
	const StkEntry *e = find(name);
	if (!e)
		return std::nullopt;
	return read(*e);
}

} // namespace gob
