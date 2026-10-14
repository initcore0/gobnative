// gob2native - alignment guide for repainting a room (gobrun --guide-at)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "roomguide.h"

#include "gfx/hdpack.h"
#include "gfx/pixelsource.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace gob::guide {

namespace {

tool::LabelFont gameFont(const Draw &draw) {
	tool::LabelFont lf;
	for (int i = 0; i < Draw::kFontCount; i++) {
		const Font *f = draw.font(i);
		if (!f || !f->monospaced() || f->width() > 8 || f->height() > 8)
			continue;
		lf.w = f->width();
		lf.h = f->height();
		for (int c = 32; c < 256; c++)
			if (f->hasChar(uint8_t(c)))
				lf.glyphs[uint8_t(c)] = f->glyphBits8(uint8_t(c));
		if (lf.valid())
			break;
	}
	return lf;
}

// The pieces of frame `frame` of `layer` of the anim in `slot`, placed at
// the object's position (1000: the layer's own), as Scenery::updateAnim
// composes them.
std::vector<PieceUse> framePieces(const Scenery &sc, int slot, int layer, int frame, int x, int y) {
	std::vector<PieceUse> out;
	const Scenery::Layer *l = sc.layer(slot, layer);
	if (!l || frame < 0 || size_t(frame) >= l->frameStart.size())
		return out;
	const Scenery::Anim &a = sc.anim(slot);
	for (size_t i = l->frameStart[size_t(frame)]; i < l->pieces.size(); i++) {
		const Scenery::Piece &pc = l->pieces[i];
		const int hx = (pc.pictByte >> 6) & 3, hy = (pc.pictByte >> 4) & 3;
		int dx = pc.destX >= 0 ? pc.destX + hx * 128 : pc.destX - hx * 128;
		int dy = pc.destY >= 0 ? pc.destY + hy * 128 : pc.destY - hy * 128;
		dx += x == 1000 ? l->posX : x;
		dy += y == 1000 ? l->posY : y;
		const unsigned pict = unsigned((pc.pictByte & 15) - 1);
		if (pict < unsigned(a.pictCount) && pict < a.pieces.size() && pc.pieceIndex < a.pieces[pict].size()) {
			const Scenery::PieceRect r = a.pieces[pict][pc.pieceIndex];
			if (r.left <= r.right && r.top <= r.bottom)
				out.push_back({int(pict), a.pictResId[pict], pc.pieceIndex, r.left, r.top, r.right, r.bottom, dx, dy});
		}
		if (pc.notFinal != 1)
			break;
	}
	return out;
}

std::string rectJson(int l, int t, int r, int b) {
	return "[" + std::to_string(l) + ", " + std::to_string(t) + ", " + std::to_string(r) + ", " + std::to_string(b) + "]";
}

std::string scaledRectJson(int l, int t, int r, int b, int n) {
	return rectJson(l * n, t * n, (r + 1) * n - 1, (b + 1) * n - 1);
}

} // namespace

