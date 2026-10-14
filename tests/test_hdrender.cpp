// gob2native - HD compositor tests: layers kept in step with the indexed
// drawing, repairs, pack pictures, palette mapping, dirty rectangles.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/draw.h"
#include "engine/hdrender.h"
#include "gfx/hdpack.h"
#include "gfx/png.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
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

namespace {

int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

// Every live HD pixel shows its surface pixel and the mirror agrees with
// the surface (what the compositor relies on).
bool inStep(const Surface &s) {
	const HdLayer *l = s.hd();
	if (!l)
		return false;
	const int n = l->scale;
	for (int y = 0; y < s.height(); y++)
		for (int x = 0; x < s.width(); x++) {
			if (l->nat[size_t(y) * s.width() + x] != s.get(x, y))
				return false;
			for (int j = 0; j < n; j++)
				for (int i = 0; i < n; i++) {
					const size_t o = size_t(y * n + j) * l->w + size_t(x * n + i);
					if (HdLayer::kind(l->px[o]) != HdLayer::Hd && l->idx[o] != s.get(x, y))
						return false;
				}
		}
	return true;
}

VgaPalette testPalette() {
	VgaPalette p{};
	for (int i = 0; i < 256; i++) {
		p[size_t(i) * 3] = uint8_t(i & 63);
		p[size_t(i) * 3 + 1] = uint8_t((i * 7) & 63);
		p[size_t(i) * 3 + 2] = uint8_t(63 - (i & 63));
	}
	return p;
}

uint8_t c8(uint8_t v) { return uint8_t((v << 2) | (v >> 4)); }

uint32_t palColour(const VgaPalette &p, int i) {
	return uint32_t(c8(p[size_t(i) * 3])) | uint32_t(c8(p[size_t(i) * 3 + 1])) << 8 |
	       uint32_t(c8(p[size_t(i) * 3 + 2])) << 16 | 0xFF000000u;
}

void testMirror() {
	Draw d;
	HdRenderer r;
	r.attach(d, nullptr, 3, [] { return std::string("TEST"); });
	CHECK(d.front().hd() && d.back().hd() && d.front().hd()->scale == 3);

	Surface &back = d.back();
	back.fillRect(10, 10, 100, 60, 5);
	back.putPixel(3, 4, 9);
	back.drawLine(0, 199, 319, 0, 12);
	d.createSprite(5, 40, 30);
	Surface &spr = *d.sprite(5);
	spr.fillRect(0, 0, 39, 29, 0);
	spr.fillRect(5, 5, 20, 20, 7);
	spr.putPixel(30, 3, 8);
	back.blit(spr, 0, 0, 39, 29, 50, 40, 0, false);  // transparent
	back.blit(spr, 0, 0, 39, 29, 150, 40, 0, true);  // transparent, mirrored
	back.blit(spr, 2, 2, 30, 25, -10, 190, -1, false); // opaque, clipped
	back.blit(back, 0, 0, 99, 99, 20, 20);             // overlapping self-blit
	d.createSprite(30, 64, 20);                         // a capture
	d.sprite(30)->blit(back, 48, 30, 111, 49, 0, 0);
	back.fillRect(48, 30, 111, 49, 1);
	back.blit(*d.sprite(30), 0, 0, 63, 19, 48, 30);
	d.front().blit(back);
	CHECK(inStep(back));
	CHECK(inStep(spr));
	CHECK(inStep(*d.sprite(30)));
	CHECK(inStep(d.front()));

	// The frame shows the front surface in the screen palette.
	d.vgaPalette = testPalette();
	d.setPalette();
	CHECK(r.update());
	CHECK(r.frameWidth() == 960 && r.frameHeight() == 600);
	bool same = true;
	for (int y = 0; y < 600 && same; y++)
		for (int x = 0; x < 960 && same; x++)
			same = r.frame()[size_t(y) * 960 + x] == palColour(d.vgaPalette, d.front().get(x / 3, y / 3));
	CHECK(same);
	CHECK(r.stats().mode == HdRenderer::Identity && r.stats().lastRepaired == 0);

	// Nothing changed: nothing to upload.
	CHECK(r.update());
	CHECK(r.changedRects().empty());

	// One pixel: one tile.
	d.front().putPixel(40, 9, 3);
	CHECK(r.update());
	auto rects = r.changedRects();
	CHECK(rects.size() == 1 && rects[0].x == 32 * 3 && rects[0].y == 8 * 3 && rects[0].w == 16 * 3 &&
	      rects[0].h == 8 * 3);

	// A change behind the layer's back is repaired from the surface.
	d.front().data()[size_t(100) * 320 + 200] = 77;
	CHECK(r.update());
	CHECK(r.stats().lastRepaired == 1);
	CHECK(r.frame()[size_t(301) * 960 + 601] == palColour(d.vgaPalette, 77));
	CHECK(inStep(d.front()));
	CHECK(r.coverage().repaired == 1);

	// Overlays are uploaded now and recoloured next time.
	CHECK(r.update());
	r.overlay(0, 0, 0, 0);
	CHECK(r.changedRects().size() == 1);
	r.frame()[0] = 0xFF00FF00u;
	CHECK(r.update());
	CHECK(r.frame()[0] == palColour(d.vgaPalette, d.front().get(0, 0)));

	// Detaching drops the layers.
	r.detach();
	CHECK(!d.front().hd() && !d.back().hd() && !d.sprite(5)->hd());
}

void testPackPictures() {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("gobnative_hdrender_" + std::to_string(getpid()));
	fs::remove_all(dir);
	fs::create_directories(dir / "TEST");

	// Picture 1: 4x2, colour 0 on the left half; the pack has it at 2x
	// in solid colours per original pixel.
	Image orig;
	orig.width = 4;
	orig.height = 2;
	orig.pixels = {0, 0, 3, 4, 0, 0, 5, 6};
	std::vector<uint8_t> rgb(8 * 4 * 3);
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 8; x++) {
			uint8_t *p = &rgb[size_t(y * 8 + x) * 3];
			p[0] = uint8_t(x * 30);
			p[1] = uint8_t(y * 60);
			p[2] = 200;
		}
	CHECK(writePng(dir / "TEST" / "1.png", rgb.data(), 8, 4, 3));
	// Picture 2: the same picture at 4x (mixed scales: enlarged... reduced).
	std::vector<uint8_t> rgb4(16 * 8 * 3, 90);
	CHECK(writePng(dir / "TEST" / "2.png", rgb4.data(), 16, 8, 3));

