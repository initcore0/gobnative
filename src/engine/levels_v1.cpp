// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gobliiins' level select, quick save and quick load (docs/levels.md 9).
// The game strings its 22 levels together through INTER.TOT: a level sets
// v[286] to the number of the next one when it starts, and when it is
// solved it shows that level's access code and loads INTER, which maps the
// number to the level's script file (100 + n, as an access code sets it,
// counts as n; 100 is the first level). The energy (v[296], 0-10) carries
// over from level to level; an access code holds the level and the energy.
//
//   - A level jump sets v[286] and the energy as a valid access code does and
//     loads INTER, from the level's main loop or any screen of INTER or a
//     level that waits for a click.
//   - The game has no saves, so a quick save is a snapshot of our own
//     (gob1-quick.sav): the level, the variables, the goblins' and objects'
//     state (GoblinsV1::saveState), the back surface and the clean room
//     picture, and the cursors. Loading jumps to the saved level with the
//     saved energy, fast-forwards through INTER and the level's start, and
//     once the level is idle in its main loop puts the snapshot in place.
//     The level redraws its pictures (the room, the energy bar) on the way;
//     the goblins and objects are drawn again from their restored state, and
//     only the pixels that still differ (the portrait, the item name) are
//     copied from the snapshot.

#include "engine/interpreter.h"
#include "engine/snapshot.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace gob {

namespace {

constexpr uint32_t kVarResult = 16;     // hotspot result; a level's result (1 lost, 3 solved) when it ends
constexpr uint32_t kVarActedItem = 285; // the item a puzzle step runs for (0: none)
constexpr uint32_t kVarExitSign = 136;  // 1 while the level's exit sign shows
constexpr uint32_t kVarNextLevel = 286; // the level INTER starts: n, 100 + n (access code), 200 = first, -1 game over
constexpr uint32_t kVarEnergy = 296;    // 0..10
constexpr uint32_t kVarLanguage = 758;  // the scripts' copy of the language (INTRO sets it from VAR 16)
constexpr int kFullEnergy = 10;
constexpr int kFirstLevel = 100;        // INTER's number of the first level
constexpr int kKeptVars = 16;           // VAR 0-15 (input, time, configuration) stay the running game's
// Cleared as a valid access code clears them (INTER 0x0A1D, a level's 0x2A63).
constexpr uint32_t kCodeVars[] = {128, 435, 426, 391, 378};

struct Level {
	const char *tot;
	int number; // INTER's number
};

// In play order: each level sets the next one's number when it starts.
constexpr Level kLevels[] = {
	{"AVT003", kFirstLevel}, {"AVT00", 3}, {"AVT002", 0}, {"AVT004", 2}, {"AVT005", 4}, {"AVT001", 5},
	{"AVT007", 1}, {"AVT006", 7}, {"AVT009", 6}, {"AVT008", 9}, {"AVT010", 10}, {"AVT011", 11},
	{"AVT012", 12}, {"AVT014", 13}, {"AVT015", 14}, {"AVT016", 15}, {"AVT017", 16}, {"AVT018", 17},
	{"AVT019", 18}, {"AVT020", 19}, {"AVT021", 20}, {"AVT022", 21},
};

// The base letters of each level number's access code (INTER 0x0B12; 22
// is the ending, 23 unused).
constexpr const char *kCodeBases[] = {
	"GAGEAA", "BULBEB", "CANONC", "TOTODD", "DRUIDE", "FOUDRF", "GATEAG", "HAHAHH", "HIHIHI", "JONASJ",
	"FLUTEK", "DROITL", "BANJOM", "CUBENN", "RALERO", "RATOPP", "GOBLIQ", "ABCNSR", "LEMEIS", "LLEURT",
	"JEUDEU", "ROLEDV", "ETOUTW", "LESTPX",
};

const Level *findLevel(const std::string &tot) {
	for (const Level &l : kLevels)
		if (tot == l.tot)
			return &l;
	return nullptr;
}

// The code a level shows for the next one (a level's 0x17EB): the base
// letters raised by the energy, two per letter over the first five, the
// level letter, then a checksum letter. INTER takes the energy back as the
// sum of the raised amounts.
std::string accessCode(int number, int energy) {
	if (number < 0 || number >= int(std::size(kCodeBases)))
		return {};
	std::string code = kCodeBases[number];
	for (size_t i = 0; i < 5; i++) {
		int add = std::min(energy, 2);
		code[i] = char(code[i] + add);
		energy -= add;
	}
	int sum = 0;
	for (char c : code)
		sum += c;
	code.push_back(char('A' + sum % 26));
	return code;
}

uint32_t get32le(const std::vector<uint8_t> &d, size_t o) {
	return o + 3 < d.size() ? uint32_t(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (uint32_t(d[o + 3]) << 24)) : 0;
}

constexpr char kSnapshotMagic[4] = {'G', '1', 'Q', 'S'};
constexpr uint32_t kSnapshotVersion = 1;

} // namespace

