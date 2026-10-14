// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/cdsource.h"

#include "audio/resample.h"
#include "dr_mp3.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace gob {

namespace {

constexpr int kOutRate = 44100;
constexpr int kSamplesPerFrame = 1152;
constexpr int kPreroll = 2; // frames decoded and discarded to prime the bit reservoir

// Bytes in the MPEG-1 Layer III frame starting at p, or 0 if not a frame.
size_t mp3FrameSize(const uint8_t *p, size_t avail) {
	static const int kBitrates[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
	static const int kRates[4] = {44100, 48000, 32000, 0};
	if (avail < 4 || p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
		return 0;
	int br = kBitrates[p[2] >> 4];
	int sr = kRates[(p[2] >> 2) & 3];
	if (!br || !sr)
		return 0;
	return size_t(144 * br * 1000 / sr + ((p[2] >> 1) & 1));
}

int16_t toS16(float v) { return int16_t(std::lrint(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); }

// --- The CD image

class ImageStream : public CdSource {
public:
	ImageStream(std::shared_ptr<const CdImage::Data> data, size_t firstFrame, uint64_t skip)
		: _data(std::move(data)), _skip(skip) {
		size_t off = _data->frames[firstFrame];
		_open = drmp3_init_memory(&_dec, _data->mp3.data() + off, _data->mp3.size() - off, nullptr);
	}
	~ImageStream() override {
		if (_open)
			drmp3_uninit(&_dec);
	}
	bool ok() const { return _open; }

	size_t read(int16_t *out, size_t frames) override {
		while (_skip > 0) {
			uint64_t n = std::min<uint64_t>(_skip, frames);
			uint64_t got = drmp3_read_pcm_frames_s16(&_dec, n, out);
			if (got == 0)
				return 0;
			_skip -= got;
		}
		return size_t(drmp3_read_pcm_frames_s16(&_dec, frames, out));
	}

private:
	std::shared_ptr<const CdImage::Data> _data;
	drmp3 _dec{};
	bool _open = false;
	uint64_t _skip; // PCM frames to discard (pre-roll)
};

// --- Music files: decoders produce float frames at the file's own rate and
// channel count; ConvertedSource turns them into 44.1 kHz stereo.

class FloatReader {
public:
	virtual ~FloatReader() = default;
	// Reads up to `frames` frames of `channels` interleaved floats in -1..1.
	virtual size_t read(float *out, size_t frames) = 0;
	int rate = 0, channels = 0;
};

class Mp3Reader : public FloatReader {
public:
	explicit Mp3Reader(std::vector<uint8_t> data) : _data(std::move(data)) {
		_open = drmp3_init_memory(&_dec, _data.data(), _data.size(), nullptr);
		if (_open) {
			rate = int(_dec.sampleRate);
			channels = int(_dec.channels);
		}
	}
	~Mp3Reader() override {
		if (_open)
			drmp3_uninit(&_dec);
	}
	bool ok() const { return _open && rate > 0 && channels > 0; }
	size_t read(float *out, size_t frames) override { return size_t(drmp3_read_pcm_frames_f32(&_dec, frames, out)); }

private:
	std::vector<uint8_t> _data;
	drmp3 _dec{};
	bool _open = false;
};

uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t(le16(p + 2)) << 16); }

struct WavFormat {
	int format = 0; // 1 integer PCM, 3 IEEE float
	int channels = 0, rate = 0, bits = 0, blockAlign = 0;
	uint64_t dataOffset = 0, dataSize = 0;
};

// Walks the RIFF chunks up to "data" (WAVE_FORMAT_EXTENSIBLE is read through
// its sub-format).
bool parseWav(std::istream &in, WavFormat &w, std::string &error) {
	uint8_t hdr[12];
	if (!in.read(reinterpret_cast<char *>(hdr), 12) || std::memcmp(hdr, "RIFF", 4) || std::memcmp(hdr + 8, "WAVE", 4)) {
		error = "not a RIFF WAVE file";
		return false;
	}
	bool haveFmt = false;
	uint64_t pos = 12;
	for (;;) {
		uint8_t ch[8];
		if (!in.read(reinterpret_cast<char *>(ch), 8)) {
			error = haveFmt ? "no data chunk" : "no fmt chunk";
			return false;
		}
		uint32_t size = le32(ch + 4);
		pos += 8;
		if (!std::memcmp(ch, "fmt ", 4)) {
			uint8_t f[40] = {};
			if (size < 16 || !in.read(reinterpret_cast<char *>(f), std::min<uint32_t>(size, 40))) {
				error = "bad fmt chunk";
				return false;
			}
			w.format = le16(f);
			w.channels = le16(f + 2);
			w.rate = int(le32(f + 4));
			w.blockAlign = le16(f + 12);
			w.bits = le16(f + 14);
			if (w.format == 0xFFFE && size >= 26)
				w.format = le16(f + 24);
			haveFmt = true;
		} else if (!std::memcmp(ch, "data", 4)) {
			if (!haveFmt) {
				error = "data before fmt chunk";
				return false;
			}
			w.dataOffset = pos;
			w.dataSize = size;
			break;
		}
		pos += size + (size & 1);
		in.seekg(std::streamoff(pos));
	}
	bool pcm = w.format == 1 && (w.bits == 8 || w.bits == 16 || w.bits == 24 || w.bits == 32);
	bool flt = w.format == 3 && (w.bits == 32 || w.bits == 64);
	if (!pcm && !flt) {
		error = "unsupported WAV encoding (format " + std::to_string(w.format) + ", " + std::to_string(w.bits) + " bit)";
		return false;
	}
	if (w.channels < 1 || w.rate < 1000 || w.rate > 384000 || w.blockAlign != w.channels * w.bits / 8) {
		error = "bad WAV format header";
		return false;
	}
	return true;
}

// Reads the whole data chunk when the file is opened and keeps it as 16-bit
// samples (at most two channels, at the file's own rate), so that playing
// never touches the disk: read() runs in the audio callback.
class WavReader : public FloatReader {
public:
	bool open(const std::string &path, std::string &error) {
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			error = "cannot open";
			return false;
		}
		WavFormat fmt;
		if (!parseWav(in, fmt, error))
			return false;
		in.seekg(std::streamoff(fmt.dataOffset));
		const size_t block = size_t(fmt.blockAlign), keep = size_t(std::min(fmt.channels, 2));
		const uint64_t frames = fmt.dataSize / block;
		_pcm.resize(size_t(frames) * keep);
		std::vector<uint8_t> raw((65536 / block + 1) * block);
		uint64_t done = 0;
		while (done < frames) {
			size_t want = size_t(std::min<uint64_t>(frames - done, raw.size() / block));
			in.read(reinterpret_cast<char *>(raw.data()), std::streamsize(want * block));
			size_t got = size_t(in.gcount()) / block;
			convert(fmt, raw.data(), got, keep, &_pcm[size_t(done) * keep]);
			done += got;
			if (got < want)
				break; // truncated file: play what is there
		}
		_pcm.resize(size_t(done) * keep);
		rate = fmt.rate;
		channels = int(keep);
		return true;
	}

	size_t read(float *out, size_t frames) override {
		const size_t ch = size_t(channels);
		frames = std::min(frames, (_pcm.size() - _pos) / ch);
		for (size_t i = 0; i < frames * ch; i++)
			out[i] = _pcm[_pos + i] / 32768.0f;
		_pos += frames * ch;
		return frames;
	}

private:
	// `frames` frames of raw WAV data to 16-bit samples, keeping the first
	// `keep` channels of each frame.
	static void convert(const WavFormat &fmt, const uint8_t *raw, size_t frames, size_t keep, int16_t *out) {
		const int bytes = fmt.bits / 8;
		for (size_t f = 0; f < frames; f++) {
			const uint8_t *p = raw + f * size_t(fmt.blockAlign);
			for (size_t c = 0; c < keep; c++, p += bytes) {
				int16_t v;
				if (fmt.format == 3) {
					double d;
					if (bytes == 4) {
						uint32_t u = le32(p);
						float x;
						std::memcpy(&x, &u, 4);
						d = x;
					} else {
						uint64_t u = le32(p) | (uint64_t(le32(p + 4)) << 32);
						std::memcpy(&d, &u, 8);
					}
					v = int16_t(std::lrint(std::clamp(d, -1.0, 1.0) * 32767.0));
				} else if (bytes == 1) {
					v = int16_t((int(p[0]) - 128) * 256);
				} else if (bytes == 2) {
					v = int16_t(le16(p));
				} else if (bytes == 3) {
					v = int16_t(le16(p + 1));
				} else {
					v = int16_t(le16(p + 2));
				}
				*out++ = v;
			}
		}
	}

	std::vector<int16_t> _pcm; // interleaved, `channels` per frame
	size_t _pos = 0;
};

// Any FloatReader as 44.1 kHz stereo: mono is doubled, channels past the
// second are dropped, other rates go through a windowed-sinc low-pass at
// 0.47 of the lower of the two rates.
class ConvertedSource : public CdSource {
public:
	explicit ConvertedSource(std::unique_ptr<FloatReader> in) : _in(std::move(in)) {
		_direct = _in->rate == kOutRate;
		_step = double(_in->rate) / kOutRate;
		_scale = 2.0 * 0.47 * std::min(1.0, 1.0 / _step);
		_taps = resample::halfTaps(_scale);
		_bufStart = -_taps;
		_buf.assign(size_t(_taps) * 2, 0.0f); // silence before the start
	}

