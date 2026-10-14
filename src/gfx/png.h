// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal PNG writer: 8-bit gray, gray+alpha, RGB or RGBA, per-row filters
// chosen with the usual minimum-sum heuristic, compressed with a small LZ77 +
// fixed-Huffman deflate encoder (no external zlib). And a PNG reader for
// every standard format (any bit depth and colour type, interlaced or not,
// tRNS transparency), converted to 8-bit RGBA, with its own inflater.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gob {

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc = 0);
uint32_t adler32(const uint8_t *data, size_t size, uint32_t adler = 1);

// A zlib stream (RFC 1950) holding one fixed-Huffman deflate block (RFC 1951).
std::vector<uint8_t> zlibCompress(const uint8_t *data, size_t size);

// `pixels` is `width` x `height` pixels of `channels` (1 = gray, 2 = gray
// and alpha, 3 = RGB, 4 = RGBA) bytes each, top row first, rows tightly
// packed.
std::vector<uint8_t> encodePng(const uint8_t *pixels, int width, int height, int channels);
bool writePng(const std::filesystem::path &path, const uint8_t *pixels, int width, int height, int channels);

// Decompresses a zlib stream (stored, fixed and dynamic Huffman blocks).
// Fails on a bad header, corrupt data, a wrong Adler-32 or more than
// `maxSize` bytes of output.
bool zlibDecompress(const uint8_t *data, size_t size, std::vector<uint8_t> &out, size_t maxSize);
// The same for a raw deflate stream (RFC 1951, as in zip files), without
// header or checksum.
bool inflateRaw(const uint8_t *data, size_t size, std::vector<uint8_t> &out, size_t maxSize);

struct PngInfo {
	int width = 0, height = 0;
	int bitDepth = 0;  // 1, 2, 4, 8 or 16
	int colorType = 0; // 0 gray, 2 RGB, 3 palette, 4 gray+alpha, 6 RGBA
	bool interlaced = false;
	bool hasAlpha = false; // alpha channel or tRNS
};

// The decoded image as 8-bit RGBA (16-bit samples keep their high byte,
// low bit depths are scaled to 0-255, palette and tRNS applied).
struct PngImage {
	PngInfo info;
	std::vector<uint8_t> rgba;
};

// Reads the IHDR only (cheap: for size checks without decoding).
bool readPngInfo(const uint8_t *data, size_t size, PngInfo &info, std::string *error = nullptr);
bool readPngInfo(const std::filesystem::path &path, PngInfo &info, std::string *error = nullptr);

// Images over `maxPixels` pixels are refused (default 64 Mpixel, 256 MB of
// RGBA).
bool decodePng(const uint8_t *data, size_t size, PngImage &out, std::string *error = nullptr,
               size_t maxPixels = size_t(1) << 26);
bool readPng(const std::filesystem::path &path, PngImage &out, std::string *error = nullptr,
             size_t maxPixels = size_t(1) << 26);

} // namespace gob