	HdPack pack;
	CHECK(pack.open(dir, [&](const std::string &tot, int id) -> std::optional<Image> {
		if (tot == "TEST" && (id == 1 || id == 2))
			return orig;
		return std::nullopt;
	}));
	CHECK(pack.size() == 2 && pack.maxScale() == 4);

	Draw d;
	HdRenderer r;
	r.attach(d, &pack, 2, [] { return std::string("test"); });
	Surface pic(4, 2);
	std::copy(orig.pixels.begin(), orig.pixels.end(), pic.data());
	r.decorate(pic, 1, true);
	const HdLayer *l = pic.hd();
	CHECK(l && l->scale == 2 && l->w == 8 && l->h == 4);
	if (l) {
		CHECK(HdLayer::kind(l->px[0]) == HdLayer::Hd && l->idx[0] == 0);  // transparent
		CHECK(l->idx[size_t(1) * 8 + 5] == 3 && l->idx[size_t(3) * 8 + 7] == 6);
		CHECK((l->px[size_t(2) * 8 + 6] & 0xFFFFFF) == (uint32_t(180) | uint32_t(120) << 8 | uint32_t(200) << 16));
	}
	// Reduced 4x picture.
	Surface pic2(4, 2);
	std::copy(orig.pixels.begin(), orig.pixels.end(), pic2.data());
	r.decorate(pic2, 2, false);
	CHECK(pic2.hd() && (pic2.hd()->px[0] & 0xFFFFFF) == 0x5A5A5Au && pic2.hd()->idx[0] == 0);
	// A picture the pack lacks: live, enlarged.
	Surface pic3(4, 2);
	std::copy(orig.pixels.begin(), orig.pixels.end(), pic3.data());
	r.decorate(pic3, 9, true);
	CHECK(pic3.hd() && HdLayer::kind(pic3.hd()->px[0]) == HdLayer::Live && inStep(pic3));
	CHECK(r.stats().hdPictures == 2 && r.stats().nativePictures == 1);
	// Room backgrounds only: an animation sheet keeps its original pixels.
	HdRenderOptions rooms;
	rooms.roomsOnly = true;
	r.setOptions(rooms);
	Surface pic4(4, 2), pic5(4, 2);
	std::copy(orig.pixels.begin(), orig.pixels.end(), pic4.data());
	std::copy(orig.pixels.begin(), orig.pixels.end(), pic5.data());
	r.decorate(pic4, 1, true, PictureUse::Animation);
	r.decorate(pic5, 1, true, PictureUse::Screen);
	CHECK(pic4.hd() && HdLayer::kind(pic4.hd()->px[size_t(2) * 8 + 6]) == HdLayer::Live);
	CHECK(pic5.hd() && HdLayer::kind(pic5.hd()->px[size_t(2) * 8 + 6]) == HdLayer::Hd);
	r.setOptions(HdRenderOptions());