	size_t read(int16_t *out, size_t frames) override {
		if (_direct) {
			_buf.clear();
			size_t got = pull(_buf, frames);
			for (size_t i = 0; i < got * 2; i++)
				out[i] = toS16(_buf[i]);
			return got;
		}
		size_t done = 0;
		for (; done < frames; done++) {
			const double pos = double(_outFrames) * _step;
			const long base = long(std::floor(pos));
			while (_bufStart + long(_buf.size() / 2) <= base + _taps) {
				if (_eof) {
					_buf.resize(_buf.size() + 2 * size_t(base + _taps + 1 - _bufStart - long(_buf.size() / 2)), 0.0f);
					break;
				}
				if (pull(_buf, kChunk) < kChunk) {
					_eof = true;
					_inputEnd = _bufStart + long(_buf.size() / 2);
				}
			}
			if (_eof && pos >= double(_inputEnd))
				break;
			float l = 0.0f, r = 0.0f;
			for (long k = base - _taps + 1; k <= base + _taps; k++) {
				float w = resample::kernel((pos - double(k)) * _scale);
				const float *f = &_buf[size_t(k - _bufStart) * 2];
				l += f[0] * w;
				r += f[1] * w;
			}
			out[done * 2] = toS16(l * float(_scale));
			out[done * 2 + 1] = toS16(r * float(_scale));
			_outFrames++;
		}
		long keep = long(std::floor(double(_outFrames) * _step)) - _taps + 1;
		if (keep > _bufStart) {
			size_t drop = std::min(size_t(keep - _bufStart), _buf.size() / 2);
			_buf.erase(_buf.begin(), _buf.begin() + long(drop * 2));
			_bufStart += long(drop);
		}
		return done;
	}

private:
	static constexpr size_t kChunk = 4096;

