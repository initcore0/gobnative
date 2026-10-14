// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Goblins 3's level select, quick save and quick load (docs/levels-gob3.md),
// built on its own room loop and menu:
//   - MENU.TOT runs the room named in s[580] with totSub, again and again; a
//     room's main loop ends when b@3670 = 1 and no keyframed sequence runs.
//   - The menu's LOAD sets b@3670 = 1, v[16] = 200 (the hotspot block ends)
//     and v[163] = 1000 + slot from inside the room; MENU then reads the slot
//     and runs the room again with v[163] = 1, which restores it.
//   - The menu's SAVE first copies the room's state into the per-room
//     storage (MENU 0x5B5B), then writes the variables and a screenshot.
// Everything here acts while the room's main hotspot loop waits for input
// in the state in which the player could open the icon bar.

#include "engine/interpreter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace gob {

namespace {

constexpr uint32_t kVarResult = 16;        // hotspot result; 200 = leave the hotspot block
constexpr uint32_t kVarAction0 = 60;       // Blount's pending action (0 = none)
constexpr uint32_t kVarAction1 = 1018;     // the other goblin's
constexpr uint32_t kVarRestore = 163;      // 1 = the room restores itself; 1000 + n = MENU loads slot n
constexpr uint32_t kVarMultSlots = 18;     // v[18]..v[21]: frame of multSub slot 0..3, -1 = idle
constexpr uint32_t kRoomName = 580 * 4;    // string: the room MENU runs next
constexpr uint32_t kLeaveRoom = 3670;      // byte: 1 = the room's main loop ends
constexpr uint32_t kWorld = 3737;          // byte: the world (1-8) of the room MENU runs next
constexpr uint32_t kLoadedWorld = 3736;    // byte: the world MENU has loaded the pictures of
constexpr uint32_t kVisits = 3740;         // bytes: visits of room number n at 3740 + n (0: never)
constexpr char kRoomStatesMagic[4] = {'G', '3', 'R', 'S'};
constexpr uint32_t kRoomStatesVersion = 1;
constexpr uint16_t kSavePrep = 0x5B5B;     // MENU: copies the room's state to its storage before a save

struct RoomV3 {
	const char *tot;
	int world;  // b@3737
	int number; // the room's number, which picks its name
};

// In play order (docs/levels-gob3.md 1).
const RoomV3 kRoomsV3[] = {
	{"EMAP1000", 1, 0},  {"EMAP1002", 1, 2},  {"EMAP1003", 1, 3},  {"EMAP1004", 2, 4},  {"EMAP1005", 2, 5},
	{"EMAP1006", 2, 6},  {"EMAP1007", 3, 7},  {"EMAP1008", 3, 8},  {"EMAPBB08", 3, 8},  {"EMAP1009", 3, 9},
	{"EMAP1010", 4, 10}, {"EMAP1011", 4, 11}, {"EMAP1012", 4, 12}, {"EMAPBB11", 4, 11}, {"EMAP1013", 5, 13},
	{"EMAP1014", 5, 14}, {"EMAP1015", 6, 15}, {"EMAP1016", 6, 16}, {"EMAPBB15", 6, 15}, {"EMAP1017", 7, 17},
	{"EMAP1018", 7, 18}, {"EMAP1019", 8, 19},
};

// MENU's text item with each world's room names, and the world's first
// room number (MENU 0x483D, the name a save is proposed under).
constexpr int kWorldNames[8] = {27, 11, 14, 15, 10, 20, 21, 22};
constexpr int kWorldFirst[8] = {0, 4, 7, 10, 13, 15, 17, 19};

const RoomV3 *findRoomV3(const std::string &tot) {
	for (const RoomV3 &r : kRoomsV3)
		if (tot == r.tot)
			return &r;
	return nullptr;
}

// The lines of a text item (positioned runs; a move to another row starts
// a new line), as getTotTextItemPart counts them.
std::vector<std::string> textLines(const std::vector<uint8_t> &d) {
	std::vector<std::string> lines;
	auto at = [&](size_t o) -> uint8_t { return o < d.size() ? d[o] : 1; };
	auto u16 = [&](size_t o) -> int16_t { return int16_t(at(o) | (at(o + 1) << 8)); };
	size_t p = 0;
	while (p + 1 < d.size() && u16(p) != -1)
		p += 9;
	p += 2;
	int y = -1000;
	std::string cur;
	while (p < d.size() && at(p) != 1) {
		uint8_t c = at(p);
		if (c == 2 || c == 5) {
			int ny = u16(p + 3);
			if (std::abs(ny - y) > 1 && !cur.empty()) {
				lines.push_back(cur);
				cur.clear();
			}
			y = ny;
			p += 5;
		} else if (c == 3 || c == 4) {
			p += 2;
		} else {
			if (c >= 32)
				cur.push_back(char(c));
			p++;
		}
	}
	if (!cur.empty())
		lines.push_back(cur);
	return lines;
}

std::string titleCase(const std::string &raw) {
	std::string out;
	bool wordStart = true;
	for (unsigned char c : raw) {
		out.push_back(c < 0x80 ? char(wordStart ? std::toupper(c) : std::tolower(c)) : '?');
		wordStart = c == ' ' || c == '-';
	}
	return out;
}

} // namespace

