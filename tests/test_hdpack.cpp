// gob2native - PNG reader, edge smoothing and HD pack loader tests (no game data)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/hdpack.h"
#include "gfx/png.h"
#include "gfx/smooth.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include <vector>

using namespace gob;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// --- handcrafted PNG files ---------------------------------------------------

static void put32be(std::vector<uint8_t> &out, uint32_t v) {
	for (int s = 24; s >= 0; s -= 8)
		out.push_back(uint8_t(v >> s));
}

static void chunk(std::vector<uint8_t> &png, const char *type, const std::vector<uint8_t> &body) {
	put32be(png, uint32_t(body.size()));
	size_t start = png.size();
	png.insert(png.end(), type, type + 4);
	png.insert(png.end(), body.begin(), body.end());
	put32be(png, crc32(&png[start], body.size() + 4));
}

// A zlib stream of stored (uncompressed) deflate blocks: independent of the
// library's own compressor.
static std::vector<uint8_t> zlibStored(const std::vector<uint8_t> &data) {
	std::vector<uint8_t> z = {0x78, 0x01};
	size_t pos = 0;
	do {
		size_t n = std::min<size_t>(data.size() - pos, 65535);
		bool last = pos + n == data.size();
		z.push_back(last ? 1 : 0);
		z.push_back(uint8_t(n));
		z.push_back(uint8_t(n >> 8));
		z.push_back(uint8_t(~n));
		z.push_back(uint8_t(~n >> 8));
		z.insert(z.end(), data.begin() + long(pos), data.begin() + long(pos + n));
		pos += n;
	} while (pos < data.size());
	put32be(z, adler32(data.data(), data.size()));
	return z;
}

struct Extra {
	std::vector<uint8_t> plte, trns;
};

// `raw` is the filtered scanline data (filter byte per row, all passes).
static std::vector<uint8_t> makePng(int w, int h, int depth, int colorType, bool interlace,
                                    const std::vector<uint8_t> &raw, const Extra &extra = Extra()) {
	std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
	std::vector<uint8_t> ihdr;
	put32be(ihdr, uint32_t(w));
	put32be(ihdr, uint32_t(h));
	ihdr.push_back(uint8_t(depth));
	ihdr.push_back(uint8_t(colorType));
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(interlace ? 1 : 0);
	chunk(png, "IHDR", ihdr);
	if (!extra.plte.empty())
		chunk(png, "PLTE", extra.plte);
	if (!extra.trns.empty())
		chunk(png, "tRNS", extra.trns);
	std::vector<uint8_t> z = zlibStored(raw);
	// Split the data over two IDAT chunks.
	size_t half = z.size() / 2;
	chunk(png, "IDAT", std::vector<uint8_t>(z.begin(), z.begin() + long(half)));
	chunk(png, "IDAT", std::vector<uint8_t>(z.begin() + long(half), z.end()));
	chunk(png, "IEND", {});
	return png;
}

static const uint8_t *px(const PngImage &img, int x, int y) {
	return &img.rgba[(size_t(y) * size_t(img.info.width) + size_t(x)) * 4];
}

static bool rgbaIs(const PngImage &img, int x, int y, int r, int g, int b, int a) {
	const uint8_t *p = px(img, x, y);
	return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
}