	// Drawn transparently into the back surface: HD pixels only where the
	// original is opaque.
	d.back().fillRect(0, 0, 319, 199, 1);
	d.back().blit(pic, 0, 0, 3, 1, 10, 10, 0);
	d.front().blit(d.back());
	const HdLayer *fl = d.front().hd();
	CHECK(HdLayer::kind(fl->px[size_t(20) * fl->w + 20]) == HdLayer::Live);
	CHECK(HdLayer::kind(fl->px[size_t(20) * fl->w + 24]) == HdLayer::Hd);

	// Palette mapping. Settled: the pack's colours.
	const VgaPalette pal = testPalette();
	d.vgaPalette = pal;
	d.setPalette();
	CHECK(r.update() && r.stats().mode == HdRenderer::Identity);
	const uint32_t hdPx = l->px[size_t(2) * 8 + 6]; // R 180, G 120, B 200
	const size_t at = size_t(22) * fl->w + 26;      // the same pixel on screen
	CHECK(r.frame()[at] == (hdPx | 0xFF000000u));
	// Fading out: every channel down by the same amount.
	d.fadeStep(VgaPalette{}, 10);
	CHECK(r.update() && r.stats().mode == HdRenderer::FadeOut);
	const int sub = c8(10);
	CHECK(r.frame()[at] == (uint32_t(180 - sub) | uint32_t(120 - sub) << 8 | uint32_t(200 - sub) << 16 | 0xFF000000u));
	CHECK(r.frame()[0] == palColour(d.screenVga(), 1)); // live pixels: the screen palette
	while (!d.fadeStep(VgaPalette{}, 10)) {
	}
	CHECK(r.update() && r.frame()[at] == 0xFF000000u);
	// Fading in from black (the room palette staged again).
	d.vgaPalette = pal;
	d.fadeStep(pal, 20);
	CHECK(r.update() && r.stats().mode == HdRenderer::FadeIn);
	const int top = c8(20);
	CHECK(r.frame()[at] == (uint32_t(std::min(180, top)) | uint32_t(std::min(120, top)) << 8 |
	                        uint32_t(std::min(200, top)) << 16 | 0xFF000000u));
	// Another palette shown directly: HD detail over the new colour.
	VgaPalette other = pal;
	for (size_t i = 0; i < other.size(); i++)
		other[i] = uint8_t((other[i] + 9) & 63);
	while (!d.fadeStep(other, 63)) {
	}
	CHECK(r.update() && r.stats().mode == HdRenderer::Retint);
	const int idx = l->idx[size_t(2) * 8 + 6];
	auto ch = [&](int v, int c) {
		return std::clamp(v + c8(other[size_t(idx) * 3 + size_t(c)]) - c8(pal[size_t(idx) * 3 + size_t(c)]), 0, 255);
	};
	CHECK(r.frame()[at] == (uint32_t(ch(180, 0)) | uint32_t(ch(120, 1)) << 8 | uint32_t(ch(200, 2)) << 16 | 0xFF000000u));