Capture capture(Interpreter &interp) {
	Capture cap;
	Draw &draw = interp.draw();
	const Surface &front = draw.front();
	cap.tot = interp.currentLevel().empty() ? interp.currentTot() : interp.currentLevel();
	cap.w = front.width();
	cap.h = front.height();
	cap.indices.assign(front.data(), front.data() + size_t(cap.w) * size_t(cap.h));
	cap.palette = interp.screenPalette();
	cap.font = gameFont(draw);
	for (int id = 1;; id++) {
		const Draw::PictureSource *p = draw.pictureSource(id);
		if (!p)
			break;
		cap.pictures.push_back({p->tot, p->resId, p->use, p->width, p->height});
	}
	if (const PixelSources *src = front.sources())
		cap.sources = src->top;
	const size_t n = size_t(cap.w) * size_t(cap.h);
	cap.owner.assign(n, -1);

	// The source picture of a screen pixel, if it is an opaque pixel of a
	// picture.
	auto sourcePicture = [&](size_t i) -> const Capture::Picture * {
		if (cap.sources.empty())
			return nullptr;
		const uint32_t v = cap.sources[i];
		if (!PixelSources::isRef(v) || PixelSources::isClear(v))
			return nullptr;
		const int id = PixelSources::picture(v);
		return id >= 1 && size_t(id) <= cap.pictures.size() ? &cap.pictures[size_t(id) - 1] : nullptr;
	};
	for (size_t i = 0; i < n; i++)
		if (const Capture::Picture *p = sourcePicture(i))
			if (p->use == PictureUse::Animation)
				cap.owner[i] = -2;

	const Scenery &sc = interp.scenery();
	std::vector<Interpreter::ObjectInfo> infos = interp.objects();
	std::stable_sort(infos.begin(), infos.end(), [](const auto &a, const auto &b) { return a.order < b.order; });
	for (const Interpreter::ObjectInfo &o : infos) {
		if (o.left < 0 || o.right < o.left || o.bottom < o.top)
			continue;
		Object obj;
		obj.index = o.index;
		obj.goblin = o.isGoblin;
		obj.slot = o.drawnAnimation;
		obj.layer = o.drawnLayer;
		obj.frame = o.drawnFrame;
		obj.order = o.order;
		obj.x = o.x;
		obj.y = o.y;
		obj.left = o.left;
		obj.top = o.top;
		obj.right = o.right;
		obj.bottom = o.bottom;
		if (obj.slot >= 0 && obj.slot < Scenery::kAnimCount && sc.anim(obj.slot).pictCount > 0)
			obj.animRes = sc.anim(obj.slot).resId;
		obj.pieces = framePieces(sc, obj.slot, obj.layer, obj.frame, obj.x, obj.y);
		// The drawn rect is the pieces' bounding box (unless clipped): if the
		// object moved after it was drawn, place the pieces where they were.
		if (!obj.pieces.empty()) {
			int bl = 1 << 30, bt = 1 << 30, br = -(1 << 30), bb = -(1 << 30);
			for (const PieceUse &p : obj.pieces) {
				bl = std::min(bl, p.destX);
				bt = std::min(bt, p.destY);
				br = std::max(br, p.destX + p.right - p.left);
				bb = std::max(bb, p.destY + p.bottom - p.top);
			}
			if ((bl != obj.left || bt != obj.top) && br - bl == obj.right - obj.left && bb - bt == obj.bottom - obj.top)
				for (PieceUse &p : obj.pieces) {
					p.destX += obj.left - bl;
					p.destY += obj.top - bt;
				}
		}
		for (const PieceUse &p : obj.pieces)
			if (std::find(obj.sheets.begin(), obj.sheets.end(), p.sheet) == obj.sheets.end())
				obj.sheets.push_back(p.sheet);
		// Its visible pixels: screen pixels showing exactly the sheet pixel one
		// of its pieces puts there.
		for (const PieceUse &p : obj.pieces)
			for (int y = std::max(p.destY, 0); y <= std::min(p.destY + p.bottom - p.top, cap.h - 1); y++)
				for (int x = std::max(p.destX, 0); x <= std::min(p.destX + p.right - p.left, cap.w - 1); x++) {
					const size_t i = size_t(y) * size_t(cap.w) + size_t(x);
					const Capture::Picture *pic = sourcePicture(i);
					if (!pic || pic->use != PictureUse::Animation || pic->resId != p.sheet)
						continue;
					const uint32_t v = cap.sources[i];
					const int sx = PixelSources::x(v); // the picture pixel (a mirror flips only its block)
					if (sx == p.left + x - p.destX && PixelSources::y(v) == p.top + y - p.destY)
						cap.owner[i] = int16_t(obj.index);
				}
		cap.objects.push_back(std::move(obj));
	}
	for (const Interpreter::HotspotInfo &h : interp.hotspots()) {
		if (h.disabled() || h.right < h.left || h.bottom < h.top)
			continue;
		if (h.left >= cap.w || h.top >= cap.h || h.right < 0 || h.bottom < 0)
			continue; // parked off screen
		if ((h.right - h.left + 1) * (h.bottom - h.top + 1) >= cap.w * cap.h / 2)
			continue; // the whole screen (clicks anywhere)
		if ((h.right - h.left + 1) < 2 || (h.bottom - h.top + 1) < 3)
			continue; // the top line that opens the icon bar
		cap.hotspots.push_back({h.slot, h.id, h.left, h.top, h.right, h.bottom, {}});
	}
	for (Object &obj : cap.objects) {
		int best = 0;
		for (const Hotspot &h : cap.hotspots) {
			const int w = std::min(obj.right, h.right) - std::max(obj.left, h.left) + 1;
			const int hh = std::min(obj.bottom, h.bottom) - std::max(obj.top, h.top) + 1;
			if (w > 0 && hh > 0 && w * hh > best) {
				best = w * hh;
				obj.hotspot = h.slot;
			}
		}
	}
	// Hover probe: the visible pixel nearest the middle of the object,
	// preferring pixels in its own hotspot that no earlier hotspot (the game
	// takes the first that matches) covers.
	for (Object &obj : cap.objects) {
		long sx = 0, sy = 0;
		auto mine = [&](int x, int y) {
			return x >= 0 && y >= 0 && x < cap.w && y < cap.h && cap.owner[size_t(y) * size_t(cap.w) + size_t(x)] == obj.index;
		};
		for (int y = obj.top; y <= obj.bottom; y++)
			for (int x = obj.left; x <= obj.right; x++)
				if (mine(x, y)) {
					obj.visible++;
					sx += x;
					sy += y;
				}
		if (!obj.visible)
			continue;
		auto shadowed = [&](int x, int y) {
			for (const Hotspot &h : cap.hotspots) {
				if (h.slot >= obj.hotspot)
					break;
				if (x >= h.left && x <= h.right && y >= h.top && y <= h.bottom)
					return true;
			}
			return false;
		};
		const double cx = double(sx) / obj.visible, cy = double(sy) / obj.visible;
		double best = 1e18;
		for (int y = obj.top; y <= obj.bottom; y++)
			for (int x = obj.left; x <= obj.right; x++)
				if (mine(x, y)) {
					const double d = (x - cx) * (x - cx) + (y - cy) * (y - cy) + (shadowed(x, y) ? 1e9 : 0);
					if (d < best) {
						best = d;
						obj.probeX = x;
						obj.probeY = y;
					}
				}
	}
	return cap;
}

