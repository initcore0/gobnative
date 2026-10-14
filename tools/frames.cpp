// gob2native - per-object frame export and import for HD packs
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frames.h"

#include "minijson.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>

namespace fs = std::filesystem;

namespace gob::frames {

namespace {

std::string rectJson(const AnimPieceRect &r) {
	return "[" + std::to_string(r.left) + ", " + std::to_string(r.top) + ", " + std::to_string(r.right) + ", " +
	       std::to_string(r.bottom) + "]";
}

std::string upper(std::string s) {
	for (char &c : s)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

} // namespace

const Layer *Room::find(int anim, int layer) const {
	for (const Layer &l : layers)
		if (l.anim == anim && l.index == layer)
			return &l;
	return nullptr;
}

bool loadRoom(const TotFile &tot, const PaletteFor &paletteFor, Room &out) {
	out = Room{};
	out.tot = tot.name();
	Disassembler dis(tot);
	dis.run();
	const std::vector<ResourceUse> &uses = dis.resourceUses();

	// The scripts' scenery descriptors: per anim resource, per picture, the
	// sheet and its piece table (the first descriptor wins).
	struct Pic {
		int sheet = -1, table = -1;
	};
	std::map<int, std::map<int, Pic>> anims;
	for (const ResourceUse &u : uses) {
		if (u.kind != ResourceUse::AnimPicture)
			continue;
		Pic &p = anims[u.animResId][u.picture];
		if (p.sheet < 0)
			p = {u.resId, u.pictDescId};
		else if (p.sheet != u.resId || p.table != u.pictDescId)
			out.warnings.push_back("anim " + std::to_string(u.animResId) + " picture " + std::to_string(u.picture) +
			                       " is loaded with different sheets; using sheet " + std::to_string(p.sheet));
	}

	std::map<int, std::vector<AnimPieceRect>> tables;
	for (auto &[animRes, pics] : anims) {
		auto res = tot.resource(uint16_t(animRes));
		auto anim = res ? parseAnimResource(res->data) : std::nullopt;
		if (!anim) {
			out.warnings.push_back("anim " + std::to_string(animRes) + ": missing or unreadable");
			continue;
		}
		for (auto &[index, p] : pics) {
			if (!out.sheets.count(p.sheet)) {
				auto sr = tot.resource(uint16_t(p.sheet));
				auto img = sr ? decodeSprite(*sr) : std::nullopt;
				if (!img) {
					out.warnings.push_back("sheet " + std::to_string(p.sheet) + ": missing or not a picture");
					continue;
				}
				Sheet s;
				s.resId = p.sheet;
				s.image = std::move(*img);
				s.palette = paletteFor(tot, uses, p.sheet);
				out.sheets[p.sheet] = std::move(s);
			}
			if (!tables.count(p.table))
				if (auto t = tot.resource(uint16_t(p.table)))
					tables[p.table] = parsePieceTable(t->data);
		}

		for (size_t li = 0; li < anim->layers.size(); li++) {
			const AnimLayerData &ld = anim->layers[li];
			Layer layer;
			layer.anim = animRes;
			layer.index = int(li);
			layer.posX = ld.posX;
			layer.posY = ld.posY;
			layer.transparent = ld.transparent;
			bool any = false;
			for (size_t fi = 0; fi < ld.frames.size(); fi++) {
				Frame f;
				f.index = int(fi);
				int l = 1 << 30, t = 1 << 30, r = -(1 << 30), b = -(1 << 30);
				for (const AnimFramePiece &fp : ld.frames[fi]) {
					auto pi = pics.find(fp.picture);
					if (fp.picture < 0 || pi == pics.end() || !out.sheets.count(pi->second.sheet))
						continue; // no picture: skipped, as the game does
					auto ti = tables.find(pi->second.table);
					if (ti == tables.end() || fp.pieceIndex >= int(ti->second.size()) || !ti->second[size_t(fp.pieceIndex)].valid)
						continue;
					Piece pc;
					pc.order = int(f.pieces.size());
					pc.picture = fp.picture;
					pc.sheet = pi->second.sheet;
					pc.pieceTable = pi->second.table;
					pc.pieceIndex = fp.pieceIndex;
					pc.src = ti->second[size_t(fp.pieceIndex)];
					pc.offX = fp.destX;
					pc.offY = fp.destY;
					l = std::min(l, pc.offX);
					t = std::min(t, pc.offY);
					r = std::max(r, pc.offX + pc.src.width() - 1);
					b = std::max(b, pc.offY + pc.src.height() - 1);
					f.pieces.push_back(pc);
					if (std::find(layer.sheets.begin(), layer.sheets.end(), pc.sheet) == layer.sheets.end())
						layer.sheets.push_back(pc.sheet);
				}
				if (!f.pieces.empty()) {
					f.left = l;
					f.top = t;
					f.w = r - l + 1;
					f.h = b - t + 1;
					for (Piece &pc : f.pieces) {
						pc.x = pc.offX - l;
						pc.y = pc.offY - t;
					}
					any = true;
				}
				layer.frames.push_back(std::move(f));
			}
			if (any) {
				std::sort(layer.sheets.begin(), layer.sheets.end());
				out.layers.push_back(std::move(layer));
			}
		}
	}
	return true;
}

Render render(const Room &room, const Layer &layer, const Frame &frame) {
	Render r;
	r.rgba = tool::Canvas(std::max(frame.w, 0), std::max(frame.h, 0));
	r.top.assign(size_t(r.rgba.w) * size_t(r.rgba.h), -1);
	for (const Piece &pc : frame.pieces) {
		auto si = room.sheets.find(pc.sheet);
		if (si == room.sheets.end())
			continue;
		const Sheet &s = si->second;
		for (int v = 0; v < pc.src.height(); v++)
			for (int u = 0; u < pc.src.width(); u++) {
				const int sx = pc.src.left + u, sy = pc.src.top + v;
				if (sx < 0 || sy < 0 || sx >= s.image.width || sy >= s.image.height)
					continue;
				const uint8_t idx = s.image.pixels[size_t(sy) * size_t(s.image.width) + size_t(sx)];
				if (layer.transparent && idx == 0)
					continue;
				const int x = pc.x + u, y = pc.y + v;
				r.rgba.set(x, y, {s.palette[idx].r, s.palette[idx].g, s.palette[idx].b, 255});
				r.top[size_t(y) * size_t(r.rgba.w) + size_t(x)] = int16_t(pc.order);
			}
	}
	return r;
}

// ---------------------------------------------------------------------------
// Export

namespace {

const char *const kFramesReadme = R"(gobnative frame export
======================

Every animation frame of the room's objects and goblins, one PNG per frame,
made from your own game files by gobexport (keep it for yourself). Use it to
repaint objects frame by frame with an image model, then put them back into
an HD pack with gobexport --import-frames.

Layout:

