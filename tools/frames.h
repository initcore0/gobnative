// gob2native - per-object frame export and import for HD packs
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game cuts every animation frame out of a sheet by exact piece
// rectangles (docs/animation.md 1.2), so a sheet repainted as a whole
// scrambles them. These tools let the player repaint frames instead:
//
//   gobexport <gamedir> --frames OUTDIR [--only TOT] [--guide GUIDEDIR]
//     writes every frame of every animation layer as its own RGBA PNG,
//     <OUTDIR>/<TOT>/anim<A>/layer<L>/frame<F>.png (cropped to the frame,
//     transparent where the game draws nothing), a contact sheet and a JSON
//     description of the pieces per layer, layers.txt (which objects use
//     which layer, from a gobrun --guide-at folder) and a README.
//
//   gobexport <gamedir> --import-frames FRAMESDIR EXPORTDIR PACKDIR [--scale N]
//             [--alpha | --key-color RRGGBB [--key-tolerance T]] [--only TOT]
//     puts repainted frames (same layout as the export, any size) back into
//     the sheets: <PACKDIR>/<TOT>/<sheet>.png at N times, starting from the
//     pack's sheet if it has one, else the picture export's sheet
//     (EXPORTDIR, a plain gobexport folder) enlarged by nearest neighbour.
//
// Frames belong to animation resources, not sheets: one frame often combines
// pieces of several sheets (a goblin holding an object), so the folders are
// per animation resource and the JSON names the sheet of every piece.

#pragma once

#include "data/datafiles.h"
#include "formats/anim.h"
#include "formats/tot.h"
#include "gfx/image.h"
#include "script/disasm.h"
#include "toolimage.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gob::frames {

// The palette an animation sheet is shown with (the exporter's choice).
using PaletteFor = std::function<Palette(const TotFile &tot, const std::vector<ResourceUse> &uses, int sheetRes)>;

struct Sheet {
	int resId = 0;
	Image image;
	Palette palette{};
};

struct Piece {
	int order = 0;      // drawing order within the frame
	int picture = 0;    // picture index in the anim's descriptor
	int sheet = 0;      // its sprite resource
	int pieceTable = 0; // piece table resource
	int pieceIndex = 0;
	AnimPieceRect src;  // in the sheet
	int x = 0, y = 0;   // top-left in the frame image
	int offX = 0, offY = 0; // offset from the layer's origin (what the game adds to the position)
};

struct Frame {
	int index = 0;
	int left = 0, top = 0, w = 0, h = 0; // bounding box, relative to the layer's origin
	std::vector<Piece> pieces;
	bool empty() const { return w <= 0 || h <= 0; }
};

struct Layer {
	int anim = 0, index = 0;
	int posX = 0, posY = 0;
	bool transparent = true;
	std::vector<Frame> frames;
	std::vector<int> sheets;
	std::string folder() const { return "anim" + std::to_string(anim) + "/layer" + std::to_string(index); }
};

struct Room {
	std::string tot;
	std::map<int, Sheet> sheets;
	std::vector<Layer> layers; // only layers with at least one frame
	std::vector<std::string> warnings;
	const Layer *find(int anim, int layer) const;
};

bool loadRoom(const TotFile &tot, const PaletteFor &paletteFor, Room &out);

// A frame drawn at 1x: colours, and per pixel the order of the piece seen
// there (-1: nothing drawn).
struct Render {
	tool::Canvas rgba;
	std::vector<int16_t> top;
};
Render render(const Room &room, const Layer &layer, const Frame &frame);

int exportFrames(DataFiles &files, const std::vector<std::string> &tots, const std::filesystem::path &outDir,
                 const std::filesystem::path &guideDir, const PaletteFor &paletteFor);

struct ImportOptions {
	std::filesystem::path framesDir, exportDir, packDir;
	int scale = 0; // 0: the pack sheet's scale, else 4
	enum class Alpha { Silhouette, FromFrames, KeyColor } alpha = Alpha::Silhouette;
	tool::Color key{};
	int keyTolerance = 24;
	std::string only;
};
int importFrames(DataFiles &files, const ImportOptions &opt, const PaletteFor &paletteFor);

} // namespace gob::frames
