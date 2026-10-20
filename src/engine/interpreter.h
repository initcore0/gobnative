// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The bytecode interpreter. Structure follows the original engine: TOT files
// run in "environments" (a TOT plus its script cursor and variable space);
// functions are blocks executed by funcBlock()/callSub(); opcodes may nest
// blocks recursively. Everything that can wait is a coroutine, so the whole
// call chain can suspend until the next frame.

#pragma once

#include "data/datafiles.h"
#include "audio/mixer.h"
#include "audio/musicpack.h"
#include "engine/cdaudio.h"
#include "engine/draw.h"
#include "engine/game.h"
#include "engine/goblins_v1.h"
#include "engine/host.h"
#include "engine/levels.h"
#include "engine/rules.h"
#include "engine/scenery.h"
#include "engine/task.h"
#include "formats/imd.h"
#include "formats/tot.h"
#include "script/disasm.h"
#include "script/expression.h"
#include "script/random.h"
#include "script/script.h"
#include "script/variables.h"

#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gob {

class Interpreter {
public:
	// Plays `game` (its scripts, rules and version variants) from `files`.
	Interpreter(DataFiles &files, Host &host, const GameInfo &game);

	const GameInfo &game() const { return _game; }
	const GameRules &rules() const { return _rules; }

	// Root task: plays the game starting from `startTot` (e.g. "INTRO").
	Task<void> run(std::string startTot);

	FrameScheduler &scheduler() { return _sched; }

	// Runs the game for one host frame: resumes the scheduler once at 1x,
	// more often when the game runs faster (see `speed`).
	void runFrame();

	void requestQuit() { _quit = true; }
	bool quitRequested() const { return _quit; }

	// True while blocked on player input that the current build can't provide
	// yet (hotspot selection before the hotspot system exists).
	bool blockedOnInput() const { return _blockedOnInput; }

	const std::string &currentTot() const;
	Variables &variables() { return _vars ? *_vars : _nullVars; }
	const Variables &variables() const { return _vars ? *_vars : _nullVars; }

	// What the player currently sees.
	const Surface &screen() { return _draw.screenSurface(); }
	// The drawing layer (sharp text: Draw::recordText, Draw::visibleText).
	Draw &draw() { return _draw; }
	const Palette &screenPalette() const { return _draw.screenPalette(); }

	// Audio output: the front end pulls mixed samples from here.
	Mixer &mixer() { return _mixer; }

	// Replacement music: when set, a CD track with a file in the pack plays
	// that file instead of the CD image (the CD timeline the scripts see is
	// unchanged). Owned by the host.
	MusicPack *musicPack = nullptr;
	// The CD tracks of the loaded .LIC file (empty before the intro reads it).
	const std::vector<CdAudio::Track> &cdTracks() const { return _cd.tracks(); }

	// Mouse cursor overlay: the front end draws it at the live mouse position.
	struct CursorImage {
		const Surface *strip = nullptr;
		int frameX = 0, width = 0, height = 0;
		int hotX = 0, hotY = 0;
	};
	std::optional<CursorImage> cursor();

	// Game language: selects the localized text files and is passed to the
	// scripts in VAR 16 (INTRO copies it for the menu strings, so changing it
	// needs a restart from INTRO).
	Language language = Language::American;

	// Goblins Quest 3: draw the text items flagged as subtitles (bit 15 of
	// their first word). Always on for now, as ScummVM has it for the CD
	// version (some texts that must show are flagged too); the speech
	// option (docs/port-plan.md G3.9) builds on it.
	bool subtitles = true;
	// Goblins 3: play the speech (the sound of the videos from the speech
	// archive). Off, the talking heads move silently and the subtitles,
	// which always show, carry the dialogue.
	bool speech = true;

	// Skip the original copy-protection screen (as ScummVM does by default).
	bool skipCopyProtection = true;

	// Debugging: trace every executed opcode (very verbose).
	bool traceOps = false;

	// Game speed. Game logic time (sleeps, frame pacing, hotspot timers,
	// cursor animation, VAR 12) runs on a game clock that advances `speed`
	// times as fast as Host::ticks(), and runFrame() runs `speed` engine
	// frames per host frame (fades, input polls); changing it mid-game
	// doesn't make the clock jump. The CD timeline and the audio stay on
	// real time.
	double speed = 1.0;

	// The engine's random numbers (script rand(), goblin boredom), the same
	// sequence on every platform for one seed (script/random.h). gobrun
	// takes --seed; saves don't carry its state, as the original's didn't.
	void setRandomSeed(uint32_t seed) { _random.setSeed(seed); }
	int32_t random(int32_t n) { return _random.below(n); }

	// Cutscenes always run at 1x: the intro and the ending poll the CD
	// position to stay in sync with the music. A cutscene is any playMult
	// plus the TOTs around them (INTRO, INTRO0, FINAL).
	bool cutscenesAtNormalSpeed = true;
	bool inCutscene() const;
	double effectiveSpeed() const;