std::vector<LevelInfo> Interpreter::levelsV1() const {
	std::vector<LevelInfo> out;
	int n = 0;
	for (const Level &l : kLevels) {
		LevelInfo info;
		info.tot = l.tot;
		info.number = ++n;
		info.name = "Level " + std::to_string(n);
		if (l.number != kFirstLevel)
			info.code = accessCode(l.number, kFullEnergy);
		out.push_back(std::move(info));
	}
	return out;
}

// A level script is running (the innermost script) with its objects loaded.
bool Interpreter::levelRunningV1() const {
	return _rules.levels == LevelSystem::Gob1 && _cur == int(_envs.size()) - 1 && _envs[_cur]->tot &&
	       findLevel(_envs[_cur]->name) && _gobV1 && _gobV1->loaded();
}

// The level waits for the player: no puzzle step runs (v[285], the item
// being acted on, and v[59], the scripts' busy lock, are clear; AVT006 keeps
// v[59] at 1 once the exit sign shows, v[136]), the level isn't ending and
// the current goblin neither walks nor acts.
bool Interpreter::levelIdleV1() const {
	if (!levelRunningV1() || _vars->slot(kVarResult) != 0 || _vars->slot(kVarActedItem) != 0 ||
	    (_vars->slot(59) != 0 && _vars->slot(kVarExitSign) != 1) || _gobV1->goingSomewhere())
		return false;
	const GoblinsV1::Object *g = _gobV1->goblin(_gobV1->currentGoblin());
	return g && g->state != 10 && g->state != 11;
}

std::string Interpreter::currentLevelTotV1() const {
	const bool top = _cur == int(_envs.size()) - 1 && _envs[_cur]->tot;
	return top && findLevel(_envs[_cur]->name) ? _envs[_cur]->name : std::string();
}

// After a script file has been loaded.
void Interpreter::noteTotStartV1(const std::string &tot) {
	if (findLevel(tot)) {
		_roomLoopReached = false; // its start runs until then (engine/skip.cpp)
		_levelStarts++;
	}
	if (!_rules.isCutsceneTot(tot))
		_skipAllMults = false;
}

std::string Interpreter::quickSavePathV1() const {
	std::string dir = _host.saveDirectory();
	return dir.empty() ? std::string() : (fs::path(dir) / (std::string(_game.key) + "-quick.sav")).string();
}

void Interpreter::requestLevelV1(const std::string &tot) {
	_levelRequest = LevelRequest::Jump;
	_requestedRoom = tot;
	trace("level jump to " + tot + " requested");
	if (inCutscene())
		_skipAllMults = true; // the intro's title animation too
	skipCutscene();
}

void Interpreter::requestQuickSaveV1() {
	if (quickSavePathV1().empty()) {
		_host.notify("Saving is disabled");
		return;
	}
	if (!levelRunningV1()) {
		_host.notify("Can't save here");
		return;
	}
	_saveRequested = true;
}

void Interpreter::requestQuickLoadV1() {
	if (!hasQuickSave()) {
		trace("quick load refused: no quick save");
		_host.notify("No quick save to load");
		return;
	}
	_levelRequest = LevelRequest::Load;
	trace("quick load requested");
	if (inCutscene())
		_skipAllMults = true;
	skipCutscene();
}

