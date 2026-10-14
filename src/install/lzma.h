// gob2native - GOG installer import
// SPDX-License-Identifier: GPL-3.0-or-later
//
// LZMA and LZMA2 decoders (raw streams, no .xz/.lzma container), written
// for the Inno Setup installers GOG ships the games in (install/inno.h),
// after the algorithm as Igor Pavlov describes it in the LZMA SDK's
// specification (lzma-specification.txt, public domain).
//
// The decoder pulls compressed bytes from a ByteSource and produces as many
// output bytes as asked for, so a caller can decode a stream in small
// steps (a progress bar, one step per frame) and stop wherever it likes:
// the stream's end needs no end marker or known size. Plain C++ without
// threads or platform calls (it also builds for the web).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gob {

// Something to read bytes from: returns how many were read into `dst`
// (fewer than `size` only at the end of the data, or on an error).
class ByteSource {
public:
	virtual ~ByteSource() = default;
	virtual size_t read(uint8_t *dst, size_t size) = 0;
};

class LzmaDecoder {
public:
	// A raw LZMA stream: `props` is the lc/lp/pb byte ((pb * 5 + lp) * 9 + lc).
	// `maxOutput`, when known, caps the dictionary's memory (a dictionary
	// larger than everything that will be decoded is never needed).
	bool initLzma1(ByteSource *in, uint8_t props, uint32_t dictSize, uint64_t maxOutput = UINT64_MAX);
	// A raw LZMA2 stream: `dictProp` is the dictionary size byte (0-40).
	bool initLzma2(ByteSource *in, uint8_t dictProp, uint64_t maxOutput = UINT64_MAX);

	// Decodes up to `size` bytes into `out` (null: decode and drop them).
	// Fewer bytes come back only at the end of the stream (finished()) or
	// on an error (failed(), error()).
	size_t decode(uint8_t *out, size_t size);

	bool finished() const { return _finished; }
	bool failed() const { return !_error.empty(); }
	const std::string &error() const { return _error; }
	uint64_t produced() const { return _totalPos; }

private:
	using Prob = uint16_t;
	struct LenDecoder {
		Prob choice, choice2;
		Prob low[16][8], mid[16][8], high[256];
	};

	bool setProps(uint8_t props);
	bool allocateDictionary(uint32_t dictSize, uint64_t maxOutput);
	void resetState();
	void resetDictionary();
	bool initRangeCoder();

	uint8_t inputByte();
	void normalize() {
		if (_range < (1u << 24)) {
			_range <<= 8;
			_code = (_code << 8) | inputByte();
		}
	}
	unsigned bit(Prob &p);
	unsigned directBits(unsigned count);
	unsigned bitTree(Prob *probs, unsigned bits);
	unsigned reverseBitTree(Prob *probs, unsigned bits);
	unsigned decodeLength(LenDecoder &d, unsigned posState);
	uint32_t decodeDistance(unsigned len);

	uint8_t dictByte(uint32_t distance) const { // distance >= 1
		size_t i = _dictPos >= distance ? _dictPos - distance : _dictPos + _dict.size() - distance;
		return _dict[i];
	}
	void put(uint8_t b, uint8_t *&out) {
		_dict[_dictPos] = b;
		if (++_dictPos == _dict.size()) {
			_dictPos = 0;
			_dictFull = true;
		}
		_totalPos++;
		if (out)
			*out++ = b;
	}

	// LZMA (one LZMA2 chunk): decodes until `size` bytes are out or the
	// chunk/stream ends.
	size_t decodeLzma(uint8_t *out, size_t size);
	bool lzma2NextChunk();
	bool fail(const std::string &message);

	ByteSource *_in = nullptr;
	uint8_t _inBuf[1 << 16];
	size_t _inPos = 0, _inLen = 0;
	uint64_t _inCount = 0; // bytes taken from _in
	bool _inEnded = false;

	bool _lzma2 = false;
	bool _finished = false;
	std::string _error;

	unsigned _lc = 0, _lp = 0, _pb = 0;
	std::vector<Prob> _literal;
	Prob _isMatch[12][16], _isRep[12], _isRepG0[12], _isRepG1[12], _isRepG2[12], _isRep0Long[12][16];
	Prob _posSlot[4][64], _posDecoders[115], _align[16];
	LenDecoder _len, _repLen;
	uint32_t _rep[4] = {0, 0, 0, 0};
	unsigned _state = 0;
	uint32_t _pending = 0; // bytes of the current match still to copy

	uint32_t _range = 0, _code = 0;

	std::vector<uint8_t> _dict;
	size_t _dictPos = 0;
	bool _dictFull = false;
	uint64_t _totalPos = 0; // bytes decoded
	uint64_t _pos = 0;      // position since the last dictionary reset (for the position states)

	// LZMA2
	bool _needDictReset = true, _needProps = true;
	bool _chunkLzma = false;     // the current chunk is LZMA-compressed (else stored)
	uint32_t _chunkLeft = 0;     // its output bytes still to come
	uint64_t _chunkInEnd = 0;    // _inCount at its end (LZMA chunks)
};

} // namespace gob
