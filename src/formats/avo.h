// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gobliiins level object files (.AVO, docs/goblins-v1.md section 1): the
// walk and item maps of a level, its way points and item positions, the
// sound list, and the state tables of the goblins and objects. This is the
// pure file format, as Map_v1::loadMapObjects reads it; the engine
// (engine/goblins_v1.cpp) applies the hard-coded patches on top.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gob {

// One cell of an object's state machine: the animation layer it plays and
// the sound effect it triggers. With sndFrame's high byte set, both bytes of
// sndFrame, sndItem, freq (in hundreds of Hz) and repCount each describe
// one of two effects (docs/goblins-v1.md 3.4).
struct AvoState {
	int16_t animation = 0, layer = 0;
	int16_t unk0 = 0, unk1 = 0;
	int16_t sndItem = -1; // -1: no effect
	int16_t freq = 0, repCount = 0, sndFrame = 0;
};

// A goblin or object: its starting position record and its state machine,
// 40 states of up to 6 columns (only column 0 is used by the engine).
struct AvoObject {
	static constexpr int kStates = 40, kColumns = 6;

	int16_t xPos = 0, yPos = 0, order = 0, state = 0;
	uint8_t multObjIndex = 0; // the goblin whose states multState > 39 refers to
	std::array<std::array<std::optional<AvoState>, kColumns>, kStates> states;

	int stateCount() const;
};

struct AvoFile {
	static constexpr int kMapWidth = 26, kMapHeight = 28;
	static constexpr int kWayPoints = 40, kItemPoses = 20;
	static constexpr int kMaxSounds = 20, kMaxGoblins = 4, kMaxObjects = 20;

	struct WayPoint { int16_t x = 0, y = 0; };
	struct ItemPos { int8_t x = 0, y = 0, orient = 0; };

	std::array<int8_t, kMapWidth * kMapHeight> pass{};   // [y * 26 + x]
	std::array<int16_t, kMapWidth * kMapHeight> items{}; // [y * 26 + x], bytes sign-extended
	std::array<WayPoint, kWayPoints> wayPoints{};
	std::array<ItemPos, kItemPoses> itemPoses{};
	std::vector<std::string> sounds; // sample names, without ".SND"
	std::vector<AvoObject> goblins;  // 4 in every level: 3 goblins and the pseudo-goblin
	std::vector<AvoObject> objects;
	bool hasItemToObject = false;
	std::vector<int16_t> itemToObject; // item id -> object index (-1: none)

	// Fields the engine skips, kept for the dump.
	std::array<int, 3> nameCounts{};      // the three 14-byte name lists
	int gobPosCount = 0, objPosCount = 0; // position records (goblins read past theirs when 0)
	int extraRecords = 0;                 // the records between the objects and the item table

	size_t size = 0;     // file size
	size_t consumed = 0; // bytes up to the end of the last field the engine reads

	int8_t passAt(int x, int y) const { return pass[size_t(y * kMapWidth + x)]; }
	int16_t itemAt(int x, int y) const { return items[size_t(y * kMapWidth + x)]; }
};

// Parses an .AVO file. Fails (with a reason in `error`) when a field would
// lie past the end of the data or a count exceeds the engine's tables.
std::optional<AvoFile> parseAvo(const std::vector<uint8_t> &data, std::string *error = nullptr);

} // namespace gob