std::vector<LevelInfo> Interpreter::levelsV3() {
	// The names from MENU's texts in the game language (once the archives
	// are open).
	TotFile menu;
	const bool names = _files.exists("MENU.TOT") && menu.load(_files, "MENU", language);
	loadRoomStatesV3();
	std::vector<LevelInfo> out;
	int lastWorld = 0, n = 0;
	for (const RoomV3 &r : kRoomsV3) {
		LevelInfo l;
		l.tot = r.tot;
		l.world = r.world;
		l.worldStart = r.world != lastWorld;
		l.number = ++n;
		l.reached = _roomStatesV3.count(r.tot) != 0;
		lastWorld = r.world;
		if (names)
			if (auto item = menu.text(uint16_t(kWorldNames[r.world - 1]))) {
				auto lines = textLines(*item);
				size_t part = size_t(r.number - kWorldFirst[r.world - 1]);
				if (part < lines.size())
					l.name = titleCase(lines[part]);
			}
		out.push_back(std::move(l));
	}
	return out;
}

// A room script run by MENU's room loop is the innermost environment.
bool Interpreter::roomUnderMenuV3() const {
	return _rules.levels == LevelSystem::Gob3 && _cur >= 1 && _cur == int(_envs.size()) - 1 && _envs[_cur]->tot &&
	       findRoomV3(_envs[_cur]->name) && _envs[_cur - 1]->name == "MENU";
}

// The room is in its main loop and would let the player open the icon bar:
// no sequence runs, no goblin has an action pending, it isn't being left or
// restored.
bool Interpreter::levelIdleV3() const {
	if (!roomUnderMenuV3() || _vars->read8(kLeaveRoom) != 0 || _vars->slot(kVarRestore) != 0)
		return false;
	// Not before the main loop's timed update has run once: it copies the
	// goblins' positions into the variables a save keeps.
	if (_roomLoopPasses < 1)
		return false;
	for (uint32_t i = 0; i < 4; i++)
		if (_vars->slot(kVarMultSlots + i) != uint32_t(-1))
			return false;
	return _vars->slot(kVarAction0) == 0 && _vars->slot(kVarAction1) == 0;
}

bool Interpreter::requestLevelV3(const std::string &name) {
	const bool number = !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c); });
	const RoomV3 *room = nullptr;
	int n = 0;
	for (const RoomV3 &r : kRoomsV3)
		if (name == r.tot || (number && std::atoi(name.c_str()) == ++n)) {
			room = &r;
			break;
		}
	if (!room)
		return false;
	_levelRequest = LevelRequest::Jump;
	_requestedRoom = room->tot;
	trace("level jump to " + _requestedRoom + " requested");
	skipCutscene();
	return true;
}

std::optional<std::array<uint8_t, 40>> Interpreter::quickSlotNameV3() const {
	return slotNameV3(kQuickSaveSlotV3);
}

bool Interpreter::hasQuickSaveV3() const {
	auto name = quickSlotNameV3();
	if (!name)
		return false;
	size_t len = sizeof(kQuickSaveName) - 1;
	if (std::memcmp(name->data(), kQuickSaveName, len) != 0)
		return false;
	for (size_t i = len; i < name->size(); i++)
		if ((*name)[i] != 0 && (*name)[i] != ' ')
			return false;
	return true;
}