static void testRoundTrip() {
	for (int channels = 1; channels <= 4; channels++)
		for (auto [w, h] : {std::pair{1, 1}, {17, 5}, {64, 33}}) {
			std::vector<uint8_t> pixels(size_t(w) * size_t(h) * size_t(channels));
			uint32_t seed = 12345u + uint32_t(channels * 7 + w);
			for (size_t i = 0; i < pixels.size(); i++) {
				seed = seed * 1103515245u + 12345u;
				// Mix of smooth gradients and noise, so every filter gets chosen.
				pixels[i] = (i / size_t(channels)) % 3 == 0 ? uint8_t(seed >> 24) : uint8_t(i * 3);
			}
			std::vector<uint8_t> png = encodePng(pixels.data(), w, h, channels);
			PngImage img;
			std::string err;
			CHECK(decodePng(png.data(), png.size(), img, &err));
			CHECK(img.info.width == w && img.info.height == h && img.info.bitDepth == 8);
			bool same = img.rgba.size() == size_t(w) * size_t(h) * 4;
			for (int i = 0; same && i < w * h; i++) {
				const uint8_t *s = &pixels[size_t(i) * size_t(channels)];
				const uint8_t *d = &img.rgba[size_t(i) * 4];
				switch (channels) {
				case 1: same = d[0] == s[0] && d[1] == s[0] && d[2] == s[0] && d[3] == 255; break;
				case 2: same = d[0] == s[0] && d[1] == s[0] && d[2] == s[0] && d[3] == s[1]; break;
				case 3: same = d[0] == s[0] && d[1] == s[1] && d[2] == s[2] && d[3] == 255; break;
				case 4: same = std::memcmp(d, s, 4) == 0; break;
				}
			}
			CHECK(same);
			PngInfo info;
			CHECK(readPngInfo(png.data(), png.size(), info));
			CHECK(info.width == w && info.height == h);
		}
}

