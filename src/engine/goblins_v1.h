// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gobliiins' goblins, objects and level map (docs/goblins-v1.md): the
// version 1 Map and Goblin of the original engine. Unlike Gobliins 2,
// where goblins are mult objects, Gobliiins keeps its three goblins, a
// pseudo-goblin and up to 20 objects as state machines of its own, drawn
// by goblin op 1003 over the anim surface, and a 26 x 28 walk map with an
// items map that tells which item lies on each cell.
//
// The component belongs to the Interpreter (a friend) and uses its script
// cursor, variables, scenery, drawing, sound slots and CD timeline.

#pragma once

#include "formats/avo.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gob {

class Interpreter;

class GoblinsV1 {
public:
	explicit GoblinsV1(Interpreter &in);
	~GoblinsV1();

	// Runs goblin function `cmd` (F25 goblinFunc, after its command and
	// parameter count words). Returns false for a command without a
	// handler; its operands are then unread.
	bool goblinFunc(int16_t cmd, int16_t paramCount);

	// Frees the level's objects and sounds (also done by goblin op 1001).
	void freeAllObjects();

	// --- snapshots (Gobliiins' quick save, engine/levels_v1.cpp)

	// The level's state as the goblin ops have changed it since
	// loadObjects: the walk and item maps, item positions, every goblin and
	// object, the drawing list, the pocket and the movement state. The
	// state tables and sounds are not included: they come from the level's
	// AVO file, which the level loads again before a snapshot is restored.
	std::vector<uint8_t> saveState() const;
	// Restores a snapshot made in the same level. False, with nothing
	// changed, when it doesn't fit the loaded level (another level, or a
	// snapshot of another format version).
	bool loadState(const std::vector<uint8_t> &data);
	// Draws every shown object over the clean room (the anim surface), in
	// drawing order and with the static planes in front of them, as the
	// screen looked when the snapshot was made (after loadState).
	void redrawAll();

	// --- state, for inspection (gobrun --print-objects) and tests

	struct State {
		int16_t animation = 0, layer = 0, unk0 = 0, unk1 = 0;
		int16_t sndItem = -1, freq = 0, repCount = 0, sndFrame = 0;
	};
	using StateLine = std::array<std::optional<State>, 6>;
	using StateMachine = std::vector<StateLine>;

	// Gob_Object (goblin.h:69). Field types follow the original: the
	// char fields wrap as signed bytes.
	struct Object {
		int16_t animation = 0, state = 0, stateColumn = 0, curFrame = 0;
		int16_t xPos = 0, yPos = 0;
		int16_t dirtyLeft = 0, dirtyTop = 0, dirtyRight = 0, dirtyBottom = 0;
		int16_t left = 0, top = 0, right = 0, bottom = 0;
		int16_t nextState = 0, multState = 0, actionStartState = 0, curLookDir = 0;
		int16_t pickable = 0, relaxTime = 0;
		StateMachine *stateMach = nullptr; // the states in use: its own, or a goblin's (multState > 39)
		StateMachine realStateMach;        // its own states
		int8_t doAnim = 0, order = 0, noTick = 0, toRedraw = 0, type = 0, maxTick = 0, tick = 0;
		int8_t multObjIndex = 0, unk14 = 0, visible = 0;

		bool ownStates() const { return stateMach == &realStateMach; }
	};

	struct Pos { int8_t x = 0, y = 0; };
	struct ItemPos { int8_t x = 0, y = 0, orient = 0; };
	struct WayPoint { int16_t x = 0, y = 0, notWalkable = 0; };

	static constexpr int kMapWidth = 26, kMapHeight = 28;
	static constexpr int kGoblins = 4, kObjects = 20, kSounds = 16;
	// Interpreter sound slots holding the goblin sounds (Gobliiins' scripts
	// load no sounds of their own; mult sound keys use slots below 20).
	static constexpr int kSoundSlotBase = 40;

