// gob2native - mixer tests: effect quality modes, speaker clicks, music files
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/cdsource.h"
#include "audio/mixer.h"
#include "audio/musicpack.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace gob;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

using Q = Mixer::EffectsQuality;

// Left channel of `frames` mixed frames.
static std::vector<double> mixLeft(Mixer &m, int frames) {
	std::vector<int16_t> buf(size_t(frames) * 2);
	m.mix(buf.data(), frames);
	std::vector<double> out(static_cast<size_t>(frames));
	for (int i = 0; i < frames; i++)
		out[size_t(i)] = buf[size_t(i) * 2];
	return out;
}

// Amplitude of the `freq` Hz component (Goertzel, Hann window).
static double amplitude(const std::vector<double> &x, double freq) {
	const double w = 2 * M_PI * freq / Mixer::kRate;
	double s1 = 0, s2 = 0;
	for (size_t i = 0; i < x.size(); i++) {
		double hann = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(x.size()));
		double s = x[i] * hann + 2 * std::cos(w) * s1 - s2;
		s2 = s1;
		s1 = s;
	}
	return std::sqrt(s1 * s1 + s2 * s2 - 2 * std::cos(w) * s1 * s2) / double(x.size());
}

static double db(double a, double b) { return 20 * std::log10((a + 1e-12) / (b + 1e-12)); }

static std::shared_ptr<std::vector<uint8_t>> sine8(double freq, int rate, int n) {
	auto pcm = std::make_shared<std::vector<uint8_t>>(size_t(n));
	for (int i = 0; i < n; i++)
		(*pcm)[size_t(i)] = uint8_t(int8_t(std::lrint(100 * std::sin(2 * M_PI * freq * i / rate))));
	return pcm;
}

static void writeWav(const std::string &path, int rate, double freq, double seconds) {
	std::vector<int16_t> s(size_t(rate * seconds));
	for (size_t i = 0; i < s.size(); i++)
		s[i] = int16_t(std::lrint(16000 * std::sin(2 * M_PI * freq * double(i) / rate)));
	auto u32 = [](std::ofstream &f, uint32_t v) { for (int i = 0; i < 4; i++) f.put(char(v >> (8 * i))); };
	auto u16 = [](std::ofstream &f, uint32_t v) { f.put(char(v)); f.put(char(v >> 8)); };
	std::ofstream f(path, std::ios::binary);
	uint32_t bytes = uint32_t(s.size() * 2);
	f.write("RIFF", 4);
	u32(f, 36 + bytes);
	f.write("WAVEfmt ", 8);
	u32(f, 16);
	u16(f, 1);
	u16(f, 1);
	u32(f, uint32_t(rate));
	u32(f, uint32_t(rate * 2));
	u16(f, 2);
	u16(f, 16);
	f.write("data", 4);
	u32(f, bytes);
	f.write(reinterpret_cast<const char *>(s.data()), std::streamsize(bytes));
}

static void testRepeatsAndPlayingState() {
	for (Q q : {Q::Original, Q::Enhanced}) {
		Mixer m;
		m.setEffectsQuality(q);
		m.playSample(0, sine8(1000, 8000, 800), 8000, 2); // 2 x 0.1 s = 8820 output frames
		mixLeft(m, 8800);
		CHECK(m.samplePlaying(0));
		mixLeft(m, 40);
		CHECK(!m.samplePlaying(0)); // the enhanced filter's ring-out doesn't count
	}
}

static void testAliasing() {
	double base[2], image[2];
	for (int i = 0; i < 2; i++) {
		Mixer m;
		m.setEffectsQuality(i ? Q::Enhanced : Q::Original);
		m.playSample(0, sine8(2000, 8000, 8000), 8000, 1);
		mixLeft(m, 11025);
		std::vector<double> x = mixLeft(m, 22050);
		base[i] = amplitude(x, 2000);
		image[i] = amplitude(x, 6000); // 8000 - 2000: the first image above Nyquist (4000)
	}
	std::printf("2 kHz tone at 8 kHz: image at 6 kHz %.1f dB (original), %.1f dB (enhanced); tone %+.2f dB\n",
	            db(image[0], base[0]), db(image[1], base[1]), db(base[1], base[0]));
	CHECK(db(image[0], base[0]) > -15);
	CHECK(db(image[1], base[1]) < -60);
	CHECK(std::fabs(db(base[1], base[0])) < 1.5);
}

