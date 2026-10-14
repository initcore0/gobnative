// gob2native - IMD decoder tests with synthetic videos (no game data needed):
// every block type, both LZ77 variants, palettes, sound, frame tables, jumps
// and broken data
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formats/imd.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

using namespace gob;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

using Bytes = std::vector<uint8_t>;

void put(Bytes &out, uint32_t v, int bytes) {
	for (int i = 0; i < bytes; i++)
		out.push_back(uint8_t(v >> (8 * i)));
}

struct Spec {
	int version = 4;
	uint16_t features = 0;
	int x = 0, y = 0, w = 8, h = 4;
	bool stdCoords = false;
	int stdX = 0, stdY = 0, stdW = 0, stdH = 0;
	bool framePos = false;
	std::vector<std::array<int16_t, 4>> coords; // per frame (with kFrameCoords)
	int soundFreq = 0, sliceSize = 0, slicesCount = 0;
	std::vector<Bytes> frames;                  // each frame's command bytes
};

// Builds an IMD: header, palette (colour i = i & 63), coordinates, table
// offsets, sound properties, frames, then the tables.
Bytes build(const Spec &s) {
	Bytes out;
	uint16_t features = s.features;
	if (s.soundFreq)
		features |= ImdDecoder::kSound;
	if (!s.coords.empty())
		features |= ImdDecoder::kFrameCoords;
	put(out, 0, 2);
	out.push_back(uint8_t(s.version));
	out.push_back(uint8_t(features));
	put(out, uint32_t(s.frames.size()), 2);
	put(out, uint32_t(s.x), 2);
	put(out, uint32_t(s.y), 2);
	put(out, uint32_t(s.w), 2);
	put(out, uint32_t(s.h), 2);
	put(out, 0, 2); // flags
	size_t firstFramePosAt = out.size();
	put(out, 0, 2);
	for (int i = 0; i < 768; i++)
		out.push_back(uint8_t((i / 3) & 63));
	if (s.version >= 3) {
		put(out, s.stdCoords ? 1 : 0, 2);
		if (s.stdCoords) {
			put(out, uint32_t(s.stdX), 2);
			put(out, uint32_t(s.stdY), 2);
			put(out, uint32_t(s.stdW), 2);
			put(out, uint32_t(s.stdH), 2);
		}
	}
	size_t framePosAt = 0, coordsAt = 0;
	if (s.version >= 4) {
		framePosAt = out.size();
		put(out, 0, 4);
	}
	if (!s.coords.empty()) {
		coordsAt = out.size();
		put(out, 0, 4);
	}
	if (s.soundFreq) {
		put(out, uint32_t(s.soundFreq), 2);
		put(out, uint32_t(s.sliceSize), 2);
		put(out, uint32_t(s.slicesCount), 2);
	}
	if (features & ImdDecoder::kDataSize) {
		put(out, 100, 2);
		put(out, 100, 2);
	}
	auto patch = [&](size_t at, uint32_t v, int bytes) {
		for (int i = 0; i < bytes; i++)
			out[at + size_t(i)] = uint8_t(v >> (8 * i));
	};
	patch(firstFramePosAt, uint32_t(out.size()), 2);
	std::vector<uint32_t> positions;
	for (const Bytes &f : s.frames) {
		positions.push_back(uint32_t(out.size()));
		out.insert(out.end(), f.begin(), f.end());
	}
	if (s.framePos) {
		patch(framePosAt, uint32_t(out.size()), 4);
		for (uint32_t p : positions)
			put(out, p, 4);
	}
	if (!s.coords.empty()) {
		patch(coordsAt, uint32_t(out.size()), 4);
		for (const auto &c : s.coords)
			for (int16_t v : c)
				put(out, uint32_t(uint16_t(v)), 2);
	}
	return out;
}

// A video command: chunk data (type byte first) with its length word.
Bytes chunk(const Bytes &data) {
	Bytes out;
	put(out, uint32_t(data.size() - 2), 2);
	out.insert(out.end(), data.begin(), data.end());
	return out;
}

Bytes cat(std::initializer_list<Bytes> parts) {
	Bytes out;
	for (const Bytes &p : parts)
		out.insert(out.end(), p.begin(), p.end());
	return out;
}