bool Interpreter::quickSaveSlotTakenV3() const {
	return slotHasFileV3(kQuickSaveSlotV3) && !hasQuickSaveV3();
}

void Interpreter::requestQuickSaveV3() {
	if (slotPath(kQuickSaveSlotV3).empty()) {
		_host.notify("Saving is disabled");
		return;
	}
	if (!roomUnderMenuV3()) {
		_host.notify("Can't save here");
		return;
	}
	if (quickSaveSlotTakenV3()) {
		trace("quick save refused: slot " + std::to_string(kQuickSaveSlotV3) + " holds a manual save");
		_host.notify("Quick save slot (slot " + std::to_string(kQuickSaveSlotV3 + 1) +
		             ") holds a manual save - free it in the game's save menu");
		return;
	}
	_saveRequested = true;
}

void Interpreter::requestQuickLoadV3() {
	if (!hasQuickSaveV3()) {
		trace("quick load refused: no quick save in slot " + std::to_string(kQuickSaveSlotV3));
		_host.notify("No quick save to load");
		return;
	}
	_levelRequest = LevelRequest::Load;
	trace("quick load requested");
	skipCutscene();
}

// Called while the room's main hotspot loop waits for input.
Task<void> Interpreter::serviceLevelRequestsV3() {
	if ((!_saveRequested && _levelRequest == LevelRequest::None) || !levelIdleV3())
		co_return;

	if (_saveRequested) {
		_saveRequested = false;
		if (quickSaveSlotTakenV3()) { // saved from the game's menu since the request
			_host.notify("Quick save slot (slot " + std::to_string(kQuickSaveSlotV3 + 1) +
			             ") holds a manual save - free it in the game's save menu");
		} else {
			// As the menu's SAVE: the room's state into its storage first.
			co_await switchTotSub(0, int16_t(kSavePrep));
			bool ok = writeQuickSaveV3();
			_host.notify(ok ? "Saved" : "Quick save failed");
		}
	}
	if (_levelRequest == LevelRequest::Load && !hasQuickSaveV3()) {
		_levelRequest = LevelRequest::None;
		_host.notify("No quick save to load");
	}
	if (_levelRequest == LevelRequest::None)
		co_return;
	if (_ffSkip)
		stopFastForward("level request");

	// Leave the room the way the menu's LOAD does.
	_vars->write8(kLeaveRoom, 1);
	writeVar(kVarResult, 200);
	if (_levelRequest == LevelRequest::Load) {
		writeVar(kVarRestore, uint32_t(1000 + kQuickSaveSlotV3));
		_loadArmed = true;
		trace("quick load: leaving " + env().name);
	} else {
		_jumpArmed = _requestedRoom;
		trace("level jump: leaving " + env().name + " for " + _jumpArmed);
	}
	_levelRequest = LevelRequest::None;
}

// gob3-rooms.sav in the save folder: "G3RS", u32 version (1), u32 count,
// then per room an 8-byte name (zero-padded), u32 size and the variables.
std::string Interpreter::roomStatesPathV3() const {
	const std::string dir = _host.saveDirectory();
	return dir.empty() ? std::string() : dir + "/" + _game.key + "-rooms.sav";
}

void Interpreter::loadRoomStatesV3() {
	if (_roomStatesLoadedV3)
		return;
	_roomStatesLoadedV3 = true;
	const std::string path = roomStatesPathV3();
	std::ifstream f(path, std::ios::binary);
	if (path.empty() || !f)
		return;
	const std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
	auto u32 = [&](size_t o) {
		return o + 4 <= d.size() ? uint32_t(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (uint32_t(d[o + 3]) << 24)) : 0;
	};
	if (d.size() < 12 || std::memcmp(d.data(), kRoomStatesMagic, 4) != 0 || u32(4) != kRoomStatesVersion)
		return;
	size_t p = 12;
	for (uint32_t i = 0, n = u32(8); i < n && p + 12 <= d.size(); i++) {
		std::string name(reinterpret_cast<const char *>(&d[p]), 8);
		name.resize(std::strlen(name.c_str()));
		const uint32_t size = u32(p + 8);
		p += 12;
		if (p + size > d.size())
			break;
		if (findRoomV3(name))
			_roomStatesV3[name].assign(d.begin() + long(p), d.begin() + long(p + size));
		p += size;
	}
}

