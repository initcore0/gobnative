// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Level select, quick save and quick load, built on the game's own room and
// save flow (docs/levels.md):
//   - MENU.TOT runs rooms in a loop: `totSub s[3734]`, then whatever room the
//     finished one put in s[3734]. A room's main loop ends when v[60] = 1.
//   - The in-game menu loads a save by setting v[60] = 1, v[16] = 200 and
//     v[790] = 1000 + slot from inside the room (MENU 0x1A45); MENU then reads
//     the slot (0x3446) and runs the room again with v[790] = 1, which makes
//     the room restore itself from the variables instead of starting afresh.
//   - Saving copies the objects' state into a backup area (MENU 0x339A) and
//     writes the whole variable space.
// Everything here acts at the moment the room's main hotspot loop is idle
// and the player could open the menu, so the state matches a menu save.

#include "engine/interpreter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace gob {

namespace {

// Variables (32-bit slot indices) used by MENU and the room scripts.
constexpr uint32_t kVarResult = 16;       // hotspot result; 200 = leave the hotspot block
constexpr uint32_t kVarLevelExit = 60;    // 0 playing, 2 walking out, 1 leave the room now
constexpr uint32_t kVarActiveGoblin = 1400; // 0 or 1; 3 in rooms with a third character
constexpr uint32_t kVarAction0 = 474;     // action in progress for goblin 0 (0 = none)
constexpr uint32_t kVarAction1 = 1407;    // ... for goblin 1
constexpr uint32_t kVarRestore = 790;     // 1 = room restores from variables; 1000 + n = load slot n
constexpr uint32_t kVarRoomName = 3734;   // string: the room MENU runs next
constexpr uint32_t kVarNewWorld = 1353;   // world whose MONDEn.CAT MENU loads next (0 = none)
constexpr uint32_t kVarEntry = 1883;      // how the goblins enter the room
constexpr uint32_t kVarVisits = 2323;     // five per-room visit counters, cleared at each new world
constexpr uint32_t kVarJoker = 4345;      // 100 = MENU runs JOKER.TOT (hints) before the room
constexpr uint32_t kVarEndGame = 791;     // 1 = MENU's loop ends
constexpr uint32_t kVarSelection = 476;   // selected inventory item / menu entry (-1 = none)
// In-game options the player may have changed since the game started; kept
// across a level jump.
constexpr uint32_t kOptionVars[] = {480 /* language */, 2433 /* item names */, 3748 /* music */};

// MENU 0x339A: the 15 objects' animation records and positions and their
// backups, which a room re-entered with v[790] = 1 restores from.
constexpr uint32_t kObjAnimData = 2435 * 4, kObjAnimDataBackup = 3750 * 4, kObjAnimDataSize = 15 * 40;
constexpr uint32_t kObjPosX = 3185 * 4, kObjPosXBackup = 3903 * 4;
constexpr uint32_t kObjPosY = 3260 * 4, kObjPosYBackup = 3918 * 4, kObjPosSize = 15 * 4;


struct Room {
	const char *tot;
	int world;
	int nameIndex; // entry in MONDEn.CAT, -1 = none
	int entry;     // v[1883] as the room the player first comes from sets it
	               // (INTRO0 sets 1 for the first room)
};

// In play order: each world's first room, then the others roughly in the
// order the exits lead to them. Names are the world's map entries (see docs/levels.md for how they were matched).
const Room kRooms[] = {
	{"GOB01", 1, 11, 1}, {"GOB02", 1, 12, 0}, {"GOB00", 1, 10, 0}, {"GOB03", 1, 13, 0},
	{"GOB08", 2, 21, 0}, {"GOB07", 2, 22, 0}, {"GOB04", 2, 23, 0}, {"GOB05", 2, 24, 0}, {"GOB06", 2, 25, 0},
	{"GOB10", 3, 13, 0}, {"GOB09", 3, 12, 0}, {"GOB11", 3, 14, 0},
	{"GOB12", 4, 10, 0}, {"GOB13", 4, 11, 0},
	{"GOB14", 5, 12, 0}, {"GOB15", 5, 13, 0}, {"GOB16", 5, 14, 0},
	{"GOB17", 6, -1, 0}, {"GOB18", 6, 14, 0}, {"GOB19", 6, 15, 0}, {"GOB20", 6, 16, 0},
	{"GOB21", 7, 8, 0},  {"GOB22", 7, 9, 0},
};

// Entries per MONDEn.CAT language block (items, then places).
constexpr int kWorldNames[] = {14, 26, 15, 12, 15, 17, 10};

const Room *findRoom(const std::string &tot) {
	for (const Room &r : kRooms)
		if (tot == r.tot)
			return &r;
	return nullptr;
}

bool isRoom(const std::string &tot) {
	return findRoom(tot) != nullptr;
}

// Language block in the .CAT files: French, German, English, Spanish, Italian.
int catLanguage(Language lang) {
	switch (lang) {
	case Language::French: return 0;
	case Language::German: return 1;
	case Language::Spanish: return 3;
	case Language::Italian: return 4;
	default: return 2;
	}
}

// Upper/lower-case UTF-8 for the accented letters of the game's character
// set (code page 437 plus É, È and Ô at 0x90, 0x91 and 0x93 for capitals).
struct Accent {
	uint8_t code;
	const char *upper, *lower;
};
const Accent kAccents[] = {
	{0x80, "Ç", "ç"}, {0x81, "Ü", "ü"}, {0x82, "É", "é"}, {0x83, "Â", "â"}, {0x84, "Ä", "ä"}, {0x85, "À", "à"},
	{0x87, "Ç", "ç"}, {0x88, "Ê", "ê"}, {0x89, "Ë", "ë"}, {0x8A, "È", "è"}, {0x8B, "Ï", "ï"}, {0x8C, "Î", "î"},
	{0x8E, "Ä", "ä"}, {0x90, "É", "é"}, {0x91, "È", "è"}, {0x93, "Ô", "ô"}, {0x94, "Ö", "ö"}, {0x96, "Û", "û"},
	{0x97, "Ù", "ù"}, {0x99, "Ö", "ö"}, {0x9A, "Ü", "ü"}, {0xA0, "Á", "á"}, {0xA1, "Í", "í"}, {0xA2, "Ó", "ó"},
	{0xA3, "Ú", "ú"}, {0xA4, "Ñ", "ñ"}, {0xA5, "Ñ", "ñ"}, {0xE1, "ß", "ß"},
};

// "DEATH KINGDOM" -> "Death Kingdom", as UTF-8.
std::string titleCase(const std::string &raw) {
	std::string out;
	bool wordStart = true;
	for (unsigned char c : raw) {
		if (c < 0x80) {
			out.push_back(char(wordStart ? std::toupper(c) : std::tolower(c)));
		} else {
			const Accent *a = nullptr;
			for (const Accent &e : kAccents)
				if (e.code == c)
					a = &e;
			out += a ? (wordStart ? a->upper : a->lower) : "?";
		}
		wordStart = c == ' ' || c == '-' || c == '\'';
	}
	return out;
}

} // namespace

