// gob2native - GOG installer import
// SPDX-License-Identifier: GPL-3.0-or-later

#include "install/lzma.h"

#include <algorithm>
#include <cstring>

namespace gob {

namespace {

constexpr uint16_t kProbInit = 1024; // half of 2^11
constexpr unsigned kEndPosModelIndex = 14;
constexpr unsigned kMatchMinLen = 2;

template <typename T, size_t N>
void fillProbs(T (&a)[N]) {
	for (T &p : a)
		p = kProbInit;
}
template <typename T, size_t N, size_t M>
void fillProbs(T (&a)[N][M]) {
	for (auto &row : a)
		fillProbs(row);
}

} // namespace

bool LzmaDecoder::fail(const std::string &message) {
	if (_error.empty())
		_error = message;
	return false;
}

bool LzmaDecoder::setProps(uint8_t props) {
	if (props >= 9 * 5 * 5)
		return fail("bad LZMA properties");
	_lc = props % 9;
	props /= 9;
	_lp = props % 5;
	_pb = props / 5;
	_literal.assign(size_t(0x300) << (_lc + _lp), kProbInit);
	return true;
}

bool LzmaDecoder::allocateDictionary(uint32_t dictSize, uint64_t maxOutput) {
	uint64_t size = std::max<uint64_t>(dictSize, 4096);
	size = std::min<uint64_t>(size, std::max<uint64_t>(maxOutput, 4096));
	try {
		_dict.assign(size_t(size), 0);
	} catch (const std::bad_alloc &) {
		return fail("not enough memory for the LZMA dictionary (" + std::to_string(size >> 20) + " MB)");
	}
	resetDictionary();
	return true;
}

void LzmaDecoder::resetDictionary() {
	_dictPos = 0;
	_dictFull = false;
	_pos = 0;
}

void LzmaDecoder::resetState() {
	std::fill(_literal.begin(), _literal.end(), kProbInit);
	fillProbs(_isMatch);
	fillProbs(_isRep);
	fillProbs(_isRepG0);
	fillProbs(_isRepG1);
	fillProbs(_isRepG2);
	fillProbs(_isRep0Long);
	fillProbs(_posSlot);
	fillProbs(_posDecoders);
	fillProbs(_align);
	for (LenDecoder *d : {&_len, &_repLen}) {
		d->choice = d->choice2 = kProbInit;
		fillProbs(d->low);
		fillProbs(d->mid);
		fillProbs(d->high);
	}
	std::fill(std::begin(_rep), std::end(_rep), 0);
	_state = 0;
	_pending = 0;
}

uint8_t LzmaDecoder::inputByte() {
	if (_inPos == _inLen) {
		_inPos = 0;
		_inLen = _inEnded || !_in ? 0 : _in->read(_inBuf, sizeof(_inBuf));
		if (_inLen == 0) {
			_inEnded = true;
			fail("the compressed data ends too early");
			return 0;
		}
	}
	_inCount++;
	return _inBuf[_inPos++];
}

bool LzmaDecoder::initRangeCoder() {
	_range = 0xFFFFFFFF;
	_code = 0;
	if (inputByte() != 0)
		return fail("bad LZMA stream (first byte not 0)");
	for (int i = 0; i < 4; i++)
		_code = (_code << 8) | inputByte();
	if (_code == _range)
		return fail("bad LZMA stream");
	return !failed();
}

bool LzmaDecoder::initLzma1(ByteSource *in, uint8_t props, uint32_t dictSize, uint64_t maxOutput) {
	_in = in;
	_lzma2 = false;
	_finished = false;
	_error.clear();
	_totalPos = 0;
	if (!setProps(props) || !allocateDictionary(dictSize, maxOutput))
		return false;
	resetState();
	return initRangeCoder();
}

bool LzmaDecoder::initLzma2(ByteSource *in, uint8_t dictProp, uint64_t maxOutput) {
	_in = in;
	_lzma2 = true;
	_finished = false;
	_error.clear();
	_totalPos = 0;
	if (dictProp > 40)
		return fail("bad LZMA2 dictionary size");
	const uint32_t dictSize = dictProp == 40 ? 0xFFFFFFFF : (2u | (dictProp & 1u)) << (dictProp / 2 + 11);
	if (!allocateDictionary(dictSize, maxOutput))
		return false;
	_needDictReset = _needProps = true;
	_chunkLeft = 0;
	return true;
}

unsigned LzmaDecoder::bit(Prob &p) {
	const uint32_t bound = (_range >> 11) * p;
	unsigned b;
	if (_code < bound) {
		_range = bound;
		p = Prob(p + ((2048 - p) >> 5));
		b = 0;
	} else {
		_range -= bound;
		_code -= bound;
		p = Prob(p - (p >> 5));
		b = 1;
	}
	normalize();
	return b;
}

unsigned LzmaDecoder::directBits(unsigned count) {
	unsigned result = 0;
	while (count--) {
		_range >>= 1;
		_code -= _range;
		const uint32_t t = 0 - (_code >> 31); // all ones when the subtraction went below zero
		_code += _range & t;
		result = (result << 1) + (t + 1);
		normalize();
	}
	return result;
}

unsigned LzmaDecoder::bitTree(Prob *probs, unsigned bits) {
	unsigned m = 1;
	for (unsigned i = 0; i < bits; i++)
		m = (m << 1) + bit(probs[m]);
	return m - (1u << bits);
}

unsigned LzmaDecoder::reverseBitTree(Prob *probs, unsigned bits) {
	unsigned m = 1, symbol = 0;
	for (unsigned i = 0; i < bits; i++) {
		const unsigned b = bit(probs[m]);
		m = (m << 1) + b;
		symbol |= b << i;
	}
	return symbol;
}

unsigned LzmaDecoder::decodeLength(LenDecoder &d, unsigned posState) {
	if (!bit(d.choice))
		return bitTree(d.low[posState], 3);
	if (!bit(d.choice2))
		return 8 + bitTree(d.mid[posState], 3);
	return 16 + bitTree(d.high, 8);
}

uint32_t LzmaDecoder::decodeDistance(unsigned len) {
	const unsigned lenState = std::min(len, 3u);
	const unsigned posSlot = bitTree(_posSlot[lenState], 6);
	if (posSlot < 4)
		return posSlot;
	const unsigned numDirect = (posSlot >> 1) - 1;
	uint32_t dist = (2u | (posSlot & 1u)) << numDirect;
	if (posSlot < kEndPosModelIndex)
		dist += reverseBitTree(_posDecoders + dist - posSlot, numDirect);
	else {
		dist += directBits(numDirect - 4) << 4;
		dist += reverseBitTree(_align, 4);
	}
	return dist;
}

size_t LzmaDecoder::decodeLzma(uint8_t *out, size_t size) {
	size_t done = 0;
	const unsigned pbMask = (1u << _pb) - 1, lpMask = (1u << _lp) - 1;
	while (done < size && !failed()) {
		if (_pending) {
			const uint32_t n = uint32_t(std::min<size_t>(_pending, size - done));
			const uint32_t distance = _rep[0] + 1;
			for (uint32_t i = 0; i < n; i++)
				put(dictByte(distance), out);
			_pending -= n;
			done += n;
			if (_lzma2)
				_chunkLeft -= n;
			continue;
		}
		if (_lzma2 && _chunkLeft == 0)
			break;
		const unsigned posState = unsigned(_pos) & pbMask;
		if (!bit(_isMatch[_state][posState])) {
			// A literal, coded against the byte at rep0 after a match.
			const unsigned prev = (_dictPos > 0 || _dictFull) ? dictByte(1) : 0;
			Prob *probs = &_literal[size_t(0x300) * (((unsigned(_pos) & lpMask) << _lc) + (prev >> (8 - _lc)))];
			unsigned symbol = 1;
			if (_state >= 7) {
				unsigned matchByte = dictByte(_rep[0] + 1);
				do {
					const unsigned matchBit = (matchByte >> 7) & 1;
					matchByte <<= 1;
					const unsigned b = bit(probs[((1 + matchBit) << 8) + symbol]);
					symbol = (symbol << 1) | b;
					if (matchBit != b)
						break;
				} while (symbol < 0x100);
			}
			while (symbol < 0x100)
				symbol = (symbol << 1) | bit(probs[symbol]);
			put(uint8_t(symbol - 0x100), out);
			_pos++;
			_state = _state < 4 ? 0 : _state < 10 ? _state - 3 : _state - 6;
			done++;
			if (_lzma2)
				_chunkLeft--;
			continue;
		}
		unsigned len;
		if (bit(_isRep[_state])) {
			if (_dictPos == 0 && !_dictFull) {
				fail("bad LZMA stream (repeat before any data)");
				break;
			}
			if (!bit(_isRepG0[_state])) {
				if (!bit(_isRep0Long[_state][posState])) {
					// "short rep": one byte from rep0
					_state = _state < 7 ? 9 : 11;
					put(dictByte(_rep[0] + 1), out);
					_pos++;
					done++;
					if (_lzma2)
						_chunkLeft--;
					continue;
				}
			} else {
				uint32_t dist;
				if (!bit(_isRepG1[_state]))
					dist = _rep[1];
				else {
					if (!bit(_isRepG2[_state]))
						dist = _rep[2];
					else {
						dist = _rep[3];
						_rep[3] = _rep[2];
					}
					_rep[2] = _rep[1];
				}
				_rep[1] = _rep[0];
				_rep[0] = dist;
			}
			len = decodeLength(_repLen, posState);
			_state = _state < 7 ? 8 : 11;
		} else {
			_rep[3] = _rep[2];
			_rep[2] = _rep[1];
			_rep[1] = _rep[0];
			len = decodeLength(_len, posState);
			_state = _state < 7 ? 7 : 10;
			_rep[0] = decodeDistance(len);
			if (_rep[0] == 0xFFFFFFFF) {
				// The end marker.
				if (_lzma2)
					fail("bad LZMA2 stream (end marker in a chunk)");
				else
					_finished = true;
				break;
			}
			if (_rep[0] >= (_dictFull ? _dict.size() : _dictPos)) {
				fail("bad LZMA stream (distance beyond the data)");
				break;
			}
		}
		_pending = len + kMatchMinLen;
		_pos += _pending; // the position states only need the low bits
		if (_lzma2 && _pending > _chunkLeft) {
			fail("bad LZMA2 stream (match crosses a chunk)");
			break;
		}
	}
	return done;
}

bool LzmaDecoder::lzma2NextChunk() {
	// What is left of an LZMA chunk's input after its last symbol (the range
	// coder's flush) is skipped; reading beyond the chunk is an error.
	if (_chunkLzma && _inCount > _chunkInEnd)
		return fail("bad LZMA2 stream (chunk overrun)");
	while (_chunkLzma && _inCount < _chunkInEnd && !failed())
		inputByte();
	_chunkLzma = false;
	const uint8_t control = inputByte();
	if (failed())
		return false;
	if (control == 0) {
		_finished = true;
		return false;
	}
	if (control == 1 || control == 2) {
		// Stored, with (1) or without a dictionary reset.
		if (control == 1) {
			resetDictionary();
			_needDictReset = false;
		} else if (_needDictReset)
			return fail("bad LZMA2 stream (no dictionary reset)");
		_chunkLeft = (uint32_t(inputByte()) << 8) + inputByte() + 1;
		return !failed();
	}
	if (control < 0x80)
		return fail("bad LZMA2 stream (control byte)");
	_chunkLeft = (uint32_t(control & 0x1F) << 16) + (uint32_t(inputByte()) << 8) + inputByte() + 1;
	const uint32_t packed = (uint32_t(inputByte()) << 8) + inputByte() + 1;
	const unsigned reset = (control >> 5) & 3;
	if (reset == 3) {
		resetDictionary();
		_needDictReset = false;
	} else if (_needDictReset)
		return fail("bad LZMA2 stream (no dictionary reset)");
	if (reset >= 2) {
		const uint8_t props = inputByte();
		if (failed() || !setProps(props))
			return false;
		if (_lc + _lp > 4)
			return fail("bad LZMA2 properties");
		_needProps = false;
	} else if (_needProps)
		return fail("bad LZMA2 stream (no properties)");
	if (reset >= 1)
		resetState();
	_chunkLzma = true;
	_chunkInEnd = _inCount + packed;
	return initRangeCoder();
}

size_t LzmaDecoder::decode(uint8_t *out, size_t size) {
	size_t done = 0;
	while (done < size && !_finished && !failed()) {
		if (!_lzma2) {
			done += decodeLzma(out ? out + done : nullptr, size - done);
			break;
		}
		if (_chunkLeft == 0 && _pending == 0) {
			if (!lzma2NextChunk())
				break;
			continue;
		}
		if (_chunkLzma) {
			done += decodeLzma(out ? out + done : nullptr, size - done);
			continue;
		}
		// A stored chunk.
		uint8_t *o = out ? out + done : nullptr;
		const size_t n = std::min<size_t>(_chunkLeft, size - done);
		for (size_t i = 0; i < n && !failed(); i++) {
			put(inputByte(), o);
			_pos++;
		}
		_chunkLeft -= uint32_t(n);
		done += n;
	}
	return done;
}

} // namespace gob