	r.detach();
	fs::remove_all(dir);
}

void testSmoothNative() {
	Draw d;
	HdRenderer r;
	r.attach(d, nullptr, 4, [] { return std::string(); });
	HdRenderOptions o;
	o.filter = HdSpriteFilter::Scale2x;
	r.setOptions(o);
	Surface pic(8, 8);
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x++)
			pic.putPixel(x, y, x < y ? 5 : 0); // a staircase
	r.decorate(pic, 1, true);
	const HdLayer *l = pic.hd();
	CHECK(l && l->w == 32);
	bool onlyOld = true, rounded = false;
	for (int y = 0; y < 32; y++)
		for (int x = 0; x < 32; x++) {
			const uint8_t v = l->idx[size_t(y) * 32 + x];
			onlyOld &= v == 0 || v == 5;
			rounded |= v != pic.get(x / 4, y / 4);
		}
	CHECK(onlyOld && rounded);
	CHECK(l->nat[size_t(7) * 8] == 5);
	r.detach();
}

// Smoothing filters for pictures without HD art: mixes of the picture's own
// indices, the transparent silhouette exactly the original one.
void testSpriteFilters() {
	Draw d;
	HdRenderer r;
	r.attach(d, nullptr, 4, [] { return std::string(); });
	Surface pic(10, 10);
	for (int y = 0; y < 10; y++)
		for (int x = 0; x < 10; x++)
			pic.putPixel(x, y, x < y ? 5 : (x < y + 3 ? 7 : 0)); // two diagonal bands and transparency
	for (HdSpriteFilter f : {HdSpriteFilter::SmoothEdges, HdSpriteFilter::Smooth}) {
		for (bool transparent : {true, false}) {
			HdRenderOptions o;
			o.filter = f;
			r.setOptions(o);
			Surface p(10, 10);
			p.blit(pic);
			r.decorate(p, 1, transparent);
			const HdLayer *l = p.hd();
			CHECK(l && l->w == 40);
			bool silhouette = true, ownIndices = true, mixed = false;
			for (int y = 0; y < 40; y++)
				for (int x = 0; x < 40; x++) {
					const size_t o2 = size_t(y) * 40 + x;
					const uint8_t orig = pic.get(x / 4, y / 4);
					silhouette &= l->idx[o2] == orig;
					if (HdLayer::kind(l->px[o2]) == HdLayer::Mix) {
						const uint8_t a = l->px[o2] & 0xFF, b = (l->px[o2] >> 8) & 0xFF;
						ownIndices &= (a == 5 || a == 7 || (!transparent && a == 0)) &&
						              (b == 5 || b == 7 || (!transparent && b == 0));
						ownIndices &= !transparent || orig != 0;
						mixed = true;
					} else {
						ownIndices &= HdLayer::kind(l->px[o2]) == HdLayer::Live;
					}
				}
			CHECK(silhouette && ownIndices && mixed);
		}
	}
	// A mix is coloured from the screen palette.
	d.vgaPalette = testPalette();
	d.setPalette();
	CHECK(r.update());
	const uint32_t m = 5 | 7 << 8 | 255u << 16 | uint32_t(HdLayer::Mix) << 24;
	CHECK(r.resolve(m, 5) == palColour(d.vgaPalette, 7));
	CHECK(r.resolve(m & 0xFF00FFFFu, 5) == palColour(d.vgaPalette, 5));
	r.detach();
}

