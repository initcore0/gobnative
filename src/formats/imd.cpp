// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formats/imd.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace gob {

namespace {

// Frame commands.
constexpr uint16_t kCmdNextSound = 0xFF00;
constexpr uint16_t kCmdStartSound = 0xFF01;
constexpr uint16_t kCmdBreak = 0xFFF0;
constexpr uint16_t kCmdBreakSkip0 = 0xFFF1;
constexpr uint16_t kCmdBreakSkip16 = 0xFFF2;
constexpr uint16_t kCmdBreakSkip32 = 0xFFF3;
constexpr uint16_t kCmdBreakMask = 0xFFF8;
constexpr uint16_t kCmdPalette = 0xFFF4;
constexpr uint16_t kCmdVideoData = 0xFFFC;
constexpr uint16_t kCmdJump = 0xFFFD;

ImdDecoder::Rect clip(ImdDecoder::Rect r, const ImdDecoder::Rect &to) {
	r.left = std::max(r.left, to.left);
	r.top = std::max(r.top, to.top);
	r.right = std::min(r.right, to.right);
	r.bottom = std::min(r.bottom, to.bottom);
	return r;
}

// Bounded reading of a chunk's pixel data.
struct Src {
	const uint8_t *p, *end;
	bool overrun = false;
	uint8_t next() {
		if (p < end)
			return *p++;
		overrun = true;
		return 0;
	}
	// Copies n bytes to `dst` (when not null) and advances.
	void copy(uint8_t *dst, size_t n, size_t take) {
		size_t have = size_t(end - p);
		if (have < n) {
			overrun = true;
			n = have;
		}
		if (dst)
			std::memcpy(dst, p, std::min(n, take));
		p += n;
	}
};

struct Dst {
	uint8_t *pixels;
	int w, h, pitch;
	uint8_t *at(int x, int y) const { return pixels + size_t(y) * size_t(pitch) + size_t(x); }
};

// Each renderer gets the chunk's rectangle `src` (its pixel data's layout)
// and `rect`, the part of it on the destination.

// A whole block: every pixel, row by row.
void renderWhole(const Dst &d, Src &s, const ImdDecoder::Rect &src, const ImdDecoder::Rect &rect) {
	for (int y = src.top; y < src.bottom; y++) {
		bool on = y >= rect.top && y < rect.bottom;
		const uint8_t *row = s.p;
		s.copy(nullptr, size_t(src.width()), 0);
		if (on && !s.overrun && rect.width() > 0)
			std::memcpy(d.at(rect.left, y), row + (rect.left - src.left), size_t(rect.width()));
	}
}

// A quarter-wide whole block: each byte fills 4 pixels.
void renderWhole4X(const Dst &d, Src &s, const ImdDecoder::Rect &src, const ImdDecoder::Rect &rect) {
	for (int y = src.top; y < src.bottom; y++) {
		const uint8_t *row = s.p;
		s.copy(nullptr, size_t(src.width() / 4), 0);
		if (y < rect.top || y >= rect.bottom || s.overrun)
			continue;
		for (int x = rect.left; x < rect.right; x++)
			*d.at(x, y) = row[(x - src.left) / 4];
	}
}

// A half-high whole block: each row is drawn twice.
void renderWhole2Y(const Dst &d, Src &s, const ImdDecoder::Rect &src, const ImdDecoder::Rect &rect) {
	for (int y = src.top; y < src.bottom; y += 2) {
		const uint8_t *row = s.p;
		s.copy(nullptr, size_t(src.width()), 0);
		if (s.overrun)
			return;
		for (int dy = 0; dy < 2; dy++)
			if (y + dy >= rect.top && y + dy < rect.bottom && rect.width() > 0)
				std::memcpy(d.at(rect.left, y + dy), row + (rect.left - src.left), size_t(rect.width()));
	}
}

// A sparse block: per row, runs of pixels (count byte with bit 7 set: count
// & 0x7F + 1 literal pixels) and holes (count + 1 pixels left as they are).
// `rows` is 2 for the half-high variant.
void renderSparse(const Dst &d, Src &s, const ImdDecoder::Rect &src, const ImdDecoder::Rect &rect, int rows) {
	for (int y = src.top; y < src.bottom; y += rows) {
		int x = 0;
		while (x < src.width()) {
			int count = s.next();
			if (s.overrun)
				return;
			if (count & 0x80) {
				count = std::min((count & 0x7F) + 1, src.width() - x);
				const uint8_t *run = s.p;
				s.copy(nullptr, size_t(count), 0);
				if (s.overrun)
					return;
				for (int i = 0; i < count; i++) {
					int px = src.left + x + i;
					if (px < rect.left || px >= rect.right)
						continue;
					for (int dy = 0; dy < rows; dy++)
						if (y + dy >= rect.top && y + dy < rect.bottom)
							*d.at(px, y + dy) = run[i];
				}
				x += count;
			} else {
				x += count + 1;
			}
		}
	}
}

} // namespace

