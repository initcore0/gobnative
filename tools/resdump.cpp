// gob2native - TOT/EXT resource inspection tool
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage:
//   resdump <gamedir> <TOTBASE>               list resources of e.g. GOB00
//   resdump <gamedir> <TOTBASE> <outdir>      also write decodable sprites as BMP
//   resdump <gamedir> --avo                   summary of every Gobliiins .AVO level file
//   resdump <gamedir> --avo <NAME>            one .AVO in full (maps, way points, states)
// --game gob1|gob2|gob3 picks the game instead of detecting it.

#include "data/datafiles.h"
#include "engine/game.h"
#include "formats/avo.h"
#include "formats/tot.h"
#include "gfx/bmp.h"
#include "gfx/image.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace gob;

static void listResource(const TotFile &tot, uint16_t id, const fs::path *outDir, const Palette &pal) {
	auto res = tot.resource(id);
	if (!res) {
		std::printf("%5u  (missing)\n", id);
		return;
	}
	const auto &d = res->data;
	std::printf("%5u  %3dx%-3d %7zu  %02x %02x %02x %02x", id, res->width, res->height, d.size(),
	            d.size() > 0 ? d[0] : 0, d.size() > 1 ? d[1] : 0, d.size() > 2 ? d[2] : 0, d.size() > 3 ? d[3] : 0);

	if (d.size() == 768 || res->width == 0) {
		std::printf("  %s\n", d.size() == 768 ? "palette?" : "data");
		return;
	}
	auto img = decodeSprite(*res);
	std::printf("  %s\n", img ? "sprite" : "UNDECODED");
	if (img && outDir)
		writeBmp(*outDir / (tot.name() + "_" + std::to_string(id) + ".bmp"), img->pixels.data(), img->width, img->height, pal);
}

// --- .AVO files (docs/goblins-v1.md 1)

static void printObjectSummary(const char *kind, size_t i, const AvoObject &o) {
	std::printf("  %s %zu: pos (%d, %d) order %d state %d, multObj %u, %d states\n", kind, i, o.xPos, o.yPos,
	            o.order, o.state, o.multObjIndex, o.stateCount());
}

static void printStates(const AvoObject &o) {
	for (int s = 0; s < AvoObject::kStates; s++)
		for (int c = 0; c < AvoObject::kColumns; c++) {
			const auto &st = o.states[size_t(s)][size_t(c)];
			if (!st)
				continue;
			std::printf("    state %2d.%d: anim %d layer %3d unk %d %d", s, c, st->animation, st->layer, st->unk0,
			            st->unk1);
			if (st->sndFrame & 0xFF00)
				std::printf("  sounds 0x%04X 0x%04X freq 0x%04X rep 0x%04X", uint16_t(st->sndItem),
				            uint16_t(st->sndFrame), uint16_t(st->freq), uint16_t(st->repCount));
			else if (st->sndItem != -1)
				std::printf("  sound %d at frame %d, %d Hz x%d", st->sndItem, st->sndFrame, st->freq, st->repCount);
			std::printf("\n");
		}
}

static void dumpAvo(const std::string &name, const std::vector<uint8_t> &data, bool full) {
	std::string err;
	auto f = parseAvo(data, &err);
	if (!f) {
		std::printf("%s: %zu bytes, ERROR: %s\n", name.c_str(), data.size(), err.c_str());
		return;
	}
	std::printf("%s: %zu bytes, %zu read, %zu left; names %d/%d/%d, positions %d/%d, records %d\n", name.c_str(),
	            f->size, f->consumed, f->size - f->consumed, f->nameCounts[0], f->nameCounts[1], f->nameCounts[2],
	            f->gobPosCount, f->objPosCount, f->extraRecords);
	std::printf("  sounds:");
	for (const std::string &s : f->sounds)
		std::printf(" %s", s.c_str());
	std::printf("\n");
	for (size_t i = 0; i < f->goblins.size(); i++) {
		printObjectSummary("goblin", i, f->goblins[i]);
		if (full)
			printStates(f->goblins[i]);
	}
	for (size_t i = 0; i < f->objects.size(); i++) {
		printObjectSummary("object", i, f->objects[i]);
		if (full)
			printStates(f->objects[i]);
	}
	std::printf("  item -> object:");
	for (int16_t v : f->itemToObject)
		std::printf(" %d", v);
	std::printf("%s\n", f->hasItemToObject ? "" : " (none)");
	if (!full)
		return;

	std::printf("  pass map (26x28; . = 0):\n");
	for (int y = 0; y < AvoFile::kMapHeight; y++) {
		std::printf("    %2d ", y);
		for (int x = 0; x < AvoFile::kMapWidth; x++) {
			int v = f->passAt(x, y);
			std::printf("%c", v == 0 ? '.' : v > 0 && v < 10 ? char('0' + v) : '?');
		}
		std::printf("\n");
	}
	std::printf("  items map (26x28, hex; .. = 0):\n");
	for (int y = 0; y < AvoFile::kMapHeight; y++) {
		std::printf("    %2d", y);
		for (int x = 0; x < AvoFile::kMapWidth; x++) {
			int v = f->itemAt(x, y);
			if (v == 0)
				std::printf(" ..");
			else
				std::printf(" %02X", v & 0xFF);
		}
		std::printf("\n");
	}
	std::printf("  way points:");
	for (const auto &w : f->wayPoints)
		std::printf(" (%d,%d)", w.x, w.y);
	std::printf("\n  item positions:");
	for (const auto &p : f->itemPoses)
		std::printf(" (%d,%d,%d)", p.x, p.y, p.orient);
	std::printf("\n");
}