// The same drawing with the pack attached from the start, and attached
// (or switched to) afterwards: rebuilding the layers from the pixel sources
// must give exactly the layers the drawing made.
void testRebuild() {
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("gobnative_rebuild_" + std::to_string(getpid()));
	fs::remove_all(dir);
	fs::create_directories(dir / "TEST");
	// Picture 1: a 6x4 sprite with transparent corners; picture 2: an 8x6
	// background. Both in the pack at 2x with a colour per HD pixel.
	Image spr, bg;
	spr.width = 6, spr.height = 4;
	spr.pixels = {0, 3, 3, 4, 4, 0, 3, 5, 5, 6, 6, 4, 3, 5, 7, 7, 6, 4, 0, 3, 3, 4, 4, 0};
	bg.width = 8, bg.height = 6;
	for (int i = 0; i < 48; i++)
		bg.pixels.push_back(uint8_t(10 + i % 7));
	auto writeHd = [&](const Image &img, int id) {
		std::vector<uint8_t> rgb(size_t(img.width) * img.height * 12);
		for (size_t i = 0; i < rgb.size(); i++)
			rgb[i] = uint8_t(i * 37 + id * 101);
		CHECK(writePng(dir / "TEST" / (std::to_string(id) + ".png"), rgb.data(), img.width * 2, img.height * 2, 3));
	};
	writeHd(spr, 1);
	writeHd(bg, 2);
	auto resOf = [](const Image &img) {
		Resource r;
		r.width = img.width;
		r.height = img.height;
		r.data = {1, 2, 2};
		r.data.insert(r.data.end(), img.pixels.begin(), img.pixels.end());
		return r;
	};
	for (HdAlpha alpha : {HdAlpha::Nearest, HdAlpha::Smooth}) {
		HdPackOptions opt;
		opt.alpha = alpha;
		HdPack pack;
		CHECK(pack.open(dir, [&](const std::string &tot, int id) -> std::optional<Image> {
			if (tot == "TEST" && id == 1)
				return spr;
			if (tot == "TEST" && id == 2)
				return bg;
			return std::nullopt;
		}, opt));
		auto drawScene = [&](Draw &d) {
			d.scriptName = [] { return std::string("TEST"); };
			Surface &back = d.back();
			back.fillRect(0, 0, 319, 199, 2);
			for (int x = 0; x < 300; x += 7) {
				auto b = d.decodePicture(resOf(bg), 2, false, PictureUse::Screen);
				back.blit(*b, 0, 0, 7, 5, x, x / 3);
			}
			d.createSprite(19, 6, 4); // a picture sprite, as loadAnim makes them
			d.sprite(19)->blit(*d.decodePicture(resOf(spr), 1, true, PictureUse::Animation));
			d.createSprite(draw::kAnimSurface, 320, 200);
			for (int i = 0; i < 40; i++) // overlapping, mirrored, clipped sprites
				back.blit(*d.sprite(19), 0, 0, 5, 3, (i * 13) % 330 - 4, (i * 5) % 70, 0, i % 3 == 0);
			d.sprite(draw::kAnimSurface)->blit(back); // the snapshot
			back.putPixel(30, 30, 9);
			back.fillRect(50, 10, 60, 12, 1);
			for (int i = 0; i < 20; i++) // more sprites, and erasing from the snapshot
				back.blit(*d.sprite(19), 0, 0, 5, 3, (i * 7) % 100, 20 + i % 9, 0, i % 2 == 0);
			back.blit(*d.sprite(draw::kAnimSurface), 10, 20, 40, 30, 10, 20);
			d.front().blit(back);
		};
		auto same = [](const Surface &a, const Surface &b) {
			const HdLayer *x = a.hd(), *y = b.hd();
			return x && y && x->scale == y->scale && x->px == y->px && x->idx == y->idx && x->nat == y->nat;
		};

		Draw ref;
		ref.setTrackSources(true);
		HdRenderer rr;
		rr.attach(ref, &pack, 2, [] { return std::string("TEST"); });
		drawScene(ref);

		Draw d; // drawn without a pack, then switched
		d.setTrackSources(true);
		drawScene(d);
		HdRenderer r;
		r.attach(d, &pack, 2, [] { return std::string("TEST"); });
		CHECK(r.stats().rebuildPictures == 2 && r.stats().rebuildLost == 0);
		CHECK(same(d.front(), ref.front()));
		CHECK(same(d.back(), ref.back()));
		CHECK(same(*d.sprite(19), *ref.sprite(19)));
		CHECK(same(*d.sprite(draw::kAnimSurface), *ref.sprite(draw::kAnimSurface)));

		// Another scale and back.
		r.detach();
		r.attach(d, nullptr, 3, [] { return std::string("TEST"); });
		CHECK(d.front().hd() && d.front().hd()->scale == 3 && inStep(d.front()));
		r.attach(d, &pack, 2, [] { return std::string("TEST"); });
		CHECK(same(d.front(), ref.front()) && same(d.back(), ref.back()));

		// A pixel changed behind the operations' back is shown live.
		d.back().data()[size_t(4) * 320 + 5] ^= 0x55; // a background pixel
		r.rebuild();
		CHECK(r.stats().rebuildLost == 1 && inStep(d.back()));
		rr.detach();
		r.detach();
	}
	fs::remove_all(dir);
}

} // namespace