static void testSpeakerClick() {
	double maxStep[2];
	for (int i = 0; i < 2; i++) {
		Mixer m;
		m.setEffectsQuality(i ? Q::Enhanced : Q::Original);
		m.playSpeaker(50, 5);
		std::vector<double> x = mixLeft(m, 1000);
		maxStep[i] = 0;
		for (size_t k = 1; k < x.size(); k++)
			maxStep[i] = std::max(maxStep[i], std::fabs(x[k] - x[k - 1]));
		CHECK(!m.samplePlaying(Mixer::kSpeakerChannel));
	}
	CHECK(maxStep[0] > 10000);    // instant edges
	CHECK(maxStep[1] < 1500);     // soft, band-limited edges
}

// Speech: a voice of its own with its own volume, beside the effects.
static void testSpeech() {
	Mixer m;
	auto tone = sine8(1000, 22050, 22050 / 10); // 0.1 s
	m.playSpeech(tone, 22050);
	CHECK(m.speechPlaying() && !m.samplePlaying(0));
	auto x = mixLeft(m, 2000);
	CHECK(amplitude(std::vector<double>(x.begin() + 500, x.end()), 1000) > 1000);
	Mixer::Volumes v;
	v.speech = 0;
	m.setVolumes(v);
	x = mixLeft(m, 1000);
	CHECK(amplitude(x, 1000) < 1);
	m.playSample(0, tone, 22050, 1); // the effects keep their volume
	x = mixLeft(m, 2000);
	CHECK(amplitude(std::vector<double>(x.begin() + 500, x.end()), 1000) > 1000);
	m.stopSpeech();
	CHECK(!m.speechPlaying() && m.samplePlaying(0));
	mixLeft(m, 6000);
	CHECK(!m.speechPlaying() && !m.samplePlaying(0));
}

static void testMusicFiles(const fs::path &dir) {
	fs::create_directories(dir);
	writeWav((dir / "Mine.WAV").string(), 22050, 1000, 0.2);
	std::ofstream(dir / "notes.txt") << "not music";

	MusicPack pack;
	pack.scan(dir.string());
	CHECK(pack.entries().size() == 1);
	const MusicPack::Entry *e = pack.find("mine");
	CHECK(e && e->track == "MINE" && e->info.error.empty());
	CHECK(e && e->info.description.find("22050 Hz mono") != std::string::npos);

	// Shorter than the track: plays from its start, then silence.
	Mixer m;
	m.playCdSource(pack.open("MINE"), 0.5);
	std::vector<double> x = mixLeft(m, 44100);
	size_t last = 0;
	for (size_t i = 0; i < x.size(); i++)
		if (x[i] != 0)
			last = i;
	CHECK(last > 8800 && last < 8900); // 0.2 s at 44.1 kHz plus the resampler's ring-out
	CHECK(std::fabs(db(amplitude(std::vector<double>(x.begin() + 1000, x.begin() + 8000), 1000), 16000 / 4.0)) < 0.5); // Hann-windowed Goertzel: A / 4

	// Longer than the track: cut at the track's length.
	m.playCdSource(pack.open("MINE"), 0.1);
	x = mixLeft(m, 8000);
	CHECK(x[4409] != 0 || x[4408] != 0);
	for (size_t i = 4410; i < x.size(); i++)
		CHECK(x[i] == 0);

	// The file is read when opened: deleting it while it plays changes nothing.
	writeWav((dir / "gone.wav").string(), 44100, 1000, 0.2);
	auto gone = openMusicFile((dir / "gone.wav").string());
	CHECK(gone != nullptr);
	fs::remove(dir / "gone.wav");
	m.playCdSource(std::move(gone), 1.0);
	x = mixLeft(m, 11025);
	CHECK(std::fabs(db(amplitude(std::vector<double>(x.begin() + 1000, x.begin() + 8000), 1000), 16000 / 4.0)) < 0.5);

	std::string err;
	CHECK(!openMusicFile((dir / "notes.txt").string(), &err) && !err.empty());
}

int main() {
	testRepeatsAndPlayingState();
	testAliasing();
	testSpeakerClick();
	testSpeech();
	fs::path dir = fs::temp_directory_path() / ("gobnative_test_mixer_" + std::to_string(getpid()));
	testMusicFiles(dir);
	std::error_code ec;
	fs::remove_all(dir, ec);
	if (failures) {
		std::printf("%d mixer test(s) failed\n", failures);
		return 1;
	}
	std::printf("all mixer tests passed\n");
	return 0;
}
