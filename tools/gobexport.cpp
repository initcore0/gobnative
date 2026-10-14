// gob2native - image exporter for HD packs
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: gobexport <gamedir> <outdir> [--bleed N] [--only TOT] [--quiet]
//        gobexport <gamedir> --check-pack <packdir>
//        gobexport <gamedir> --scale-export <exportdir> <packdir> <N>
//        gobexport <gamedir> --frames <outdir> [--only TOT] [--guide GUIDEDIR]
//        gobexport <gamedir> --import-frames <framesdir> <exportdir> <packdir> [--scale N]
//                  [--alpha | --key-color RRGGBB [--key-tolerance T]] [--only TOT]
//
// --check-pack loads an HD pack as gobnative does and lists its problems
// (and decodes every picture). --scale-export makes a test pack from an
// export by plain nearest-neighbour enlargement (N = 2, 3, 4 or 8), to try
// the pipeline without an AI upscaler. --frames and --import-frames export
// every animation frame as its own picture and put repainted frames back
// into a pack's sheets (tools/frames.h). The game is detected from the
// folder (printed to stderr); --game gob1|gob2|gob3 picks it.
//
// Writes every image of every script file (TOT/EXT resources that decode as
// sprites) as AI-upscaler friendly files, one folder per TOT:
//
//   <outdir>/<TOT>/<resid>.png        RGB, the right palette applied; for
//                                     transparent images the transparent
//                                     pixels are filled by bleeding the
//                                     nearest opaque colours outward
//   <outdir>/<TOT>/<resid>.mask.png   8-bit gray mask (255 = opaque), only
//                                     for images drawn with transparency
//   <outdir>/<TOT>/<resid>.json       metadata: size, kind, palette, uses,
//                                     the animation pieces cut from it
//   <outdir>/index.json               every image, statistics, duplicates
//   <outdir>/README.txt               the workflow
//
// Which palette goes with which picture is worked out from the bytecode
// (docs/hdpacks.md): the palette a function fades in after loading the
// picture, else the room's palette (the one it loads most often), with a
// fixed table for INTER's three random pictures; menus that bring no palette
// of their own are shown over a room and get GOB00's.

#include "data/datafiles.h"
#include "engine/game.h"
#include "formats/tot.h"
#include "frames.h"
#include "gfx/hdpack.h"
#include "gfx/image.h"
#include "gfx/png.h"
#include "gfx/smooth.h"
#include "script/disasm.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace gob;

namespace {

const char *const kKindNames[] = {"background", "animation", "cursor", "overlay", "sprite", "other"};
enum Kind { Background, Animation, Cursor, Overlay, SpriteKind, Other, KindCount };

struct Piece {
	int table = 0; // piece table resource
	int index = 0;
	int left = 0, top = 0, right = 0, bottom = 0; // inclusive
};

struct UseInfo {
	std::string op;
	uint32_t offset = 0;
	int surface = 0, transparency = 0, anim = -1, picture = -1, descWidth = 0, descHeight = 0;
};

struct ImageRec {
	int resId = 0;
	Image image;
	std::vector<UseInfo> uses;
	std::set<int> pieceTables;
	Kind kind = Other;
	bool transparent = false;
	int paletteRes = -1;
	std::string paletteTot, paletteSource;
	int transparentPixels = 0, colorsUsed = 0;
	std::string sameAs; // "TOT/resid" of an identical earlier image
};

std::string jsonString(const std::string &s) {
	std::string o = "\"";
	for (unsigned char c : s) {
		if (c == '"' || c == '\\')
			o += '\\', o += char(c);
		else if (c < 0x20) {
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\u%04x", c);
			o += buf;
		} else
			o += char(c);
	}
	return o + "\"";
}

std::string hex(uint32_t v) {
	char buf[16];
	std::snprintf(buf, sizeof(buf), "\"0x%04X\"", v);
	return buf;
}

// Fills the transparent pixels (mask 0) of `rgb` with colours bled outward
// from the opaque ones: each pass gives every unfilled pixel that touches a
// filled one the weighted average of its filled 8-neighbours (sides count
// twice). After `passes` the rest gets the mean opaque colour, so the
// upscaler sees no dark halo around shapes.
void bleed(std::vector<uint8_t> &rgb, const std::vector<uint8_t> &mask, int w, int h, int passes) {
	std::vector<uint8_t> filled(mask.size());
	double sum[3] = {0, 0, 0};
	size_t opaque = 0;
	for (size_t i = 0; i < mask.size(); i++) {
		filled[i] = mask[i] ? 1 : 0;
		if (mask[i]) {
			for (int c = 0; c < 3; c++)
				sum[c] += rgb[i * 3 + size_t(c)];
			opaque++;
		}
	}
	if (opaque == 0 || opaque == mask.size())
		return;
	std::vector<uint8_t> next = filled;
	for (int pass = 0; pass < passes; pass++) {
		bool any = false;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++) {
				size_t i = size_t(y) * size_t(w) + size_t(x);
				if (filled[i])
					continue;
				int acc[3] = {0, 0, 0}, wsum = 0;
				for (int dy = -1; dy <= 1; dy++)
					for (int dx = -1; dx <= 1; dx++) {
						int nx = x + dx, ny = y + dy;
						if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= w || ny >= h)
							continue;
						size_t n = size_t(ny) * size_t(w) + size_t(nx);
						if (!filled[n])
							continue;
						int wt = (dx == 0 || dy == 0) ? 2 : 1;
						for (int c = 0; c < 3; c++)
							acc[c] += rgb[n * 3 + size_t(c)] * wt;
						wsum += wt;
					}
				if (wsum) {
					for (int c = 0; c < 3; c++)
						rgb[i * 3 + size_t(c)] = uint8_t((acc[c] + wsum / 2) / wsum);
					next[i] = 1;
					any = true;
				}
			}
		filled = next;
		if (!any)
			break;
	}
	for (size_t i = 0; i < mask.size(); i++)
		if (!filled[i])
			for (int c = 0; c < 3; c++)
				rgb[i * 3 + size_t(c)] = uint8_t(sum[c] / double(opaque) + 0.5);
}