	// Appends up to n input frames to dst as stereo; returns the count.
	size_t pull(std::vector<float> &dst, size_t n) {
		const size_t ch = size_t(_in->channels);
		_tmp.resize(n * ch);
		size_t got = _in->read(_tmp.data(), n);
		size_t at = dst.size();
		dst.resize(at + got * 2);
		for (size_t i = 0; i < got; i++) {
			dst[at + i * 2] = _tmp[i * ch];
			dst[at + i * 2 + 1] = _tmp[i * ch + (ch > 1 ? 1 : 0)];
		}
		return got;
	}

	std::unique_ptr<FloatReader> _in;
	bool _direct = false, _eof = false;
	double _step = 1.0, _scale = 1.0;
	int _taps = 0;
	uint64_t _outFrames = 0;
	std::vector<float> _buf; // stereo input frames from index _bufStart
	long _bufStart = 0, _inputEnd = 0;
	std::vector<float> _tmp;
};

bool readFile(const std::string &path, std::vector<uint8_t> &out, size_t limit = SIZE_MAX) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return false;
	out.clear();
	std::vector<char> buf(65536);
	while (out.size() < limit) {
		f.read(buf.data(), std::streamsize(std::min(buf.size(), limit - out.size())));
		if (f.gcount() <= 0)
			break;
		out.insert(out.end(), buf.begin(), buf.begin() + f.gcount());
	}
	return true;
}