std::string hoverText(Interpreter &interp) {
	std::vector<Draw::TextCell> cells;
	interp.draw().visibleText(cells);
	const int lineTop = interp.draw().front().height() - 12;
	std::vector<const Draw::TextCell *> line;
	for (const Draw::TextCell &c : cells)
		if (c.y >= lineTop)
			line.push_back(&c);
	std::sort(line.begin(), line.end(), [](const auto *a, const auto *b) { return a->y != b->y ? a->y < b->y : a->x < b->x; });
	// Glyph ink back to characters, over every loaded font (printable ASCII
	// wins over other codes with the same glyph).
	std::map<Font::Rows, char> byInk;
	for (int i = Draw::kFontCount - 1; i >= 0; i--)
		if (const Font *f = interp.draw().font(i))
			for (int c = 255; c >= 33; c--)
				if (const Font::Rows ink = f->glyphRows(uint8_t(c)); ink != Font::Rows{} && (c < 0x7F || !byInk.count(ink)))
					byInk[ink] = char(c);
	std::string s;
	int nextX = -1, row = -1;
	for (const Draw::TextCell *c : line) {
		if (row >= 0 && c->y != row)
			s += ' ';
		else if (nextX >= 0 && c->x > nextX && c->w)
			s.append(size_t((c->x - nextX) / c->w), ' ');
		auto it = byInk.find(c->ink);
		s += it != byInk.end() ? it->second : '?';
		nextX = c->x + c->w;
		row = c->y;
	}
	return s;
}

