// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Software audio mixer. The engine side starts/stops sounds; the platform
// side (SDL audio callback, or a web audio worklet later) pulls mixed
// 16-bit stereo frames with mix(). Thread-safe between the two.

#pragma once

#include "audio/cdsource.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace gob {

class Mixer {
public:
	static constexpr int kRate = 44100;

	Mixer();
	~Mixer();
	Mixer(const Mixer &) = delete;
	Mixer &operator=(const Mixer &) = delete;

	// Fills `frames` interleaved stereo frames. Called from the audio thread.
	void mix(int16_t *out, int frames);

	// --- CD audio: the GOG release ships the CD's audio as one constant
	// bitrate MP3 laid out by CD sector (75 sectors per second, starting at
	// sector 0).
	bool loadCdImage(std::vector<uint8_t> mp3);
	bool hasCdImage() const { return !_cdImage.empty(); }
	// Plays `lengthSeconds` of the CD image from `startSeconds`.
	void playCd(double startSeconds, double lengthSeconds);
	// Plays another source (a replacement music file) as the CD music, for
	// at most `lengthSeconds` (< 0: until the source ends).
	void playCdSource(std::unique_ptr<CdSource> source, double lengthSeconds);
	void stopCd();
	// CD music (from either source) is playing.
	bool cdPlaying();

	// --- Digitized samples (signed 8-bit mono PCM at any rate).
	// `repeat` is the number of times to play (0 = loop until stopped);
	// `fadeInMs` ramps the volume up from silence.
	void playSample(int channel, std::shared_ptr<const std::vector<uint8_t>> pcm, int rate, int repeat,
	                int fadeInMs = 0);
	// Stops a channel, optionally fading out over `fadeOutMs` first.
	void stopSample(int channel, int fadeOutMs = 0);
	bool samplePlaying(int channel);
	static constexpr int kSampleChannels = 4;
	static constexpr int kSpeakerChannel = 1; // PC-speaker tones; the other channels are effects

	// --- Video sound (Goblins 3), signed 8-bit mono, on a voice of its own:
	// the speech (the talking heads, `voice`) at the speech volume, other
	// videos' sound at the effects volume. Starting it replaces what it
	// played before.
	void playSpeech(std::shared_ptr<const std::vector<uint8_t>> pcm, int rate, bool voice = true);
	void stopSpeech();
	bool speechPlaying();

	// PC-speaker square wave at `freq` Hz on kSpeakerChannel: `ms` long, or
	// until stopSample() when `ms` is 0.
	void playSpeaker(int freq, int ms);

	// How effect samples and PC-speaker tones are rendered.
	//  Original: like the Sound Blaster DAC, each 8-bit sample is held until
	//    the next (no interpolation), so the images of the spectrum above the
	//    sample's Nyquist frequency stay in; speaker tones are plain squares.
	//  Enhanced: band-limited windowed-sinc interpolation with a low-pass just
	//    below the sample's Nyquist frequency; hard stops and voice changes
	//    fade out over 2 ms instead of clicking; speaker tones are low-passed
	//    squares with soft edges.
	enum class EffectsQuality { Original, Enhanced };
	void setEffectsQuality(EffectsQuality q);

	// Output volumes (0..1) per source, all scaled by `master`.
	struct Volumes {
		float master = 1.0f, music = 1.0f, effects = 1.0f, speaker = 1.0f, speech = 1.0f;
	};
	void setVolumes(const Volumes &v);

private:
	struct Voice {
		std::shared_ptr<const std::vector<float>> data; // sample values in 8-bit units (-128..127)
		double pos = 0, step = 0; // position in the current pass, in source samples
		int repeatsLeft = 0;      // passes left including this one; 0 = loop forever
		bool firstPass = true;
		bool active = false;
		bool tail = false; // the sound is over for the game; the filter rings out (enhanced)
		float volume = 1.0f, volumeStep = 0.0f; // per output frame; negative = fading out
		bool stopAtSilence = false;
	};
	void startVoice(int channel, std::shared_ptr<const std::vector<float>> data, int rate, int repeat,
	                int fadeInMs);
	void retire(int channel); // moves the voice to its fade-out slot (enhanced) or stops it
	void mixVoice(Voice &v, int *acc, int frames, float channelVolume, bool enhanced);

	std::mutex _lock;
	CdImage _cdImage;
	std::unique_ptr<CdSource> _cd;
	uint64_t _cdRemaining = 0; // frames left to play; UINT64_MAX = until the source ends
	static constexpr int kSpeechVoice = kSampleChannels; // after the sample channels
	bool _speechIsVoice = true;                          // the video sound playing is speech
	static constexpr int kVoices = kSampleChannels + 1;
	Voice _voices[kVoices];
	Voice _fading[kVoices]; // replaced or stopped voices fading out (enhanced)
	EffectsQuality _quality = EffectsQuality::Enhanced;
	Volumes _volumes;
	std::vector<int16_t> _cdBuf;
	std::vector<int> _acc;
};

} // namespace gob