static int avoMain(DataFiles &files, const char *only) {
	std::vector<std::string> names;
	for (const std::string &n : files.list()) {
		std::string up = n;
		std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return char(std::toupper(c)); });
		if (up.size() > 4 && up.compare(up.size() - 4, 4, ".AVO") == 0)
			names.push_back(up);
	}
	std::sort(names.begin(), names.end());
	names.erase(std::unique(names.begin(), names.end()), names.end());
	if (only) {
		std::string want = only;
		std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return char(std::toupper(c)); });
		if (want.find('.') == std::string::npos)
			want += ".AVO";
		names = {want};
	}
	int bad = 0;
	for (const std::string &n : names) {
		auto data = files.read(n);
		if (!data) {
			std::printf("%s: not found\n", n.c_str());
			bad++;
			continue;
		}
		dumpAvo(n, *data, only != nullptr);
		bad += !parseAvo(*data).has_value();
	}
	if (!only)
		std::printf("%zu files, %d failed\n", names.size(), bad);
	return bad ? 1 : 0;
}

int main(int argc, char **argv) {
	const std::string gameKey = takeGameOption(argc, argv);
	if (argc < 3) {
		std::fprintf(stderr, "usage: %s <gamedir> <TOTBASE> [outdir] [--game gob1|gob2|gob3]\n"
		                     "       %s <gamedir> --avo [NAME]\n", argv[0], argv[0]);
		return 2;
	}
	if (!selectToolGame(argv[1], gameKey))
		return 2;

	DataFiles files(argv[1]);
	files.openAllArchives();
	if (std::string(argv[2]) == "--avo")
		return avoMain(files, argc >= 4 ? argv[3] : nullptr);

	TotFile tot;
	if (!tot.load(files, argv[2])) {
		std::fprintf(stderr, "cannot load %s.TOT\n", argv[2]);
		return 1;
	}

	const TotHeader &h = tot.header();
	std::printf("%s: v%d.%d, %zu bytes, script ends at %u, %d TOT res, %d EXT res, %d texts\n",
	            tot.name().c_str(), h.versionMajor, h.versionMinor, tot.bytes().size(), h.scriptEnd,
	            tot.totResourceCount(), tot.extResourceCount(), tot.textCount());

	// Use the first palette-sized resource found, if any, for BMP output.
	Palette pal = grayscalePalette();
	for (int pass = 0; pass < 2; pass++) {
		int count = pass == 0 ? tot.totResourceCount() : tot.extResourceCount();
		bool found = false;
		for (int i = 0; i < count && !found; i++) {
			auto r = tot.resource(uint16_t(pass == 0 ? i : 30000 + i));
			if (r && r->data.size() == 768)
				if (auto p = decodeVgaPalette(r->data)) {
					pal = *p;
					found = true;
					std::printf("using palette from resource %d\n", pass == 0 ? i : 30000 + i);
				}
		}
		if (found)
			break;
	}

	std::optional<fs::path> outDir;
	if (argc >= 4) {
		outDir = fs::path(argv[3]);
		fs::create_directories(*outDir);
	}
	const fs::path *out = outDir ? &*outDir : nullptr;

	for (int i = 0; i < tot.totResourceCount(); i++)
		listResource(tot, uint16_t(i), out, pal);
	for (int i = 0; i < tot.extResourceCount(); i++)
		listResource(tot, uint16_t(30000 + i), out, pal);
	return 0;
}