	// --- Skipping (engine/skip.cpp)
	//
	// Two kinds of sequences can be skipped:
	//  - the intro and the ending (inCutscene()): as Escape does in the
	//    original, but also when requested before the animation starts: the
	//    next playMult stops at once and CD sync loops see the track as over;
	//  - scripted sequences around and inside rooms, during which the player
	//    has no control: the INTER picture and MENU between two rooms, a
	//    room's entry (a world's intermission picture and music, goblins
	//    dropping in) up to its main loop, a room being left (v[60] != 0),
	//    and synchronous animations the room's logic runs instead of
	//    returning to its main loop. These are fast-forwarded: the game
	//    logic runs unchanged, as many frames per host frame as time allows,
	//    with the game clock and the CD timeline advancing one 60 Hz frame
	//    per engine frame, music and effects silenced, until the player has
	//    control again (the room idle in its main loop with no keyframed
	//    sequence running), a cutscene or another screen starts, or the game
	//    waits for a key or a click.
	// Never true while the game waits for the player (the room idle in its
	// main loop, the icon bar, a dialog), so Escape keeps its meaning there.
	bool canSkip() const;
	// A fast-forward is in progress (or an intro/ending skip is pending).
	bool skipping() const { return _ffSkip || _skipCutscene; }
	// Skips whatever canSkip() reports; a no-op otherwise.
	void skipCutscene();
	// The game is in a text field (Gobliins 2's save name) and takes typed
	// text: a TV shows its on-screen keyboard meanwhile.
	bool editingText() const { return _editingText; }
	// Wall-clock time runFrame() may spend per host frame while
	// fast-forwarding (0: no limit, only a frame count; deterministic, for
	// headless runs).
	double skipFrameBudgetMs = 12.0;

	// --- Levels, quick save and quick load (engine/levels.cpp, docs/levels.md)
	//
	// These act at a safe point: when the running room is idle in its main
	// loop, between frames, in the state in which the room would let the
	// player open the in-game menu. Requests made before that wait for it
	// (and skip the intro if it is running). Results are reported with
	// Host::notify().
	//
	// Gobliiins (engine/levels_v1.cpp) has its own versions: a jump starts
	// the level through INTER with full energy, as its access code would
	// (from its main loop or any screen of a level or INTER that waits for a
	// click); quick save writes a snapshot of the level (gob1-quick.sav), and
	// quick load goes back to that level, fast-forwards to its main loop and
	// restores the snapshot there.

	// Every room in play order, with its place name in the game language
	// (Gobliiins: its levels, with their access codes).
	std::vector<LevelInfo> levels();
	// The room script running under MENU, or empty (intro, between rooms).
	// Gobliiins: the level running, its objects loaded.
	std::string currentLevel() const;
	// The room runs its main loop in the state in which level requests act
	// (Goblins 2 and 3; Gobliiins: its level is running). For test scripts.
	bool roomIdle() const;
	// Starts room `tot` ("GOB08") as if the player had just reached it: the
	// room is left the way a menu load leaves it; when MENU is back in
	// control the variables are reset to their state when the first room
	// started, the target world's progression variables are set the way the
	// room before it sets them, and MENU starts the room. False for an
	// unknown room. Gobliiins: a level's script ("AVT005") or its number in
	// play order ("5").
	bool requestLevel(const std::string &tot);
	// Saves into slot kQuickSaveSlot under the name "QUICKSAVE", as the
	// in-game menu saves; the slot shows up in the game's own load list.
	// Refused (with a notification) while that slot holds a save made in
	// the game's menu under another name.
	void requestQuickSave();
	// Loads the quick save through the menu's own load path; refused (with
	// a notification) when slot kQuickSaveSlot doesn't hold a quick save.
	void requestQuickLoad();
	// A quick save to load: slot kQuickSaveSlot, named "QUICKSAVE".
	bool hasQuickSave() const;
	// Slot kQuickSaveSlot exists but isn't a quick save (another name, or a
	// file this build can't read), so a quick save must not overwrite it.
	bool quickSaveSlotTaken() const;
	static constexpr int kQuickSaveSlot = 14;
	static constexpr char kQuickSaveName[] = "QUICKSAVE"; // the menu lists names up to 10 characters

	// --- Inspection for debugging tools (engine/inspect.cpp): snapshots of
	// the engine's state for the front end. variables() gives write access.

	struct EnvInfo {
		std::string name;  // TOT base name, upper case
		bool loaded = false; // its script file is loaded (not between TOTs)
		uint32_t pos = 0;  // script offset
		bool current = false;
	};
	// The TOT environments, outermost (INTRO) first.
	std::vector<EnvInfo> envStack() const;

	// Game clock in milliseconds (see `speed`).
	double gameClockMs() const { return _gameMs; }

	struct HotspotInfo {
		int slot = 0;
		uint16_t id = 0, flags = 0, key = 0;
		int left = 0, top = 0, right = 0, bottom = 0; // inclusive screen pixels (signed)
		bool hasEnter = false, hasLeave = false;
		bool expression = false; // coordinates re-evaluated as objects move

		int state() const { return id >> 12; }
		int type() const { return flags & 0xF; }
		int button() const { return (flags >> 4) & 7; } // 0 left, 1 right, 2 any
		bool disabled() const { return state() & 4; }
	};
	// The hotspot table up to its first free entry (the entries the game
	// tests the mouse and keys against), and how many sets are pushed below.
	std::vector<HotspotInfo> hotspots() const;
	int hotspotStackDepth() const { return int(_hotspotStack.size()); }