int levelWorld(const std::string &tot) {
	const Room *r = findRoom(tot);
	return r ? r->world : 0;
}

std::vector<LevelInfo> Interpreter::levels() {
	if (_rules.levels == LevelSystem::Gob1)
		return levelsV1();
	if (_rules.levels == LevelSystem::Gob3)
		return levelsV3();
	if (_rules.levels != LevelSystem::Gob2)
		return {};
	std::vector<std::vector<std::string>> names(7);
	int lang = catLanguage(language);
	for (int w = 0; w < 7; w++) {
		auto data = _files.read("MONDE" + std::to_string(w + 1) + ".CAT");
		size_t base = 18 + size_t(lang * kWorldNames[w] * 40);
		for (int i = 0; data && i < kWorldNames[w] && base + size_t(i + 1) * 40 <= data->size(); i++) {
			const char *p = reinterpret_cast<const char *>(data->data() + base + size_t(i) * 40);
			names[size_t(w)].push_back(titleCase(std::string(p, strnlen(p, 40))));
		}
	}

	std::vector<LevelInfo> out;
	int lastWorld = 0;
	for (const Room &r : kRooms) {
		LevelInfo l;
		l.tot = r.tot;
		l.world = r.world;
		l.worldStart = r.world != lastWorld;
		lastWorld = r.world;
		const auto &n = names[size_t(r.world - 1)];
		if (r.nameIndex >= 0 && size_t(r.nameIndex) < n.size())
			l.name = n[size_t(r.nameIndex)];
		out.push_back(std::move(l));
	}
	return out;
}