	const Object *goblin(int i) const { return i >= 0 && i < kGoblins ? _goblins[size_t(i)].get() : nullptr; }
	const Object *object(int i) const { return i >= 0 && i < kObjects ? _objects[size_t(i)].get() : nullptr; }
	int currentGoblin() const { return _currentGoblin; }
	Pos gobPosition(int i) const { return i >= 0 && i < 3 ? _gobPositions[size_t(i)] : Pos{}; }
	int itemIndInPocket() const { return _itemIndInPocket; }
	int itemIdInPocket() const { return _itemIdInPocket; }
	bool loaded() const { return _goblins[0] != nullptr; }
	int8_t getPass(int x, int y) const;
	int16_t getItem(int x, int y) const;
	// The animation layer of the object's current state.
	int16_t layerOf(const Object &o) const { return curState(o).layer; }
	int pathExistence() const { return _pathExistence; }
	// The current goblin walks to a target or is about to act there.
	bool goingSomewhere() const { return _pathExistence != 0 || _goesAtTarget != 0 || _readyToAct != 0; }

	// The items on the map, i.e. what a click acts on (section 4 of
	// docs/goblins-v1.md): every item id in either byte of a cell, with the
	// cells holding it and the level object it stands for, if any.
	struct ItemSpot {
		int16_t id = 0;
		int object = -1;                         // index in object(), -1: a fixed spot of the room
		int left = 0, top = 0, right = 0, bottom = 0; // the cells' bounding box, inclusive screen pixels
		std::array<uint32_t, kMapHeight> rows{}; // bit x of row y: cell (x, y) holds the id
		bool has(int x, int y) const {
			return x >= 0 && x < kMapWidth && y >= 0 && y < kMapHeight && (rows[size_t(y)] >> x & 1);
		}
	};
	static constexpr int kCellW = 12, kCellH = 6;
	std::vector<ItemSpot> itemSpots() const;

private:
	// --- map (Map + Map_v1)
	enum Direction : int16_t {
		kDirNone = 0, kDirNW = 0x4700, kDirN = 0x4800, kDirNE = 0x4900, kDirW = 0x4B00,
		kDirE = 0x4D00, kDirSW = 0x4F00, kDirS = 0x5000, kDirSE = 0x5100,
	};
	void setPass(int x, int y, int8_t pass);
	void setItem(int x, int y, int16_t item);
	void placeMapItem(int16_t x, int16_t y, int16_t id);
	Direction getDirection(int16_t x0, int16_t y0, int16_t x1, int16_t y1) const;
	static void moveDirection(Direction dir, int16_t &x, int16_t &y);
	int16_t checkDirectPath(int16_t x0, int16_t y0, int16_t x1, int16_t y1) const;
	int16_t checkLongPath(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t i0, int16_t i1) const;
	int16_t findNearestWayPoint(int16_t x, int16_t y) const;
	void findNearestToGob();
	void findNearestToDest();
	void optimizePoints();
	const WayPoint &wayPoint(int n) const;
	ItemPos &itemPos(int n);
	void loadMapObjects(const std::string &avoName);
	void loadSounds(const AvoFile &f);
	void loadMapsInitGobs();

	// --- objects and goblins (Goblin + Goblin_v1)
	Object *gob(int i) { return i >= 0 && i < kGoblins ? _goblins[size_t(i)].get() : nullptr; }
	Object *obj(int i) { return i >= 0 && i < kObjects ? _objects[size_t(i)].get() : nullptr; }
	int16_t itemToObject(int item) const;
	bool objPickable(int item) const; // the object linked to `item` is pickable
	const State &stateOf(const Object &o, int state, int column) const;
	const State &curState(const Object &o) const { return stateOf(o, o.state, o.stateColumn); }
	const State &state0(const Object &o) const { return stateOf(o, o.state, 0); }
	int framesCount(int animation, int layer) const;