void probeNames(Capture &cap, const std::function<std::string(int, int)> &nameAt) {
	// Between two probes the mouse rests on a point outside every hotspot,
	// so the hover line is cleared and a probe never reads the last name.
	int restX = -1, restY = -1;
	for (int y = cap.h / 4; y < cap.h * 3 / 4 && restX < 0; y += 4)
		for (int x = 8; x < cap.w - 8 && restX < 0; x += 4) {
			bool free = true;
			for (const Hotspot &h : cap.hotspots)
				free = free && !(x >= h.left - 2 && x <= h.right + 2 && y >= h.top - 2 && y <= h.bottom + 2);
			if (free)
				restX = x, restY = y;
		}
	auto probe = [&](int x, int y) {
		if (restX >= 0)
			nameAt(restX, restY);
		return nameAt(x, y);
	};
	for (Hotspot &h : cap.hotspots) {
		// The point nearest its middle that no earlier hotspot covers.
		const int cx = (h.left + h.right) / 2, cy = (h.top + h.bottom) / 2;
		int px = cx, py = cy;
		long best = -1;
		for (int y = std::max(h.top, 0); y <= std::min(h.bottom, cap.h - 1); y++)
			for (int x = std::max(h.left, 0); x <= std::min(h.right, cap.w - 1); x++) {
				bool covered = false;
				for (const Hotspot &e : cap.hotspots) {
					if (e.slot >= h.slot)
						break;
					covered = covered || (x >= e.left && x <= e.right && y >= e.top && y <= e.bottom);
				}
				const long d = long(x - cx) * (x - cx) + long(y - cy) * (y - cy);
				if (!covered && (best < 0 || d < best))
					best = d, px = x, py = y;
			}
		h.name = probe(px, py);
	}
	for (Object &obj : cap.objects) {
		if (obj.probeX >= 0)
			obj.name = probe(obj.probeX, obj.probeY);
		if (obj.name.empty())
			for (const Hotspot &h : cap.hotspots)
				if (h.slot == obj.hotspot)
					obj.name = h.name;
	}
}