// A room script run by MENU's room loop is the innermost environment.
bool Interpreter::roomUnderMenu() const {
	return _rules.levels == LevelSystem::Gob2 && _cur >= 1 && _cur == int(_envs.size()) - 1 && _envs[_cur]->tot && isRoom(_envs[_cur]->name) &&
	       _envs[_cur - 1]->name == "MENU";
}

std::string Interpreter::currentLevel() const {
	if (_rules.levels == LevelSystem::Gob1)
		return levelRunningV1() ? _envs[_cur]->name : std::string();
	if (_rules.levels == LevelSystem::Gob3)
		return roomUnderMenuV3() ? _envs[_cur]->name : std::string();
	return roomUnderMenu() ? _envs[_cur]->name : std::string();
}

bool Interpreter::roomIdle() const {
	if (_rules.levels == LevelSystem::Gob1)
		return levelIdleV1();
	return _rules.levels == LevelSystem::Gob3 ? levelIdleV3() : levelIdle();
}

// The room is in its main loop, not leaving, and would let the player open
// the in-game menu now (the condition of the rooms' icon-bar hotspot).
bool Interpreter::levelIdle() const {
	if (!roomUnderMenu() || _vars->slot(kVarLevelExit) != 0 || _vars->slot(kVarRestore) != 0)
		return false;
	uint32_t active = _vars->slot(kVarActiveGoblin);
	return (active == 0 && _vars->slot(kVarAction0) == 0) || (active == 1 && _vars->slot(kVarAction1) == 0) ||
	       active == 3;
}

bool Interpreter::requestLevel(const std::string &tot) {
	std::string name = tot;
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	if (_rules.levels == LevelSystem::Gob1) { // a script name, or the level's number in play order
		const bool number = !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); });
		for (const LevelInfo &l : levelsV1())
			if (l.tot == name || (number && l.number == std::atoi(name.c_str()))) {
				requestLevelV1(l.tot);
				return true;
			}
		return false;
	}
	if (_rules.levels == LevelSystem::Gob3)
		return requestLevelV3(name);
	if (_rules.levels != LevelSystem::Gob2 || !isRoom(name))
		return false;
	_levelRequest = LevelRequest::Jump;
	_requestedRoom = name;
	trace("level jump to " + name + " requested");
	skipCutscene();
	if (_atLevelLoop)
		serviceLevelRequests();
	return true;
}

void Interpreter::requestQuickSave() {
	if (_rules.levels == LevelSystem::Gob1) {
		requestQuickSaveV1();
		return;
	}
	if (_rules.levels == LevelSystem::Gob3) {
		requestQuickSaveV3();
		return;
	}
	if (!_rules.hasQuickSave()) {
		_host.notify("Quick save isn't available for this game yet");
		return;
	}
	if (slotPath(kQuickSaveSlot).empty()) {
		_host.notify("Saving is disabled");
		return;
	}
	if (!roomUnderMenu()) {
		_host.notify("Can't save here");
		return;
	}
	if (quickSaveSlotTaken()) {
		notifyQuickSaveSlotTaken();
		return;
	}
	_saveRequested = true;
	if (_atLevelLoop)
		serviceLevelRequests();
}

void Interpreter::requestQuickLoad() {
	if (_rules.levels == LevelSystem::Gob1) {
		requestQuickLoadV1();
		return;
	}
	if (_rules.levels == LevelSystem::Gob3) {
		requestQuickLoadV3();
		return;
	}
	if (!_rules.hasQuickSave()) {
		_host.notify("Quick load isn't available for this game yet");
		return;
	}
	if (!hasQuickSave()) {
		trace("quick load refused: no quick save in slot " + std::to_string(kQuickSaveSlot));
		_host.notify("No quick save to load");
		return;
	}
	_levelRequest = LevelRequest::Load;
	trace("quick load requested");
	skipCutscene();
	if (_atLevelLoop)
		serviceLevelRequests();
}

void Interpreter::notifyQuickSaveSlotTaken() {
	trace("quick save refused: slot " + std::to_string(kQuickSaveSlot) + " holds a manual save");
	_host.notify("Quick save slot (slot " + std::to_string(kQuickSaveSlot + 1) +
	             ") holds a manual save - free it in the game's save menu");
}