static void testHandcrafted() {
	std::string err;
	PngImage img;

	// 1-bit palette with tRNS: 10x2, palette 0 = transparent red, 1 = blue.
	{
		Extra ex;
		ex.plte = {255, 0, 0, 0, 0, 255};
		ex.trns = {0};
		std::vector<uint8_t> raw = {0, 0b10110000, 0b01000000, 0, 0b00000000, 0b11000000};
		std::vector<uint8_t> png = makePng(10, 2, 1, 3, false, raw, ex);
		CHECK(decodePng(png.data(), png.size(), img, &err));
		CHECK(img.info.hasAlpha);
		CHECK(rgbaIs(img, 0, 0, 0, 0, 255, 255));
		CHECK(rgbaIs(img, 1, 0, 255, 0, 0, 0));
		CHECK(rgbaIs(img, 2, 0, 0, 0, 255, 255));
		CHECK(rgbaIs(img, 9, 0, 0, 0, 255, 255));
		CHECK(rgbaIs(img, 8, 1, 0, 0, 255, 255));
		CHECK(rgbaIs(img, 7, 1, 255, 0, 0, 0));
	}
	// 4-bit gray, 3x1: values 0, 15, 8 -> 0, 255, 136.
	{
		std::vector<uint8_t> raw = {0, 0x0F, 0x80};
		std::vector<uint8_t> png = makePng(3, 1, 4, 0, false, raw);
		CHECK(decodePng(png.data(), png.size(), img, &err));
		CHECK(rgbaIs(img, 0, 0, 0, 0, 0, 255));
		CHECK(rgbaIs(img, 1, 0, 255, 255, 255, 255));
		CHECK(rgbaIs(img, 2, 0, 136, 136, 136, 255));
	}
	// 16-bit RGB with tRNS on one colour, and filter 1 (sub) on the 2nd row.
	{
		std::vector<uint8_t> raw = {0, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0, 1, 0, 0, 0, 0,
		                            1,    0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
		Extra ex;
		ex.trns = {0, 1, 0, 0, 0, 0};
		std::vector<uint8_t> png = makePng(2, 2, 16, 2, false, raw, ex);
		CHECK(decodePng(png.data(), png.size(), img, &err));
		CHECK(rgbaIs(img, 0, 0, 0x12, 0x56, 0x9A, 255));
		CHECK(rgbaIs(img, 1, 0, 0x00, 0x00, 0x00, 0)); // (1, 0, 0) = the tRNS colour
		CHECK(rgbaIs(img, 0, 1, 0x01, 0x02, 0x03, 255));
		CHECK(rgbaIs(img, 1, 1, 0x01, 0x02, 0x03, 255)); // sub: same as the left pixel
	}
	// Adam7 interlaced 8-bit gray, 3x3 with pixel value 10*y + x: pass 1
	// holds (0,0); pass 4 (2,0); pass 5 (0,2), (2,2); pass 6 (1,0), (1,2);
	// pass 7 row 1.
	{
		std::vector<uint8_t> raw = {0, 0,  0, 2,  0, 20, 22, 0, 1, 0, 21, 0, 10, 11, 12};
		std::vector<uint8_t> png = makePng(3, 3, 8, 0, true, raw);
		CHECK(decodePng(png.data(), png.size(), img, &err));
		bool ok = true;
		for (int y = 0; y < 3; y++)
			for (int x = 0; x < 3; x++)
				ok &= px(img, x, y)[0] == 10 * y + x;
		CHECK(ok);
	}
	// 8-bit gray+alpha with filters 2 (up), 3 (average) and 4 (Paeth).
	{
		std::vector<uint8_t> raw = {0, 100, 200, 50, 60,
		                            2, 1,   2,   3,  4,
		                            3, 10,  10,  10, 10,
		                            4, 0,   0,   0,  0};
		std::vector<uint8_t> png = makePng(2, 4, 8, 4, false, raw);
		CHECK(decodePng(png.data(), png.size(), img, &err));
		CHECK(rgbaIs(img, 0, 1, 101, 101, 101, 202));
		CHECK(rgbaIs(img, 1, 1, 53, 53, 53, 64));
		// average: (left + up) / 2 + raw
		CHECK(rgbaIs(img, 0, 2, 60, 60, 60, 111));
		CHECK(rgbaIs(img, 1, 2, (60 + 53) / 2 + 10, (60 + 53) / 2 + 10, (60 + 53) / 2 + 10, (111 + 64) / 2 + 10));
		// Paeth with zero residuals repeats the row above here.
		CHECK(px(img, 0, 3)[0] == 60 && px(img, 0, 3)[3] == 111);
	}
	// Corruption is refused.
	{
		std::vector<uint8_t> raw = {0, 1, 2, 3};
		std::vector<uint8_t> png = makePng(1, 1, 8, 2, false, raw);
		std::vector<uint8_t> bad = png;
		bad[bad.size() - 20] ^= 0x40; // inside the last IDAT
		CHECK(!decodePng(bad.data(), bad.size(), img, &err));
		CHECK(!decodePng(png.data(), 20, img, &err));
		std::vector<uint8_t> notPng(png.begin(), png.end());
		notPng[1] = 'X';
		CHECK(!decodePng(notPng.data(), notPng.size(), img, &err));
		CHECK(!decodePng(png.data(), png.size(), img, &err, 0)); // over the pixel limit
	}
}

// zlib.compress(data, 9) of Python 3 (dynamic Huffman block), data below.
static const uint8_t kDynamic[] = {
#include "test_hdpack_zlib.inc"
};

static void testDynamicHuffman() {
	std::vector<uint8_t> want;
	for (int i = 0; i < 600; i++)
		want.push_back(uint8_t(i * 7 + (i >> 3)));
	for (int i = 0; i < 30; i++)
		for (char c : std::string("abcabcabd"))
			want.push_back(uint8_t(c));
	for (int i = 0; i < 256; i++)
		want.push_back(uint8_t(i));
	CHECK(((kDynamic[2] >> 1) & 3) == 2);
	std::vector<uint8_t> out;
	CHECK(zlibDecompress(kDynamic, sizeof(kDynamic), out, 1 << 20));
	CHECK(out == want);
	CHECK(!zlibDecompress(kDynamic, sizeof(kDynamic), out, 100)); // output limit
	std::vector<uint8_t> bad(kDynamic, kDynamic + sizeof(kDynamic));
	bad[bad.size() - 1] ^= 1; // Adler-32
	CHECK(!zlibDecompress(bad.data(), bad.size(), out, 1 << 20));
	// And the library's own compressor.
	std::vector<uint8_t> z = zlibCompress(want.data(), want.size());
	CHECK(zlibDecompress(z.data(), z.size(), out, 1 << 20) && out == want);
}

static void testSmoothing() {
	// A one-pixel diagonal stays connected and becomes a straight band:
	// at 8x, every row of the band is covered without gaps.
	const uint8_t diag[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	std::vector<uint8_t> up = smoothUpscaleMask(diag, 4, 4, 8);
	bool connected = true;
	for (int y = 4; y < 28; y++) {
		int count = 0;
		for (int x = 0; x < 32; x++)
			count += up[size_t(y) * 32 + size_t(x)] != 0;
		connected &= count >= 6 && count <= 14;
	}
	CHECK(connected);
	CHECK(up[0] == 0);                    // the outer corner of the end pixel is rounded off
	CHECK(up[size_t(4) * 32 + 4] == 255); // pixel centres stay
	CHECK(up[size_t(8) * 32 + 8] == 255); // the joint between two pixels is filled
	// The staircase is filled: the clear pixel (1, 0) gets its lower-left
	// corner, the set pixel (0, 0) loses its upper-right one.
	CHECK(up[size_t(7) * 32 + 9] == 255);
	CHECK(up[size_t(0) * 32 + 7] == 0);

	// A solid block keeps its edges straight, with slightly rounded corners.
	const uint8_t block[9] = {1, 1, 1, 1, 1, 1, 1, 1, 1};
	std::vector<uint8_t> b = smoothUpscaleMask(block, 3, 3, 4);
	CHECK(b[0] == 0 && b[1] == 255 && b[size_t(5) * 12] == 255);

	std::vector<uint8_t> n = nearestUpscale(diag, 4, 4, 2);
	CHECK(n.size() == 64 && n[0] == 1 && n[1] == 1 && n[8] == 1 && n[2] == 0);

	// Distance field: inside > 128 > outside, outline near 128.
	std::vector<uint8_t> sdf = glyphDistanceField(block, 3, 3, 8, 1.0);
	CHECK(sdf.size() == 24 * 24);
	CHECK(sdf[size_t(12) * 24 + 12] == 255); // centre: 1.5 px inside, clamped
	CHECK(sdf[size_t(12) * 24 + 0] > 128 && sdf[size_t(12) * 24 + 0] < 150); // 1/16 px inside the edge
	const uint8_t dot[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0};
	std::vector<uint8_t> d = glyphDistanceField(dot, 3, 3, 8, 1.0);
	CHECK(d[0] < 60);                       // the corner of the cell is far outside
	CHECK(d[size_t(12) * 24 + 12] > 165);   // near the dot's centre
}

// --- HD pack -------------------------------------------------------------------

static std::map<std::string, Image> originals;

static Image makeImage(int w, int h, std::initializer_list<int> px) {
	Image img;
	img.width = w;
	img.height = h;
	for (int v : px)
		img.pixels.push_back(uint8_t(v));
	img.pixels.resize(size_t(w) * size_t(h), 1);
	return img;
}

static std::optional<Image> provide(const std::string &tot, int id) {
	auto it = originals.find(tot + "/" + std::to_string(id));
	if (it == originals.end())
		return std::nullopt;
	return it->second;
}

// Writes `w` x `h` RGB with colour (x, y, 77).
static void writeRgb(const fs::path &p, int w, int h) {
	std::vector<uint8_t> rgb;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			rgb.push_back(uint8_t(x));
			rgb.push_back(uint8_t(y));
			rgb.push_back(77);
		}
	fs::create_directories(p.parent_path());
	CHECK(writePng(p, rgb.data(), w, h, 3));
}

static bool hasIssue(const std::vector<HdIssue> &issues, HdIssue::Severity s, const std::string &file,
                     const std::string &text) {
	for (const HdIssue &i : issues)
		if (i.severity == s && i.file == file && i.message.find(text) != std::string::npos)
			return true;
	return false;
}

static void testPack() {
	const fs::path root = fs::temp_directory_path() / ("gobnative_test_hdpack_" + std::to_string(getpid()));
	fs::remove_all(root);
	const fs::path pack = root / "Test Pack";

	originals["TEST/1"] = makeImage(4, 3, {0, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0});
	originals["TEST/2"] = makeImage(2, 2, {1, 1, 1, 1});
	originals["TEST/3"] = makeImage(3, 3, {0, 5, 0, 5, 5, 5, 0, 5, 0});
	originals["OTHER/9"] = originals["TEST/3"];      // identical picture elsewhere
	originals["TEST/5"] = makeImage(2, 1, {0, 1});
	originals["TEST/6"] = makeImage(2, 2, {1, 2, 3, 4});

	writeRgb(pack / "TEST" / "1.png", 16, 12);       // 4x
	writeRgb(pack / "TEST" / "2.png", 5, 5);         // wrong size
	writeRgb(pack / "test" / "3.png", 6, 6);         // 2x (folder case doesn't matter)
	writeRgb(pack / "TEST" / "4.png", 8, 8);         // no such original
	writeRgb(pack / "TEST" / "5.png", 4, 2);         // 2x, with an upscaled mask:
	{
		const uint8_t mask[8] = {255, 0, 255, 0, 0, 0, 255, 255};
		CHECK(writePng(pack / "TEST" / "5.mask.png", mask, 4, 2, 1));
	}
	writeRgb(pack / "TEST" / "6.png", 6, 6);         // 3x: a mixed scale
	writeRgb(pack / "TEST" / "6.mask.png", 5, 5);    // mask of the wrong size
	writeRgb(pack / "loose.png", 4, 4);              // not in a TOT folder
	writeRgb(pack / "TEST" / "abc.png", 4, 4);       // not a number
	std::ofstream(pack / "TEST" / "1.json") << "{}";
	std::ofstream(pack / "notes.bin") << "x";
	{
		std::ofstream bad(pack / "TEST" / "7.png", std::ios::binary);
		bad << "not a png";
	}

	CHECK(HdPack::list(root) == std::vector<std::string>{"Test Pack"});

	HdPack hd;
	CHECK(hd.open(pack, provide));
	CHECK(hd.isOpen() && hd.name() == "Test Pack");
	auto issues = hd.issues();
	for (const HdIssue &i : issues)
		std::printf("  issue %d %s: %s\n", int(i.severity), i.file.c_str(), i.message.c_str());
	CHECK(hasIssue(issues, HdIssue::Error, "TEST/2.png", "wrong size 5x5"));
	CHECK(hasIssue(issues, HdIssue::Error, "TEST/4.png", "no picture TEST/4"));
	CHECK(hasIssue(issues, HdIssue::Error, "TEST/7.png", "can't read"));
	CHECK(hasIssue(issues, HdIssue::Warning, "loose.png", "<TOT>/<id>.png"));
	CHECK(hasIssue(issues, HdIssue::Warning, "TEST/abc.png", "not a resource number"));
	CHECK(hasIssue(issues, HdIssue::Warning, "TEST/6.mask.png", "differs"));
	CHECK(hasIssue(issues, HdIssue::Info, "", "mixed scales"));
	CHECK(hasIssue(issues, HdIssue::Info, "notes.bin", "ignored"));
	CHECK(!hasIssue(issues, HdIssue::Info, "TEST/1.json", ""));
	CHECK(hd.errorCount() == 3);
	CHECK(hd.size() == 4); // 1, 3, 5, 6
	CHECK(hd.scale() == 2 && hd.mixedScales() && hd.maxScale() == 4);
	CHECK(hd.contains({"TEST", 3}) && !hd.contains({"TEST", 2}));

	// Picture 1 at 4x: colours from the file, alpha from the original mask.
	auto img = hd.find("test", 1);
	CHECK(img && img->width == 16 && img->height == 12 && img->scale == 4 && !img->maskFromPack);
	if (img) {
		auto at = [&](int x, int y) { return &img->rgba[(size_t(y) * 16 + size_t(x)) * 4]; };
		CHECK(at(5, 7)[0] == 5 && at(5, 7)[1] == 7 && at(5, 7)[2] == 77);
		bool alphaOk = true;
		const Image &o = originals["TEST/1"];
		for (int y = 0; y < 12; y++)
			for (int x = 0; x < 16; x++)
				alphaOk &= at(x, y)[3] == (o.pixels[size_t(y / 4) * 4 + size_t(x / 4)] ? 255 : 0);
		CHECK(alphaOk);
		CHECK(img->key == (HdKey{"TEST", 1}) && img->source == img->key);
	}
	CHECK(hd.find("TEST", 1) == img); // cached
	CHECK(hd.stats().hits == 1 && hd.stats().decodes == 1);

	// Picture 5 uses its upscaled mask.
	auto m = hd.find("TEST", 5);
	CHECK(m && m->maskFromPack);
	if (m)
		CHECK(m->rgba[3] == 255 && m->rgba[7] == 0 && m->rgba[4 * 4 + 3] == 0 && m->rgba[7 * 4 + 3] == 255);

	// OTHER/9 is not in the pack but identical to TEST/3.
	auto f = hd.find("OTHER", 9);
	CHECK(f && f->source == (HdKey{"TEST", 3}) && f->key == (HdKey{"OTHER", 9}) && f->scale == 2);
	CHECK(hd.stats().fallbacks == 1);
	CHECK(!hd.find("TEST", 2));  // refused at open
	CHECK(!hd.find("NONE", 1));  // nothing anywhere

	// The memory cap evicts the least recently used pictures, but images
	// handed out stay valid.
	HdPack::Options opt = hd.options();
	opt.memoryCap = 16 * 12 * 4 + 10;
	hd.setOptions(opt);
	auto a = hd.find("TEST", 1);
	auto b = hd.find("TEST", 3);
	auto c = hd.find("TEST", 6);
	CHECK(a && b && c);
	CHECK(hd.stats().evictions >= 1 && hd.stats().cachedBytes <= opt.memoryCap);
	CHECK(a->rgba.size() == 16 * 12 * 4); // still usable after eviction

	// Smooth alpha: the rounded outer corner of the plus-shaped picture 3.
	opt.alpha = HdPack::Alpha::Smooth;
	opt.memoryCap = size_t(64) << 20;
	hd.setOptions(opt);
	auto s = hd.find("TEST", 3);
	CHECK(s && s->rgba[(size_t(1) * 6 + 2) * 4 + 3] == 255 && s->rgba[(size_t(0) * 6 + 0) * 4 + 3] == 0);

	// compose() refuses non-integer scales.
	HdImage out;
	std::string err;
	std::vector<uint8_t> rgba(size_t(10) * 10 * 4);
	CHECK(!HdPack::compose(originals["TEST/2"], rgba.data(), 10, 10, nullptr, opt, out, &err)); // 5x
	CHECK(HdPack::compose(originals["TEST/2"], rgba.data(), 8, 8, nullptr, opt, out, &err) && out.scale == 4);
	CHECK(HdPack::scaleOf(640, 400, 320, 200) == 2 && HdPack::scaleOf(640, 600, 320, 200) == 0 &&
	      HdPack::scaleOf(2560, 1600, 320, 200) == 8 && HdPack::scaleOf(320, 200, 320, 200) == 1 &&
	      HdPack::scaleOf(1600, 1000, 320, 200) == 0);

	hd.close();
	CHECK(!hd.isOpen() && !hd.find("TEST", 1));
	CHECK(!hd.open(root / "missing", provide));
	fs::remove_all(root);
}

int main() {
	testRoundTrip();
	testHandcrafted();
	testDynamicHuffman();
	testSmoothing();
	testPack();
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("all hdpack tests passed\n");
	return 0;
}
