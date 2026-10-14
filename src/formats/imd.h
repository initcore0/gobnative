// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IMD video (Coktel Vision, docs/imd.md): a header with a palette, optional
// coordinate and frame tables, and frames made of commands: sound slices
// (8-bit unsigned PCM), palettes, jumps and video chunks. A video chunk
// draws one rectangle, sparse or whole, plain or LZ77-packed. Frames are
// drawn in place into an 8-bit surface (the screen or a sprite), at the
// video's position. Pure: no engine dependencies, so tools and tests can use
// it. Format knowledge from ScummVM's video/coktel_decoder.cpp (IMDDecoder).

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gob {

class ImdDecoder {
public:
	// Header feature bits.
	enum Features : uint16_t {
		kDataSize = 0x0020,    // suggests the size of the frame buffers
		kSound = 0x0040,       // has sound
		kFrameCoords = 0x0080, // has a table of per-frame rectangles
		kStdCoords = 0x0100,   // has a standard rectangle (version 3+)
		kFramePos = 0x0200,    // has a table of frame offsets (version 4+)
	};

	// A rectangle on the destination surface, right and bottom exclusive.
	struct Rect {
		int left = 0, top = 0, right = 0, bottom = 0;
		int width() const { return right - left; }
		int height() const { return bottom - top; }
		bool empty() const { return right <= left || bottom <= top; }
	};

	// What decoding a frame produced besides the pixels.
	struct Frame {
		std::vector<Rect> dirty;     // rectangles drawn (clipped to the video)
		bool paletteChanged = false; // palette() changed
		bool soundStart = false;     // the first sound command: playback starts here
		std::vector<int8_t> audio;   // this frame's sound (signed 8-bit mono), possibly silence
	};

	// Parses the header and the tables. False (and error()) for data that is
	// not an IMD of version 2 or later.
	bool load(std::vector<uint8_t> data);

	int version() const { return _version; }
	uint16_t features() const { return _features; }
	int frameCount() const { return _frameCount; }
	int width() const { return _width; }
	int height() const { return _height; }
	int defaultX() const { return _defaultX; }
	int defaultY() const { return _defaultY; }
	uint16_t flags() const { return _flags; }
	bool hasSound() const { return _features & kSound; }
	int soundFreq() const { return _soundFreq; }
	int soundSliceSize() const { return _soundSliceSize; }
	int soundSlicesCount() const { return _soundSlicesCount; }
	// Frames per second: the sound rate over the slice size, else 12.
	double frameRate() const;

	// The palette (6-bit VGA values), as of the last decoded frame.
	const std::array<uint8_t, 768> &palette() const { return _palette; }

	// Where the video is drawn; -1 keeps a coordinate. Moves the standard
	// and per-frame rectangles along.
	void setXY(int x, int y);
	int x() const { return _x; }
	int y() const { return _y; }

	// The frame decoded last: -1 before the first.
	int curFrame() const { return _curFrame; }
	bool endOfVideo() const { return _curFrame >= _frameCount - 1; }
	// Positions the video so that the next frame decoded is `frame` + 1
	// (-1: the start). Without a frame offset table, a frame other than -1
	// or 0 is reached by decoding from the start into `dst` (as decodeFrame;
	// the player does that only for videos without sound or whose sound is
	// cut, as the original).
	bool seek(int frame, uint8_t *dst, int dstW, int dstH, int pitch);

	// The whole sound track as playback queues it frame by frame (the first
	// slices, a slice per frame, silence for frames without one), from the
	// first frame; playback starts at frame `*startFrame` (-1: never).
	std::vector<int8_t> soundTrack(int *startFrame = nullptr) const;

	// Decodes the next frame into the 8-bit surface `dst` (`dstW` x `dstH`,
	// `pitch` bytes per row), clipped to it. False at the end of the video
	// or when the frame's data is broken (see error()).
	bool decodeFrame(uint8_t *dst, int dstW, int dstH, int pitch, Frame &out);

	// The first problem met: data running out, a chunk larger than its
	// buffer, packed data that overruns. Empty while all is well.
	const std::string &error() const { return _error; }

private:
	uint8_t u8();
	uint16_t u16();
	int16_t s16() { return int16_t(u16()); }
	uint32_t u32();
	void skip(size_t n);
	void fail(const std::string &what);

	Rect frameRect(int frame) const;
	bool renderChunk(Rect rect, uint8_t *dst, int dstW, int dstH, int pitch, Frame &out);
	size_t deLz77(std::vector<uint8_t> &dest, const uint8_t *src, size_t srcSize);
	void soundSlices(int count, bool keep, Frame &out);

	std::vector<uint8_t> _data;
	size_t _pos = 0;
	std::string _error;

	int _version = 0;
	uint16_t _features = 0;
	int _frameCount = 0;
	int _defaultX = 0, _defaultY = 0, _width = 0, _height = 0;
	uint16_t _flags = 0;
	uint32_t _firstFramePos = 0;
	std::array<uint8_t, 768> _palette{};
	int _x = 0, _y = 0;
	int _stdX = -1, _stdY = -1, _stdWidth = -1, _stdHeight = -1;
	std::vector<uint32_t> _framePos;
	std::vector<Rect> _frameCoords; // right/bottom inclusive as stored; left -1: none
	size_t _bufferSize = 0;
	int _soundFreq = 0, _soundSliceSize = 0, _soundSlicesCount = 0;
	int _curFrame = -1;
	bool _skipVideo = false; // soundTrack(): commands only
	std::vector<uint8_t> _chunk, _unpacked;
};

} // namespace gob