void Interpreter::storeRoomStatesV3() const {
	const std::string path = roomStatesPathV3();
	if (path.empty())
		return;
	std::vector<uint8_t> d(kRoomStatesMagic, kRoomStatesMagic + 4);
	auto put32 = [&](uint32_t v) {
		for (int i = 0; i < 4; i++)
			d.push_back(uint8_t(v >> (8 * i)));
	};
	put32(kRoomStatesVersion);
	put32(uint32_t(_roomStatesV3.size()));
	for (const auto &[name, vars] : _roomStatesV3) {
		char n[8] = {};
		std::memcpy(n, name.data(), std::min<size_t>(name.size(), 8));
		d.insert(d.end(), n, n + 8);
		put32(uint32_t(vars.size()));
		d.insert(d.end(), vars.begin(), vars.end());
	}
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char *>(d.data()), long(d.size()));
}

// Before MENU runs a room: the variables at the first room of a game are
// the fallback for level jumps; those at the first visit of every room
// (its visit counter still 0, not restored from a save) are what a jump to
// that room starts from, kept across runs in gob3-rooms.sav.
void Interpreter::noteRoomStartV3(const std::string &room, int callerEnv) {
	const RoomV3 *r = findRoomV3(room);
	if (!r)
		return;
	_roomLoopReached = false; // its entry runs until then (engine/skip.cpp)
	_roomLoopPasses = 0;
	if (!_vars || _envs[size_t(callerEnv)]->name != "MENU")
		return;
	if (_freshVars.empty())
		_freshVars.assign(_vars->data(), _vars->data() + _vars->size());
	if (!_jumpStateRoomV3.empty()) {
		// A jump to a recorded room: MENU's world change (0x4E98) has given
		// the goblins the world's first items; the whole state again, as it
		// was when the room was first reached.
		auto state = _roomStatesV3.find(_jumpStateRoomV3);
		if (_jumpStateRoomV3 == room && state != _roomStatesV3.end() && state->second.size() == _vars->size())
			std::copy(state->second.begin(), state->second.end(), _vars->data());
		_jumpStateRoomV3.clear();
		return;
	}
	const bool unvisited = _vars->read8(uint32_t(kVisits + r->number)) == 0;
	// Nothing is recorded after a jump without a state until a save is
	// loaded or a new game starts: the goblins lack what they would have.
	if (_vars->slot(kVarRestore) != 0 || (unvisited && r->number == 0))
		_jumpWithoutStateV3 = false;
	if (unvisited && _vars->slot(kVarRestore) == 0 && !_jumpWithoutStateV3) {
		loadRoomStatesV3();
		_roomStatesV3[room].assign(_vars->data(), _vars->data() + _vars->size());
		storeRoomStatesV3();
		trace("room state of " + room + " recorded");
	}
}

// After totSub returns from `room` to MENU with a level jump armed: the
// variables as at the start of the game, with the target room and its
// world, so MENU's loop runs that room next.
void Interpreter::finishLevelJumpV3(const std::string &room) {
	if (_jumpArmed.empty() || !findRoomV3(room) || env().name != "MENU")
		return;
	const RoomV3 *r = findRoomV3(_jumpArmed);
	_jumpArmed.clear();
	if (!r)
		return;
	loadRoomStatesV3();
	auto state = _roomStatesV3.find(r->tot);
	const bool recorded = state != _roomStatesV3.end() && state->second.size() == _vars->size();
	if (recorded)
		std::copy(state->second.begin(), state->second.end(), _vars->data());
	else if (!_freshVars.empty() && _freshVars.size() == _vars->size())
		std::copy(_freshVars.begin(), _freshVars.end(), _vars->data());
	_jumpWithoutStateV3 = !recorded;
	_jumpStateRoomV3 = recorded ? r->tot : std::string();
	_vars->writeString(kRoomName, r->tot);
	_vars->write8(kWorld, uint8_t(r->world));
	_vars->write8(kLoadedWorld, 0xFF); // MENU loads the world's pictures again
	_vars->write8(kLeaveRoom, 0);
	writeVar(kVarRestore, 0);
	trace(std::string("level jump: MENU starts ") + r->tot + " (world " + std::to_string(r->world) + ", " +
	      (recorded ? "as first reached" : "from the first room's variables") + ")");
}

} // namespace gob