uint8_t ImdDecoder::u8() {
	if (_pos < _data.size())
		return _data[_pos++];
	fail("data ends early");
	return 0;
}

uint16_t ImdDecoder::u16() {
	uint16_t lo = u8();
	return uint16_t(lo | (u8() << 8));
}

uint32_t ImdDecoder::u32() {
	uint32_t lo = u16();
	return lo | (uint32_t(u16()) << 16);
}

void ImdDecoder::skip(size_t n) {
	if (n > _data.size() - std::min(_pos, _data.size())) {
		fail("data ends early");
		_pos = _data.size();
		return;
	}
	_pos += n;
}

void ImdDecoder::fail(const std::string &what) {
	if (_error.empty())
		_error = what + " (frame " + std::to_string(_curFrame) + ", offset " + std::to_string(_pos) + ")";
}

double ImdDecoder::frameRate() const {
	return hasSound() && _soundSliceSize > 0 ? double(_soundFreq) / _soundSliceSize : 12.0;
}

bool ImdDecoder::load(std::vector<uint8_t> data) {
	*this = ImdDecoder();
	_data = std::move(data);

	uint16_t handle = u16();
	_version = u8();
	if (handle != 0 || _version < 2 || !_error.empty()) {
		_error = "not an IMD (handle " + std::to_string(handle) + ", version " + std::to_string(_version) + ")";
		return false;
	}
	_features = u8();
	_frameCount = u16();
	_defaultX = s16();
	_defaultY = s16();
	_width = s16();
	_height = s16();
	_flags = u16();
	_firstFramePos = u16();
	_x = _defaultX;
	_y = _defaultY;
	for (uint8_t &c : _palette)
		c = u8() & 0x3F;

	// Standard coordinates (version 3 on).
	if (_version >= 3) {
		uint16_t count = u16();
		if (count > 1) {
			_error = "more than one standard rectangle";
			return false;
		}
		if (count == 1) {
			_stdX = s16();
			_stdY = s16();
			_stdWidth = s16();
			_stdHeight = s16();
			_features |= kStdCoords;
		}
	}

	// Frame tables (offsets from version 4 on, rectangles if flagged).
	uint32_t framePosPos = 0, frameCoordsPos = 0;
	if (_version >= 4) {
		framePosPos = u32();
		if (framePosPos != 0)
			_features |= kFramePos;
	}
	if (_features & kFrameCoords)
		frameCoordsPos = u32();

	if (_features & kSound) {
		_soundFreq = std::abs(s16());
		_soundSliceSize = s16();
		_soundSlicesCount = s16();
		if (_soundSlicesCount < 0)
			_soundSlicesCount = -_soundSlicesCount - 1;
		if (_soundSlicesCount > 40 || _soundSliceSize <= 0) {
			_error = "bad sound properties";
			return false;
		}
	}

	size_t suggested = 0;
	if (_features & kDataSize) {
		uint32_t size1 = u16(), size2;
		if (size1 == 0) {
			size1 = u32();
			size2 = u32();
		} else {
			size2 = u16();
		}
		suggested = std::max(size1, size2);
	}
	_bufferSize = std::max(size_t(std::max(_width, 0)) * size_t(std::max(_height, 0)) + 1000, suggested);

	if (framePosPos) {
		_pos = framePosPos;
		_framePos.resize(size_t(_frameCount));
		for (uint32_t &p : _framePos)
			p = u32();
	}
	if (_features & kFrameCoords) {
		_pos = frameCoordsPos;
		_frameCoords.resize(size_t(_frameCount));
		for (Rect &r : _frameCoords) {
			r.left = s16();
			r.top = s16();
			r.right = s16();
			r.bottom = s16();
		}
	}
	if (!_error.empty())
		return false;
	_pos = _firstFramePos;
	return true;
}