	struct ObjectInfo {
		int index = 0;
		int x = 0, y = 0;
		int animation = 0, layer = 0, frame = 0, animType = 0, order = 0;
		bool isPaused = false, isStatic = false;
		int left = -1, top = -1, right = -1, bottom = -1; // where last drawn (-1: not drawn)
		int drawnAnimation = 0, drawnLayer = 0, drawnFrame = 0; // what was drawn there (before the frame advanced)
		bool isGoblin = false;
		// goblins only
		int cellX = 0, cellY = 0;       // logical map cell
		int destX = 0, destY = 0;       // current leg target
		int gobDestX = 0, gobDestY = 0; // final target
		int pathExistence = 0, state = 0, nextState = 0, lookDir = 0, isBusy = 0;
	};
	// The mult objects of the running room (empty when there are none).
	std::vector<ObjectInfo> objects() const;
	// What the player can click on in the running room, for the player's
	// own help (frontend/hints.cpp). Gobliiins: the items on the level's map,
	// each as its map cells (docs/goblins-v1.md section 4). The other games:
	// the room's own hotspots (its hotspot block, not one pushed over it such
	// as the icon bar), without the entries kept out of the player's way
	// (parked off screen, full-screen catch-alls, timer ticks, strips at most
	// 2 pixels thick). Inclusive pixels of the visible screen (a scrolled
	// room's view offset taken off); empty outside a room.
	struct ClickSpot {
		struct Rect {
			int left = 0, top = 0, right = 0, bottom = 0;
		};
		int id = 0;     // Gobliiins: the item id; else the hotspot id's low 15 bits
		int object = -1; // Gobliiins: the level object the item stands for (-1: none)
		std::vector<Rect> rects; // Gobliiins: one per map cell
	};
	std::vector<ClickSpot> clickSpots() const;
	// Gobliiins: how many times a level's script file has been loaded, so a
	// new run of a level (a jump to it, AGAIN, the next level) can be told
	// from the one before, even of the same level (engine/hints.h).
	uint32_t levelStarts() const { return _levelStarts; }
	// Gobliiins' goblins, objects and map (null for the other games).
	const GoblinsV1 *goblinsV1() const { return _gobV1.get(); }
	// The loaded scenery animations (an object's `animation` is a slot).
	const Scenery &scenery() const { return _scenery; }

private:
	friend class GoblinsV1; // engine/goblins_v1.cpp: Gobliiins' goblins use the interpreter's services

	struct Env {
		std::string name;              // TOT base name, upper case
		std::unique_ptr<TotFile> tot;  // loaded script file, null between TOTs
		std::unique_ptr<Disassembler> dis; // decodes not-yet-implemented opcodes
		Script script;
	};

	struct Params {
		int retFlag = 0;
		int cmdCount = 0;
		int counter = 0;
		bool doReturn = false;
		uint32_t opStart = 0; // offset of the current opcode byte
	};

	// Variable stack used by pushVars/popVars.
	struct VarStackEntry {
		std::vector<uint8_t> data;
	};

	Env &env() { return *_envs[_cur]; }
	Script &script() { return env().script; }
	void bindEnv();

	// Execution core.
	Task<void> playTot(int function);
	Task<void> callSub(int retFlag);
	Task<void> funcBlock(int retFlag);
	Task<void> execFunc(uint8_t index, Params &p);
	Task<void> execDraw(uint32_t opStart);
	Task<void> evaluateHotspots();
	Task<void> sleep(uint32_t ms);
	Task<void> holdMissingClip();
	Task<void> totSub(uint8_t flags, std::string name);
	Task<void> palLoad();
	Task<void> fadeToPalette(const VgaPalette &target);
	void spriteOp(int op);
	void copySprite();

	// --- hotspots and input (engine/hotspots.cpp, docs/graphics.md 6)
	struct Hotspot {
		uint16_t id = 0;
		uint16_t left = 0xFFFF, top = 0, right = 0, bottom = 0; // inclusive; left 0xFFFF = free
		uint16_t flags = 0; // type (0-3), button (4-6), special (7), cursor (12-15)
		uint16_t key = 0;
		uint16_t funcEnter = 0, funcLeave = 0, funcPos = 0;

		bool isEnd() const { return left == 0xFFFF; }
		int state() const { return id >> 12; }
		int type() const { return flags & 0xF; }
		bool disabled() const { return state() & 4; }
		bool contains(int x, int y) const {
			return x >= int16_t(left) && x <= int16_t(right) && y >= int16_t(top) && y <= int16_t(bottom);
		}
	};
	struct HotspotSet {
		std::vector<Hotspot> entries;
		bool shouldPush;
		uint16_t key, id, index;
		int x, y;
	};
	static constexpr int kHotspotCount = 250;

	int firstFreeHotspot() const;
	void hotspotsClear();
	void hotspotAdd(const Hotspot &h);
	void hotspotRemove(uint16_t id);
	void hotspotsPush(int all, bool force);
	void hotspotsPop();
	void recalculateHotspots(bool force);
	Task<void> hotspotCall(uint16_t offset);
	Task<void> hotspotEnter(int index);
	Task<void> hotspotLeave(int index);
	bool hotspotValid(uint16_t key, int index) const;
	uint16_t checkMouse(bool click, uint16_t &id, uint16_t &index);
	int findCursor(int x, int y) const;
	int findKey(int key) const;
	Task<void> checkHotspotChanged();
	Task<int> checkHotspots(int handleMouse, int delay, uint16_t *id, uint16_t *index);
	struct InputField { int hotspot; uint32_t var; int font; uint8_t back, front; int type; };
	Task<int> handleInputs(std::vector<InputField> &inputs, int handleMouse, uint16_t *id, uint16_t *index);
	Task<int> updateInput(const InputField &in, int handleMouse, uint16_t *id, uint16_t *index);