	int8_t rotateState(int16_t from, int16_t to) const;
	int16_t peekGoblin(const Object *cur) const;
	void sortByOrder();
	bool soundLoaded(int index) const;
	void playSound(int index, int16_t repCount, int16_t freq);
	void drawObjects();
	void animateObjects();
	int16_t getObjMaxFrame(const Object &o) const;
	bool objIntersected(const Object *a, const Object *b) const;
	int16_t nextLayer(Object &o);
	void showBoredom(int16_t gobIndex);
	void switchGoblin(int16_t index);
	void adjustDest(int16_t posX, int16_t posY);
	void adjustTarget();
	void targetDummyItem(Object &o);
	void targetItem();
	void moveFindItem(int16_t posX, int16_t posY);
	void moveCheckSelect(int16_t framesCount, Object &o, int16_t &gobIndex, int16_t &nextAct);
	void moveInitStep(int16_t framesCount, int16_t action, int16_t cont, Object &o, int16_t &gobIndex,
	                  int16_t &nextAct);
	void moveTreatRopeStairs(Object &o);
	int16_t doMove(Object &o, int16_t cont, int16_t action);
	void freeObjects();
	void loadObjects(const std::string &source);
	void placeObject(Object &o, int8_t animated);
	void initVarPointers();
	void saveGobDataToVars(int16_t xPos, int16_t yPos, int16_t someVal);
	void loadGobDataFromVars();
	void pickItem(int16_t indexToPocket, int16_t idToPocket);
	void placeItem(int16_t indexInPocket, int16_t idInPocket);
	void swapItems(int16_t indexToPick, int16_t idToPick);
	void treatItemPick(int16_t itemId);
	int16_t treatItem(int16_t action);
	void initiateMove();
	void movePathFind(Object &o, int16_t nextAct);
	void moveAdvance(Object &o, int16_t nextAct, int16_t framesCount);
	void manipulateMap(int16_t xPos, int16_t yPos, int16_t item);
	void initGoblin();
	void playBgMusic();
	void speaker(int freq, int ms);

	// Snapshots: one serializer for both directions (snap::Writer or
	// snap::Reader); false when a snapshot doesn't fit the loaded level.
	template <class IO>
	bool serialize(IO &io);
	int16_t objectRef(const Object *o) const;   // goblin i: i, object i: 100 + i, none: -1
	Object *objectFromRef(int16_t ref);
	int16_t machineRef(const StateMachine *m) const; // the owner's objectRef, none: -1
	StateMachine *machineFromRef(int16_t ref);

	// Variables: the goblin record at bytes 236-456 (initVarPointers).
	int32_t var(uint32_t off) const;
	void setVar(uint32_t off, int32_t v);

	void trace(const std::string &line) const;

	Interpreter &_in;

	// Map_v1
	std::array<int8_t, kMapWidth * kMapHeight> _passMap{};
	std::array<int16_t, kMapWidth * kMapHeight> _itemsMap{};
	std::array<WayPoint, 40> _wayPoints{};
	std::array<ItemPos, 40> _itemPoses{};
	ItemPos _dummyItemPos;
	int16_t _nearestWayPoint = 0, _nearestDest = 0;
	int16_t _curGoblinX = 0, _curGoblinY = 0, _destX = 0, _destY = 0;
	bool _loadFromAvo = false;

	// Goblin
	std::array<std::unique_ptr<Object>, kGoblins> _goblins;
	std::array<std::unique_ptr<Object>, kObjects> _objects;
	std::vector<Object *> _objList; // drawing list, sorted by order
	bool _hasList = false;
	int16_t _currentGoblin = 0;
	int16_t _gobStateLayer = 0;
	int8_t _goesAtTarget = 0, _readyToAct = 0;
	int16_t _gobAction = 0; // 0 move, 3 act, 4 pick
	std::array<Pos, 3> _gobPositions{};
	int16_t _gobDestX = 0, _gobDestY = 0, _pressedMapX = 0, _pressedMapY = 0;
	int8_t _pathExistence = 0;
	int16_t _destItemType = 0, _destItemState = 0;
	std::array<int16_t, 20> _itemToObject{};
	int16_t _objCount = 0, _gobsCount = 0;
	int16_t _itemIndInPocket = 5, _itemIdInPocket = 2;
	int8_t _itemByteFlag = 0;
	int16_t _destItemId = -1, _destActionItem = 0;
	Object *_actDestItemDesc = nullptr;
	std::array<int16_t, 10> _forceNextState{-1, -1, -1, -1, -1, -1, -1, 0, 0, 0};
	int8_t _boreCounter = 0;
	int16_t _positionedGob = 5;
	int8_t _noPick = 0;
	bool _varsBound = false; // initVarPointers has run (the original's references are unset before)
	mutable bool _warnedState = false;
};

} // namespace gob