void ImdDecoder::setXY(int x, int y) {
	if (_stdX != -1) {
		if (x != -1)
			_stdX = _stdX - _x + x;
		if (y != -1)
			_stdY = _stdY - _y + y;
	}
	for (Rect &r : _frameCoords) {
		if (r.left == -1)
			continue;
		if (x != -1) {
			r.left = r.left - _x + x;
			r.right = r.right - _x + x;
		}
		if (y != -1) {
			r.top = r.top - _y + y;
			r.bottom = r.bottom - _y + y;
		}
	}
	if (x != -1)
		_x = x;
	if (y != -1)
		_y = y;
}

bool ImdDecoder::seek(int frame, uint8_t *dst, int dstW, int dstH, int pitch) {
	if (frame < -1 || frame >= _frameCount)
		return false;
	if (frame == _curFrame)
		return true;
	uint32_t pos;
	if (frame == -1) {
		pos = _firstFramePos;
	} else if (frame == 0) { // the first frame's size precedes it
		_pos = _firstFramePos;
		pos = _firstFramePos + u16() + 4;
	} else if (!_framePos.empty()) {
		pos = _framePos[size_t(frame) + 1 < _framePos.size() ? size_t(frame) + 1 : size_t(frame)];
	} else { // decode up to it
		_curFrame = -1;
		_pos = _firstFramePos;
		Frame f;
		while (_curFrame < frame)
			if (!decodeFrame(dst, dstW, dstH, pitch, f))
				return false;
		return true;
	}
	_pos = pos;
	_curFrame = frame;
	return true;
}

// The rectangle frame `frame` draws: the whole video for the first frame,
// else its own rectangle, the standard one, or the whole video.
ImdDecoder::Rect ImdDecoder::frameRect(int frame) const {
	if (frame > 0 && size_t(frame) < _frameCoords.size() && _frameCoords[size_t(frame)].left != -1) {
		const Rect &c = _frameCoords[size_t(frame)];
		return {c.left, c.top, c.right + 1, c.bottom + 1};
	}
	if (frame > 0 && _stdX != -1)
		return {_stdX, _stdY, _stdX + _stdWidth, _stdY + _stdHeight};
	return {_x, _y, _x + _width, _y + _height};
}

void ImdDecoder::soundSlices(int count, bool keep, Frame &out) {
	size_t n = size_t(_soundSliceSize) * size_t(count);
	if (!keep) {
		skip(n);
		return;
	}
	size_t have = std::min(n, _data.size() - std::min(_pos, _data.size()));
	for (size_t i = 0; i < have; i++)
		out.audio.push_back(int8_t(_data[_pos + i] ^ 0x80));
	if (have < n)
		fail("sound data ends early");
	_pos += have;
}