  layers.txt                         start here: which layer is which object
  <TOT>/anim<A>/layer<L>/frame<F>.png  one frame, cropped to its own size,
                                     transparent where the game draws nothing
  <TOT>/anim<A>/layer<L>/preview.png the layer's frames side by side, aligned
                                     as the game places them, with numbers
  <TOT>/anim<A>/layer<L>/layer.json  how the game builds each frame: every
                                     piece's rectangle in the sheet (the
                                     animation picture), where it goes in the
                                     frame and in which order

A layer is one animation of one object (a goblin's walk to the left, the
flowers swaying, ...). Frames combine pieces of the room's animation sheets
(<TOT>/<sheet>.png in the picture export); the same piece is often used by
several frames, so repainting one frame changes the others that share it.

Repainting
----------

- Repaint at any size, but keep each frame's composition in place: the same
  framing, the object where it is in the PNG, nothing cropped or moved. The
  import scales your image to the frame size (times the pack's scale), and
  the game draws exactly that rectangle at a fixed position: anything moved
  or resized shows up offset in the game.
- Keep the file names and folders; leave out frames you don't repaint.
- Transparent or flat-coloured backgrounds are both fine. By default the
  import keeps the game's original outline (silhouette) for every frame, so
  the background colour never shows. With --alpha it takes the outline from
  your PNG's transparency, with --key-color RRGGBB from a flat key colour.
- Frames that show the same object in the same pose must stay consistent:
  when two repainted frames disagree about a shared piece, the import keeps
  the one where the piece is most visible and reports the conflict.

Then, from the source folder:

  build/gobexport /path/to/gobliins2 --import-frames REPAINTED EXPORT PACK [--scale 4]

REPAINTED is a folder with the same layout as this one (only the frames you
repainted), EXPORT your gobexport picture export, PACK your HD pack folder
(HDPacks/<name>). It writes <TOT>/<sheet>.png into the pack (starting from the
pack's own sheet if it has one) and checks the pack.
)";

struct GuideObject {
	int index = 0, layer = 0, frame = 0, anim = -1;
	std::string name;
	bool goblin = false;
};

std::vector<GuideObject> readGuide(const fs::path &dir, const std::string &tot, std::string &guideTot) {
	std::vector<GuideObject> out;
	std::ifstream in(dir / "objects.json");
	if (!in)
		return out;
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	json::Value v;
	if (!json::Parser(text).parse(v)) {
		std::fprintf(stderr, "%s: not readable JSON\n", (dir / "objects.json").string().c_str());
		return out;
	}
	guideTot = upper(v["tot"].asString());
	if (guideTot != upper(tot))
		return out;
	for (const json::Value &o : v["objects"].a) {
		GuideObject g;
		g.index = o["index"].asInt();
		g.anim = o["anim_resource"].asInt(-1);
		g.layer = o["layer"].asInt();
		g.frame = o["frame"].asInt();
		g.name = o["name"].asString();
		g.goblin = o["goblin"].type == json::Value::Bool && o["goblin"].b;
		if (o["visible_pixels"].asInt(1) > 0)
			out.push_back(g);
	}
	return out;
}

// The layer's frames side by side, each in a cell as large as the union of
// all frames so they line up as in the game, numbered.
tool::Canvas contactSheet(const std::vector<std::pair<const Frame *, const tool::Canvas *>> &frames) {
	int l = 1 << 30, t = 1 << 30, r = -(1 << 30), b = -(1 << 30);
	for (const auto &[f, c] : frames) {
		l = std::min(l, f->left);
		t = std::min(t, f->top);
		r = std::max(r, f->left + f->w - 1);
		b = std::max(b, f->top + f->h - 1);
	}
	const int uw = r - l + 1, uh = b - t + 1;
	const int ps = std::clamp(128 / std::max(uw, uh), 1, 4);
	const int pad = 4, label = 14, cw = uw * ps + 2 * pad, ch = uh * ps + 2 * pad + label;
	const int cols = std::max(1, std::min(int(frames.size()), 1600 / cw));
	const int rows = (int(frames.size()) + cols - 1) / cols;
	tool::Canvas out(cols * cw, rows * ch, {40, 40, 48, 255});
	for (size_t i = 0; i < frames.size(); i++) {
		const auto &[f, c] = frames[i];
		const int cx = int(i) % cols * cw, cy = int(i) / cols * ch;
		// Checkerboard behind the frame area, then the frame.
		for (int y = 0; y < uh * ps; y++)
			for (int x = 0; x < uw * ps; x++) {
				const uint8_t g = ((x / 8 + y / 8) & 1) ? 150 : 110;
				out.set(cx + pad + x, cy + pad + y, {g, g, g, 255});
			}
		for (int y = 0; y < f->h * ps; y++)
			for (int x = 0; x < f->w * ps; x++) {
				const uint8_t *p = c->at(x / ps, y / ps);
				if (p[3])
					out.set(cx + pad + (f->left - l) * ps + x, cy + pad + (f->top - t) * ps + y, {p[0], p[1], p[2], 255});
			}
		tool::drawNumber(out, cx + pad, cy + ch - label + 2, f->index, 2, {255, 255, 255, 255});
	}
	return out;
}

} // namespace

int exportFrames(DataFiles &files, const std::vector<std::string> &tots, const fs::path &outDir, const fs::path &guideDir,
                 const PaletteFor &paletteFor) {
	std::error_code ec;
	fs::create_directories(outDir, ec);
	if (!fs::is_directory(outDir)) {
		std::fprintf(stderr, "cannot create %s\n", outDir.string().c_str());
		return 1;
	}
	int totalFrames = 0, totalLayers = 0;
	std::string guideTot;
	bool guideUsed = false;
	for (const std::string &name : tots) {
		TotFile tot;
		if (!tot.load(files, name))
			continue;
		Room room;
		loadRoom(tot, paletteFor, room);
		if (room.layers.empty())
			continue;
		std::vector<GuideObject> guide = guideDir.empty() ? std::vector<GuideObject>() : readGuide(guideDir, name, guideTot);
		guideUsed = guideUsed || !guide.empty();
		for (const std::string &w : room.warnings)
			std::fprintf(stderr, "%s: %s\n", name.c_str(), w.c_str());

		std::ostringstream index;
		index << name << ": animation layers and their frames\n"
		      << std::string(name.size() + 34, '=') << "\n\n";
		if (!guide.empty()) {
			index << "Objects on screen when the room is idle (from " << (guideDir / "objects.json").string() << "):\n\n";
			for (const GuideObject &g : guide) {
				const Layer *l = room.find(g.anim, g.layer);
				char line[256];
				std::snprintf(line, sizeof(line), "  object %2d  %-14s anim%d/layer%d/frame%d.png", g.index, g.name.c_str(),
				              g.anim, g.layer, g.frame);
				index << line;
				if (l) {
					index << "  (" << l->frames.size() << " frame" << (l->frames.size() == 1 ? "" : "s") << ", sheet";
					for (size_t i = 0; i < l->sheets.size(); i++)
						index << (i ? ", " : " ") << l->sheets[i];
					index << ")";
				}
				index << "\n";
				// Other frames drawn with the same pieces: repainting one
				// repaints the others (and they must agree).
				if (l && g.frame < int(l->frames.size())) {
					std::map<std::pair<int, int>, std::set<int>> shared;
					for (const Piece &mine : l->frames[size_t(g.frame)].pieces)
						for (const Layer &other : room.layers)
							for (const Frame &f : other.frames) {
								if (&other == l && f.index == g.frame)
									continue;
								for (const Piece &pc : f.pieces)
									if (pc.pieceTable == mine.pieceTable && pc.pieceIndex == mine.pieceIndex) {
										shared[{other.anim, other.index}].insert(f.index);
										break;
									}
							}
					if (!shared.empty()) {
						std::string line = "             its pieces also appear in:";
						for (const auto &[key, set] : shared) {
							const std::vector<int> fr(set.begin(), set.end());
							const std::string folder = "anim" + std::to_string(key.first) + "/layer" + std::to_string(key.second);
							std::string frs;
							for (size_t i = 0; i < fr.size(); i++) {
								size_t j = i;
								while (j + 1 < fr.size() && fr[j + 1] == fr[j] + 1)
									j++;
								frs += (frs.empty() ? "" : ",") + std::to_string(fr[i]) + (j > i ? "-" + std::to_string(fr[j]) : "");
								i = j;
							}
							const std::string item = " " + folder + " (frame" + (fr.size() > 1 ? "s " : " ") + frs + ")";
							if (line.size() + item.size() > 118) {
								index << line << "\n";
								line = "            ";
							}
							line += item;
						}
						index << line << "\n";
					}
				}
			}
			index << "\n";
		} else {
			index << "(No guide: run gobrun --guide-at for this room and export again with --guide DIR to see which\n"
			         "object uses which layer.)\n\n";
		}
		index << "All layers (folder, frames, sheets used, default position, objects):\n\n";

		for (const Layer &layer : room.layers) {
			const fs::path dir = outDir / name / ("anim" + std::to_string(layer.anim)) / ("layer" + std::to_string(layer.index));
			fs::create_directories(dir, ec);
			std::vector<tool::Canvas> canvases;
			canvases.reserve(layer.frames.size());
			std::vector<std::pair<const Frame *, const tool::Canvas *>> shown;
			std::ostringstream j;
			j << "{\n  \"format\": \"gob2native-frames\",\n  \"version\": 1,\n  \"tot\": " << tool::jsonString(name)
			  << ",\n  \"anim\": " << layer.anim << ",\n  \"layer\": " << layer.index << ",\n  \"position\": [" << layer.posX
			  << ", " << layer.posY << "],\n  \"transparent\": " << (layer.transparent ? "true" : "false") << ",\n  \"sheets\": [";
			for (size_t i = 0; i < layer.sheets.size(); i++)
				j << (i ? ", " : "") << layer.sheets[i];
			j << "],\n  \"frames\": [";
			int written = 0;
			for (const Frame &f : layer.frames) {
				j << (f.index ? ",\n" : "\n") << "    {\"frame\": " << f.index;
				if (f.empty()) {
					j << ", \"file\": null, \"pieces\": []}";
					canvases.emplace_back();
					continue;
				}
				Render r = render(room, layer, f);
				const std::string file = "frame" + std::to_string(f.index) + ".png";
				if (!r.rgba.save(dir / file)) {
					std::fprintf(stderr, "cannot write %s\n", (dir / file).string().c_str());
					return 1;
				}
				written++;
				canvases.push_back(std::move(r.rgba));
				shown.push_back({&f, &canvases.back()});
				j << ", \"file\": " << tool::jsonString(file) << ", \"size\": [" << f.w << ", " << f.h << "], \"offset\": ["
				  << f.left << ", " << f.top << "],\n     \"pieces\": [";
				for (size_t p = 0; p < f.pieces.size(); p++) {
					const Piece &pc = f.pieces[p];
					j << (p ? ",\n                " : "") << "{\"order\": " << pc.order << ", \"sheet\": " << pc.sheet
					  << ", \"piece_table\": " << pc.pieceTable << ", \"piece\": " << pc.pieceIndex
					  << ", \"source\": " << rectJson(pc.src) << ", \"dest\": [" << pc.x << ", " << pc.y << "]}";
				}
				j << "]}";
			}
			j << "\n  ]\n}\n";
			std::ofstream(dir / "layer.json") << j.str();
			if (!shown.empty())
				contactSheet(shown).save(dir / "preview.png");
			totalFrames += written;
			totalLayers++;

			char line[256];
			std::string sheets;
			for (int s : layer.sheets)
				sheets += (sheets.empty() ? "" : ",") + std::to_string(s);
			std::snprintf(line, sizeof(line), "  %-20s %3d frame%s  sheets %-17s at %d,%d", layer.folder().c_str(), written,
			              written == 1 ? " " : "s", sheets.c_str(), layer.posX, layer.posY);
			index << line;
			for (const GuideObject &g : guide)
				if (g.anim == layer.anim && g.layer == layer.index)
					index << "  <- object " << g.index << " " << g.name << " (frame " << g.frame << ")";
			index << "\n";
		}
		std::ofstream(outDir / name / "layers.txt") << index.str();
		std::printf("%-8s %3zu layers with frames\n", name.c_str(), room.layers.size());
	}
	std::ofstream(outDir / "README.txt") << kFramesReadme;
	std::printf("%d frames in %d layers written to %s\n", totalFrames, totalLayers, outDir.string().c_str());
	if (!guideDir.empty() && !guideUsed)
		std::fprintf(stderr, "guide %s: no objects for these script files (guide is for %s)\n", guideDir.string().c_str(),
		             guideTot.empty() ? "?" : guideTot.c_str());
	return 0;
}

// ---------------------------------------------------------------------------
// Import

namespace {

struct Repainted {
	fs::path file;
	const Layer *layer = nullptr;
	const Frame *frame = nullptr;
	tool::Canvas hd;  // fitted to N x the frame
	Render orig;      // the original frame at 1x
	std::string note;
};

// Luminance gradient magnitude of `c` composited over mid grey, at its size.
std::vector<float> edges(const tool::Canvas &c) {
	std::vector<float> lum(size_t(c.w) * size_t(c.h)), out(lum.size());
	for (int y = 0; y < c.h; y++)
		for (int x = 0; x < c.w; x++) {
			const uint8_t *p = c.at(x, y);
			const float a = p[3] / 255.0f;
			lum[size_t(y) * size_t(c.w) + size_t(x)] = (0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]) * a + 128 * (1 - a);
		}
	for (int y = 0; y < c.h; y++)
		for (int x = 0; x < c.w; x++) {
			auto L = [&](int xx, int yy) {
				return lum[size_t(std::clamp(yy, 0, c.h - 1)) * size_t(c.w) + size_t(std::clamp(xx, 0, c.w - 1))];
			};
			out[size_t(y) * size_t(c.w) + size_t(x)] = std::fabs(L(x + 1, y) - L(x - 1, y)) + std::fabs(L(x, y + 1) - L(x, y - 1));
		}
	return out;
}

// Normalised cross-correlation of b shifted by (dx, dy) against a.
double ncc(const std::vector<float> &a, const std::vector<float> &b, int w, int h, int dx, int dy) {
	double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
	long n = 0;
	for (int y = std::max(0, dy); y < std::min(h, h + dy); y++)
		for (int x = std::max(0, dx); x < std::min(w, w + dx); x++) {
			const double va = a[size_t(y) * size_t(w) + size_t(x)], vb = b[size_t(y - dy) * size_t(w) + size_t(x - dx)];
			sa += va, sb += vb, saa += va * va, sbb += vb * vb, sab += va * vb;
			n++;
		}
	if (n < 4)
		return 0;
	const double cov = sab - sa * sb / double(n), va = saa - sa * sa / double(n), vb = sbb - sb * sb / double(n);
	return va > 1e-9 && vb > 1e-9 ? cov / std::sqrt(va * vb) : 0;
}

// Compares a repainted frame with the original at twice the frame's size
// and returns the shift (in half original pixels) that lines them up best,
// if it is clearly better than none.
bool detectShift(const tool::Canvas &orig1x, const tool::Canvas &hd, int &dx2, int &dy2, double &gain) {
	const int w = orig1x.w * 2, h = orig1x.h * 2;
	if (w < 6 || h < 6)
		return false;
	const std::vector<float> a = edges(tool::enlarge(orig1x, 2)), b = edges(tool::resample(hd, w, h));
	const double base = ncc(a, b, w, h, 0, 0);
	double best = base;
	dx2 = dy2 = 0;
	const int range = std::min(6, std::min(w, h) / 3);
	for (int dy = -range; dy <= range; dy++)
		for (int dx = -range; dx <= range; dx++) {
			const double v = ncc(a, b, w, h, dx, dy);
			if (v > best + 1e-9) {
				best = v;
				dx2 = dx;
				dy2 = dy;
			}
		}
	gain = best - base;
	return (dx2 || dy2) && gain > 0.03 && best > 0.3;
}

// "anim0/layer73 frames 1-17,24; anim30004/layer5 frame 0"
std::string frameList(const std::map<std::string, std::set<int>> &frames) {
	std::string out;
	for (const auto &[folder, set] : frames) {
		const std::vector<int> fr(set.begin(), set.end());
		std::string list;
		for (size_t i = 0; i < fr.size(); i++) {
			size_t j = i;
			while (j + 1 < fr.size() && fr[j + 1] == fr[j] + 1)
				j++;
			list += (list.empty() ? "" : ",") + std::to_string(fr[i]) + (j > i ? "-" + std::to_string(fr[j]) : "");
			i = j;
		}
		out += (out.empty() ? "" : "; ") + folder + (fr.size() > 1 ? " frames " : " frame ") + list;
	}
	return out;
}

bool parseInt(const std::string &s, const char *prefix, int &out) {
	const size_t n = std::strlen(prefix);
	if (s.size() <= n || s.compare(0, n, prefix) != 0)
		return false;
	for (size_t i = n; i < s.size(); i++)
		if (!std::isdigit(static_cast<unsigned char>(s[i])))
			return false;
	out = std::atoi(s.c_str() + n);
	return true;
}

} // namespace

int importFrames(DataFiles &files, const ImportOptions &opt, const PaletteFor &paletteFor) {
	// Repainted files: <TOT>/anim<A>/layer<L>/frame<F>.<png>
	std::map<std::string, std::vector<std::tuple<int, int, int, fs::path>>> byTot;
	std::error_code ec;
	for (auto it = fs::recursive_directory_iterator(opt.framesDir, ec); !ec && it != fs::recursive_directory_iterator();
	     it.increment(ec)) {
		if (!it->is_regular_file())
			continue;
		const fs::path p = it->path();
		const fs::path rel = fs::relative(p, opt.framesDir);
		std::vector<std::string> parts;
		for (const fs::path &q : rel)
			parts.push_back(q.string());
		std::string ext = upper(p.extension().string());
		if (ext != ".PNG" || parts.size() != 4) {
			if (ext == ".PNG" && p.filename() != "preview.png")
				std::printf("ignored %s (not <TOT>/anim<A>/layer<L>/frame<F>.png)\n", rel.string().c_str());
			continue;
		}
		int a = 0, l = 0, f = 0;
		const std::string stem = p.stem().string();
		if (!parseInt(parts[1], "anim", a) || !parseInt(parts[2], "layer", l) || !parseInt(stem, "frame", f)) {
			if (p.filename() != "preview.png")
				std::printf("ignored %s (not <TOT>/anim<A>/layer<L>/frame<F>.png)\n", rel.string().c_str());
			continue;
		}
		const std::string tot = upper(parts[0]);
		if (!opt.only.empty() && tot != upper(opt.only))
			continue;
		byTot[tot].push_back({a, l, f, p});
	}
	if (byTot.empty()) {
		std::fprintf(stderr, "no repainted frames found in %s\n", opt.framesDir.string().c_str());
		return 1;
	}

	int warnings = 0, errors = 0;
	for (auto &[totName, list] : byTot) {
		std::sort(list.begin(), list.end());
		TotFile tot;
		if (!tot.load(files, totName)) {
			std::fprintf(stderr, "%s: no such script file\n", totName.c_str());
			errors++;
			continue;
		}
		Room room;
		loadRoom(tot, paletteFor, room);

		// Which sheets are touched, and the scale.
		std::set<int> touched;
		for (auto &[a, l, f, p] : list)
			if (const Layer *layer = room.find(a, l))
				if (f < int(layer->frames.size()))
					for (const Piece &pc : layer->frames[size_t(f)].pieces)
						touched.insert(pc.sheet);
		int scale = opt.scale;
		for (int s : touched) {
			const fs::path packSheet = opt.packDir / totName / (std::to_string(s) + ".png");
			PngInfo info;
			if (!fs::exists(packSheet) || !readPngInfo(packSheet, info))
				continue;
			const Sheet &sh = room.sheets.at(s);
			const int ps = info.width / std::max(1, sh.image.width);
			if (info.width != ps * sh.image.width || info.height != ps * sh.image.height) {
				std::fprintf(stderr, "%s: size %dx%d is not a whole multiple of the sheet (%dx%d)\n", packSheet.string().c_str(),
				             info.width, info.height, sh.image.width, sh.image.height);
				return 1;
			}
			if (scale == 0)
				scale = ps;
			else if (ps != scale) {
				std::fprintf(stderr, "%s is at %dx, the import at %dx: use --scale %d or remove that sheet\n",
				             packSheet.string().c_str(), ps, scale, ps);
				return 1;
			}
		}
		if (scale == 0)
			scale = 4;
		const int N = scale;
		std::printf("%s: importing %zu frame(s) at %dx into %s\n", totName.c_str(), list.size(), N,
		            (opt.packDir / totName).string().c_str());

		// Load and fit the repainted frames, and check their framing.
		std::vector<Repainted> frames;
		for (auto &[a, l, f, p] : list) {
			const Layer *layer = room.find(a, l);
			const std::string rel = fs::relative(p, opt.framesDir).string();
			if (!layer || f >= int(layer->frames.size()) || layer->frames[size_t(f)].empty()) {
				std::printf("  %s: the room has no such frame; ignored\n", rel.c_str());
				warnings++;
				continue;
			}
			Repainted r;
			r.file = p;
			r.layer = layer;
			r.frame = &layer->frames[size_t(f)];
			tool::Canvas in;
			std::string err;
			if (!tool::loadCanvas(p, in, &err)) {
				std::printf("  %s: %s; ignored\n", rel.c_str(), err.c_str());
				errors++;
				continue;
			}
			const int fw = r.frame->w, fh = r.frame->h;
			const double aspIn = double(in.w) / in.h, aspFrame = double(fw) / fh;
			std::string note = std::to_string(in.w) + "x" + std::to_string(in.h);
			if (in.w != fw * N || in.h != fh * N)
				note += " -> " + std::to_string(fw * N) + "x" + std::to_string(fh * N);
			if (std::fabs(aspIn / aspFrame - 1) > 0.03) {
				note += " (WARNING: aspect " + std::to_string(aspIn).substr(0, 4) + " vs the frame's " +
				        std::to_string(aspFrame).substr(0, 4) + ": stretched; the framing must match the exported frame)";
				warnings++;
			}
			r.hd = tool::resample(in, fw * N, fh * N);
			r.orig = render(room, *layer, *r.frame);
			if (opt.alpha == ImportOptions::Alpha::KeyColor)
				for (size_t i = 0; i < r.hd.px.size(); i += 4) {
					const int d = std::max({std::abs(r.hd.px[i] - opt.key.r), std::abs(r.hd.px[i + 1] - opt.key.g),
					                        std::abs(r.hd.px[i + 2] - opt.key.b)});
					r.hd.px[i + 3] = d <= opt.keyTolerance ? 0 : 255;
				}
			int dx2 = 0, dy2 = 0;
			double gain = 0;
			const bool shifted = detectShift(r.orig.rgba, r.hd, dx2, dy2, gain);
			if (opt.alpha == ImportOptions::Alpha::Silhouette) {
				// The original outline decides: where the repaint is clear
				// inside it, use the nearest painted colours, not black.
				int clearInside = 0, inside = 0;
				for (int y = 0; y < r.hd.h; y++)
					for (int x = 0; x < r.hd.w; x++)
						if (r.orig.top[size_t(y / N) * size_t(fw) + size_t(x / N)] >= 0) {
							inside++;
							clearInside += r.hd.at(x, y)[3] < 128;
						}
				if (clearInside) {
					tool::bleedClear(r.hd, 4 * N);
					if (clearInside * 20 > inside) {
						char buf[160];
						std::snprintf(buf, sizeof(buf), " (%d%% of the original outline is clear in the repaint: filled from "
						              "the nearest colours)", clearInside * 100 / inside);
						note += buf;
					}
				}
			}
			if (shifted) {
				char buf[200];
				std::snprintf(buf, sizeof(buf),
				              " (WARNING: looks shifted by %+.1f,%+.1f original pixels = %+d,%+d at %dx; keep the exported "
				              "framing)",
				              -dx2 / 2.0, -dy2 / 2.0, -dx2 * N / 2, -dy2 * N / 2, N);
				note += buf;
				warnings++;
			}
			r.note = note;
			std::printf("  %s: %s\n", rel.c_str(), note.c_str());
			frames.push_back(std::move(r));
		}

		// Every piece of every repainted frame, ranked: the frame in which
		// the piece is most visible (least covered by later pieces) first,
		// then by layer and frame number.
		struct Instance {
			const Repainted *rf;
			const Piece *pc;
			double visible = 0; // fraction of its opaque pixels seen in the frame
		};
		std::vector<Instance> inst;
		for (const Repainted &rf : frames)
			for (const Piece &pc : rf.frame->pieces) {
				int opaque = 0, seen = 0;
				const Sheet &s = room.sheets.at(pc.sheet);
				for (int v = 0; v < pc.src.height(); v++)
					for (int u = 0; u < pc.src.width(); u++) {
						const int sx = pc.src.left + u, sy = pc.src.top + v;
						if (sx < 0 || sy < 0 || sx >= s.image.width || sy >= s.image.height)
							continue;
						if (rf.layer->transparent && s.image.pixels[size_t(sy) * size_t(s.image.width) + size_t(sx)] == 0)
							continue;
						opaque++;
						seen += rf.orig.top[size_t(pc.y + v) * size_t(rf.frame->w) + size_t(pc.x + u)] == pc.order;
					}
				inst.push_back({&rf, &pc, opaque ? double(seen) / opaque : 0});
			}
		std::stable_sort(inst.begin(), inst.end(), [](const Instance &a, const Instance &b) {
			if (a.visible != b.visible)
				return a.visible > b.visible;
			if (a.rf->layer->anim != b.rf->layer->anim)
				return a.rf->layer->anim < b.rf->layer->anim;
			if (a.rf->layer->index != b.rf->layer->index)
				return a.rf->layer->index < b.rf->layer->index;
			return a.rf->frame->index < b.rf->frame->index;
		});

		for (int sheetRes : touched) {
			const Sheet &s = room.sheets.at(sheetRes);
			const int W = s.image.width, H = s.image.height;
			const fs::path packSheet = opt.packDir / totName / (std::to_string(sheetRes) + ".png");
			const fs::path packMask = opt.packDir / totName / (std::to_string(sheetRes) + ".mask.png");
			tool::Canvas base;
			std::string baseFrom;
			if (fs::exists(packSheet) && tool::loadCanvas(packSheet, base)) {
				baseFrom = "the pack's sheet";
			} else {
				tool::Canvas orig;
				const fs::path exported = opt.exportDir / totName / (std::to_string(sheetRes) + ".png");
				if (tool::loadCanvas(exported, orig) && orig.w == W && orig.h == H) {
					baseFrom = "the export's sheet, enlarged";
				} else {
					orig = tool::Canvas(W, H);
					for (int y = 0; y < H; y++)
						for (int x = 0; x < W; x++) {
							const Rgb c = s.palette[s.image.pixels[size_t(y) * size_t(W) + size_t(x)]];
							orig.set(x, y, {c.r, c.g, c.b, 255});
						}
					baseFrom = "the game's sheet, enlarged (no export sheet at " + exported.string() + ")";
				}
				base = tool::enlarge(orig, N);
			}
			for (size_t i = 3; i < base.px.size(); i += 4)
				base.px[i] = 255;
			const bool withMask = opt.alpha != ImportOptions::Alpha::Silhouette;
			std::vector<uint8_t> mask;
			if (withMask) {
				tool::Canvas m;
				if (fs::exists(packMask) && tool::loadCanvas(packMask, m) && m.w == W * N && m.h == H * N) {
					mask.resize(size_t(m.w) * size_t(m.h));
					for (size_t i = 0; i < mask.size(); i++)
						mask[i] = m.px[i * 4];
				} else {
					mask.resize(size_t(W * N) * size_t(H * N));
					for (int y = 0; y < H * N; y++)
						for (int x = 0; x < W * N; x++)
							mask[size_t(y) * size_t(W * N) + size_t(x)] =
							    s.image.pixels[size_t(y / N) * size_t(W) + size_t(x / N)] ? 255 : 0;
				}
			} else if (fs::exists(packMask)) {
				std::printf("  note: %s exists and still decides the outline (silhouette mode leaves it alone)\n",
				            packMask.string().c_str());
			}

			// Per sheet pixel, the instance that paints it.
			std::vector<int> owner(size_t(W) * size_t(H), -1);
			struct Conflict {
				long pixels = 0;
				double diff = 0;
				std::map<std::string, long> winners;           // frame -> pixels it won
				std::map<std::string, std::set<int>> others;   // layer folder -> frames overruled
			};
			std::map<std::pair<int, int>, Conflict> conflicts; // (table, piece)
			auto frameName = [](const Repainted &rf) {
				return rf.layer->folder() + "/frame" + std::to_string(rf.frame->index);
			};
			std::set<std::pair<int, int>> piecesDone;
			for (size_t k = 0; k < inst.size(); k++) {
				const Instance &in = inst[k];
				if (in.pc->sheet != sheetRes)
					continue;
				const Repainted &rf = *in.rf;
				const Piece &pc = *in.pc;
				for (int v = 0; v < pc.src.height(); v++)
					for (int u = 0; u < pc.src.width(); u++) {
						const int sx = pc.src.left + u, sy = pc.src.top + v;
						if (sx < 0 || sy < 0 || sx >= W || sy >= H)
							continue;
						const bool opaque = !rf.layer->transparent || s.image.pixels[size_t(sy) * size_t(W) + size_t(sx)] != 0;
						const int fx = pc.x + u, fy = pc.y + v;
						const int top = rf.orig.top[size_t(fy) * size_t(rf.frame->w) + size_t(fx)];
						// Opaque pixels where this piece is the one seen; with
						// alpha from the frames also clear pixels nothing covers.
						const bool seen = opaque ? top == pc.order : (withMask && top < 0);
						if (!seen)
							continue;
						int &o = owner[size_t(sy) * size_t(W) + size_t(sx)];
						if (o >= 0) {
							if (!opaque)
								continue;
							// Already painted by a better-ranked frame: measure the
							// disagreement.
							const Instance &w = inst[size_t(o)];
							const int wx = w.pc->x + (sx - w.pc->src.left), wy = w.pc->y + (sy - w.pc->src.top);
							double d = 0;
							for (int j = 0; j < N; j++)
								for (int i = 0; i < N; i++) {
									const uint8_t *a = rf.hd.at(fx * N + i, fy * N + j);
									const uint8_t *b = w.rf->hd.at(wx * N + i, wy * N + j);
									d += (std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2])) / 3.0;
								}
							if (w.rf != &rf && d / (N * N) >= 6) {
								Conflict &c = conflicts[{pc.pieceTable, pc.pieceIndex}];
								c.pixels++;
								c.diff += d / (N * N);
								c.winners[frameName(*w.rf)]++;
								c.others[rf.layer->folder()].insert(rf.frame->index);
							}
							continue;
						}
						o = int(k);
						for (int j = 0; j < N; j++)
							for (int i = 0; i < N; i++) {
								const uint8_t *src = rf.hd.at(fx * N + i, fy * N + j);
								uint8_t *dst = base.at(sx * N + i, sy * N + j);
								if (withMask) {
									mask[size_t(sy * N + j) * size_t(W * N) + size_t(sx * N + i)] = src[3];
									if (src[3] == 0)
										continue; // keep the colour under clear pixels
								}
								dst[0] = src[0], dst[1] = src[1], dst[2] = src[2];
							}
						piecesDone.insert({pc.pieceTable, pc.pieceIndex});
					}
			}
			long painted = 0, stillHidden = 0;
			for (int o : owner)
				painted += o >= 0;
			// Opaque pixels of repainted pieces that no repainted frame shows.
			std::vector<uint8_t> inPiece(owner.size());
			for (const Instance &in : inst) {
				if (in.pc->sheet != sheetRes)
					continue;
				for (int v = 0; v < in.pc->src.height(); v++)
					for (int u = 0; u < in.pc->src.width(); u++) {
						const int sx = in.pc->src.left + u, sy = in.pc->src.top + v;
						if (sx >= 0 && sy >= 0 && sx < W && sy < H &&
						    (!in.rf->layer->transparent || s.image.pixels[size_t(sy) * size_t(W) + size_t(sx)]))
							inPiece[size_t(sy) * size_t(W) + size_t(sx)] = 1;
					}
			}
			for (size_t i = 0; i < owner.size(); i++)
				stillHidden += inPiece[i] && owner[i] < 0;

			fs::create_directories(packSheet.parent_path(), ec);
			std::vector<uint8_t> rgb(size_t(base.w) * size_t(base.h) * 3);
			for (size_t i = 0, n = size_t(base.w) * size_t(base.h); i < n; i++)
				for (int c = 0; c < 3; c++)
					rgb[i * 3 + size_t(c)] = base.px[i * 4 + size_t(c)];
			if (!writePng(packSheet, rgb.data(), base.w, base.h, 3)) {
				std::fprintf(stderr, "cannot write %s\n", packSheet.string().c_str());
				return 1;
			}
			if (withMask && !writePng(packMask, mask.data(), W * N, H * N, 1)) {
				std::fprintf(stderr, "cannot write %s\n", packMask.string().c_str());
				return 1;
			}
			std::printf("  sheet %d: %zu piece(s), %ld pixel(s) repainted at %dx (from %s)%s\n", sheetRes, piecesDone.size(),
			            painted, N, baseFrom.c_str(), withMask ? ", mask written" : "");
			if (stillHidden)
				std::printf("    %ld pixel(s) of those pieces are covered in every repainted frame: kept as they were\n",
				            stillHidden);
			for (const auto &[key, c] : conflicts) {
				const double mean = c.diff / double(std::max<long>(c.pixels, 1));
				std::string winner;
				long most = -1;
				for (const auto &[f, n] : c.winners)
					if (n > most)
						most = n, winner = f;
				std::printf("    conflict: piece %d (table %d) is drawn differently by several frames (%ld pixel(s) differ, "
				            "by %.0f/255 on average): kept %s, where it is most visible, over %s\n",
				            key.second, key.first, c.pixels, mean, winner.c_str(), frameList(c.others).c_str());
				warnings++;
			}
		}
	}
	std::printf("%d warning(s), %d error(s)\n", warnings, errors);
	return errors ? 1 : 0;
}

} // namespace gob::frames
