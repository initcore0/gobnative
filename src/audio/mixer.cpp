// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/mixer.h"

#include "audio/resample.h"

#include <algorithm>
#include <cmath>

namespace gob {

namespace {

// Enhanced effects: low-pass cutoff as a fraction of the sample rate (the
// Nyquist frequency is 0.5). With the kernel's transition band this keeps
// the response flat to about 0.35 and the stopband (-80 dB) from 0.49 up.
constexpr double kEffectsCutoff = 0.42;
// Enhanced effects: fade-out for voices that stop or are replaced at once.
constexpr int kDeclickMs = 2;
// Enhanced PC speaker: low-pass cutoff (Hz) and edge ramp length.
constexpr double kSpeakerCutoff = 4000.0;
constexpr int kSpeakerRamp = Mixer::kRate / 2000; // 0.5 ms
constexpr float kSpeakerLevel = 40.0f;            // square wave amplitude in 8-bit units

int16_t clamp16(int v) { return int16_t(std::clamp(v, -32768, 32767)); }

// PC-speaker square wave: one-shot `ms` long, or one period to loop (ms 0).
// Enhanced: raised-cosine edges on a one-shot, then a windowed-sinc
// low-pass (circular for a loop; a one-shot gets the filter's ring-out).
std::vector<float> speakerWave(int freq, int ms, bool enhanced) {
	const int half = std::max(1, Mixer::kRate / (2 * freq));
	const bool loop = ms <= 0;
	const size_t len = loop ? size_t(2 * half) : std::max<size_t>(1, size_t(Mixer::kRate) * size_t(ms) / 1000);
	std::vector<float> sq(len);
	for (size_t i = 0; i < len; i++)
		sq[i] = ((int(i) / half) & 1) ? kSpeakerLevel : -kSpeakerLevel;
	if (!enhanced)
		return sq;

	if (!loop) {
		size_t ramp = std::min<size_t>(size_t(kSpeakerRamp), len / 2);
		for (size_t i = 0; i < ramp; i++) {
			float g = 0.5f - 0.5f * std::cos(float(M_PI) * (float(i) + 0.5f) / float(ramp));
			sq[i] *= g;
			sq[len - 1 - i] *= g;
		}
	}
	const double scale = 2.0 * kSpeakerCutoff / Mixer::kRate;
	const int taps = resample::halfTaps(scale);
	std::vector<float> h(size_t(2 * taps + 1));
	for (int j = -taps; j <= taps; j++)
		h[size_t(j + taps)] = float(scale) * resample::kernel(j * scale);

	const long n = long(len);
	std::vector<float> out(loop ? len : len + size_t(2 * taps), 0.0f);
	for (long o = 0; o < long(out.size()); o++) {
		long centre = loop ? o : o - taps; // a one-shot starts `taps` early to ring in
		float acc = 0.0f;
		for (int j = -taps; j <= taps; j++) {
			long k = centre - j;
			if (loop)
				k = ((k % n) + n) % n;
			else if (k < 0 || k >= n)
				continue;
			acc += h[size_t(j + taps)] * sq[size_t(k)];
		}
		out[size_t(o)] = acc;
	}
	return out;
}

} // namespace

Mixer::Mixer() = default;
Mixer::~Mixer() = default;

bool Mixer::loadCdImage(std::vector<uint8_t> mp3) {
	CdImage image;
	bool ok = image.load(std::move(mp3));
	std::lock_guard<std::mutex> g(_lock);
	_cdImage = std::move(image);
	return ok;
}

void Mixer::playCd(double startSeconds, double lengthSeconds) {
	std::unique_ptr<CdSource> old;
	std::lock_guard<std::mutex> g(_lock);
	old = std::move(_cd);
	_cd = _cdImage.open(startSeconds);
	_cdRemaining = uint64_t(std::max(0.0, lengthSeconds) * kRate);
}

void Mixer::playCdSource(std::unique_ptr<CdSource> source, double lengthSeconds) {
	std::unique_ptr<CdSource> old;
	std::lock_guard<std::mutex> g(_lock);
	old = std::move(_cd);
	_cd = std::move(source);
	_cdRemaining = lengthSeconds < 0 ? UINT64_MAX : uint64_t(lengthSeconds * kRate);
}

void Mixer::stopCd() {
	std::unique_ptr<CdSource> old;
	std::lock_guard<std::mutex> g(_lock);
	old = std::move(_cd);
}

bool Mixer::cdPlaying() {
	std::lock_guard<std::mutex> g(_lock);
	return _cd != nullptr;
}

void Mixer::playSample(int channel, std::shared_ptr<const std::vector<uint8_t>> pcm, int rate, int repeat,
                       int fadeInMs) {
	if (channel < 0 || channel >= kSampleChannels || !pcm || pcm->empty() || rate <= 0)
		return;
	auto data = std::make_shared<std::vector<float>>(pcm->size());
	for (size_t i = 0; i < pcm->size(); i++)
		(*data)[i] = float(int8_t((*pcm)[i]));
	startVoice(channel, std::move(data), rate, repeat, fadeInMs);
}

void Mixer::playSpeech(std::shared_ptr<const std::vector<uint8_t>> pcm, int rate, bool voice) {
	if (!pcm || pcm->empty() || rate <= 0)
		return;
	{
		std::lock_guard<std::mutex> g(_lock);
		_speechIsVoice = voice;
	}
	auto data = std::make_shared<std::vector<float>>(pcm->size());
	for (size_t i = 0; i < pcm->size(); i++)
		(*data)[i] = float(int8_t((*pcm)[i]));
	startVoice(kSpeechVoice, std::move(data), rate, 1, 0);
}

void Mixer::stopSpeech() {
	std::lock_guard<std::mutex> g(_lock);
	retire(kSpeechVoice);
}

bool Mixer::speechPlaying() {
	std::lock_guard<std::mutex> g(_lock);
	return _voices[kSpeechVoice].active && !_voices[kSpeechVoice].tail;
}

void Mixer::playSpeaker(int freq, int ms) {
	if (freq <= 0)
		return;
	bool enhanced;
	{
		std::lock_guard<std::mutex> g(_lock);
		enhanced = _quality == EffectsQuality::Enhanced;
	}
	auto data = std::make_shared<std::vector<float>>(speakerWave(freq, ms, enhanced));
	startVoice(kSpeakerChannel, std::move(data), kRate, ms > 0 ? 1 : 0, ms <= 0 && enhanced ? kDeclickMs : 0);
}

void Mixer::startVoice(int channel, std::shared_ptr<const std::vector<float>> data, int rate, int repeat,
                       int fadeInMs) {
	std::lock_guard<std::mutex> g(_lock);
	retire(channel);
	Voice &v = _voices[channel];
	v = Voice{};
	v.data = std::move(data);
	v.step = double(rate) / kRate;
	v.repeatsLeft = repeat;
	v.active = true;
	if (fadeInMs > 0) {
		v.volume = 0.0f;
		v.volumeStep = 1000.0f / (float(fadeInMs) * kRate);
	}
}

void Mixer::retire(int channel) {
	Voice &v = _voices[channel];
	if (v.active && _quality == EffectsQuality::Enhanced) {
		Voice &f = _fading[channel];
		f = v;
		float step = v.volume * 1000.0f / (float(kDeclickMs) * kRate);
		if (!f.stopAtSilence || -f.volumeStep < step)
			f.volumeStep = -step;
		f.stopAtSilence = true;
	}
	v.active = false;
}

void Mixer::stopSample(int channel, int fadeOutMs) {
	if (channel < 0 || channel >= kSampleChannels)
		return;
	std::lock_guard<std::mutex> g(_lock);
	Voice &v = _voices[channel];
	if (fadeOutMs > 0 && v.active && !v.tail) {
		v.volumeStep = -v.volume * 1000.0f / (float(fadeOutMs) * kRate);
		v.stopAtSilence = true;
	} else {
		retire(channel);
	}
}

bool Mixer::samplePlaying(int channel) {
	if (channel < 0 || channel >= kSampleChannels)
		return false;
	std::lock_guard<std::mutex> g(_lock);
	return _voices[channel].active && !_voices[channel].tail;
}

void Mixer::setEffectsQuality(EffectsQuality q) {
	std::lock_guard<std::mutex> g(_lock);
	_quality = q;
}

void Mixer::setVolumes(const Volumes &v) {
	std::lock_guard<std::mutex> g(_lock);
	_volumes = v;
}

// One voice into the accumulator. Original: zero-order hold (each source
// sample is repeated until the next one is due). Enhanced: windowed-sinc
// interpolation through a low-pass at kEffectsCutoff of the playback rate;
// across a loop the next pass's samples feed the filter, and after the last
// pass the filter rings out (the voice no longer counts as playing then).
void Mixer::mixVoice(Voice &v, int *acc, int frames, float channelVolume, bool enhanced) {
	const std::vector<float> &d = *v.data;
	const long n = long(d.size());
	const bool sinc = enhanced && v.step != 1.0;
	const double scale = 2.0 * kEffectsCutoff * std::min(1.0, 1.0 / v.step);
	const int taps = resample::halfTaps(scale);
	auto at = [&](long k) -> float {
		if (k >= 0 && k < n)
			return d[size_t(k)];
		if (k < 0)
			return v.firstPass || k + n < 0 ? 0.0f : d[size_t(k + n)];
		return v.repeatsLeft == 1 || k - n >= n ? 0.0f : d[size_t(k - n)];
	};

	for (int i = 0; i < frames && v.active; i++) {
		if (v.pos >= double(n) && !v.tail) {
			if (v.repeatsLeft == 1) {
				if (!sinc) {
					v.active = false;
					break;
				}
				v.tail = true;
			} else {
				if (v.repeatsLeft > 1)
					v.repeatsLeft--;
				v.pos -= double(n);
				v.firstPass = false;
			}
		}
		if (v.tail && v.pos >= double(n + taps)) {
			v.active = false;
			break;
		}
		float s = sinc ? resample::interpolate(v.pos, scale, taps, at) : at(long(v.pos));
		int out = int(s * 256.0f * v.volume * channelVolume);
		acc[size_t(i) * 2] += out;
		acc[size_t(i) * 2 + 1] += out;
		v.pos += v.step;

		if (v.volumeStep != 0.0f) {
			v.volume += v.volumeStep;
			if (v.volume >= 1.0f) {
				v.volume = 1.0f;
				v.volumeStep = 0.0f;
			} else if (v.volume <= 0.0f) {
				v.volume = 0.0f;
				v.volumeStep = 0.0f;
				if (v.stopAtSilence)
					v.active = false;
			}
		}
	}
}

void Mixer::mix(int16_t *out, int frames) {
	std::lock_guard<std::mutex> g(_lock);
	_acc.assign(size_t(frames) * 2, 0);
	int *acc = _acc.data();
	const float musicVolume = _volumes.master * _volumes.music;

	// CD music (the image or a replacement file; 44.1 kHz stereo).
	if (_cd) {
		_cdBuf.resize(size_t(frames) * 2);
		int filled = 0;
		while (filled < frames && _cdRemaining > 0) {
			uint64_t want = std::min<uint64_t>(uint64_t(frames - filled), _cdRemaining);
			size_t got = _cd->read(_cdBuf.data() + size_t(filled) * 2, size_t(want));
			if (got == 0) {
				_cdRemaining = 0;
				break;
			}
			for (size_t i = 0; i < got * 2; i++)
				acc[size_t(filled) * 2 + i] += int(_cdBuf[size_t(filled) * 2 + i] * musicVolume);
			filled += int(got);
			if (_cdRemaining != UINT64_MAX)
				_cdRemaining -= got;
		}
		if (_cdRemaining == 0)
			_cd.reset();
	}

	const bool enhanced = _quality == EffectsQuality::Enhanced;
	for (int ch = 0; ch < kVoices; ch++) {
		const float channelVolume = _volumes.master * (ch == kSpeechVoice      ? (_speechIsVoice ? _volumes.speech : _volumes.effects)
		                                               : ch == kSpeakerChannel ? _volumes.speaker
		                                                                       : _volumes.effects);
		if (_fading[ch].active)
			mixVoice(_fading[ch], acc, frames, channelVolume, enhanced);
		if (_voices[ch].active)
			mixVoice(_voices[ch], acc, frames, channelVolume, enhanced);
	}

	for (size_t i = 0; i < _acc.size(); i++)
		out[i] = clamp16(acc[i]);
}

} // namespace gob