bool ImdDecoder::decodeFrame(uint8_t *dst, int dstW, int dstH, int pitch, Frame &out) {
	out = Frame();
	if (endOfVideo() || !_error.empty())
		return false;
	_curFrame++;

	bool hasNextCmd = false;
	do {
		uint16_t cmd = u16();

		if ((cmd & kCmdBreakMask) == kCmdBreak) {
			if (cmd == kCmdBreak) {
				skip(2);
				cmd = u16();
			}
			// A skip ends the frame's commands (the original's `continue` in
			// a do-while that only repeats after a jump).
			if (cmd == kCmdBreakSkip0) {
				continue;
			} else if (cmd == kCmdBreakSkip16) {
				skip(u16());
				continue;
			} else if (cmd == kCmdBreakSkip32) {
				skip(u32());
				continue;
			}
		}

		// Sound: a slice, the initial slices, or a silent slice.
		if (cmd == kCmdNextSound) {
			soundSlices(1, !hasNextCmd && hasSound(), out);
			cmd = u16();
		} else if (cmd == kCmdStartSound) {
			bool keep = !hasNextCmd && hasSound();
			soundSlices(_soundSlicesCount, keep, out);
			out.soundStart |= keep;
			cmd = u16();
		} else if (!hasNextCmd && hasSound()) {
			out.audio.insert(out.audio.end(), size_t(_soundSliceSize), int8_t(0));
		}

		if (cmd == kCmdPalette) {
			skip(2);
			for (uint8_t &c : _palette)
				c = u8() & 0x3F;
			out.paletteChanged = true;
			cmd = u16();
		}

		hasNextCmd = false;
		if (cmd == kCmdJump) {
			// As ScummVM has it: the frame number becomes the target's
			// minus one while the target's commands run (no Goblins 3
			// video jumps).
			int16_t frame = s16();
			if (!_framePos.empty()) {
				if (frame < 0 || size_t(frame) >= _framePos.size()) {
					fail("jump to frame " + std::to_string(frame) + " out of range");
					return false;
				}
				_curFrame = frame - 1;
				_pos = _framePos[size_t(frame)];
				hasNextCmd = true;
			}
		} else if (cmd != 0) {
			size_t len = cmd == kCmdVideoData ? size_t(u32()) + 2 : size_t(cmd) + 2;
			if (len > _bufferSize) {
				fail("video chunk of " + std::to_string(len) + " bytes is larger than the frame buffer");
				return false;
			}
			if (len > _data.size() - std::min(_pos, _data.size())) {
				fail("video chunk overruns the data");
				return false;
			}
			if (_skipVideo) {
				_pos += len;
				continue; // ends the frame unless after a jump, as a chunk does
			}
			_chunk.assign(_data.begin() + long(_pos), _data.begin() + long(_pos + len));
			_pos += len;
			Rect rect = frameRect(_curFrame);
			if (!renderChunk(rect, dst, dstW, dstH, pitch, out))
				return false;
		}
	} while (hasNextCmd);

	return _error.empty();
}

std::vector<int8_t> ImdDecoder::soundTrack(int *startFrame) const {
	std::vector<int8_t> track;
	if (startFrame)
		*startFrame = -1;
	if (!hasSound())
		return track;
	ImdDecoder c;
	c._data = _data; // the header fields and tables, without the frame state
	c._features = _features;
	c._frameCount = _frameCount;
	c._framePos = _framePos;
	c._soundSliceSize = _soundSliceSize;
	c._soundSlicesCount = _soundSlicesCount;
	c._bufferSize = _bufferSize;
	c._skipVideo = true;
	c._pos = _firstFramePos;
	Frame f;
	for (int n = 0; n < 2 * _frameCount && c.decodeFrame(nullptr, 0, 0, 0, f); n++) { // (jumps can loop)
		if (f.soundStart && startFrame && *startFrame < 0)
			*startFrame = c._curFrame;
		track.insert(track.end(), f.audio.begin(), f.audio.end());
	}
	return track;
}

