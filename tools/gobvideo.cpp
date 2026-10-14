// gob2native - IMD video tool: list, check and decode the videos of a game
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage:
//   gobvideo <gamedir> --list                 every IMD in the game's archives and folder
//   gobvideo <gamedir> --check-all            decode every IMD, report broken ones
//   gobvideo <gamedir> NAME[.IMD] <outdir>    frames as BMP and the sound as WAV
//   gobvideo <file.imd> <outdir>              the same for a loose file
//
// Frames are written at the video's own size (position 0, 0); the sound is
// 8-bit mono at the video's rate. Format: docs/imd.md.

#include "data/datafiles.h"
#include "engine/game.h"
#include "formats/imd.h"
#include "gfx/bmp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace gob;

namespace {

std::string upper(std::string s) {
	for (char &c : s)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

bool isImd(const std::string &name) {
	return upper(name).ends_with(".IMD");
}

std::vector<uint8_t> readFile(const fs::path &p) {
	std::ifstream f(p, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// The IMDs of a game: the archives' (by name) and loose files in its folder.
struct Video {
	std::string name;
	fs::path loose; // empty: in an archive
};

std::vector<Video> gameVideos(DataFiles &files) {
	std::vector<Video> out;
	for (const std::string &n : files.list())
		if (isImd(n))
			out.push_back({n, {}});
	std::error_code ec;
	for (const auto &e : fs::directory_iterator(files.gameDir(), ec))
		if (e.is_regular_file() && isImd(e.path().filename().string()))
			out.push_back({upper(e.path().filename().string()), e.path()});
	return out;
}

std::vector<uint8_t> videoData(DataFiles &files, const Video &v) {
	if (!v.loose.empty())
		return readFile(v.loose);
	auto d = files.read(v.name);
	return d ? *d : std::vector<uint8_t>{};
}

void writeWav(const fs::path &path, const std::vector<int8_t> &pcm, int rate) {
	std::vector<uint8_t> out;
	auto put = [&](uint32_t v, int bytes) {
		for (int i = 0; i < bytes; i++)
			out.push_back(uint8_t(v >> (8 * i)));
	};
	out.insert(out.end(), {'R', 'I', 'F', 'F'});
	put(uint32_t(36 + pcm.size()), 4);
	out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
	put(16, 4);
	put(1, 2); // PCM
	put(1, 2); // mono
	put(uint32_t(rate), 4);
	put(uint32_t(rate), 4);
	put(1, 2);
	put(8, 2);
	out.insert(out.end(), {'d', 'a', 't', 'a'});
	put(uint32_t(pcm.size()), 4);
	for (int8_t s : pcm)
		out.push_back(uint8_t(s) ^ 0x80);
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(out.data()), long(out.size()));
}

struct Decoded {
	int frames = 0;
	size_t audio = 0;
	std::string error;
};

// Decodes every frame at position 0, 0; `onFrame` sees each one.
template <typename F>
Decoded decodeAll(ImdDecoder &imd, std::vector<uint8_t> &canvas, F onFrame) {
	Decoded r;
	int w = std::max(imd.width(), 1), h = std::max(imd.height(), 1);
	canvas.assign(size_t(w) * size_t(h), 0);
	imd.setXY(0, 0);
	ImdDecoder::Frame f;
	while (!imd.endOfVideo()) {
		if (!imd.decodeFrame(canvas.data(), w, h, w, f))
			break;
		r.frames++;
		r.audio += f.audio.size();
		onFrame(f);
	}
	r.error = imd.error();
	return r;
}

std::string describe(const ImdDecoder &imd) {
	char buf[200];
	std::snprintf(buf, sizeof(buf), "v%d %dx%d at %d,%d, %d frames, %.2f fps, features 0x%03X", imd.version(),
	              imd.width(), imd.height(), imd.defaultX(), imd.defaultY(), imd.frameCount(), imd.frameRate(),
	              imd.features());
	std::string s = buf;
	if (imd.hasSound())
		s += ", sound " + std::to_string(imd.soundFreq()) + " Hz (slices of " + std::to_string(imd.soundSliceSize()) +
		     ", " + std::to_string(imd.soundSlicesCount()) + " first)";
	return s;
}

int list(DataFiles &files) {
	auto videos = gameVideos(files);
	for (const Video &v : videos) {
		ImdDecoder imd;
		if (imd.load(videoData(files, v)))
			std::printf("%-14s %s%s\n", v.name.c_str(), describe(imd).c_str(), v.loose.empty() ? "" : " (loose)");
		else
			std::printf("%-14s ERROR %s\n", v.name.c_str(), imd.error().c_str());
	}
	std::printf("%zu videos\n", videos.size());
	return 0;
}

int checkAll(DataFiles &files) {
	auto videos = gameVideos(files);
	int failed = 0;
	long frames = 0;
	std::vector<uint8_t> canvas;
	for (const Video &v : videos) {
		ImdDecoder imd;
		std::string problem;
		if (!imd.load(videoData(files, v))) {
			problem = imd.error();
		} else {
			Decoded d = decodeAll(imd, canvas, [](const ImdDecoder::Frame &) {});
			frames += d.frames;
			if (!d.error.empty())
				problem = d.error;
			else if (d.frames != imd.frameCount())
				problem = std::to_string(d.frames) + " of " + std::to_string(imd.frameCount()) + " frames";
		}
		if (!problem.empty()) {
			std::printf("%-14s FAILED: %s\n", v.name.c_str(), problem.c_str());
			failed++;
		}
	}
	std::printf("%zu videos, %ld frames decoded, %d failed\n", videos.size(), frames, failed);
	return failed ? 1 : 0;
}

int decode(const std::string &name, std::vector<uint8_t> data, const fs::path &outDir) {
	ImdDecoder imd;
	if (!imd.load(std::move(data))) {
		std::fprintf(stderr, "%s: %s\n", name.c_str(), imd.error().c_str());
		return 1;
	}
	std::printf("%s: %s\n", name.c_str(), describe(imd).c_str());
	fs::create_directories(outDir);
	std::string base = name.substr(0, name.rfind('.'));
	std::vector<uint8_t> canvas;
	std::vector<int8_t> audio;
	int n = 0;
	Decoded d = decodeAll(imd, canvas, [&](const ImdDecoder::Frame &f) {
		audio.insert(audio.end(), f.audio.begin(), f.audio.end());
		Palette pal{};
		for (int i = 0; i < 256; i++) {
			auto c = [&](int k) { return uint8_t(imd.palette()[size_t(i * 3 + k)] * 255 / 63); };
			pal[size_t(i)] = {c(0), c(1), c(2)};
		}
		char file[64];
		std::snprintf(file, sizeof(file), "%s_%04d.bmp", base.c_str(), n++);
		writeBmp(outDir / file, canvas.data(), std::max(imd.width(), 1), std::max(imd.height(), 1), pal);
	});
	if (imd.hasSound())
		writeWav(outDir / (base + ".wav"), audio, imd.soundFreq());
	std::printf("%d frames%s written to %s\n", d.frames, imd.hasSound() ? " and the sound" : "", outDir.string().c_str());
	if (!d.error.empty()) {
		std::fprintf(stderr, "error: %s\n", d.error.c_str());
		return 1;
	}
	return 0;
}

} // namespace

int main(int argc, char **argv) {
	std::string gameKey = takeGameOption(argc, argv);
	if (argc < 3) {
		std::fprintf(stderr,
		             "usage: %s <gamedir> --list | --check-all | NAME[.IMD] <outdir>\n"
		             "       %s <file.imd> <outdir>\n",
		             argv[0], argv[0]);
		return 2;
	}
	fs::path first = argv[1];
	if (fs::is_regular_file(first))
		return decode(upper(first.filename().string()), readFile(first), argv[2]);

	if (!selectToolGame(first, gameKey))
		return 2;
	DataFiles files(first);
	files.openAllArchives();
	std::string arg = argv[2];
	if (arg == "--list")
		return list(files);
	if (arg == "--check-all")
		return checkAll(files);
	if (argc < 4) {
		std::fprintf(stderr, "missing the output folder\n");
		return 2;
	}
	std::string name = upper(arg);
	if (!isImd(name))
		name += ".IMD";
	for (const Video &v : gameVideos(files))
		if (v.name == name)
			return decode(name, videoData(files, v), argv[3]);
	std::fprintf(stderr, "%s: not found\n", name.c_str());
	return 1;
}