// Called while the room's main hotspot loop waits for input.
void Interpreter::serviceLevelRequests() {
	if ((!_saveRequested && _levelRequest == LevelRequest::None) || !levelIdle())
		return;

	if (_saveRequested) {
		_saveRequested = false;
		quickSaveNow();
	}
	if (_levelRequest == LevelRequest::Load && !hasQuickSave()) { // replaced from the game's menu since
		_levelRequest = LevelRequest::None;
		trace("quick load refused: no quick save in slot " + std::to_string(kQuickSaveSlot));
		_host.notify("No quick save to load");
	}
	if (_levelRequest == LevelRequest::None)
		return;

	// A request skips what plays before the safe point (engine/skip.cpp), but
	// not the target room's entry.
	if (_ffSkip)
		stopFastForward("level request");

	// Leave the room the way the menu's load does: the hotspot block ends at
	// its next timer tick (v[16] != 0) and the main loop at v[60] = 1.
	writeVar(kVarLevelExit, 1);
	writeVar(kVarResult, 200);
	if (_levelRequest == LevelRequest::Load) {
		writeVar(kVarRestore, 1000 + kQuickSaveSlot); // MENU loads the slot (0x3446)
		_loadArmed = true;
		trace("quick load: leaving " + env().name);
	} else {
		_jumpArmed = _requestedRoom;
		trace("level jump: leaving " + env().name + " for " + _jumpArmed);
	}
	_levelRequest = LevelRequest::None;
}

void Interpreter::quickSaveNow() {
	uint8_t *v = _vars->data();
	std::memmove(v + kObjAnimDataBackup, v + kObjAnimData, kObjAnimDataSize);
	std::memmove(v + kObjPosXBackup, v + kObjPosX, kObjPosSize);
	std::memmove(v + kObjPosYBackup, v + kObjPosY, kObjPosSize);

	if (quickSaveSlotTaken()) { // saved from the game's menu since the request
		notifyQuickSaveSlotTaken();
		return;
	}
	std::array<uint8_t, kSlotNameLength> name{};
	std::memcpy(name.data(), kQuickSaveName, sizeof(kQuickSaveName) - 1);
	bool ok = writeSlotFile(kQuickSaveSlot, name.data());
	_host.notify(ok ? "Saved" : "Quick save failed");
}

// Before MENU runs a room: the variables at the first room of a game are
// the starting point for level jumps.
void Interpreter::noteRoomStart(const std::string &room, int callerEnv) {
	if (_rules.levels == LevelSystem::Gob3)
		noteRoomStartV3(room, callerEnv);
	if (_rules.levels != LevelSystem::Gob2)
		return;
	if (isRoom(room))
		_roomLoopReached = false; // its entry sequence runs until then (engine/skip.cpp)
	if (_freshVars.empty() && _vars && isRoom(room) && _envs[size_t(callerEnv)]->name == "MENU")
		_freshVars.assign(_vars->data(), _vars->data() + _vars->size());
}

// After totSub returns from `room` to MENU: if a level jump is armed, set the
// variables up so that MENU's loop starts the requested room, as it would
// after the room before it in the game.
void Interpreter::finishLevelJump(const std::string &room) {
	if (_rules.levels == LevelSystem::Gob3) {
		finishLevelJumpV3(room);
		return;
	}
	if (_jumpArmed.empty() || !isRoom(room) || env().name != "MENU")
		return;
	const Room *r = findRoom(_jumpArmed);
	_jumpArmed.clear();
	if (!r)
		return;

	if (!_freshVars.empty() && _freshVars.size() == _vars->size()) {
		uint32_t options[std::size(kOptionVars)];
		for (size_t i = 0; i < std::size(kOptionVars); i++)
			options[i] = _vars->slot(kOptionVars[i]);
		std::copy(_freshVars.begin(), _freshVars.end(), _vars->data());
		for (size_t i = 0; i < std::size(kOptionVars); i++)
			writeVar(kOptionVars[i], options[i]);
	}

	_vars->writeString(kVarRoomName * 4, r->tot);
	writeVar(kVarNewWorld, uint32_t(r->world)); // MENU loads the world's names and carried items
	writeVar(kVarEntry, uint32_t(r->entry));
	for (uint32_t i = 0; i < 5; i++)
		writeVar(kVarVisits + i, 0);
	if (std::strcmp(r->tot, "GOB00") == 0)
		writeVar(1898, 1); // set by GOB02's exit to the wizard's room
	writeVar(kVarRestore, 0);
	writeVar(kVarLevelExit, 0);
	writeVar(kVarJoker, 0);
	writeVar(kVarEndGame, 0);
	writeVar(kVarSelection, uint32_t(-1));
	trace(std::string("level jump: MENU starts ") + r->tot + " (world " + std::to_string(r->world) + ")");
}

} // namespace gob