	int checkKeys();
	void pollMouseButtons();
	void forceMouseUp(bool onlyWhenSynced);
	void flushKeys();
	Task<void> waitMouseRelease(bool drawCursor);
	void animateCursor(int cursor);
	void storeMouse();
	Task<void> waitEndFrame();

	void capturePush(int left, int top, int width, int height);
	void capturePop(bool draw);

	// --- files and save games (engine/saves.cpp)
	enum class SaveKind { None, Game, TempSprite, Notes, Screenshot, Ignore };
	SaveKind saveKind(const std::string &file) const;
	int32_t saveFileSize(SaveKind kind);
	bool saveLoad(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset);
	bool saveStore(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset);
	std::string slotPath(int slot) const;
	std::string notesPath() const;
	void readData();
	void writeData();
	bool writeSlotFile(int slot, const uint8_t *name);
	// Goblins 3 (engine/saves_v3.cpp).
	SaveKind saveKindV3(const std::string &file) const;
	int slotCountV3() const;
	int32_t saveFileSizeV3(SaveKind kind);
	bool saveLoadV3(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset);
	bool saveStoreV3(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset);
	bool tempSpriteToSprite(int32_t size);
	bool spriteToTempSprite(int32_t size);
	std::array<uint8_t, 500> _propsV3{};
	std::array<uint8_t, 1200> _saveIndexV3{};
	bool _hasSaveIndexV3 = false;
	std::array<uint8_t, 80> _shotIndexV3{};
	bool _catSizeAsked = false;

	// --- levels and quick saves (engine/levels.cpp)
	bool roomUnderMenu() const;
	bool levelIdle() const;
	void serviceLevelRequests();
	void finishLevelJump(const std::string &room);
	void noteRoomStart(const std::string &room, int callerEnv);
	void quickSaveNow();
	void notifyQuickSaveSlotTaken();

	// --- Gobliiins' levels and quick saves (engine/levels_v1.cpp, docs/levels.md 9)
	struct SnapshotV1 { // a quick save: the level, its variables, goblins and pictures
		std::string tot;
		int number = 0; // the level's number for INTER
		std::vector<uint8_t> vars;
		int backW = 0, backH = 0, animW = 0, animH = 0;
		std::vector<uint8_t> back, anim; // the back surface and the clean room (anim surface)
		std::array<int16_t, 40> cursors{};
		std::array<int8_t, 40> cursorAnimLow{}, cursorAnimHigh{}, cursorAnimDelays{};
		std::vector<uint8_t> goblins; // GoblinsV1::saveState()
	};
	std::vector<LevelInfo> levelsV1() const;
	bool levelRunningV1() const;
	bool levelIdleV1() const;
	std::string currentLevelTotV1() const; // the innermost script if it is a level (loaded), else empty
	void noteTotStartV1(const std::string &tot);
	void serviceLevelRequestsV1(bool levelLoop);
	void leaveForLevelV1(int number, int energy);
	void quickSaveNowV1();
	bool applySnapshotV1(const SnapshotV1 &snap);
	std::string quickSavePathV1() const;
	std::optional<SnapshotV1> readSnapshotV1() const;
	bool hasQuickSaveV1() const;
	void requestLevelV1(const std::string &tot);
	void requestQuickSaveV1();
	void requestQuickLoadV1();

	// --- Goblins 3's levels and quick saves (engine/levels_v3.cpp, docs/levels-gob3.md)
	static constexpr int kQuickSaveSlotV3 = 29; // the last of its 30 slots
	std::vector<LevelInfo> levelsV3();
	bool roomUnderMenuV3() const;
	bool levelIdleV3() const;
	bool requestLevelV3(const std::string &name);
	std::string roomStatesPathV3() const;
	void loadRoomStatesV3();
	void storeRoomStatesV3() const;
	void requestQuickSaveV3();
	void requestQuickLoadV3();
	bool hasQuickSaveV3() const;
	bool quickSaveSlotTakenV3() const;
	std::optional<std::array<uint8_t, 40>> quickSlotNameV3() const;
	Task<void> serviceLevelRequestsV3();
	void noteRoomStartV3(const std::string &room, int callerEnv);
	void finishLevelJumpV3(const std::string &room);
	// engine/saves_v3.cpp
	std::optional<std::array<uint8_t, 40>> slotNameV3(int slot) const; // a slot file's name, if it holds a save
	bool slotHasFileV3(int slot) const;
	bool writeQuickSaveV3();

	// --- sound effects (engine/mult.cpp, docs/goblins.md 3)
	struct Sample {
		std::shared_ptr<std::vector<uint8_t>> pcm; // signed 8-bit mono
		int rate = 0;
		int id = -1;
	};
	static constexpr int kSoundSlots = 60;
	int loadSoundSearch();
	bool loadSoundResource(int slot, int16_t id);
	void loadSoundData(int slot, const std::vector<uint8_t> &snd, int id);
	void freeSoundSlot(int slot);
	void freeAllSounds();
	void blasterPlay(int slot, int repCount, int freq, int fadeLength);
	void blasterStop(int fadeLength);
	bool blasterPlaying() { return _ffSkip ? _gameMs < _ffSample.end : _mixer.samplePlaying(0); }