// Called from every input wait (checkHotspots); `levelLoop` when it is the
// wait of a level's main loop.
void Interpreter::serviceLevelRequestsV1(bool levelLoop) {
	if (!_saveRequested && _levelRequest == LevelRequest::None && !_pendingSnapshot)
		return;
	const bool top = _cur == int(_envs.size()) - 1 && _envs[_cur]->tot;
	const std::string &name = _envs[_cur]->name;
	const bool idle = levelLoop && levelRunningV1() && _vars->slot(kVarResult) == 0;
	// Screens a level or INTER waits on for a click (the main loop, the
	// skull menu, AGAIN/LOAD/EXIT, game over, the access code).
	const bool canLeave = top && (findLevel(name) || name == "INTER");

	if (_pendingSnapshot && idle) {
		SnapshotV1 snap = std::move(*_pendingSnapshot);
		_pendingSnapshot.reset(); // the fast-forward ends after this frame (the player has control)
		const bool ok = snap.tot == name && applySnapshotV1(snap);
		trace(ok ? "quick load: restored " + snap.tot : "quick load failed in " + name);
		_host.notify(ok ? "Loaded" : "Quick load failed");
		return;
	}
	if (_pendingSnapshot && top && !findLevel(name) && name != "INTER") {
		_pendingSnapshot.reset(); // the game went elsewhere (a cutscene ended the level)
		_host.notify("Quick load failed");
	}

	if (_saveRequested && idle) {
		_saveRequested = false;
		quickSaveNowV1();
	} else if (_saveRequested && !levelRunningV1()) {
		_saveRequested = false; // the level ended before it was idle
		_host.notify("Can't save here");
	}

	if (_levelRequest == LevelRequest::None || !canLeave || (findLevel(name) && levelLoop && !idle))
		return;
	if (_ffSkip)
		stopFastForward("level request");

	if (_levelRequest == LevelRequest::Load) {
		_levelRequest = LevelRequest::None;
		std::optional<SnapshotV1> snap = readSnapshotV1();
		if (!snap) {
			trace("quick load refused: no quick save");
			_host.notify("No quick save to load");
			return;
		}
		const int energy = int(int32_t(get32le(snap->vars, kVarEnergy * 4)));
		const int number = snap->number;
		trace("quick load: leaving " + name + " for " + snap->tot);
		_pendingSnapshot = std::move(snap);
		leaveForLevelV1(number, std::clamp(energy, 0, kFullEnergy));
		startFastForward(); // through INTER and the level's start
		return;
	}

	const Level *l = findLevel(_requestedRoom);
	_levelRequest = LevelRequest::None;
	if (!l)
		return;
	trace("level jump: leaving " + name + " for " + l->tot);
	leaveForLevelV1(l->number, kFullEnergy);
}

// Ends the running script file the way a valid access code does: INTER
// starts level `number` with `energy`.
void Interpreter::leaveForLevelV1(int number, int energy) {
	writeVar(kVarNextLevel, uint32_t(number == kFirstLevel ? 200 : 100 + number));
	writeVar(kVarEnergy, uint32_t(energy));
	for (uint32_t v : kCodeVars)
		writeVar(v, 0);
	if (_gobV1)
		_gobV1->freeAllObjects(); // as the level's own exit does (goblin 1001)
	_totToLoad = "INTER";
	_terminate = 1;
}

void Interpreter::quickSaveNowV1() {
	const Level *l = findLevel(env().name);
	Surface *anim = _draw.sprite(draw::kAnimSurface).get();
	if (!l || !anim) {
		_host.notify("Can't save here");
		return;
	}
	snap::Writer w;
	w.bytes(reinterpret_cast<const uint8_t *>(kSnapshotMagic), 4);
	w(kSnapshotVersion);
	w.str(_game.key);
	w.str(l->tot);
	w(int32_t(l->number));
	w.block(std::vector<uint8_t>(_vars->data(), _vars->data() + _vars->size()));
	const Surface &back = _draw.back();
	w(uint16_t(back.width()));
	w(uint16_t(back.height()));
	w.bytes(back.data(), size_t(back.width()) * size_t(back.height()));
	w(uint16_t(anim->width()));
	w(uint16_t(anim->height()));
	w.bytes(anim->data(), size_t(anim->width()) * size_t(anim->height()));
	for (int16_t r : _cursorResources)
		w(r);
	for (size_t i = 0; i < 40; i++) {
		w(_draw.cursorAnimLow[i]);
		w(_draw.cursorAnimHigh[i]);
		w(_draw.cursorAnimDelays[i]);
	}
	w.block(_gobV1->saveState());

	const std::string path = quickSavePathV1();
	std::error_code ec;
	fs::create_directories(fs::path(path).parent_path(), ec);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char *>(w.data().data()), long(w.data().size()));
	const bool ok = bool(f);
	trace(ok ? "saved quick save of " + std::string(l->tot) + " to " + path : "quick save failed: " + path);
	_host.notify(ok ? "Saved" : "Quick save failed");
}

// Whether the quick save file looks loadable, from its header only (the
// overlay asks every frame); readSnapshotV1 checks the rest when loading.
bool Interpreter::hasQuickSaveV1() const {
	const std::string path = quickSavePathV1();
	if (path.empty())
		return false;
	std::ifstream f(path, std::ios::binary);
	uint8_t head[64] = {};
	f.read(reinterpret_cast<char *>(head), sizeof(head));
	const size_t n = size_t(f.gcount());
	if (n < 8 || std::memcmp(head, kSnapshotMagic, 4) != 0)
		return false;
	snap::Reader r(head + 4, n - 4);
	uint32_t version = 0;
	std::string key, tot;
	r(version);
	return version == kSnapshotVersion && r.str(key) && key == _game.key && r.str(tot) && findLevel(tot);
}