// A whole block of w x h pixels with values from `first` on, plus one pad byte.
Bytes wholeBlock(int w, int h, uint8_t first, uint8_t type = 0x02) {
	Bytes d{type};
	for (int i = 0; i < w * h; i++)
		d.push_back(uint8_t(first + i));
	d.push_back(0);
	return d;
}

struct Canvas {
	int w, h;
	Bytes px;
	Canvas(int w_, int h_, uint8_t fill = 0xEE) : w(w_), h(h_), px(size_t(w_ * h_), fill) {}
	uint8_t at(int x, int y) const { return px[size_t(y * w + x)]; }
};

bool decode(ImdDecoder &imd, Canvas &c, ImdDecoder::Frame &f) {
	return imd.decodeFrame(c.px.data(), c.w, c.h, c.w, f);
}

} // namespace

int main() {
	// Header fields, a whole first frame and a sparse second frame.
	{
		Spec s;
		s.version = 2;
		s.x = 2;
		s.y = 1;
		Bytes sparse{0x01};
		for (int row = 0; row < 4; row++) // 2 literal pixels, a hole of 3, 3 literal pixels
			sparse.insert(sparse.end(), {0x81, uint8_t(100 + row), uint8_t(110 + row), 0x02, 0x82, 1, 2, 3});
		sparse.push_back(0);
		s.frames = {chunk(wholeBlock(8, 4, 1)), chunk(sparse)};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		CHECK(imd.version() == 2 && imd.frameCount() == 2 && imd.width() == 8 && imd.height() == 4);
		CHECK(imd.defaultX() == 2 && imd.defaultY() == 1 && !imd.hasSound() && imd.frameRate() == 12.0);
		CHECK(imd.palette()[3 * 70] == 70 - 64);
		Canvas c(12, 6);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f) && imd.curFrame() == 0);
		CHECK(c.at(2, 1) == 1 && c.at(9, 1) == 8 && c.at(2, 4) == 25 && c.at(9, 4) == 32);
		CHECK(c.at(1, 1) == 0xEE && c.at(10, 1) == 0xEE && c.at(2, 0) == 0xEE && c.at(2, 5) == 0xEE);
		CHECK(f.dirty.size() == 1 && f.dirty[0].left == 2 && f.dirty[0].right == 10 && f.dirty[0].bottom == 5);
		CHECK(decode(imd, c, f) && imd.endOfVideo());
		CHECK(c.at(2, 1) == 100 && c.at(3, 1) == 110 && c.at(4, 1) == 3 && c.at(6, 1) == 5); // hole kept
		CHECK(c.at(7, 1) == 1 && c.at(9, 1) == 3 && c.at(3, 4) == 113);
		CHECK(!decode(imd, c, f) && imd.error().empty()); // the end, not an error
	}

	// Clipping: a video partly off the destination.
	{
		Spec s;
		s.x = -2;
		s.y = 2;
		s.frames = {chunk(wholeBlock(8, 4, 1))};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		Canvas c(4, 4);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f));
		CHECK(c.at(0, 2) == 3 && c.at(3, 3) == 14 && c.at(0, 1) == 0xEE);
	}

	// LZ77: standard (window at 4078, overlapping copy) and extended
	// (window at 273, a length byte).
	{
		Bytes std77{0x82};
		put(std77, 32, 4); // 4 rows of 8
		// L L L R: "ABC" then 5 bytes from 4078 = "ABCAB"; then 24 literals.
		// (The flag byte's other four items are literals 0-3.)
		std77.insert(std77.end(), {0xF7, 'A', 'B', 'C', 0xEE, 0xF2, 0, 1, 2, 3});
		for (int g = 0; g < 3; g++) {
			std77.push_back(0xFF);
			for (int i = 0; i < (g < 2 ? 8 : 4); i++)
				std77.push_back(uint8_t(4 + g * 8 + i));
		}
		std77.push_back(0);
		Bytes ext77{0x82};
		put(ext77, 32, 4);
		ext77.insert(ext77.end(), {0x34, 0x12, 0x78, 0x56});
		// L L L R(21 via the length byte) then 8 literals: "ABC" * 8 + 8 bytes.
		ext77.insert(ext77.end(), {0xF7, 'A', 'B', 'C', 0x11, 0x1F, 3, 200, 201, 202, 203});
		ext77.insert(ext77.end(), {0xFF, 204, 205, 206, 207});
		ext77.push_back(0);
		Spec s;
		s.frames = {chunk(std77), chunk(ext77)};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		Canvas c(8, 4);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f));
		const char *row0 = "ABCABCAB";
		for (int x = 0; x < 8; x++)
			CHECK(c.at(x, 0) == uint8_t(row0[x]));
		CHECK(c.at(0, 1) == 0 && c.at(7, 3) == 23);
		CHECK(decode(imd, c, f) && imd.error().empty());
		for (int i = 0; i < 24; i++)
			CHECK(c.at(i % 8, i / 8) == uint8_t("ABC"[i % 3]));
		CHECK(c.at(0, 3) == 200 && c.at(7, 3) == 207);
	}

	// Block types 0x42 (quarter wide) and 0x22 (half high), a palette in a
	// chunk (0x10) and a palette command.
	{
		Bytes quarter{0x42, 7, 9, 11, 13, 15, 17, 19, 21, 0};
		Bytes half = wholeBlock(8, 2, 50, 0x22);
		Bytes palChunk{0x12, 5}; // palette bit, then a whole block (0x02)
		for (int i = 0; i < 48; i++)
			palChunk.push_back(uint8_t(60 - i));
		for (int i = 0; i < 32; i++)
			palChunk.push_back(uint8_t(90 + i));
		palChunk.push_back(0);
		Bytes palCmd;
		put(palCmd, 0xFFF4, 2);
		put(palCmd, 0, 2);
		for (int i = 0; i < 768; i++)
			palCmd.push_back(33);
		Spec s;
		s.frames = {chunk(quarter), chunk(half), chunk(palChunk), cat({palCmd, chunk(wholeBlock(8, 4, 1))})};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		Canvas c(8, 4);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f));
		CHECK(c.at(0, 0) == 7 && c.at(3, 0) == 7 && c.at(4, 0) == 9 && c.at(7, 3) == 21);
		CHECK(decode(imd, c, f));
		CHECK(c.at(0, 0) == 50 && c.at(0, 1) == 50 && c.at(7, 3) == 65 && c.at(7, 2) == 65);
		CHECK(decode(imd, c, f) && f.paletteChanged);
		CHECK(imd.palette()[15] == 60 && imd.palette()[15 + 47] == 13 && imd.palette()[14] == (4 & 63));
		CHECK(c.at(0, 0) == 90 && c.at(7, 1) == 105);
		CHECK(decode(imd, c, f) && f.paletteChanged && imd.palette()[0] == 33 && imd.palette()[767] == 33);
	}

	// Sound: the first slices, a slice per frame, silence for frames without
	// one; breaks that skip data; the rate.
	{
		Spec s;
		s.soundFreq = 8000;
		s.sliceSize = 4;
		s.slicesCount = 2;
		Bytes f0, f1, f2;
		put(f0, 0xFF01, 2);
		for (int i = 0; i < 8; i++)
			f0.push_back(uint8_t(0x80 + i));
		f0 = cat({f0, chunk(wholeBlock(8, 4, 1))});
		put(f1, 0xFF00, 2);
		f1.insert(f1.end(), {0x7F, 0x7E, 0x7D, 0x7C});
		put(f1, 0, 2); // no video
		put(f2, 0xFFF2, 2); // skip 3 bytes, ends the frame
		put(f2, 3, 2);
		f2.insert(f2.end(), {1, 2, 3});
		s.frames = {f0, f1, f2};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		CHECK(imd.hasSound() && imd.frameRate() == 2000.0 && imd.soundSlicesCount() == 2);
		Canvas c(8, 4);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f) && f.soundStart && f.audio.size() == 8 && f.audio[0] == 0 && f.audio[7] == 7);
		CHECK(decode(imd, c, f) && !f.soundStart && f.audio.size() == 4 && f.audio[0] == -1 && f.audio[3] == -4);
		CHECK(f.dirty.empty());
		CHECK(decode(imd, c, f) && f.audio.empty() && imd.endOfVideo()); // a break skips the sound too
	}

	// Frame tables: per-frame rectangles, the standard rectangle, offsets
	// and seeking.
	{
		Spec s;
		s.version = 4;
		s.stdCoords = true;
		s.stdX = 2;
		s.stdY = 1;
		s.stdW = 2;
		s.stdH = 2;
		s.framePos = true;
		s.coords = {{-1, 0, 0, 0}, {4, 0, 5, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}};
		s.frames = {chunk(wholeBlock(8, 4, 1)), chunk(wholeBlock(2, 1, 100)), chunk(wholeBlock(2, 2, 120)),
		            chunk(wholeBlock(2, 2, 140))};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		Canvas c(8, 4);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f));
		CHECK(decode(imd, c, f) && c.at(4, 0) == 100 && c.at(5, 0) == 101 && c.at(6, 0) == 7);
		CHECK(decode(imd, c, f) && c.at(2, 1) == 120 && c.at(3, 2) == 123 && c.at(4, 1) == 13);
		CHECK(decode(imd, c, f) && c.at(2, 1) == 140 && imd.endOfVideo());
		CHECK(imd.seek(1, c.px.data(), 8, 4, 8) && imd.curFrame() == 1);
		CHECK(decode(imd, c, f) && imd.curFrame() == 2 && c.at(2, 1) == 120);
		CHECK(imd.seek(-1, c.px.data(), 8, 4, 8) && imd.curFrame() == -1);
		imd.setXY(1, -1); // moves the rectangles along
		CHECK(imd.x() == 1 && imd.y() == 0);
		Canvas d(10, 4);
		CHECK(decode(imd, d, f) && d.at(1, 0) == 1 && d.at(0, 0) == 0xEE);
		CHECK(decode(imd, d, f) && d.at(5, 0) == 100);
	}

	// A jump: to frame 1's offset; the frame number becomes 0 (as in ScummVM).
	{
		Spec s;
		s.framePos = true;
		Bytes jump;
		put(jump, 0xFFFD, 2);
		put(jump, 1, 2);
		s.frames = {chunk(wholeBlock(8, 4, 1)), chunk(wholeBlock(8, 4, 40)), jump};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		Canvas c(8, 4);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f) && decode(imd, c, f) && c.at(0, 0) == 40);
		c.px.assign(c.px.size(), 0);
		CHECK(decode(imd, c, f) && imd.curFrame() == 0 && c.at(0, 0) == 40);
	}

	// Seeking without a frame table decodes from the start.
	{
		Spec s;
		s.version = 3;
		s.frames = {chunk(wholeBlock(8, 4, 1)), chunk(wholeBlock(8, 4, 40)), chunk(wholeBlock(8, 4, 80))};
		ImdDecoder imd;
		CHECK(imd.load(build(s)));
		Canvas c(8, 4);
		CHECK(imd.seek(1, c.px.data(), 8, 4, 8) && imd.curFrame() == 1 && c.at(0, 0) == 40);
		ImdDecoder::Frame f;
		CHECK(decode(imd, c, f) && c.at(0, 0) == 80);
	}

	// Broken data: not an IMD, truncated frames, packed data that overruns.
	{
		ImdDecoder imd;
		CHECK(!imd.load({1, 0, 4}) && !imd.error().empty());
		CHECK(!imd.load({0, 0, 1}));
		Spec s;
		s.frames = {chunk(wholeBlock(8, 4, 1))};
		Bytes v = build(s);
		v.resize(v.size() - 10);
		CHECK(imd.load(v));
		Canvas c(8, 4);
		ImdDecoder::Frame f;
		CHECK(!decode(imd, c, f) && !imd.error().empty());

		Bytes bad{0x82};
		put(bad, 32, 4);
		bad.insert(bad.end(), {0xFF, 1, 2, 0});
		s.frames = {chunk(bad)};
		CHECK(imd.load(build(s)));
		CHECK(!decode(imd, c, f) && imd.error().find("packed") != std::string::npos);

		Bytes big{0x82};
		put(big, 100000, 4);
		big.insert(big.end(), {0xFF, 1, 2});
		s.frames = {chunk(big)};
		CHECK(imd.load(build(s)));
		CHECK(!decode(imd, c, f) && imd.error().find("larger") != std::string::npos);
	}

	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("imd: all tests passed\n");
	return 0;
}