	// --- scenery and mult animation (engine/mult.cpp, docs/animation.md)
	enum AnimField : int {
		kAnimation = 0, kLayer = 1, kFrame = 2, kAnimType = 3, kOrder = 4, kIsPaused = 5, kIsStatic = 6,
		kMaxTick = 7, kMaxFrame = 8, kNewLayer = 9, kNewAnimation = 10, kIntersected = 11, kNewCycle = 12,
		kState = 13, kNextState = 14, kNewState = 15, kCurLookDir = 16, kIsBusy = 17, kPathExistence = 18,
		kDestX = 19, kDestY = 20, kFramesLeft = 21, kStateType = 22, kAnimTypeBak = 23,
		kRedrawAnimation = 24, kRedrawLayer = 25, kRedrawFrame = 26,
	};
	struct MultObject {
		uint32_t posXSlot = 0, posYSlot = 0; // 32-bit VAR indices
		uint32_t animData = 0;               // byte offset of the anim-data record
		Variables *store = nullptr;          // where those live: null = the script variables
		int tick = 0;
		int16_t lastLeft = -1, lastTop = -1, lastRight = -1, lastBottom = -1;
		int16_t newLeft = 0, newTop = 0, newRight = 0, newBottom = 0;
		bool needRedraw = false;
		// goblin-only state (engine/goblins.cpp). Types matter: values wrap.
		bool isGoblin = false;
		uint8_t goblinX = 1, goblinY = 1; // logical cell
		uint8_t destX = 0, destY = 0;     // current leg target
		int8_t gobDestX = 0, gobDestY = 0; // final target
		uint8_t nearestWayPoint = 0, nearestDest = 0;
		struct GobSound { int8_t sndItem; uint8_t sndFrame; int16_t freq; int8_t repCount; uint8_t speaker; };
		struct GobState { int16_t animation, layer; std::vector<GobSound> sounds; };
		std::vector<std::optional<GobState>> goblinStates; // indexed by state number; empty = no goblin
	};
	struct MultData {
		std::vector<int> animIndices;
		std::vector<bool> animLoaded;
		struct StaticKey { int16_t frame, layer; };
		struct AnimKey { int16_t frame, layer, posX, posY, order; };
		struct TextKey { int16_t frame, cmd; };
		struct SoundKey { int16_t frame, cmd, freq, fadeLength, repCount; int soundIndex; uint16_t resId = 0xFFFF; };
		std::vector<StaticKey> staticKeys;
		std::array<std::vector<AnimKey>, 4> animKeys;
		std::vector<TextKey> textKeys;
		std::vector<SoundKey> soundKeys;
		std::vector<int> sndSlots;
		std::vector<int16_t> spriteList;
		std::array<int, 4> animKeysFrames{-1, -1, -1, -1};
		std::array<int, 4> animKeysStop{};
		std::array<std::array<int, 4>, 4> animKeysIndices{};
		std::array<std::array<int, 4>, 4> animObjs{};
		// Version 1.3 (Goblins 3): IMD keys (Mult_v2::loadImds).
		struct ImdKey {
			int16_t frame = 0, imdFile = 0, field4 = 0, field6 = 0;
			uint16_t flags = 0;
			int16_t palFrame = 0, lastFrame = 0;
			int8_t palStart = 0, palEnd = 0;
		};
		std::vector<std::string> imdFiles;
		std::array<std::vector<ImdKey>, 4> imdKeys;
		std::array<int, 4> imdIndices{-1, -1, -1, -1}; // the key playing on each track
		std::array<std::array<int, 4>, 4> imdKeysIndices{}; // per sequence slot and track: the next key

		// Version 1 only (engine/mult_v1.cpp).
		std::vector<int> staticIndices;
		std::vector<bool> staticLoaded;
		int frameRate = 0;
		std::array<std::array<uint8_t, 48>, 5> fadePal{}; // 16 colours each
		struct PalFadeKey { int16_t frame, fade, palIndex; int8_t flag; };
		struct PalKey {
			int16_t frame, cmd;
			std::array<int16_t, 4> rates;
			std::array<std::array<uint8_t, 4>, 16> subst; // colour + 1 per step and channel; 0 ends
		};
		std::vector<PalFadeKey> palFadeKeys;
		std::vector<PalKey> palKeys;
		int sndSlotsCount = 0;
	};
	// Version 1 playMult state (Mult_v1).
	struct MultV1State {
		bool animDataAllocated = false; // playMult made its own 4 objects
		Variables objectVars{4 * 4 * 2 + 4 * 40};
		bool doPalSubst = false;
		int palAnimKey = 0;
		std::array<int, 4> palAnimIndices{};
		std::array<uint8_t, 12> palAnimColor{};
		VgaPalette realPalette{}; // the palette under a palette-key animation
		bool fading = false;      // a non-blocking fade key is running
		int fadeStep = 2;
		VgaPalette fadeTarget{};
	};

	int animField(int obj, AnimField f) const;
	void setAnimField(int obj, AnimField f, int v);
	Variables &objVars(int obj) const {
		Variables *s = _objects[size_t(obj)].store;
		return s ? *s : *_vars;
	}
	int objPosX(int obj) const { return int16_t(objVars(obj).slot(_objects[size_t(obj)].posXSlot)); }
	int objPosY(int obj) const { return int16_t(objVars(obj).slot(_objects[size_t(obj)].posYSlot)); }
	void setObjPos(int obj, int x, int y) {
		objVars(obj).setSlot(_objects[size_t(obj)].posXSlot, uint32_t(x));
		objVars(obj).setSlot(_objects[size_t(obj)].posYSlot, uint32_t(y));
	}
	Scenery::ResourceLoader resourceLoader();
	int sceneryLoadAnim(bool search);
	void resolveGlobalLayer(const MultData &md, int globalLayer, int &animation, int &layer);
	void initMult();
	void freeMult();
	void loadMultObject();
	void loadMult();
	void freeMultKeys(int index);
	void multSub();
	void advanceObjects(MultData &md, int slot);
	void advanceAnimKeys(MultData &md, int slot, int t, int obj, int frame);
	bool advanceImdKeys(MultData &md, int slot, int t, int obj, int frame);
	void doSoundAnim(MultData &md, int frame);
	void animate();
	void newCycleAnim(int obj);
	Task<void> playMult(int w);