std::optional<Interpreter::SnapshotV1> Interpreter::readSnapshotV1() const {
	const std::string path = quickSavePathV1();
	if (path.empty())
		return std::nullopt;
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return std::nullopt;
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
	if (d.size() < 8 || std::memcmp(d.data(), kSnapshotMagic, 4) != 0)
		return std::nullopt;
	snap::Reader r(d.data() + 4, d.size() - 4);
	uint32_t version = 0;
	r(version);
	std::string key;
	SnapshotV1 s;
	int32_t number = 0;
	if (version != kSnapshotVersion || !r.str(key) || key != _game.key || !r.str(s.tot))
		return std::nullopt;
	r(number);
	s.number = number;
	const Level *l = findLevel(s.tot);
	if (!l || l->number != s.number || !r.block(s.vars) || (_vars && s.vars.size() != _vars->size()) ||
	    s.vars.size() < (kVarLanguage + 1) * 4)
		return std::nullopt;
	auto picture = [&](int &w, int &h, std::vector<uint8_t> &px) {
		uint16_t pw = 0, ph = 0;
		r(pw);
		r(ph);
		if (!r.ok() || pw > 1024 || ph > 1024)
			return false;
		w = pw;
		h = ph;
		px.resize(size_t(pw) * ph);
		return r.bytes(px.data(), px.size());
	};
	if (!picture(s.backW, s.backH, s.back) || !picture(s.animW, s.animH, s.anim))
		return std::nullopt;
	for (int16_t &c : s.cursors)
		r(c);
	for (size_t i = 0; i < 40; i++) {
		r(s.cursorAnimLow[i]);
		r(s.cursorAnimHigh[i]);
		r(s.cursorAnimDelays[i]);
	}
	if (!r.block(s.goblins) || !r.ok() || !r.atEnd())
		return std::nullopt;
	return s;
}

// Puts snapshot `snap` in place in its level, freshly started and idle in
// its main loop. False (the level stays as it started) when the snapshot
// doesn't fit.
bool Interpreter::applySnapshotV1(const SnapshotV1 &snap) {
	Surface *anim = _draw.sprite(draw::kAnimSurface).get();
	Surface &back = _draw.back();
	if (!_gobV1 || !anim || snap.vars.size() != _vars->size() || snap.animW != anim->width() ||
	    snap.animH != anim->height() || snap.backW != back.width() || snap.backH != back.height())
		return false;
	if (!_gobV1->loadState(snap.goblins))
		return false;

	// The variables, apart from input, time and configuration (VAR 0-15)
	// and the language the game runs in now.
	const uint32_t language = _vars->slot(kVarLanguage);
	std::copy(snap.vars.begin() + kKeptVars * 4, snap.vars.end(), _vars->data() + kKeptVars * 4);
	writeVar(kVarLanguage, language);

	// The clean room: normally as the level drew it; pixels the level
	// changed during play are copied.
	int changed = 0;
	for (int y = 0; y < anim->height(); y++)
		for (int x = 0; x < anim->width(); x++)
			if (uint8_t c = snap.anim[size_t(y) * size_t(anim->width()) + size_t(x)]; anim->get(x, y) != c) {
				anim->putPixel(x, y, c);
				changed++;
			}
	_gobV1->redrawAll();
	// Outside the objects' area (the item bar): the pixels that differ.
	const int ml = _multLeft, mt = _multTop, mr = _multLeft + _multWidth - 1, mb = _multTop + _multHeight - 1;
	int barChanged = 0;
	for (int y = 0; y < back.height(); y++)
		for (int x = 0; x < back.width(); x++) {
			if (x >= ml && x <= mr && y >= mt && y <= mb)
				continue;
			if (uint8_t c = snap.back[size_t(y) * size_t(back.width()) + size_t(x)]; back.get(x, y) != c) {
				back.putPixel(x, y, c);
				barChanged++;
			}
		}
	_draw.invalidateRect(0, 0, back.width() - 1, back.height() - 1);

	// The cursors the level had loaded, and their animation.
	for (size_t slot = 0; slot < snap.cursors.size(); slot++)
		if (snap.cursors[slot] >= 0 && snap.cursors[slot] != _cursorResources[slot])
			loadCursorResource(snap.cursors[slot], int(slot));
	_draw.cursorAnimLow = snap.cursorAnimLow;
	_draw.cursorAnimHigh = snap.cursorAnimHigh;
	_draw.cursorAnimDelays = snap.cursorAnimDelays;
	trace("quick load: " + std::to_string(changed) + " room pixels and " + std::to_string(barChanged) +
	      " bar pixels from the snapshot");
	return true;
}

} // namespace gob