bool write(const Capture &cap, const Options &opt) {
	std::error_code ec;
	fs::create_directories(opt.dir, ec);
	if (!fs::is_directory(opt.dir)) {
		std::fprintf(stderr, "guide: cannot create %s\n", opt.dir.string().c_str());
		return false;
	}

	std::unique_ptr<GameImages> originals;
	HdPack pack;
	bool havePack = false;
	if (!opt.packDir.empty()) {
		originals = std::make_unique<GameImages>(opt.gameDir);
		havePack = pack.open(opt.packDir, [&](const std::string &tot, int id) { return (*originals)(tot, id); });
		if (!havePack)
			std::fprintf(stderr, "guide: cannot open the HD pack %s\n", opt.packDir.string().c_str());
		for (const HdIssue &i : pack.issues())
			if (i.severity == HdIssue::Error)
				std::fprintf(stderr, "guide: pack: %s: %s\n", i.file.c_str(), i.message.c_str());
	}
	int n = opt.scale > 0 ? opt.scale : havePack && pack.maxScale() > 0 ? pack.maxScale() : 4;
	n = std::clamp(n, 1, 8);

	// The room as the game shows it, enlarged.
	tool::Canvas native(cap.w, cap.h);
	for (int y = 0; y < cap.h; y++)
		for (int x = 0; x < cap.w; x++) {
			const Rgb c = cap.palette[cap.indices[size_t(y) * size_t(cap.w) + size_t(x)]];
			native.set(x, y, {c.r, c.g, c.b, 255});
		}
	const tool::Canvas big = tool::enlarge(native, n);

	// Objects only.
	tool::Canvas layer(cap.w * n, cap.h * n);
	int layerPixels = 0;
	for (int y = 0; y < cap.h; y++)
		for (int x = 0; x < cap.w; x++)
			if (cap.owner[size_t(y) * size_t(cap.w) + size_t(x)] != -1) {
				layerPixels++;
				layer.fill(x * n, y * n, x * n + n - 1, y * n + n - 1, {native.at(x, y)[0], native.at(x, y)[1], native.at(x, y)[2], 255});
			}

	// The room with the pack's pictures where it has them: each screen
	// pixel from a picture becomes that picture pixel's block of the HD
	// picture (resampled to the guide's scale), blended by its alpha over
	// the enlarged original pixel.
	tool::Canvas packed;
	int packPixels = 0;
	if (havePack) {
		packed = big;
		std::map<int, std::shared_ptr<const HdImage>> hd; // by picture id
		for (int y = 0; y < cap.h; y++)
			for (int x = 0; x < cap.w; x++) {
				const size_t i = size_t(y) * size_t(cap.w) + size_t(x);
				if (cap.sources.empty() || !PixelSources::isRef(cap.sources[i]) || PixelSources::isClear(cap.sources[i]))
					continue;
				const uint32_t v = cap.sources[i];
				const int id = PixelSources::picture(v);
				if (id < 1 || size_t(id) > cap.pictures.size())
					continue;
				auto it = hd.find(id);
				if (it == hd.end()) {
					const Capture::Picture &p = cap.pictures[size_t(id) - 1];
					it = hd.emplace(id, pack.find(p.tot, p.resId)).first;
				}
				const HdImage *img = it->second.get();
				if (!img || img->origWidth <= 0)
					continue;
				const int s = img->scale;
				const int sx = PixelSources::x(v), sy = PixelSources::y(v);
				const bool mirror = PixelSources::mirrored(v);
				packPixels++;
				for (int j = 0; j < n; j++)
					for (int k = 0; k < n; k++) {
						const int kk = mirror ? n - 1 - k : k;
						const int hx = std::min(sx * s + kk * s / n, img->width - 1);
						const int hy = std::min(sy * s + j * s / n, img->height - 1);
						const uint8_t *q = &img->rgba[(size_t(hy) * size_t(img->width) + size_t(hx)) * 4];
						uint8_t *d = packed.at(x * n + k, y * n + j);
						for (int c = 0; c < 3; c++)
							d[c] = uint8_t((q[c] * q[3] + d[c] * (255 - q[3])) / 255);
					}
			}
	}

	// Outlines and labels.
	const int thick = std::max(1, n / 2), labelScale = std::max(1, n / 2);
	auto annotate = [&](tool::Canvas &c) {
		if (opt.hotspots)
			for (const Hotspot &h : cap.hotspots) {
				const tool::Color col{0, 255, 255, 255};
				c.outline(h.left * n, h.top * n, (h.right + 1) * n - 1, (h.bottom + 1) * n - 1, col, std::max(1, n / 4), 2 * n);
				const std::string text = "h" + std::to_string(h.slot) + (h.name.empty() ? "" : " " + h.name);
				cap.font.draw(c, h.left * n + labelScale, (h.bottom + 1) * n - (cap.font.h + 1) * labelScale, text,
				              std::max(1, labelScale / 2), col, {0, 0, 0, 160});
			}
		// Labels go above the outline, else below it, moved down out of
		// earlier labels' way.
		struct Box { int l, t, r, b; };
		std::vector<Box> placed;
		auto clash = [&](const Box &x) {
			for (const Box &p : placed)
				if (x.l <= p.r && p.l <= x.r && x.t <= p.b && p.t <= x.b)
					return true;
			return false;
		};
		for (const Object &o : cap.objects) {
			if (o.visible == 0)
				continue; // hidden (e.g. under an identical object)
			const tool::Color col = tool::distinctColor(o.index);
			const int l = o.left * n, t = o.top * n, r = (o.right + 1) * n - 1, b = (o.bottom + 1) * n - 1;
			c.outline(l - thick, t - thick, r + thick, b + thick, col, thick);
			std::string text = std::to_string(o.index);
			if (!o.name.empty())
				text += " " + o.name;
			else if (o.goblin)
				text += " goblin";
			const int tw = cap.font.textWidth(text, labelScale), th = cap.font.h * labelScale;
			const int tx = std::clamp(l - thick, labelScale, std::max(labelScale, c.w - tw - labelScale));
			Box box{};
			bool found = false;
			for (int ty0 : {t - thick - th - 2 * labelScale, b + thick + 2 * labelScale}) {
				for (int step = 0; step < 12 && !found; step++) {
					const int ty = ty0 + step * (th + 2 * labelScale) * (ty0 < t ? -1 : 1);
					box = {tx - labelScale, ty - labelScale, tx + tw + labelScale - 1, ty + th + labelScale - 1};
					found = box.t >= 0 && box.b < c.h && !clash(box);
				}
				if (found)
					break;
			}
			if (!found)
				box = {tx - labelScale, std::max(0, t - th - 2 * labelScale), tx + tw + labelScale - 1, 0};
			placed.push_back(box);
			cap.font.draw(c, box.l + labelScale, box.t + labelScale, text, labelScale, col, {0, 0, 0, 170});
		}
	};
	tool::Canvas guide = big;
	annotate(guide);
	bool ok = guide.save(opt.dir / "guide-original.png");
	if (havePack) {
		annotate(packed);
		ok = packed.save(opt.dir / "guide-pack.png") && ok;
	}
	ok = layer.save(opt.dir / "objects-layer.png") && ok;

	std::ostringstream j;
	j << "{\n  \"format\": \"gob2native-guide\",\n  \"version\": 1,\n  \"tot\": " << tool::jsonString(cap.tot)
	  << ",\n  \"scale\": " << n << ",\n  \"screen\": [" << cap.w << ", " << cap.h << "],\n  \"pack\": "
	  << (havePack ? tool::jsonString(opt.packDir.string()) : "null") << ",\n  \"objects\": [";
	for (size_t i = 0; i < cap.objects.size(); i++) {
		const Object &o = cap.objects[i];
		j << (i ? ",\n" : "\n") << "    {\"index\": " << o.index << ", \"name\": "
		  << tool::jsonString(o.name.empty() ? (o.goblin ? "goblin " : "object ") + std::to_string(o.index) : o.name)
		  << ", \"name_from_game\": " << (o.name.empty() ? "false" : "true") << ", \"goblin\": " << (o.goblin ? "true" : "false")
		  << ",\n     \"anim_slot\": " << o.slot << ", \"anim_resource\": " << o.animRes << ", \"layer\": " << o.layer
		  << ", \"frame\": " << o.frame << ", \"order\": " << o.order << ", \"position\": [" << o.x << ", " << o.y << "]"
		  << ", \"sheets\": [";
		for (size_t s = 0; s < o.sheets.size(); s++)
			j << (s ? ", " : "") << o.sheets[s];
		j << "],\n     \"rect\": " << rectJson(o.left, o.top, o.right, o.bottom)
		  << ", \"rect_scaled\": " << scaledRectJson(o.left, o.top, o.right, o.bottom, n)
		  << ", \"visible_pixels\": " << o.visible << ", \"hotspot\": " << o.hotspot << ", \"frames_folder\": "
		  << tool::jsonString("anim" + std::to_string(o.animRes) + "/layer" + std::to_string(o.layer)) << ",\n     \"pieces\": [";
		for (size_t p = 0; p < o.pieces.size(); p++) {
			const PieceUse &pu = o.pieces[p];
			j << (p ? ", " : "") << "{\"sheet\": " << pu.sheet << ", \"piece\": " << pu.pieceIndex
			  << ", \"source\": " << rectJson(pu.left, pu.top, pu.right, pu.bottom) << ", \"dest\": [" << pu.destX << ", "
			  << pu.destY << "]}";
		}
		j << "]}";
	}
	j << "\n  ],\n  \"hotspots\": [";
	for (size_t i = 0; i < cap.hotspots.size(); i++) {
		const Hotspot &h = cap.hotspots[i];
		j << (i ? ",\n" : "\n") << "    {\"slot\": " << h.slot << ", \"id\": " << h.id << ", \"name\": " << tool::jsonString(h.name)
		  << ", \"rect\": " << rectJson(h.left, h.top, h.right, h.bottom)
		  << ", \"rect_scaled\": " << scaledRectJson(h.left, h.top, h.right, h.bottom, n) << "}";
	}
	j << "\n  ]\n}\n";
	std::ofstream(opt.dir / "objects.json") << j.str();

	std::printf("guide: %s at %dx into %s (%zu objects, %d object pixels%s)\n", cap.tot.c_str(), n, opt.dir.string().c_str(),
	            cap.objects.size(), layerPixels,
	            havePack ? (", " + std::to_string(packPixels) + " pixels from the pack").c_str() : "");
	for (const Object &o : cap.objects) {
		std::string sheets;
		for (int s : o.sheets)
			sheets += (sheets.empty() ? "" : ",") + std::to_string(s);
		std::printf("  object %2d %-18s anim %d layer %d frame %d sheets %s rect %d,%d-%d,%d visible %d\n", o.index,
		            (o.name.empty() ? std::string(o.goblin ? "(goblin)" : "-") : o.name).c_str(), o.animRes, o.layer,
		            o.frame, sheets.c_str(), o.left, o.top, o.right, o.bottom, o.visible);
	}
	if (cap.sources.empty())
		std::printf("  (no pixel sources: objects-layer.png is empty)\n");
	return ok;
}

} // namespace gob::guide