	// --- version 1 scenery and mult (engine/mult_v1.cpp, docs/animation.md 4)
	Task<void> waitCdForLoadAnim();
	Task<int> sceneryLoadAnimV1(bool search);
	int sceneryLoadStatic(bool search);
	Task<void> loadMultV1();
	void freeMultKeysV1();
	void freeMultObjectsV1();
	void loadMultObjectV1();
	Task<void> playMultV1(bool checkEscape);
	void playMultInitV1(MultData &md);
	void drawStaticsV1(MultData &md, int frame, bool &stop);
	void drawAnimsV1(MultData &md, int frame);
	void prepPalAnimV1(MultData &md, int frame, bool &stop);
	void doPalAnimV1(MultData &md, int frame);
	Task<void> doFadeAnimV1(MultData &md, int frame, bool &stop);
	void animateV1();
	void newCycleAnimV1(int obj);
	void printTotTextV1(int16_t id);
	void resetAnimationState();

	// --- goblins and map (engine/goblins.cpp, docs/goblins.md)
	enum Direction { kDirNone = 0, kDirNW, kDirN, kDirNE, kDirW, kDirE, kDirSW, kDirS, kDirSE };
	struct WayPoint { int8_t x, y, notWalkable; };
	int getPass(int x, int y) const;
	void setPass(int x, int y, int v);
	Direction getDirection(int x0, int y0, int x1, int y1) const;
	static void moveDirection(Direction d, int &x, int &y);
	int checkDirectPath(int x0, int y0, int x1, int y1) const;
	int findNearestWayPoint(int x, int y) const;
	void optimizePoints(int obj, int x, int y);
	void findNearestWalkable(int &cx, int &cy, int mouseX, int mouseY) const;
	const MultObject::GobState *gobState(int obj, int state) const;
	int measureHeight(int animation, int layer, int x, int y);
	void anchorToCell(int obj, int gx, int gy);
	void loadMapObjects();
	void freeGoblins();
	void goblinMove(int destX, int destY, int obj);
	void initiateMove(int obj);
	void movePathFind(int obj);
	void advMovement(int state, uint8_t &x, uint8_t &y) const;
	bool isMovement(int state) const;
	void goblinPlaySounds(int obj);
	void goblinSetState(int obj, int state);
	void handleGoblins(int noTurn1, int noTurn2, int relax1, int relax2, int busy1, int busy2);
	void placeGoblin(int obj, int x, int y, int state);
	void setGoblinState(int obj, int state, int type);
	void goblinPlaceObject(int obj, int animType);
	void goblinMoveAdvance(int obj);
	void goblinAnimate(int obj);
	void speakerOn(int freq);
	void speakerOff();
	void printTotText(int16_t id);
	void printText();
	bool loadFont(int slot, const std::string &file);
	void loadCursorResource(int16_t resId, int slot); // loadCursor: picture resId into cursor slot `slot`
	Task<void> switchTotSub(int16_t index, int16_t function);

	// Control-flow helpers.
	uint32_t checkSwitchTable();
	uint32_t checkSwitchTableV1();
	void initControlVars(bool full);
	void renewTimeInVars();
	void storeKey(int16_t key);

	// Game clock (milliseconds), see `speed`.
	uint32_t gameTicks();
	bool skipPending();
	// The CD timeline's clock: real time, plus the time skipped by
	// fast-forwards.
	uint32_t cdTicks() const { return _host.ticks() + uint32_t(int64_t(_cdClockOffset)); }
	// playCDTrack: starts named track `track` on the CD timeline and the mixer.
	void startCdTrack(const std::string &name);
	// Starts the mixer's output for CD track `t`, `offset` CD frames in.
	void startCdMusic(const CdAudio::Track &t, int32_t offset, std::string &note);

	// --- skipping (engine/skip.cpp)
	bool inSkippableSequence() const;
	void startFastForward();
	void stopFastForward(const char *why);
	const char *fastForwardEnd();
	void fastForwardFrame();
	void resyncCdAudio();