std::vector<Piece> loadPieces(const TotFile &tot, int tableId) {
	std::vector<Piece> out;
	auto res = tot.resource(uint16_t(tableId));
	if (!res)
		return out;
	const auto &d = res->data;
	auto i16 = [&](size_t o) { return int(int16_t(d[o] | (d[o + 1] << 8))); };
	for (size_t i = 0; i + 8 <= d.size(); i += 8) {
		Piece p;
		p.table = tableId;
		p.index = int(i / 8);
		p.left = i16(i);
		p.right = i16(i + 2);
		p.top = i16(i + 4);
		p.bottom = i16(i + 6);
		if (p.left == -1 && p.right == -1 && p.top == -1 && p.bottom == -1)
			continue; // unused filler record
		out.push_back(p);
	}
	return out;
}

std::vector<std::string> totNames(DataFiles &files) {
	std::vector<std::string> names;
	for (const std::string &n : files.list()) {
		if (n.size() > 4 && n.compare(n.size() - 4, 4, ".TOT") == 0)
			names.push_back(n.substr(0, n.size() - 4));
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	return names;
}

const GameInfo *gGame = nullptr; // the game being exported

// The palette resource used most often by a TOT's palLoads (ties: the first).
int mainPalette(const std::vector<ResourceUse> &uses);

// The palette the room is shown in. Gobliiins' levels load up to three
// (the story text's, the room's, the AGAIN/LOAD/EXIT screen's), once each:
// the room's is the one faded in after the level has loaded its objects.
// Otherwise the one used most often.
int roomPalette(const std::vector<ResourceUse> &uses) {
	if (gGame && gGame->id == GameId::Gob1)
		for (size_t i = 0; i < uses.size(); i++)
			if (uses[i].kind == ResourceUse::LevelObjects)
				for (size_t j = i + 1; j < uses.size() && uses[j].function == uses[i].function; j++)
					if (uses[j].kind == ResourceUse::Palette)
						return uses[j].resId;
	return mainPalette(uses);
}

int mainPalette(const std::vector<ResourceUse> &uses) {
	std::map<int, int> count;
	std::vector<int> order;
	for (const ResourceUse &u : uses)
		if (u.kind == ResourceUse::Palette) {
			if (!count[u.resId]++)
				order.push_back(u.resId);
		}
	int best = -1, bestCount = 0;
	for (int id : order)
		if (count[id] > bestCount) {
			best = id;
			bestCount = count[id];
		}
	return best;
}

const char *const kReadme = R"(gobnative image export
======================

This folder holds every picture of your copy of Gobliins 2, written by
gobexport for upscaling with AI tools (ESRGAN-style models, for example in
chaiNNer) and making an HD pack for gobnative. It is made from your own
game files: keep it for yourself, don't share it.

Layout: one folder per script file (GOB00 ... GOB22 are the rooms, INTRO0
and FINAL the intro and the ending, INTER the pictures between rooms, MENU
the in-game menu), and in it, per picture:

  <id>.png        the picture in full colour. Transparent pixels are filled
                  with the nearest colours ("bleeding"), so upscalers don't
                  draw dark halos around shapes.
  <id>.mask.png   which pixels are transparent (black) or opaque (white);
                  only for pictures drawn with transparency (animation
                  sheets, sprites, cursors). Don't upscale these.
  <id>.json       what the picture is: its size, kind (background,
                  animation sheet, cursor, overlay, sprite, other), the
                  palette used, and for animation sheets the rectangles of
                  the pieces the game cuts out of it.

index.json lists all pictures and which ones are identical.

Workflow
--------

1. Upscale the .png files, not the .mask.png files, at 2x, 3x, 4x or 8x,
   writing the results into a new folder with the same sub-folders and the
   same file names. In chaiNNer: "Load Images" on this folder (with
   subdirectories; skip files ending in .mask.png), your upscale model,
   "Save Image" into the pack folder with the relative path kept.
   - Backgrounds (kind "background"): a model made for painted art.
   - Animation sheets: upscale the whole sheet as one image, never single
     frames; the game cuts the frames out of the sheet by its rectangles.
   - Every picture of one pack must use the same factor.
2. Put the results into
     ~/Library/Application Support/gobnative/Gobliins2/HDPacks/<pack name>/
   keeping the layout: HDPacks/<pack name>/GOB00/30007.png and so on.
   Masks are not needed: gobnative rebuilds the transparency from the
   original pictures (an upscaled <id>.mask.png, if you make one, is used to
   smooth the edges).
3. In gobnative open the overlay (` key), Video tab, "HD pack": pick the
   pack. The list shows problems (wrong sizes, mixed factors, unknown
   files).

Pictures you leave out simply keep their original pixels.
)";

// The README for `game`: kReadme is Gobliins 2's.
std::string readme(const GameInfo &game) {
	std::string text = kReadme;
	auto replace = [&text](const std::string &from, const std::string &to) {
		if (size_t at = text.find(from); at != std::string::npos)
			text.replace(at, from.size(), to);
	};
	if (game.id == GameId::Gob1) {
		replace("your copy of Gobliins 2", "your copy of Gobliiins");
		replace("Layout: one folder per script file (GOB00 ... GOB22 are the rooms, INTRO0\n"
		        "and FINAL the intro and the ending, INTER the pictures between rooms, MENU\n"
		        "the in-game menu), and in it, per picture:",
		        "Layout: one folder per script file (AVT00 and AVT001 ... AVT022 are the 22\n"
		        "levels, INTRO the intro with the title and credits, FINAL the ending,\n"
		        "INTER the pictures between levels and the game over and access code\n"
		        "screens), and in it, per picture:");
		replace("~/Library/Application Support/gobnative/Gobliins2/HDPacks/<pack name>/",
		        "~/Library/Application Support/gobnative/Gobliiins/HDPacks/<pack name>/");
		replace("HDPacks/<pack name>/GOB00/30007.png", "HDPacks/<pack name>/AVT003/30010.png");
	} else if (game.id == GameId::Gob3) {
		replace("your copy of Gobliins 2", "your copy of Goblins Quest 3");
		replace("~/Library/Application Support/gobnative/Gobliins2/HDPacks/<pack name>/",
		        "~/Library/Application Support/gobnative/Goblins3/HDPacks/<pack name>/");
	}
	return text;
}

int checkPack(const char *gameDir, const fs::path &packDir) {
	GameImages originals(gameDir);
	HdPack pack;
	bool ok = pack.open(packDir, [&](const std::string &tot, int id) { return originals(tot, id); });
	size_t decoded = 0, failed = 0;
	if (ok)
		for (const HdKey &k : pack.keys()) {
			if (pack.find(k.tot, k.resId))
				decoded++;
			else
				failed++;
			pack.clearCache();
		}
	for (const HdIssue &i : pack.issues()) {
		static const char *const sev[] = {"info", "warning", "error"};
		std::printf("%-7s %s: %s\n", sev[i.severity], i.file.empty() ? "(pack)" : i.file.c_str(), i.message.c_str());
	}
	std::printf("%s: %zu pictures at %dx%s, %d error(s); %zu decoded, %zu failed\n", packDir.string().c_str(),
	            pack.size(), pack.scale(), pack.mixedScales() ? " (mixed scales)" : "", pack.errorCount(), decoded,
	            failed);
	return ok && pack.errorCount() == 0 ? 0 : 1;
}

int scaleExport(const fs::path &exportDir, const fs::path &packDir, int scale) {
	if (scale != 2 && scale != 3 && scale != 4 && scale != 8) {
		std::fprintf(stderr, "scale must be 2, 3, 4 or 8\n");
		return 2;
	}
	int count = 0;
	std::error_code ec;
	for (auto it = fs::recursive_directory_iterator(exportDir, ec); !ec && it != fs::recursive_directory_iterator();
	     it.increment(ec)) {
		const fs::path p = it->path();
		const std::string name = p.filename().string();
		if (!it->is_regular_file() || name.size() < 5 || name.compare(name.size() - 4, 4, ".png") != 0 ||
		    name.find(".mask.") != std::string::npos)
			continue;
		PngImage img;
		std::string err;
		if (!readPng(p, img, &err)) {
			std::fprintf(stderr, "%s: %s\n", p.string().c_str(), err.c_str());
			continue;
		}
		const int w = img.info.width, h = img.info.height;
		std::vector<uint8_t> out(size_t(w) * size_t(h) * size_t(scale) * size_t(scale) * 3);
		for (int y = 0; y < h * scale; y++)
			for (int x = 0; x < w * scale; x++)
				for (int c = 0; c < 3; c++)
					out[(size_t(y) * size_t(w * scale) + size_t(x)) * 3 + size_t(c)] =
					    img.rgba[(size_t(y / scale) * size_t(w) + size_t(x / scale)) * 4 + size_t(c)];
		const fs::path dst = packDir / fs::relative(p, exportDir);
		fs::create_directories(dst.parent_path(), ec);
		if (!writePng(dst, out.data(), w * scale, h * scale, 3)) {
			std::fprintf(stderr, "cannot write %s\n", dst.string().c_str());
			return 1;
		}
		count++;
	}
	std::printf("%d pictures enlarged %dx into %s\n", count, scale, packDir.string().c_str());
	return 0;
}

// The palette an animation sheet is exported with: the palette its function
// fades in after loading it, else the room's, else GOB00's (the same choice
// as the picture export for animation sheets).
frames::PaletteFor sheetPalettes(DataFiles &files) {
	auto inherited = std::make_shared<std::optional<Palette>>();
	return [&files, inherited](const TotFile &tot, const std::vector<ResourceUse> &uses, int sheet) {
		int pal = -1;
		for (size_t i = 0; i < uses.size() && pal < 0; i++) {
			if (uses[i].kind != ResourceUse::AnimPicture || uses[i].resId != sheet)
				continue;
			for (size_t j = i + 1; j < uses.size(); j++) {
				if (uses[j].function != uses[i].function)
					break;
				if (uses[j].kind == ResourceUse::Palette && uses[j].offset > uses[i].offset) {
					pal = uses[j].resId;
					break;
				}
			}
			break;
		}
		if (pal < 0)
			pal = roomPalette(uses);
		if (pal >= 0)
			if (auto r = tot.resource(uint16_t(pal)))
				if (auto p = decodeVgaPalette(r->data))
					return *p;
		if (!*inherited) {
			*inherited = grayscalePalette();
			TotFile g;
			if (g.load(files, "GOB00")) {
				Disassembler d(g);
				d.run();
				if (auto r = g.resource(uint16_t(mainPalette(d.resourceUses()))))
					if (auto p = decodeVgaPalette(r->data))
						*inherited = *p;
			}
		}
		return **inherited;
	};
}

int framesMode(int argc, char **argv) {
	DataFiles files(argv[1]);
	files.openAllArchives();
	const bool import = !std::strcmp(argv[2], "--import-frames");
	const int first = import ? 6 : 4;
	std::string only;
	fs::path guideDir;
	frames::ImportOptions opt;
	for (int i = first; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--only" && i + 1 < argc)
			only = argv[++i];
		else if (!import && a == "--guide" && i + 1 < argc)
			guideDir = argv[++i];
		else if (import && a == "--scale" && i + 1 < argc)
			opt.scale = std::atoi(argv[++i]);
		else if (import && a == "--alpha")
			opt.alpha = frames::ImportOptions::Alpha::FromFrames;
		else if (import && a == "--key-color" && i + 1 < argc) {
			std::string hexColor = argv[++i];
			if (!hexColor.empty() && hexColor[0] == '#')
				hexColor.erase(0, 1);
			const unsigned long v = std::strtoul(hexColor.c_str(), nullptr, 16);
			opt.alpha = frames::ImportOptions::Alpha::KeyColor;
			opt.key = {uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v), 255};
		} else if (import && a == "--key-tolerance" && i + 1 < argc)
			opt.keyTolerance = std::atoi(argv[++i]);
		else {
			std::fprintf(stderr, "unknown option %s\n", a.c_str());
			return 2;
		}
	}
	if (import) {
		if (opt.scale != 0 && opt.scale != 1 && opt.scale != 2 && opt.scale != 3 && opt.scale != 4 && opt.scale != 8) {
			std::fprintf(stderr, "--scale must be 1, 2, 3, 4 or 8\n");
			return 2;
		}
		opt.framesDir = argv[3];
		opt.exportDir = argv[4];
		opt.packDir = argv[5];
		opt.only = only;
		const int rc = frames::importFrames(files, opt, sheetPalettes(files));
		if (rc != 0)
			return rc;
		std::printf("\n");
		return checkPack(argv[1], opt.packDir);
	}
	std::vector<std::string> tots;
	for (const std::string &n : totNames(files))
		if (only.empty() || n == only)
			tots.push_back(n);
	if (tots.empty()) {
		std::fprintf(stderr, "no script files%s%s\n", only.empty() ? "" : " named ", only.c_str());
		return 1;
	}
	return frames::exportFrames(files, tots, argv[3], guideDir, sheetPalettes(files));
}

} // namespace

int main(int argc, char **argv) {
	const std::string gameKey = takeGameOption(argc, argv);
	if (argc < 3) {
		std::fprintf(stderr,
		             "usage: %s <gamedir> <outdir> [--bleed N] [--only TOT] [--quiet]\n"
		             "       %s <gamedir> --check-pack <packdir>\n"
		             "       %s <gamedir> --scale-export <exportdir> <packdir> <N>\n"
		             "       %s <gamedir> --frames <outdir> [--only TOT] [--guide GUIDEDIR]\n"
		             "       %s <gamedir> --import-frames <framesdir> <exportdir> <packdir> [--scale N]\n"
		             "                 [--alpha | --key-color RRGGBB [--key-tolerance T]] [--only TOT]\n"
		             "--game gob1|gob2|gob3 picks the game instead of detecting it.\n",
		             argv[0], argv[0], argv[0], argv[0], argv[0]);
		return 2;
	}
	gGame = selectToolGame(argv[1], gameKey);
	if (!gGame)
		return 2;
	if (!std::strcmp(argv[2], "--check-pack") && argc >= 4)
		return checkPack(argv[1], argv[3]);
	if (!std::strcmp(argv[2], "--scale-export") && argc >= 6)
		return scaleExport(argv[3], argv[4], std::atoi(argv[5]));
	if ((!std::strcmp(argv[2], "--frames") && argc >= 4) || (!std::strcmp(argv[2], "--import-frames") && argc >= 6))
		return framesMode(argc, argv);
	const fs::path outDir = argv[2];
	int bleedPasses = 8;
	std::string only;
	bool quiet = false;
	for (int i = 3; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--bleed" && i + 1 < argc)
			bleedPasses = std::max(0, std::atoi(argv[++i]));
		else if (a == "--only" && i + 1 < argc)
			only = argv[++i];
		else if (a == "--quiet")
			quiet = true;
		else {
			std::fprintf(stderr, "unknown option %s\n", a.c_str());
			return 2;
		}
	}

	DataFiles files(argv[1]);
	files.openAllArchives();
	std::vector<std::string> tots = totNames(files);
	if (tots.empty()) {
		std::fprintf(stderr, "no script files found in %s\n", argv[1]);
		return 1;
	}

	std::error_code ec;
	fs::create_directories(outDir, ec);
	if (!fs::is_directory(outDir)) {
		std::fprintf(stderr, "cannot create %s\n", outDir.string().c_str());
		return 1;
	}

	// GOB00's room palette, for the menus that are drawn over whatever room
	// is running and bring no palette of their own.
	Palette inheritedPal = grayscalePalette();
	int inheritedRes = -1;
	{
		TotFile g;
		if (g.load(files, "GOB00")) {
			Disassembler d(g);
			d.run();
			inheritedRes = mainPalette(d.resourceUses());
			if (auto r = g.resource(uint16_t(inheritedRes)))
				if (auto p = decodeVgaPalette(r->data))
					inheritedPal = *p;
		}
	}
	// Gobliins 2's INTER shows one of three pictures at random, each with
	// its palette (Gobliiins' three share the room's palette).
	std::map<std::pair<std::string, int>, int> overrides;
	if (gGame->id == GameId::Gob2)
		overrides = {{{"INTER", 0}, 3}, {{"INTER", 1}, 4}, {{"INTER", 2}, 5}};
	else if (gGame->id == GameId::Gob1) // the voodoo doll, drawn over the feast
		overrides = {{{"INTRO", 1}, 0}};

	std::ostringstream index;
	index << "{\n  \"format\": \"gob2native-export\",\n  \"version\": 1,\n  \"images\": [\n";
	bool firstEntry = true;
	int kindCount[KindCount] = {}, masks = 0, total = 0, undecodable = 0;
	long long pixels[KindCount] = {};
	std::map<std::string, std::string> firstByHash; // content hash -> "TOT/id"
	std::vector<std::pair<std::string, std::string>> duplicates;
	int sizeHist[4] = {}; // <= 32x32, <= 100x100, < 320 wide, 320 wide

	for (const std::string &name : tots) {
		if (!only.empty() && name != only)
			continue;
		TotFile tot;
		if (!tot.load(files, name)) {
			std::fprintf(stderr, "%s: cannot load\n", name.c_str());
			continue;
		}
		Disassembler dis(tot);
		dis.run();
		const std::vector<ResourceUse> &uses = dis.resourceUses();
		const int roomPal = roomPalette(uses);

		std::map<int, ImageRec> images;
		auto rec = [&](int id) -> ImageRec * {
			auto it = images.find(id);
			if (it != images.end())
				return &it->second;
			auto res = tot.resource(uint16_t(id));
			if (!res || res->width <= 0)
				return nullptr;
			auto img = decodeSprite(*res);
			if (!img)
				return nullptr;
			ImageRec &r = images[id];
			r.resId = id;
			r.image = std::move(*img);
			return &r;
		};

		// Images the scripts load, with the palette fade that follows them.
		for (size_t i = 0; i < uses.size(); i++) {
			const ResourceUse &u = uses[i];
			if (u.kind == ResourceUse::Palette || u.kind == ResourceUse::LevelObjects)
				continue;
			ImageRec *r = rec(u.resId);
			if (!r)
				continue;
			UseInfo info;
			info.offset = u.offset;
			info.surface = u.surface;
			info.transparency = u.transparency;
			Kind k = Other;
			switch (u.kind) {
			case ResourceUse::Sprite:
				info.op = "loadSpriteContent";
				if (u.surface == 21 || u.surface == 101)
					k = u.transparency ? Overlay : Background;
				else
					k = SpriteKind;
				break;
			case ResourceUse::Cursor:
				info.op = "loadCursor";
				k = Cursor;
				break;
			case ResourceUse::AnimPicture:
				info.op = "loadAnim";
				info.anim = u.animResId;
				info.picture = u.picture;
				info.descWidth = u.width;
				info.descHeight = u.height;
				if (u.pictDescId >= 0)
					r->pieceTables.insert(u.pictDescId);
				k = Animation;
				break;
			case ResourceUse::MultSprite:
				info.op = "multStaticKey";
				k = Background;
				break;
			default: break;
			}
			// Precedence: animation, cursor, overlay, sprite, background.
			static const int rank[KindCount] = {1, 5, 4, 3, 2, 0};
			if (r->uses.empty() || rank[k] > rank[r->kind])
				r->kind = k;
			r->uses.push_back(info);

			// Gobliiins' levels show their cursors over the room (also on the
			// AGAIN/LOAD/EXIT screen, which brings its own palette).
			const bool levelCursor = u.kind == ResourceUse::Cursor && gGame->id == GameId::Gob1 &&
			                         std::any_of(uses.begin(), uses.end(), [](const ResourceUse &x) {
				                         return x.kind == ResourceUse::LevelObjects;
			                         });
			if (r->paletteRes < 0 && !levelCursor) {
				int pal = -1;
				for (size_t j = i + 1; j < uses.size(); j++) {
					if (uses[j].function != u.function)
						break;
					if (uses[j].kind == ResourceUse::Palette && uses[j].offset > u.offset) {
						pal = uses[j].resId;
						break;
					}
				}
				if (pal >= 0) {
					r->paletteRes = pal;
					r->paletteSource = "faded in after the picture is loaded";
				}
			}
		}

		// Decodable pictures no script loads (kept for completeness).
		for (int pass = 0; pass < 2; pass++) {
			int count = pass == 0 ? tot.totResourceCount() : tot.extResourceCount();
			for (int i = 0; i < count; i++) {
				int id = pass == 0 ? i : 30000 + i;
				if (images.count(id))
					continue;
				auto res = tot.resource(uint16_t(id));
				if (!res || res->width <= 0 || res->height <= 0)
					continue;
				if (!rec(id))
					undecodable++;
			}
		}

		for (auto &[id, r] : images) {
			auto ov = overrides.find({name, id});
			if (ov != overrides.end()) {
				r.paletteRes = ov->second;
				r.paletteSource = gGame->id == GameId::Gob2 ? "known (INTER picks a picture and its palette at random)"
				                                            : "known (drawn while that palette is shown)";
			}
			r.paletteTot = name;
			if (r.paletteRes < 0 && r.uses.empty()) {
				// Not used by the scripts: a palette stored right after it
				// is most likely its own (INTRO0 has such a pair).
				auto pr = tot.resource(uint16_t(id + 1));
				if (pr && pr->width == 0 && pr->data.size() == 768) {
					r.paletteRes = id + 1;
					r.paletteSource = "guess: not used by the scripts; the palette stored after it";
				}
			}
			if (r.paletteRes < 0 && roomPal >= 0) {
				r.paletteRes = roomPal;
				r.paletteSource = r.uses.empty() ? "guess: not used by the scripts; the room's palette"
				                                 : "the room's palette";
			}
			Palette pal = grayscalePalette();
			bool havePal = false;
			if (r.paletteRes >= 0)
				if (auto pr = tot.resource(uint16_t(r.paletteRes)))
					if (auto p = decodeVgaPalette(pr->data)) {
						pal = *p;
						havePal = true;
					}
			if (!havePal && inheritedRes >= 0) {
				pal = inheritedPal;
				r.paletteTot = "GOB00";
				r.paletteRes = inheritedRes;
				r.paletteSource = "inherited: drawn over the running room, exported with GOB00's palette";
				havePal = true;
			}
			if (!havePal)
				r.paletteSource = "none found (grayscale)";

			const int w = r.image.width, h = r.image.height;
			const size_t n = size_t(w) * size_t(h);
			std::vector<uint8_t> rgb(n * 3), mask(n);
			bool used[256] = {};
			for (size_t i = 0; i < n; i++) {
				uint8_t idx = r.image.pixels[i];
				used[idx] = true;
				mask[i] = idx ? 255 : 0;
				r.transparentPixels += idx == 0;
				rgb[i * 3] = pal[idx].r;
				rgb[i * 3 + 1] = pal[idx].g;
				rgb[i * 3 + 2] = pal[idx].b;
			}
			for (bool b : used)
				r.colorsUsed += b;
			if (r.kind == Other)
				r.transparent = r.transparentPixels > 0 && w < 320;
			else
				r.transparent = r.kind != Background;
			if (r.transparent && r.transparentPixels == 0)
				r.transparent = false; // nothing to mask

			// Identical pictures (same pixels, same palette).
			{
				uint32_t crc = crc32(r.image.pixels.data(), n);
				crc = crc32(rgb.data(), rgb.size(), crc);
				std::string key = std::to_string(w) + "x" + std::to_string(h) + ":" + std::to_string(crc);
				auto it = firstByHash.find(key);
				if (it == firstByHash.end())
					firstByHash[key] = name + "/" + std::to_string(id);
				else {
					r.sameAs = it->second;
					duplicates.push_back({name + "/" + std::to_string(id), it->second});
				}
			}

			if (r.transparent)
				bleed(rgb, mask, w, h, bleedPasses);

			const fs::path dir = outDir / name;
			fs::create_directories(dir, ec);
			const std::string base = std::to_string(id);
			if (!writePng(dir / (base + ".png"), rgb.data(), w, h, 3)) {
				std::fprintf(stderr, "cannot write %s\n", (dir / (base + ".png")).string().c_str());
				return 1;
			}
			if (r.transparent) {
				writePng(dir / (base + ".mask.png"), mask.data(), w, h, 1);
				masks++;
			}

			// Metadata.
			std::ostringstream j;
			j << "{\n  \"tot\": " << jsonString(name) << ",\n  \"resource\": " << id << ",\n  \"width\": " << w
			  << ",\n  \"height\": " << h << ",\n  \"kind\": " << jsonString(kKindNames[r.kind])
			  << ",\n  \"transparent\": " << (r.transparent ? "true" : "false")
			  << ",\n  \"transparent_pixels\": " << r.transparentPixels << ",\n  \"colors_used\": " << r.colorsUsed
			  << ",\n  \"palette\": {\"tot\": " << jsonString(r.paletteTot) << ", \"resource\": " << r.paletteRes
			  << ", \"source\": " << jsonString(r.paletteSource) << "},\n  \"files\": {\"image\": "
			  << jsonString(base + ".png") << ", \"mask\": " << (r.transparent ? jsonString(base + ".mask.png") : "null")
			  << "},\n";
			if (!r.sameAs.empty())
				j << "  \"same_as\": " << jsonString(r.sameAs) << ",\n";
			j << "  \"uses\": [";
			for (size_t i = 0; i < r.uses.size(); i++) {
				const UseInfo &u = r.uses[i];
				j << (i ? ",\n    " : "\n    ") << "{\"op\": " << jsonString(u.op) << ", \"offset\": " << hex(u.offset);
				if (u.op == "loadAnim")
					j << ", \"anim\": " << u.anim << ", \"picture\": " << u.picture << ", \"sprite_size\": [" << u.descWidth
					  << ", " << u.descHeight << "]";
				else if (u.op != "multStaticKey")
					j << (u.op == "loadCursor" ? ", \"cursor\": " : ", \"surface\": ") << u.surface;
				if (u.op == "loadSpriteContent")
					j << ", \"transparency\": " << u.transparency;
				j << "}";
			}
			j << (r.uses.empty() ? "]" : "\n  ]");
			j << ",\n  \"pieces\": [";
			bool firstPiece = true;
			for (int table : r.pieceTables)
				for (const Piece &p : loadPieces(tot, table)) {
					j << (firstPiece ? "\n    " : ",\n    ") << "{\"table\": " << p.table << ", \"index\": " << p.index
					  << ", \"rect\": [" << p.left << ", " << p.top << ", " << p.right << ", " << p.bottom << "]}";
					firstPiece = false;
				}
			j << (firstPiece ? "]" : "\n  ]") << "\n}\n";
			std::ofstream(dir / (base + ".json")) << j.str();

			index << (firstEntry ? "    " : ",\n    ") << "{\"tot\": " << jsonString(name) << ", \"resource\": " << id
			      << ", \"width\": " << w << ", \"height\": " << h << ", \"kind\": " << jsonString(kKindNames[r.kind])
			      << ", \"transparent\": " << (r.transparent ? "true" : "false") << ", \"image\": "
			      << jsonString(name + "/" + base + ".png");
			if (!r.sameAs.empty())
				index << ", \"same_as\": " << jsonString(r.sameAs);
			index << "}";
			firstEntry = false;

			kindCount[r.kind]++;
			pixels[r.kind] += (long long)n;
			total++;
			sizeHist[w <= 32 && h <= 32 ? 0 : w <= 100 && h <= 100 ? 1 : w < 320 ? 2 : 3]++;
			if (!quiet)
				std::printf("%-8s %5d  %3dx%-3d  %-10s %s pal %s/%d\n", name.c_str(), id, w, h, kKindNames[r.kind],
				            r.transparent ? "mask" : "    ", r.paletteTot.c_str(), r.paletteRes);
		}
	}

	index << "\n  ],\n  \"duplicates\": [";
	for (size_t i = 0; i < duplicates.size(); i++)
		index << (i ? ",\n    " : "\n    ") << "[" << jsonString(duplicates[i].first) << ", "
		      << jsonString(duplicates[i].second) << "]";
	index << (duplicates.empty() ? "]" : "\n  ]") << ",\n  \"counts\": {";
	for (int k = 0; k < KindCount; k++)
		index << (k ? ", " : "") << jsonString(kKindNames[k]) << ": " << kindCount[k];
	index << "}\n}\n";
	std::ofstream(outDir / "index.json") << index.str();
	std::ofstream(outDir / "README.txt") << readme(*gGame);

	std::printf("%d images (%d with masks, %zu identical to an earlier one), %d undecodable\n", total, masks,
	            duplicates.size(), undecodable);
	for (int k = 0; k < KindCount; k++)
		std::printf("  %-10s %4d  %8.2f Mpixel\n", kKindNames[k], kindCount[k], pixels[k] / 1e6);
	std::printf("  sizes: %d up to 32x32, %d up to 100x100, %d narrower than 320, %d 320 wide\n", sizeHist[0],
	            sizeHist[1], sizeHist[2], sizeHist[3]);
	return 0;
}
