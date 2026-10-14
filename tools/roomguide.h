// gob2native - alignment guide for repainting a room (gobrun --guide-at)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Captures the room on screen with every object drawn over its background
// and writes, at an integer scale (docs/hdpacks.md, "Repainting a room"):
//
//   guide-original.png  the room enlarged, each object outlined and labelled
//                       (optionally the hotspots too, dashed)
//   guide-pack.png      the same outlines over the room as an HD pack shows
//                       it (the pack's pictures where it has them)
//   objects-layer.png   only the objects' pixels, transparent elsewhere, to
//                       lay over a repainted background in an image editor
//   objects.json        per object: animation resource, sheets, layer,
//                       frame, rectangles, the name the game shows on hover
//
// The capture needs pixel source tracking (Draw::setTrackSources) turned on
// before the room is drawn: every screen pixel then tells which picture
// pixel it came from, so the objects' exact visible pixels are known.

#pragma once

#include "engine/interpreter.h"
#include "toolimage.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace gob::guide {

struct Options {
	std::filesystem::path dir;     // output folder
	int scale = 0;                 // 0: the pack's scale, else 4
	std::filesystem::path packDir; // optional HD pack for guide-pack.png
	std::filesystem::path gameDir; // for the pack's original pictures
	bool hotspots = false;         // outline the hotspots too
};

struct PieceUse {
	int picture = 0, sheet = 0, pieceIndex = 0;
	int left = 0, top = 0, right = 0, bottom = 0; // source rect in the sheet (inclusive)
	int destX = 0, destY = 0;                     // screen position of its top-left
};

struct Object {
	int index = 0;
	bool goblin = false;
	int slot = 0, animRes = -1, layer = 0, frame = 0, order = 0;
	int x = 0, y = 0;                             // position fields (1000: the layer's own)
	int left = 0, top = 0, right = 0, bottom = 0; // last drawn rect, inclusive
	std::vector<int> sheets;
	std::vector<PieceUse> pieces;
	int visible = 0;            // screen pixels it shows
	int probeX = -1, probeY = -1; // a visible pixel near its middle (hover probe)
	std::string name;
	int hotspot = -1; // slot of the hotspot that overlaps it most
};

struct Hotspot {
	int slot = 0, id = 0, left = 0, top = 0, right = 0, bottom = 0;
	std::string name;
};

struct Capture {
	std::string tot;
	int w = 0, h = 0;
	std::vector<uint8_t> indices; // the screen
	Palette palette{};
	struct Picture {
		std::string tot;
		int resId = 0;
		PictureUse use = PictureUse::Screen;
		int w = 0, h = 0;
	};
	std::vector<Picture> pictures; // by id - 1
	std::vector<uint32_t> sources; // per screen pixel (PixelSources top), empty without tracking
	std::vector<int16_t> owner;    // per screen pixel: object index, -2 another animation pixel, -1 none
	std::vector<Object> objects;   // drawn objects
	std::vector<Hotspot> hotspots; // enabled, on screen, smaller than the screen
	tool::LabelFont font;
};

// Snapshot of the screen, the drawn objects and the hotspots.
Capture capture(Interpreter &interp);

// The text the room shows on its hover line right now (bottom of the
// screen), read back from the recorded text cells (Draw::recordText).
std::string hoverText(Interpreter &interp);

// Fills in the names: `nameAt(x, y)` moves the mouse there, lets the room
// react and returns hoverText().
void probeNames(Capture &cap, const std::function<std::string(int, int)> &nameAt);

// Writes the guide files; prints a summary to stdout. False on errors.
bool write(const Capture &cap, const Options &opt);

} // namespace gob::guide