	// --- Version layers (engine/interpreter_v1.cpp, engine/interpreter_v3.cpp;
	// docs/port-plan.md 5.2). For games other than Gobliins 2, execFunc,
	// execDraw and goblinFunc first offer the opcode (its operands not read
	// yet, apart from the goblin function's command and parameter count) to
	// the hook of the game's engine version. A hook returns true when it
	// handled the opcode; otherwise the shared (version 2) implementation
	// runs. A version 2 case that must not run for another version is masked
	// by handling it in the hook.
	Task<bool> execFuncVersion(uint8_t index, Params &p);
	Task<bool> execDrawVersion(uint8_t op, uint32_t opStart);
	Task<bool> goblinFuncVersion(int16_t cmd, int16_t paramCount);
	Task<bool> execFuncV1(uint8_t index, Params &p);
	Task<bool> execDrawV1(uint8_t op, uint32_t opStart);
	Task<bool> goblinFuncV1(int16_t cmd, int16_t paramCount);
	Task<bool> execFuncV3(uint8_t index, Params &p);
	Task<bool> execDrawV3(uint8_t op, uint32_t opStart);
	Task<bool> goblinFuncV3(int16_t cmd, int16_t paramCount);
	// Version 3 (and version 2 opcodes Gobliins 2 never uses; engine/interpreter_v3.cpp).
	void printTotTextV3(int16_t id);
	void getTotTextItemPart();
	void addHotspotV3();
	void removeHotspotV3();
	void loadSoundV3();
	Task<void> sparseRetrace(int max);
	Task<void> wobble();
	// Screens larger than the display, the split bar and scrolling
	// (Goblins 3; docs/graphics.md 9).
	void initScreenV3();
	Task<void> scrollV3();
	void setScrollOffsetV3();
	void showScrollOffset() { _draw.viewX = _draw.scrollX; _draw.viewY = _draw.scrollY; } // Util::setScrollOffset
	void evaluateScroll();
	void pollMouseV3();
	int _videoMode = 0x13, _fakeVideoMode = 0x13; // the screen mode, and what VAR 15 says
	int _mouseMinX = 0, _mouseMinY = 0, _mouseMaxX = 320, _mouseMaxY = 200;
	bool _preventScroll = false;
	bool _wantScroll = false;
	int _wantScrollX = 0, _wantScrollY = 0;
	int _lastMouseX = -1, _lastMouseY = -1; // the host's mouse at the last poll (display)
	// Palette cycling (func 0x17 animPalInit, Inter_v2::animPalette): up to 8
	// ranges of colours rotated by one entry each frame (keyFunc and
	// animatePalette). Gobliins 2 never starts one.
	void animPalInit();
	void animPalette();
	std::array<int16_t, 8> _animPalLow{}, _animPalHigh{}, _animPalDir{};
	int _curSparse = 0;       // copies to the front surface since it was last shown
	uint32_t _lastSparse = 0; // game clock at the last one
	int16_t _subtitleFont = 0, _subtitleColor = 0;

	// --- IMD videos (engine/video.cpp, docs/imd.md 4)
	struct VideoProps { // VideoPlayer::Properties
		int sprite = draw::kFrontSurface;
		int x = -1, y = -1;
		uint16_t flags = 0;
		int startFrame = -1, lastFrame = -1, endFrame = -1, palFrame = -1;
		int16_t breakKey = 27;
		uint16_t palCmd = 8;
		int palStart = 0, palEnd = 255;
		bool needBlit = false; // flag 0x40 on the front surface
		bool skippable = true; // the Skip button cancels it (not a mult's videos)
		bool canceled = false;
	};
	struct VideoSlot {
		std::string name; // upper case, with the extension
		ImdDecoder imd;
		int sprite = -1;  // the target surface, -1: none (sound only)
		Surface buffer;   // decoding buffer: the target's size, mirroring it
		std::shared_ptr<std::vector<uint8_t>> track; // the whole sound (signed 8-bit)
		int soundStartFrame = -1;
		bool soundPlaying = false;
		bool keepCursor = false;
		bool voice = false; // from the speech archive
		double frameRate = 12;
		double startMs = 0; // game clock at the first frame
	};
	Surface *videoTarget();
	bool openVideo(const std::string &file, VideoProps &props);
	void closeVideo();
	void copyVideoPalette(int palStart, int palEnd);
	void videoTargetFromFlags(VideoProps &props);
	bool videoFrame(VideoProps &props);
	Task<void> playVideoFrame(VideoProps &props);
	void playMultImd(const std::string &file, const MultData::ImdKey &key, int startFrame);
	Task<void> playVideo(VideoProps &props);
	Task<void> playImd();
	void getImdInfo();
	std::unique_ptr<VideoSlot> _video; // the primary video (playImd)
	std::string _voiceArchive;         // the speech archive openItk opened (file name)
	bool _inPlayImd = false;

	// Logs and skips an opcode that is not implemented yet.
	void stub(uint32_t opStart);

	int32_t evalInt() { return _expr.evalInt(); }
	int16_t evalVal() { return _expr.parseVal(); }
	std::string evalString() { return _expr.evalString(); }
	VarRef readVar() { return _expr.parseVarIndex(); }
	void writeVarOffset(uint32_t off, uint32_t v) { _vars->write32(off, v); }
	void writeVar(uint32_t slot, uint32_t v) { _vars->setSlot(slot, v); }

	void trace(const std::string &line);

	DataFiles &_files;
	Host &_host;
	const GameInfo &_game;
	const GameRules &_rules;
	const int _ver; // engine version: 1, 2 or 3
	FrameScheduler _sched;

	std::vector<std::unique_ptr<Env>> _envs;
	int _cur = 0;

	std::shared_ptr<Variables> _vars;
	Script _nullScript;
	Variables _nullVars;
	Expression _expr;

	std::vector<VarStackEntry> _varStack;
	CdAudio _cd;
	std::string _cdReplacing; // track whose replacement music the mixer was last given
	Mixer _mixer;
	Draw _draw;

	std::array<Hotspot, kHotspotCount> _hotspots;
	std::vector<HotspotSet> _hotspotStack;
	bool _shouldPush = false;
	uint16_t _currentKey = 0, _currentId = 0, _currentIndex = 0;
	int _currentX = 0, _currentY = 0;

	int _interMouseX = 152, _interMouseY = 92;
	int _mouseButtons = 0;   // as seen by scripts (0 none, 1 left, 2 right)
	int _eventButtons = 0;   // press/release-tracked state; cleared by forceMouseUp

