// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenery animation resources and piece tables, parsed for tools
// (docs/animation.md 1.2). The engine's own loader is Scenery::loadAnim;
// this is the same format decoded into frames of pieces, with the
// destination extension bits applied, for exporters that cut frames out of
// animation sheets without running the game.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace gob {

struct AnimFramePiece {
	int picture = -1;  // picture index in the anim's descriptor (-1: none, the piece is skipped)
	int pieceIndex = 0; // record in that picture's piece table
	int destX = 0, destY = 0; // offset from the layer's origin, extension bits applied
};

struct AnimLayerData {
	int posX = 0, posY = 0;           // default position (used for the 1000 sentinel)
	int deltaX = 0, deltaY = 0;       // cycle movement for animType 1
	bool transparent = false;         // colour 0 is transparent
	std::vector<std::vector<AnimFramePiece>> frames; // pieces in drawing order
};

struct AnimResourceData {
	std::vector<AnimLayerData> layers;
};

// Inclusive rectangle in a picture; `valid` is false for the all -1 filler
// records (the index stays, so piece indices keep their meaning).
struct AnimPieceRect {
	int left = 0, top = 0, right = 0, bottom = 0;
	bool valid = false;
	int width() const { return right - left + 1; }
	int height() const { return bottom - top + 1; }
};

std::optional<AnimResourceData> parseAnimResource(const std::vector<uint8_t> &data);
std::vector<AnimPieceRect> parsePieceTable(const std::vector<uint8_t> &data);

} // namespace gob