bool isWav(const std::vector<uint8_t> &head) { return head.size() >= 4 && !std::memcmp(head.data(), "RIFF", 4); }

std::string channelName(int ch) { return ch == 1 ? "mono" : ch == 2 ? "stereo" : std::to_string(ch) + " channels"; }

} // namespace

bool CdImage::load(std::vector<uint8_t> mp3) {
	auto d = std::make_shared<Data>();
	size_t pos = 0;
	if (mp3.size() >= 10 && mp3[0] == 'I' && mp3[1] == 'D' && mp3[2] == '3')
		pos = 10 + ((size_t(mp3[6]) << 21) | (size_t(mp3[7]) << 14) | (size_t(mp3[8]) << 7) | mp3[9]);
	while (pos + 4 <= mp3.size()) {
		size_t sz = mp3FrameSize(&mp3[pos], mp3.size() - pos);
		if (!sz) {
			pos++;
			continue;
		}
		d->frames.push_back(pos);
		pos += sz;
	}
	d->mp3 = std::move(mp3);
	_data = std::move(d);
	return !empty();
}

std::unique_ptr<CdSource> CdImage::open(double startSeconds) const {
	if (empty())
		return nullptr;
	uint64_t startSample = uint64_t(std::max(0.0, startSeconds) * kOutRate);
	size_t frame = size_t(startSample / kSamplesPerFrame);
	size_t first = frame >= size_t(kPreroll) ? frame - kPreroll : 0;
	if (first >= _data->frames.size())
		return nullptr;
	auto s = std::make_unique<ImageStream>(_data, first, startSample - uint64_t(first) * kSamplesPerFrame);
	if (!s->ok())
		return nullptr;
	return s;
}

std::unique_ptr<CdSource> openMusicFile(const std::string &path, std::string *error) {
	std::string err;
	std::unique_ptr<FloatReader> reader;
	std::vector<uint8_t> head;
	if (!readFile(path, head, 4)) {
		err = "cannot open";
	} else if (isWav(head)) {
		auto w = std::make_unique<WavReader>();
		if (w->open(path, err))
			reader = std::move(w);
	} else {
		std::vector<uint8_t> data;
		readFile(path, data);
		auto m = std::make_unique<Mp3Reader>(std::move(data));
		if (m->ok())
			reader = std::move(m);
		else
			err = "not a WAV or MP3 file";
	}
	if (!reader) {
		if (error)
			*error = err;
		return nullptr;
	}
	return std::make_unique<ConvertedSource>(std::move(reader));
}

MusicFileInfo probeMusicFile(const std::string &path) {
	MusicFileInfo info;
	std::vector<uint8_t> head;
	if (!readFile(path, head, 4)) {
		info.error = "cannot open";
		return info;
	}
	char buf[96];
	if (isWav(head)) {
		std::ifstream in(path, std::ios::binary);
		WavFormat w;
		if (!parseWav(in, w, info.error))
			return info;
		int secs = int(double(w.dataSize / uint64_t(w.blockAlign)) / w.rate + 0.5);
		std::snprintf(buf, sizeof(buf), "WAV, %d Hz %s, %d-bit%s, %d:%02d", w.rate, channelName(w.channels).c_str(),
		              w.bits, w.format == 3 ? " float" : "", secs / 60, secs % 60);
		info.description = buf;
		return info;
	}
	// MP3: the first frames are enough for the format.
	std::vector<uint8_t> data;
	readFile(path, data, 256 * 1024);
	drmp3 dec{};
	if (!drmp3_init_memory(&dec, data.data(), data.size(), nullptr)) {
		info.error = "not a WAV or MP3 file";
		return info;
	}
	std::snprintf(buf, sizeof(buf), "MP3, %u Hz %s", dec.sampleRate, channelName(int(dec.channels)).c_str());
	info.description = buf;
	drmp3_uninit(&dec);
	return info;
}

} // namespace gob