	std::array<Sample, kSoundSlots> _sounds;

	static constexpr int kSaveSlots = 15;
	static constexpr int kSlotNameLength = 40;
	static constexpr int kSaveIndexSize = kSaveSlots * kSlotNameLength;
	static constexpr int kNotesSize = 600;
	std::array<uint8_t, kSaveIndexSize> _saveIndex{};
	bool _hasSaveIndex = false;
	struct TempSprite { int width = 0, height = 0; std::vector<uint8_t> pixels; VgaPalette palette{}; bool hasPalette = false; };
	std::optional<TempSprite> _tempSprite;

	Scenery _scenery{_draw};
	std::vector<MultObject> _objects;
	int _objCount = 0;
	int _multLeft = 0, _multTop = 0, _multWidth = 320, _multHeight = 200;
	std::array<std::unique_ptr<MultData>, 8> _multDatas;
	MultData *_multData = nullptr;
	MultV1State _m1;
	int _multFrame = 0;
	int _gobsCount = -1;

	int _mapW = 26, _mapH = 33, _tilesW = 12, _tilesH = 6;
	static constexpr int kPassStride = 40;
	int _passStride = kPassStride; // pass map row stride in variable space (map version 3: 65)
	int _mapScreenW = 320;         // the width the map covers (map version 3: 640)
	int64_t _passMap = -1; // byte offset of the pass map in variable space
	std::vector<WayPoint> _wayPoints;
	std::array<int, kSoundSlots> _gobSoundSlots{};
	int _gobSoundSlotsCount = 0;
	bool _ignoreSpeakerOff = false;
	std::unique_ptr<GoblinsV1> _gobV1; // Gobliiins only

	struct Capture { int left, top, width, height; };
	std::vector<Capture> _captures;
	int _captureCounter = 0; // pushes made by the current playTot
	uint32_t _lastFrameTime = 0;

	// Control state, mirroring the original's globals.
	int _terminate = 0;    // 1 = leave TOT, 2 = quit to caller
	bool _break = false;
	int _nestLevel = 0;
	int _breakFromLevel = -1;
	std::string _totToLoad;
	bool _quit = false;
	bool _blockedOnInput = false;

	uint32_t _startTime = 0;
	uint32_t _frameBudget = 0;
	double _gameMs = 0;      // game clock
	uint32_t _hostLast = 0;  // Host::ticks() at its last update
	double _frameCredit = 0; // engine frames owed by runFrame()
	uint32_t _timerLate = 0; // how late checkHotspots' timeout fired
	bool _inPlayMult = false;
	bool _skipCutscene = false;
	int _frameRate = 12;

	// Fast-forward skips (engine/skip.cpp).
	bool _ffSkip = false;
	double _ffStartMs = 0;        // game clock when it started
	double _ffIdleSince = -1;     // game clock since the room is idle with sequences running
	bool _ffCdChanged = false;    // the game started or stopped CD tracks meanwhile
	struct VirtualSample {        // the effect voice, timed on the game clock while skipping
		double end = 0;           // game clock when it ends (infinity: loops)
		int slot = -1, repCount = 0, freq = 0;
	} _ffSample;
	double _cdClockOffset = 0;    // ms added to the CD timeline by fast-forwards
	bool _roomLoopReached = false; // the running room has reached its main loop's input wait
	uint32_t _levelStarts = 0;     // Gobliiins: level script files loaded (levelStarts())
	double _lastControlMs = 0;    // game clock when the room's main loop last waited for input
	bool _waitingForPlayer = false; // in an input wait other than the room's main loop

	// Levels and quick saves.
	int _hotspotDepth = 0;     // nested hotspot blocks being evaluated
	bool _levelBlock = false;  // the outermost hotspot block is a room's main loop
	uint32_t _lastKeyPoll = 0;  // Goblins 3: game time of the last keyFunc 2 (key poll)
	int _roomLoopPasses = 0;   // Goblins 3: 1 once the room's main loop has run its timed update
	bool _atLevelLoop = false; // suspended in that block's input wait
	enum class LevelRequest { None, Jump, Load };
	LevelRequest _levelRequest = LevelRequest::None;
	std::string _requestedRoom;
	bool _saveRequested = false;
	std::string _jumpArmed; // room to start once the current room has returned to MENU
	bool _loadArmed = false; // the quick save is being loaded by MENU
	std::vector<uint8_t> _freshVars; // variables when MENU started its first room
	// Goblins 3: the variables at each room's first visit of the player's
	// latest game (gob3-rooms.sav; docs/levels-gob3.md 4).
	std::map<std::string, std::vector<uint8_t>> _roomStatesV3;
	bool _roomStatesLoadedV3 = false;
	bool _jumpWithoutStateV3 = false; // the jump's room starts from the first room's variables
	std::string _jumpStateRoomV3; // the jump's room, its recorded state applied again when MENU starts it
	Random _random;
	bool _editingText = false; // in handleInputs()
	std::optional<SnapshotV1> _pendingSnapshot; // Gobliiins: a quick load waiting for its level to be idle
	bool _skipAllMults = false; // a skip ends every animation of the cutscene, not just the current one
	std::array<int16_t, 40> _cursorResources = filled<40>(-1); // resource loaded into each cursor slot
	template <size_t N>
	static std::array<int16_t, N> filled(int16_t v) {
		std::array<int16_t, N> a;
		a.fill(v);
		return a;
	}
};

} // namespace gob