bool ImdDecoder::renderChunk(Rect rect, uint8_t *dst, int dstW, int dstH, int pitch, Frame &out) {
	if (rect.empty())
		return true;
	rect = clip(rect, {_x, _y, _x + _width, _y + _height});
	if (rect.empty())
		return true;

	const uint8_t *data = _chunk.data();
	size_t size = _chunk.size() - 1;
	uint8_t type = *data++;

	if (type & 0x10) { // 16 palette entries from an index
		if (size < 49) {
			fail("palette chunk too short");
			return false;
		}
		int index = data[0];
		int count = std::min(255 - index, 16);
		std::memcpy(&_palette[size_t(index) * 3], data + 1, size_t(count) * 3);
		for (int i = 0; i < count * 3; i++)
			_palette[size_t(index) * 3 + size_t(i)] &= 0x3F;
		data += 49;
		size -= 49;
		type ^= 0x10;
		out.paletteChanged = true;
	}

	if (type & 0x80) { // LZ77-packed
		type &= 0x7F;
		size_t n = deLz77(_unpacked, data, size);
		if (!_error.empty())
			return false;
		data = _unpacked.data();
		size = n;
	}

	const Rect onDst = clip(rect, {0, 0, dstW, dstH});
	Dst d{dst, dstW, dstH, pitch};
	Src s{data, data + size};
	if (type == 0x01)
		renderSparse(d, s, rect, onDst, 1);
	else if (type == 0x02)
		renderWhole(d, s, rect, onDst);
	else if (type == 0x42)
		renderWhole4X(d, s, rect, onDst);
	else if ((type & 0x0F) == 0x02)
		renderWhole2Y(d, s, rect, onDst);
	else
		renderSparse(d, s, rect, onDst, 2);
	if (s.overrun) {
		fail("pixel data of block type " + std::to_string(type) + " ends early");
		return false;
	}
	out.dirty.push_back(rect);
	return true;
}

// LZ77 with a 4 KiB window: u32 unpacked size, then (optionally the
// "extended" marker 0x1234 0x5678) groups of 8 items led by a flag byte, bit
// set = a literal byte, clear = a u16 reference: offset (12 bits, low byte
// first then the high nibble of the second byte) and length (the other
// nibble + 3; the standard variant takes a length byte + 18 when it reads 0,
// the extended one when it reads 18). The window starts filled with spaces
// and its write position at 4078 (standard) or 273 (extended).
size_t ImdDecoder::deLz77(std::vector<uint8_t> &dest, const uint8_t *src, size_t srcSize) {
	if (srcSize < 4) {
		fail("packed chunk too short");
		return 0;
	}
	uint32_t length = uint32_t(src[0] | (src[1] << 8) | (src[2] << 16) | (uint32_t(src[3]) << 24));
	if (length > _bufferSize) {
		fail("unpacked size " + std::to_string(length) + " is larger than the frame buffer");
		return 0;
	}
	src += 4;
	srcSize -= 4;
	const uint8_t *end = src + srcSize;

	uint16_t bufPos1;
	bool extended = srcSize >= 4 && src[0] == 0x34 && src[1] == 0x12 && src[2] == 0x78 && src[3] == 0x56;
	if (extended) {
		src += 4;
		bufPos1 = 273;
	} else {
		bufPos1 = 4078;
	}
	std::array<uint8_t, 4096 + 274> buf;
	std::memset(buf.data(), ' ', bufPos1);

	dest.assign(length, 0);
	size_t out = 0;
	uint8_t chunkCount = 1, bits = 0;
	auto overrun = [&]() {
		fail("packed data ends early");
		return out;
	};
	while (out < length) {
		if (--chunkCount == 0) {
			if (src >= end)
				return overrun();
			chunkCount = 8;
			bits = *src++;
		}
		if (bits & 1) {
			bits >>= 1;
			if (src >= end)
				return overrun();
			buf[bufPos1] = *src;
			dest[out++] = *src++;
			bufPos1 = (bufPos1 + 1) % 4096;
			continue;
		}
		bits >>= 1;
		if (end - src < 2)
			return overrun();
		uint16_t tmp = uint16_t(src[0] | (src[1] << 8));
		src += 2;
		uint16_t chunkLength = uint16_t(((tmp & 0xF00) >> 8) + 3);
		if ((extended && (chunkLength & 0xFF) == 0x12) || (!extended && chunkLength == 0)) {
			if (src >= end)
				return overrun();
			chunkLength = uint16_t(*src++ + 0x12);
		}
		uint16_t bufPos2 = uint16_t((tmp & 0xFF) + ((tmp >> 4) & 0x0F00));
		for (int i = 0; i < chunkLength && out < length; i++) {
			uint8_t c = buf[bufPos2];
			dest[out++] = c;
			buf[bufPos1] = c;
			bufPos1 = (bufPos1 + 1) % 4096;
			bufPos2 = (bufPos2 + 1) % 4096;
		}
	}
	return out;
}

} // namespace gob