// Goblins 3: a 472x200 screen with a 40-row split, scrolled. The frame shows
// the view (the scrolled window and the unscrolled bar), kept up to date
// from the front surface's changed tiles, and follows scroll changes.
void testScrolling() {
	Draw d;
	HdRenderer r;
	r.attach(d, nullptr, 2, [] { return std::string("TEST"); });
	d.initScreen(472, 200, 40);
	Surface &front = d.front();
	CHECK(front.width() == 472 && d.back().width() == 472 && front.hd() && front.hd()->trackDirty);
	for (int x = 0; x < 472; x++)
		front.fillRect(x, 0, x, 199, uint8_t(x < 320 ? x & 0xFF : x - 200));
	front.fillRect(0, 160, 471, 199, 0);
	front.fillRect(10, 170, 30, 180, 9); // the bar
	d.vgaPalette = testPalette();
	d.setPalette();
	auto expect = [&](int x, int y) { // display pixel -> front pixel
		int fx = x, fy = y;
		d.displayToSurface(fx, fy);
		return palColour(d.vgaPalette, front.get(fx, fy));
	};
	auto matches = [&]() {
		for (int y = 0; y < 400; y++)
			for (int x = 0; x < 640; x++)
				if (r.frame()[size_t(y) * 640 + x] != expect(x / 2, y / 2))
					return false;
		return true;
	};
	d.viewX = 100;
	CHECK(r.update() && r.frameWidth() == 640 && matches());
	CHECK(r.frame()[size_t(20) * 640 + 0] == palColour(d.vgaPalette, 100)); // column 100
	CHECK(r.frame()[size_t(350) * 640 + 40] == palColour(d.vgaPalette, 9));  // the bar, unscrolled

	// A change in the shown window: only its tiles.
	front.fillRect(150, 50, 160, 60, 7);
	front.fillRect(400, 50, 410, 60, 7); // not shown: column 400 is beyond 100 + 319
	CHECK(r.update() && matches());
	auto rects = r.changedRects();
	CHECK(!rects.empty() && rects.size() <= 2);
	CHECK(r.frame()[size_t(110) * 640 + 110] == palColour(d.vgaPalette, 7));

	// Scrolling to the end shows the rest.
	d.viewX = 152;
	CHECK(r.update() && matches());
	CHECK(r.frame()[size_t(110) * 640 + (405 - 152) * 2] == palColour(d.vgaPalette, 7));
	CHECK(inStep(d.screenSurface()));

	// Back to a plain 320x200 screen: the front surface is shown itself.
	d.initScreen();
	CHECK(&d.screenSurface() == &d.front() && d.front().width() == 320);
	d.front().fillRect(0, 0, 319, 199, 4);
	CHECK(r.update() && r.frame()[0] == palColour(d.vgaPalette, 4));
}

int main() {
	testMirror();
	testPackPictures();
	testSmoothNative();
	testRebuild();
	testSpriteFilters();
	testScrolling();
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("all hdrender tests passed\n");
	return 0;
}
