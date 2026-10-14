// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gobliiins' goblins, objects and level map (docs/goblins-v1.md). A port of
// ScummVM's Map/Map_v1 (map.cpp, map_v1.cpp), Goblin/Goblin_v1 (goblin.cpp,
// goblin_v1.cpp) and the version 1 goblin opcodes (inter_v1.cpp:1516-2861).
// The logic, the order of operations and the integer types follow the
// original; where it would dereference a missing state or index past a
// table, this port uses a harmless default instead (traced once).

#include "engine/goblins_v1.h"

#include "engine/interpreter.h"
#include "engine/snapshot.h"

#include <algorithm>
#include <cstdlib>

namespace gob {

namespace {

// Variable byte offsets of the goblin record (Goblin::initVarPointers).
enum VarOffset : uint32_t {
	kGobRet = 236,
	kCurGobState = 240, kCurGobFrame = 244, kCurGobMultState = 248, kCurGobNextState = 252,
	kCurGobScrX = 256, kCurGobScrY = 260, kCurGobLeft = 264, kCurGobTop = 268, kCurGobRight = 272,
	kCurGobBottom = 276, kCurGobDoAnim = 280, kCurGobOrder = 284, kCurGobNoTick = 288, kCurGobType = 292,
	kCurGobMaxTick = 296, kCurGobTick = 300, kCurGobActStartState = 304, kCurGobLookDir = 308,
	kCurGobPickable = 320, kCurGobRelax = 324,
	kDestItemState = 328, kDestItemFrame = 332, kDestItemMultState = 336, kDestItemNextState = 340,
	kDestItemScrX = 344, kDestItemScrY = 348, kDestItemLeft = 352, kDestItemTop = 356, kDestItemRight = 360,
	kDestItemBottom = 364, kDestItemDoAnim = 368, kDestItemOrder = 372, kDestItemNoTick = 376,
	kDestItemType = 380, kDestItemMaxTick = 384, kDestItemTick = 388, kDestItemActStartSt = 392,
	kDestItemLookDir = 396, kDestItemPickable = 408, kDestItemRelax = 412, kDestItemMaxFrame = 420,
	kCurGob = 424, kSome0Val = 428, kCurGobXPos = 432, kCurGobYPos = 436, kCurGobMaxFrame = 440,
	kItemInPocket = 456,
};

constexpr int8_t kRotStates[4][4] = {
	{0, 22, 23, 24},
	{13, 2, 12, 14},
	{16, 15, 4, 17},
	{27, 25, 26, 6},
};

int random(int n) { return n > 0 ? std::rand() % n : 0; }

template <typename T>
T clip(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Per-TOT CD background music of the CD version (Sound::cdPlayBgMusic).
struct BgTrack { const char *tot, *track; };
constexpr BgTrack kBgMusic[] = {
	{"AVT00", "mine"}, {"AVT001", "nuit"}, {"AVT002", "campagne"}, {"AVT003", "extsor1"},
	{"AVT004", "interieure"}, {"AVT005", "zombie"}, {"AVT006", "zombie"}, {"AVT007", "campagne"},
	{"AVT008", "campagne"}, {"AVT009", "extsor1"}, {"AVT010", "extsor1"}, {"AVT011", "interieure"},
	{"AVT012", "zombie"}, {"AVT014", "nuit"}, {"AVT015", "interieure"}, {"AVT016", "statue"},
	{"AVT017", "zombie"}, {"AVT018", "statue"}, {"AVT019", "mine"}, {"AVT020", "statue"},
	{"AVT021", "mine"}, {"AVT022", "zombie"},
};

} // namespace

GoblinsV1::GoblinsV1(Interpreter &in) : _in(in) {
	_itemToObject.fill(0);
}

GoblinsV1::~GoblinsV1() = default;

void GoblinsV1::trace(const std::string &line) const {
	_in.trace(line);
}

int32_t GoblinsV1::var(uint32_t off) const {
	return _varsBound && _in._vars ? int32_t(_in._vars->read32(off)) : 0;
}

void GoblinsV1::setVar(uint32_t off, int32_t v) {
	if (_varsBound && _in._vars)
		_in._vars->write32(off, uint32_t(v));
}

// ---------------------------------------------------------------------------
// Map (map.cpp, map_v1.cpp)

int8_t GoblinsV1::getPass(int x, int y) const {
	if (x < 0 || y < 0 || x >= kMapWidth || y >= kMapHeight)
		return 0;
	return _passMap[size_t(y * kMapWidth + x)];
}

void GoblinsV1::setPass(int x, int y, int8_t pass) {
	if (x < 0 || y < 0 || x >= kMapWidth || y >= kMapHeight)
		return;
	_passMap[size_t(y * kMapWidth + x)] = pass;
}

int16_t GoblinsV1::getItem(int x, int y) const {
	x = clip(x, 0, kMapWidth - 1);
	y = clip(y, 0, kMapHeight - 1);
	return _itemsMap[size_t(y * kMapWidth + x)];
}

void GoblinsV1::setItem(int x, int y, int16_t item) {
	x = clip(x, 0, kMapWidth - 1);
	y = clip(y, 0, kMapHeight - 1);
	_itemsMap[size_t(y * kMapWidth + x)] = item;
}

void GoblinsV1::placeMapItem(int16_t x, int16_t y, int16_t id) {
	if ((getItem(x, y) & 0xFF00) != 0)
		setItem(x, y, int16_t((getItem(x, y) & 0xFF00) | id));
	else
		setItem(x, y, int16_t((getItem(x, y) & 0x00FF) | (id << 8)));
}

const GoblinsV1::WayPoint &GoblinsV1::wayPoint(int n) const {
	static const WayPoint none{};
	return n >= 0 && n < int(_wayPoints.size()) ? _wayPoints[size_t(n)] : none;
}

GoblinsV1::ItemPos &GoblinsV1::itemPos(int n) {
	if (n >= 0 && n < int(_itemPoses.size()))
		return _itemPoses[size_t(n)];
	_dummyItemPos = {};
	return _dummyItemPos;
}

GoblinsV1::Direction GoblinsV1::getDirection(int16_t x0, int16_t y0, int16_t x1, int16_t y1) const {
	if (x0 == x1 && y0 == y1)
		return kDirNone;
	if (x1 < 0 || x1 > kMapWidth || y1 < 0 || y1 > kMapHeight)
		return kDirNone;

	bool up = false, down = false, left = false, right = false;
	if (y1 > y0)
		down = true;
	else if (y1 < y0)
		up = true;
	if (x1 > x0)
		right = true;
	else if (x1 < x0)
		left = true;

	// On ladders (3) or ropes (6), keep climbing in the wanted direction.
	int8_t here = getPass(x0, y0);
	if ((here == 3 || here == 6) && up && getPass(x0, y0 - 1) != 0)
		return kDirN;
	if ((here == 3 || here == 6) && down && getPass(x0, y0 + 1) != 0)
		return kDirS;

	auto open = [&](int dx, int dy) { return getPass(x0 + dx, y0 + dy) != 0; };
	if (left && !up && !down)
		return open(-1, 0) ? kDirW : kDirNone;
	if (right && !up && !down)
		return open(1, 0) ? kDirE : kDirNone;
	if (up && !left && !right)
		return open(0, -1) ? kDirN : open(-1, -1) ? kDirNW : open(1, -1) ? kDirNE : kDirNone;
	if (down && !left && !right)
		return open(0, 1) ? kDirS : open(-1, 1) ? kDirSW : open(1, 1) ? kDirSE : kDirNone;
	if (right && up)
		return open(1, -1) ? kDirNE : open(0, -1) ? kDirN : open(1, 0) ? kDirE : kDirNone;
	if (right && down)
		return open(1, 1) ? kDirSE : open(0, 1) ? kDirS : open(1, 0) ? kDirE : kDirNone;
	if (left && up)
		return open(-1, -1) ? kDirNW : open(0, -1) ? kDirN : open(-1, 0) ? kDirW : kDirNone;
	return open(-1, 1) ? kDirSW : open(0, 1) ? kDirS : open(-1, 0) ? kDirW : kDirNone;
}

void GoblinsV1::moveDirection(Direction dir, int16_t &x, int16_t &y) {
	switch (dir) {
	case kDirNW: x--; y--; break;
	case kDirN: y--; break;
	case kDirNE: x++; y--; break;
	case kDirW: x--; break;
	case kDirE: x++; break;
	case kDirSW: x--; y++; break;
	case kDirS: y++; break;
	case kDirSE: x++; y++; break;
	default: break;
	}
}

// 1 when walking the greedy direction reaches (x1, y1), 3 when it gets stuck.
int16_t GoblinsV1::checkDirectPath(int16_t x0, int16_t y0, int16_t x1, int16_t y1) const {
	for (int guard = 0; guard < 100000; guard++) {
		Direction dir = getDirection(x0, y0, x1, y1);
		if (x0 == x1 && y0 == y1)
			return 1;
		if (dir == kDirNone)
			return 3;
		moveDirection(dir, x0, y0);
	}
	return 3;
}

// 1 when (x1, y1) can be reached through the way points i0 .. i1.
int16_t GoblinsV1::checkLongPath(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t i0, int16_t i1) const {
	int16_t curX = x0, curY = y0;
	int16_t nextLink = 1;
	for (int guard = 0; guard < 100000; guard++) {
		if (x0 == curX && y0 == curY)
			nextLink = 1;
		if (nextLink != 0) {
			if (checkDirectPath(x0, y0, x1, y1) == 1)
				return 1;
			nextLink = 0;
			curX = wayPoint(i0).x;
			curY = wayPoint(i0).y;
			if (i0 > i1)
				i0--;
			else if (i0 < i1)
				i0++;
		}
		if (i0 == i1 && wayPoint(i0).x == x0 && wayPoint(i0).y == y0)
			return checkDirectPath(x0, y0, x1, y1) == 1 ? 1 : 0;
		Direction dir = getDirection(x0, y0, curX, curY);
		if (dir == kDirNone)
			return 0;
		moveDirection(dir, x0, y0);
	}
	return 0;
}

int16_t GoblinsV1::findNearestWayPoint(int16_t x, int16_t y) const {
	int16_t nearest = -1, length = 30000;
	for (int i = 0; i < int(_wayPoints.size()); i++) {
		const WayPoint &w = _wayPoints[size_t(i)];
		if (w.x < 0 || w.x >= kMapWidth || w.y < 0 || w.y >= kMapHeight)
			break;
		int16_t d = int16_t(std::abs(x - w.x) + std::abs(y - w.y));
		if (d <= length) {
			nearest = int16_t(i);
			length = d;
		}
	}
	return nearest;
}

void GoblinsV1::findNearestToGob() {
	int16_t w = findNearestWayPoint(_curGoblinX, _curGoblinY);
	if (w != -1)
		_nearestWayPoint = w;
}

void GoblinsV1::findNearestToDest() {
	int16_t w = findNearestWayPoint(_destX, _destY);
	if (w != -1)
		_nearestDest = w;
}

void GoblinsV1::optimizePoints() {
	if (_nearestWayPoint < _nearestDest) {
		for (int i = _nearestWayPoint; i <= _nearestDest; i++)
			if (checkDirectPath(_curGoblinX, _curGoblinY, wayPoint(i).x, wayPoint(i).y) == 1)
				_nearestWayPoint = int16_t(i);
	} else if (_nearestWayPoint > _nearestDest) {
		for (int i = _nearestWayPoint; i >= _nearestDest; i--)
			if (checkDirectPath(_curGoblinX, _curGoblinY, wayPoint(i).x, wayPoint(i).y) == 1)
				_nearestWayPoint = int16_t(i);
	}
}

// Map_v1::loadMapObjects with the hard-coded patches (docs/goblins-v1.md 2).
void GoblinsV1::loadMapObjects(const std::string &avoName) {
	auto data = _in._files.read(avoName);
	if (!data) {
		trace("loadObjects: " + avoName + " missing (.AVJ levels are not supported)");
		_loadFromAvo = false;
		return;
	}
	std::string err;
	auto f = parseAvo(*data, &err);
	if (!f) {
		trace("loadObjects: " + avoName + ": " + err);
		_loadFromAvo = false;
		return;
	}
	_loadFromAvo = true;

	_passMap = f->pass;
	_itemsMap = f->items;
	for (int i = 0; i < AvoFile::kWayPoints; i++)
		_wayPoints[size_t(i)] = {f->wayPoints[size_t(i)].x, f->wayPoints[size_t(i)].y, 0};
	for (int i = 0; i < AvoFile::kItemPoses; i++)
		_itemPoses[size_t(i)] = {f->itemPoses[size_t(i)].x, f->itemPoses[size_t(i)].y, f->itemPoses[size_t(i)].orient};

	loadSounds(*f);

	auto makeObject = [](const AvoObject &a, int lines) {
		auto o = std::make_unique<Object>();
		o->xPos = a.xPos;
		o->yPos = a.yPos;
		o->order = int8_t(a.order);
		o->state = a.state;
		o->multObjIndex = int8_t(a.multObjIndex);
		o->realStateMach.resize(size_t(lines));
		for (int s = 0; s < AvoObject::kStates; s++)
			for (int c = 0; c < AvoObject::kColumns; c++)
				if (const auto &st = a.states[size_t(s)][size_t(c)])
					o->realStateMach[size_t(s)][size_t(c)] =
					    State{st->animation, st->layer, st->unk0, st->unk1, st->sndItem, st->freq, st->repCount, st->sndFrame};
		o->stateMach = &o->realStateMach;
		return o;
	};

	_gobsCount = int16_t(f->goblins.size());
	for (int i = 0; i < _gobsCount; i++)
		_goblins[size_t(i)] = makeObject(f->goblins[size_t(i)], i == 3 ? 70 : 40);

	auto patch = [&](int g, int s, auto fn) {
		if (Object *o = gob(g); o && s < int(o->realStateMach.size())) {
			auto &st = o->realStateMach[size_t(s)][0];
			if (!st) {
				trace("loadObjects: goblin " + std::to_string(g) + " has no state " + std::to_string(s) + " to patch");
				st = State{};
			}
			fn(*st);
		}
	};
	for (int g = 0; g < 3; g++)
		if (Object *o = gob(g))
			o->realStateMach[39][0] = State{0, int16_t(98 + g), 0, 0, -1, 0, 0, 0};
	for (int s : {10, 11, 28, 29})
		patch(2, s, [](State &st) { st.sndFrame = 13; });
	for (int s : {10, 11})
		patch(1, s, [](State &st) { st.sndFrame = 13; });
	if (Object *o = gob(3))
		for (int s = 40; s < 70; s++) {
			o->realStateMach[size_t(s)][0] = State{9, int16_t(s - 40), 0, 0, -1, 0, 0, 0};
			o->realStateMach[size_t(s)][1].reset();
		}

	_objCount = int16_t(f->objects.size());
	for (int i = 0; i < _objCount; i++)
		_objects[size_t(i)] = makeObject(f->objects[size_t(i)], 40);

	// Object 10: a one-state dummy (animation 9, layer 27), kept hidden.
	auto dummy = std::make_unique<Object>();
	dummy->realStateMach.resize(40);
	dummy->realStateMach[0][0] = State{9, 27, 0, 0, -1, 0, 0, 0};
	dummy->stateMach = &dummy->realStateMach;
	_objects[10] = std::move(dummy);
	placeObject(*_objects[10], 1);
	_objects[10]->type = 1;
	_objects[10]->unk14 = 1;

	for (size_t i = 0; i < f->itemToObject.size() && i < _itemToObject.size(); i++)
		_itemToObject[i] = f->itemToObject[i];
}

// Map_v1::loadSounds: the listed samples go into goblin sounds 0..n-1
// (names that are not files are skipped), diamant1.snd into sound 14.
void GoblinsV1::loadSounds(const AvoFile &f) {
	auto load = [&](int index, const std::string &file) {
		auto data = _in._files.read(file);
		if (!data || data->size() < 6)
			return;
		_in.loadSoundData(kSoundSlotBase + index, *data, -1);
	};
	load(14, "DIAMANT1.SND");
	for (size_t i = 0; i < f.sounds.size() && i < size_t(kSounds); i++)
		if (!f.sounds[i].empty())
			load(int(i), f.sounds[i] + ".SND");
}

void GoblinsV1::loadMapsInitGobs() {
	if (!_loadFromAvo) {
		trace("loadMap: no .AVO loaded");
		return;
	}
	for (int i = 0; i < 3; i++)
		if (Object *g = gob(i))
			nextLayer(*g);
	for (int i = 0; i < 3; i++) {
		Object *g = gob(i);
		if (!g)
			continue;
		int16_t layer = state0(*g).layer;
		_in._scenery.updateAnim(layer, 0, g->animation, 0, g->xPos, g->yPos, false);
		g->yPos = int16_t((_gobPositions[size_t(i)].y + 1) * 6 - (_in._scenery.toRedrawBottom - _in._scenery.pieceTop));
		g->xPos = int16_t(_gobPositions[size_t(i)].x * 12 - (_in._scenery.toRedrawLeft - _in._scenery.pieceLeft));
		g->order = int8_t(_in._scenery.toRedrawBottom / 24 + 3);
	}

	_currentGoblin = 0;
	_pressedMapX = _gobPositions[0].x;
	_pressedMapY = _gobPositions[0].y;
	_pathExistence = 0;
	if (Object *g = gob(0))
		g->doAnim = 0;
	if (Object *g = gob(1))
		g->doAnim = 1;
	if (Object *g = gob(2))
		g->doAnim = 1;
}

// ---------------------------------------------------------------------------
// Object helpers

int16_t GoblinsV1::itemToObject(int item) const {
	return item >= 0 && item < int(_itemToObject.size()) ? _itemToObject[size_t(item)] : int16_t(100);
}

bool GoblinsV1::objPickable(int item) const {
	const Object *o = object(itemToObject(item));
	return o && o->pickable == 1;
}

const GoblinsV1::State &GoblinsV1::stateOf(const Object &o, int state, int column) const {
	static const State none{};
	const StateMachine *m = o.stateMach;
	if (m && state >= 0 && state < int(m->size()) && column >= 0 && column < 6 && (*m)[size_t(state)][size_t(column)])
		return *(*m)[size_t(state)][size_t(column)];
	if (!_warnedState) {
		_warnedState = true;
		trace("goblins: missing state " + std::to_string(state) + "." + std::to_string(column) + " (using an empty one)");
	}
	return none;
}

int GoblinsV1::framesCount(int animation, int layer) const {
	const Scenery::Layer *l = _in._scenery.layer(animation, layer);
	return l ? l->framesCount : 0;
}

int8_t GoblinsV1::rotateState(int16_t from, int16_t to) const {
	int f = from / 2, t = to / 2;
	if (f < 0 || f > 3 || t < 0 || t > 3)
		return 0;
	return kRotStates[f][t];
}

int16_t GoblinsV1::getObjMaxFrame(const Object &o) const {
	return int16_t(framesCount(o.animation, state0(o).layer) - 1);
}

bool GoblinsV1::objIntersected(const Object *a, const Object *b) const {
	if (!a || !b)
		return false;
	if (a->type == 1 || b->type == 1)
		return false;
	if (a->right < b->left || a->left > b->right)
		return false;
	if (a->bottom < b->top || a->top > b->bottom)
		return false;
	return true;
}

// The goblin (1-3) under the mouse other than `cur`, or 0.
int16_t GoblinsV1::peekGoblin(const Object *cur) const {
	int16_t index = 0;
	int mx = _in._interMouseX, my = _in._interMouseY;
	for (const Object *d : _objList) {
		if (d == cur)
			continue;
		for (int i = 0; i < 3; i++) {
			if (d != _goblins[size_t(i)].get())
				continue;
			if (mx < d->right && mx > d->left && my < d->bottom && my > d->top)
				index = int16_t(i + 1);
		}
	}
	return index;
}

// Goblin::sortByOrder: an exchange sort on order, then bottom, the current
// goblin drawn last among equals.
void GoblinsV1::sortByOrder() {
	const Object *current = gob(_currentGoblin);
	for (size_t i = 0; i + 1 < _objList.size(); i++)
		for (size_t j = i + 1; j < _objList.size(); j++) {
			const Object *a = _objList[i], *b = _objList[j];
			if (a->order <= b->order) {
				if (a->order != b->order)
					continue;
				if (a->bottom <= b->bottom) {
					if (a->bottom != b->bottom)
						continue;
					if (a != current)
						continue;
				}
			}
			std::swap(_objList[i], _objList[j]);
		}
}

bool GoblinsV1::soundLoaded(int index) const {
	return index >= 0 && index < kSounds && _in._sounds[size_t(kSoundSlotBase + index)].pcm;
}

void GoblinsV1::playSound(int index, int16_t repCount, int16_t freq) {
	if (!soundLoaded(index))
		return;
	_in.blasterStop(0);
	_in.blasterPlay(kSoundSlotBase + index, repCount, freq, 0);
}

void GoblinsV1::speaker(int freq, int ms) {
	if (_in._ffSkip) // silent while skipping
		return;
	_in._mixer.playSpeaker(freq, ms);
}

// Restarts the level's background music whenever the CD is silent
// (o1_drawObjects, PC CD version).
void GoblinsV1::playBgMusic() {
	if (_in._cd.position({}, _in.cdTicks()) != -1)
		return;
	const std::string &tot = _in.currentTot();
	for (const BgTrack &t : kBgMusic)
		if (tot == t.tot) {
			_in.startCdTrack(t.track);
			break;
		}
}

// ---------------------------------------------------------------------------
// Drawing and animation (goblin.cpp:200-500)

void GoblinsV1::drawObjects() {
	Scenery &sc = _in._scenery;
	Draw &draw = _in._draw;
	Surface *animSurf = draw.sprite(draw::kAnimSurface).get();

	for (Object *o : _objList) {
		if (o->type == 3)
			o->toRedraw = 1;
		else if (o->type == 1)
			o->toRedraw = 0;
	}

	// Erase every object to redraw from the anim surface (the clean room),
	// and remember the area to repair.
	for (Object *o : _objList) {
		if (o->toRedraw == 0)
			continue;
		if (animSurf)
			draw.back().blit(*animSurf, o->left, o->top, o->right, o->bottom, o->left, o->top);
		draw.invalidateRect(o->left, o->top, o->right, o->bottom);
		if (o->type != 0)
			continue;

		int16_t layer = curState(*o).layer;
		sc.updateAnim(layer, o->curFrame, o->animation, 0, o->xPos, o->yPos, false);
		if (sc.toRedrawLeft == -12345) {
			o->dirtyLeft = o->left;
			o->dirtyRight = o->right;
			o->dirtyTop = o->top;
			o->dirtyBottom = o->bottom;
		} else {
			o->dirtyLeft = std::min(o->left, sc.toRedrawLeft);
			o->dirtyRight = std::max(o->right, sc.toRedrawRight);
			o->dirtyTop = std::min(o->top, sc.toRedrawTop);
			o->dirtyBottom = std::max(o->bottom, sc.toRedrawBottom);
		}
	}

	sortByOrder();
	for (Object *o : _objList) {
		if (o->toRedraw) {
			int16_t layer = curState(*o).layer;
			if (o->type == 0) {
				if (o->visible == 0)
					sc.updateAnim(layer, o->curFrame, o->animation, 0, o->xPos, o->yPos, false);
				else
					sc.updateAnim(layer, o->curFrame, o->animation, 2, o->xPos, o->yPos, true);
				if (sc.toRedrawLeft == -12345) {
					o->left = o->top = o->right = o->bottom = 0;
				} else {
					draw.invalidateRect(sc.toRedrawLeft, sc.toRedrawTop, sc.toRedrawRight, sc.toRedrawBottom);
					o->left = sc.toRedrawLeft;
					o->top = sc.toRedrawTop;
					o->right = sc.toRedrawRight;
					o->bottom = sc.toRedrawBottom;
					sc.updateStatic(o->order);
				}
			} else {
				o->left = o->top = o->right = o->bottom = 0;
				o->type = 1;
			}
			continue;
		}

		// An object that stays: redraw the parts erased under moved ones.
		if (o->type == 0 && o->visible != 0) {
			for (const Object *o2 : _objList) {
				if (o2->toRedraw == 0)
					continue;
				if (o->right < o2->dirtyLeft || o2->dirtyRight < o->left)
					continue;
				if (o->bottom < o2->dirtyTop || o2->dirtyBottom < o->top)
					continue;
				sc.toRedrawLeft = o2->dirtyLeft;
				sc.toRedrawRight = o2->dirtyRight;
				sc.toRedrawTop = o2->dirtyTop;
				sc.toRedrawBottom = o2->dirtyBottom;
				int16_t layer = curState(*o).layer;
				sc.updateAnim(layer, o->curFrame, o->animation, 4, o->xPos, o->yPos, true);
				sc.updateStatic(o->order);
			}
		}
	}

	// State sounds: one effect, or two packed into the bytes of each field.
	for (Object *o : _objList) {
		if (o->toRedraw == 0 || o->type == 1)
			continue;
		const State &st = curState(*o);
		if (st.sndFrame & 0xFF00) {
			int16_t sndFrame = (st.sndFrame >> 8) & 0xFF;
			int16_t sndItem = (st.sndItem >> 8) & 0xFF;
			int16_t freq = int16_t(100 * ((st.freq >> 8) & 0xFF));
			int16_t repCount = (st.repCount >> 8) & 0xFF;
			if (o->curFrame == sndFrame && sndItem != 0xFF)
				playSound(sndItem, repCount, freq);

			sndFrame = st.sndFrame & 0xFF;
			sndItem = st.sndItem & 0xFF;
			freq = int16_t(100 * (st.freq & 0xFF));
			repCount = st.repCount & 0xFF;
			if (o->curFrame == sndFrame && sndItem != 0xFF)
				playSound(sndItem, repCount, freq);
		} else if (o->curFrame == st.sndFrame && st.sndItem != -1) {
			playSound(st.sndItem, st.repCount, st.freq);
		}
	}
}

void GoblinsV1::animateObjects() {
	for (Object *o : _objList) {
		if (o->doAnim != 1 || o->type != 0)
			continue;
		if (o->noTick != 0)
			continue;
		if (o->tick < o->maxTick)
			o->tick++;
		if (o->tick < o->maxTick)
			continue;

		o->tick = 1;
		o->curFrame++;
		int16_t layer = state0(*o).layer;
		const Scenery::Layer *l = _in._scenery.layer(o->animation, layer);
		int fc = l ? l->framesCount : 0;
		if (o->curFrame < fc)
			continue;

		o->curFrame = 0;
		if (l) {
			o->xPos = int16_t(o->xPos + l->animDeltaX);
			o->yPos = int16_t(o->yPos + l->animDeltaY);
		}

		if (o->nextState == -1 && o->multState == -1 && o->unk14 == 0) {
			o->toRedraw = 0;
			o->curFrame = int16_t(fc - 1);
		}

		if (o->multState != -1) {
			if (o->multState > 39) {
				Object *g = gob(o->multObjIndex);
				o->stateMach = g ? g->stateMach : &o->realStateMach;
				o->state = int16_t(o->multState - 40);
			} else {
				o->stateMach = &o->realStateMach;
				o->state = o->multState;
			}
			o->animation = state0(*o).animation;
			o->multState = -1;
		} else {
			if (o->nextState == -1)
				continue;
			o->stateMach = &o->realStateMach;
			o->state = o->nextState;
			o->animation = state0(*o).animation;
			o->nextState = -1;
		}
		o->toRedraw = 1;
	}
}

// Switches to nextState (states above 39 are the pseudo-goblin's) and
// returns the new layer.
int16_t GoblinsV1::nextLayer(Object &o) {
	if (o.nextState == 10)
		o.curLookDir = 0;
	if (o.nextState == 11)
		o.curLookDir = 4;

	if (o.nextState > 39) {
		Object *g = gob(o.multObjIndex);
		o.stateMach = g ? g->stateMach : &o.realStateMach;
	} else {
		o.stateMach = &o.realStateMach;
	}

	o.curFrame = 0;
	o.state = o.nextState > 39 ? int16_t(o.nextState - 40) : o.nextState;
	o.animation = state0(o).animation;
	return state0(o).layer;
}

void GoblinsV1::showBoredom(int16_t gobIndex) {
	Object *g = gob(gobIndex);
	if (!g)
		return;
	int16_t layer = state0(*g).layer;
	int16_t frameCount = int16_t(framesCount(g->animation, layer));
	int16_t state = g->state;
	int16_t frame = g->curFrame;

	g->noTick = 0;
	g->toRedraw = 1;

	int16_t boreFlag = int16_t(1 << random(7));
	if (gobIndex != _currentGoblin && random(3) != 0) {
		if (state == 21) {
			if ((boreFlag & 16) || (boreFlag & 32))
				g->multState = int16_t(92 + gobIndex);
			else if (boreFlag & 1)
				g->multState = int16_t(86 + gobIndex);
			else if (boreFlag & 2)
				g->multState = int16_t(80 + gobIndex);
			else if (boreFlag & 4)
				g->multState = int16_t(89 + gobIndex);
			else if (boreFlag & 8)
				g->multState = int16_t(104 + gobIndex);
		}
		g->nextState = 21;
	} else if (state >= 18 && state <= 21 && _in._vars->slot(59) == 0) {
		if (frame != frameCount)
			return;
		g->multState = int16_t(104 + gobIndex);
	}
}

// index: the goblin to select + 1, or 0 for the next one.
void GoblinsV1::switchGoblin(int16_t index) {
	if (_in._vars->slot(59) != 0)
		return;
	Object *cur = gob(_currentGoblin);
	if (!cur)
		return;
	if (cur->state <= 39 && cur->curFrame != 0)
		return;
	if (index != 0 && (!gob(index - 1) || gob(index - 1)->type != 0))
		return;

	int16_t next = index == 0 ? int16_t((_currentGoblin + 1) % 3) : int16_t(index - 1);

	if (getPass(_curGoblinX, _curGoblinY) == 3 || getPass(_curGoblinX, _curGoblinY) == 6)
		return;
	if (gob((_currentGoblin + 1) % 3)->type != 0 && gob((_currentGoblin + 2) % 3)->type != 0)
		return;

	_gobPositions[size_t(_currentGoblin)].x = int8_t(_curGoblinX);
	_gobPositions[size_t(_currentGoblin)].y = int8_t(_curGoblinY);

	cur->doAnim = 1;
	cur->nextState = 21;
	nextLayer(*cur);

	_currentGoblin = next;
	if (gob(_currentGoblin)->type != 0)
		_currentGoblin = int16_t((_currentGoblin + 1) % 3);

	Object &g = *gob(_currentGoblin);
	g.doAnim = 0;
	g.nextState = g.curLookDir == 4 ? 18 : 19;
	g.toRedraw = 1;
	nextLayer(g);

	int16_t x = _gobPositions[size_t(_currentGoblin)].x;
	_pressedMapX = _destX = _gobDestX = _curGoblinX = x;
	int16_t y = _gobPositions[size_t(_currentGoblin)].y;
	_pressedMapY = _destY = _gobDestY = _curGoblinY = y;

	setVar(kCurGob, _currentGoblin);
	_pathExistence = 0;
	_readyToAct = 0;
}

// ---------------------------------------------------------------------------
// Click targets (goblin.cpp:700-1030)

// A click on a blocked cell (not an item when acting): move it to the
// nearest walkable cell in one of the four directions.
void GoblinsV1::adjustDest(int16_t posX, int16_t posY) {
	if (getPass(_pressedMapX, _pressedMapY) == 0 && (_gobAction == 0 || getItem(_pressedMapX, _pressedMapY) == 0)) {
		int16_t resDelta = -1, resDeltaDir = 0, resDeltaPix = 0, deltaPix, i;

		for (i = 1; i <= _pressedMapX && getPass(_pressedMapX - i, _pressedMapY) == 0; i++)
			;
		if (i <= _pressedMapX) {
			resDeltaPix = int16_t((i - 1) * 12 + (posX % 12) + 1);
			resDelta = i;
		}

		for (i = 1; (i + _pressedMapX) < kMapWidth && getPass(_pressedMapX + i, _pressedMapY) == 0; i++)
			;
		if ((_pressedMapX + i) < kMapWidth) {
			deltaPix = int16_t((i * 12) - (posX % 12));
			if (resDelta == -1 || deltaPix < resDeltaPix) {
				resDeltaPix = deltaPix;
				resDelta = i;
				resDeltaDir = 1;
			}
		}

		for (i = 1; (i + _pressedMapY) < kMapHeight && getPass(_pressedMapX, _pressedMapY + i) == 0; i++)
			;
		if ((_pressedMapY + i) < kMapHeight) {
			deltaPix = int16_t((i * 6) - (posY % 6));
			if (resDelta == -1 || deltaPix < resDeltaPix) {
				resDeltaPix = deltaPix;
				resDelta = i;
				resDeltaDir = 2;
			}
		}

		for (i = 1; i <= _pressedMapY && getPass(_pressedMapX, _pressedMapY - i) == 0; i++)
			;
		if (i <= _pressedMapY) {
			deltaPix = int16_t((i * 6) + (posY % 6));
			if (resDelta == -1 || deltaPix < resDeltaPix) {
				resDeltaPix = deltaPix;
				resDelta = i;
				resDeltaDir = 3;
			}
		}

		switch (resDeltaDir) {
		case 0: _pressedMapX = int16_t(_pressedMapX - resDelta); break;
		case 1: _pressedMapX = int16_t(_pressedMapX + resDelta); break;
		case 2: _pressedMapY = int16_t(_pressedMapY + resDelta); break;
		case 3: _pressedMapY = int16_t(_pressedMapY - resDelta); break;
		default: break;
		}
	}
	_pressedMapX = clip<int16_t>(_pressedMapX, 0, kMapWidth - 1);
	_pressedMapY = clip<int16_t>(_pressedMapY, 0, kMapHeight - 1);
}

void GoblinsV1::adjustTarget() {
	if (_gobAction == 4 && getItem(_pressedMapX, _pressedMapY) == 0) {
		if (_pressedMapY > 0 && getItem(_pressedMapX, _pressedMapY - 1) != 0) {
			_pressedMapY--;
		} else if (_pressedMapX < kMapWidth - 1 && getItem(_pressedMapX + 1, _pressedMapY) != 0) {
			_pressedMapX++;
		} else if (_pressedMapX < kMapWidth - 1 && _pressedMapY > 0 &&
		           getItem(_pressedMapX + 1, _pressedMapY - 1) != 0) {
			_pressedMapY--;
			_pressedMapX++;
		}
	}
	_pressedMapX = clip<int16_t>(_pressedMapX, 0, kMapWidth - 1);
	_pressedMapY = clip<int16_t>(_pressedMapY, 0, kMapHeight - 1);
}

// A click on an empty walkable cell: item 0 is a dummy target there.
void GoblinsV1::targetDummyItem(Object &o) {
	if (getItem(_pressedMapX, _pressedMapY) == 0 && getPass(_pressedMapX, _pressedMapY) == 1) {
		_itemPoses[0].x = int8_t(_pressedMapX);
		_itemPoses[0].y = int8_t(_pressedMapY);
		_itemPoses[0].orient = o.curLookDir == 0 ? -4 : -1;
	}
}

void GoblinsV1::targetItem() {
	if (_gobAction != 3 && _gobAction != 4) {
		_pressedMapX = clip<int16_t>(_pressedMapX, 0, kMapWidth - 1);
		_pressedMapY = clip<int16_t>(_pressedMapY, 0, kMapHeight - 1);
		return;
	}

	int16_t items = getItem(_pressedMapX, _pressedMapY);
	if (_gobAction == 4 && (items & 0xFF00) != 0 && objPickable((items & 0xFF00) >> 8)) {
		_destItemId = _destActionItem = int16_t((items & 0xFF00) >> 8);
		_itemByteFlag = 1;
	} else if ((items & 0xFF) == 0) {
		_destItemId = _destActionItem = int16_t((items & 0xFF00) >> 8);
		_itemByteFlag = 1;
	} else if (_gobAction == 3 && _currentGoblin == 2 && (items & 0xFF00) != 0) {
		_destItemId = _destActionItem = int16_t((items & 0xFF00) >> 8);
		_itemByteFlag = 1;
	} else {
		_destItemId = _destActionItem = int16_t(items & 0xFF);
		_itemByteFlag = 0;
	}

	_pressedMapY = _destY = _gobDestY = itemPos(_destItemId).y;

	if (_gobAction == 3 || _destActionItem == 0) {
		_pressedMapX = _destX = _gobDestX = itemPos(_destItemId).x;
	} else if ((items & 0xFF00) != 0) {
		if (itemPos(_destItemId).orient == 4) {
			if ((getItem(_pressedMapX - 1, _pressedMapY) & 0xFF00) == (getItem(_pressedMapX, _pressedMapY) & 0xFF00)) {
				_pressedMapX--;
				_destX = _gobDestX = _pressedMapX;
			}
		} else if (itemPos(_destItemId).orient == 0) {
			if ((getItem(_pressedMapX + 1, _pressedMapY) & 0xFF00) == (getItem(_pressedMapX, _pressedMapY) & 0xFF00)) {
				_pressedMapX++;
				_destX = _gobDestX = _pressedMapX;
			}
		}
		if ((getItem(_pressedMapX, _pressedMapY + 1) & 0xFF00) == (getItem(_pressedMapX, _pressedMapY) & 0xFF00)) {
			_pressedMapY++;
			_destY = _gobDestY = _pressedMapY;
		}
	} else {
		if (itemPos(_destItemId).orient == 4) {
			if (getItem(_pressedMapX - 1, _pressedMapY) == getItem(_pressedMapX, _pressedMapY)) {
				_pressedMapX--;
				_destX = _gobDestX = _pressedMapX;
			}
		} else if (itemPos(_destItemId).orient == 0) {
			if (getItem(_pressedMapX + 1, _pressedMapY) == getItem(_pressedMapX, _pressedMapY)) {
				_pressedMapX++;
				_destX = _gobDestX = _pressedMapX;
			}
		}
		if (_pressedMapY < kMapHeight - 1) {
			if (getItem(_pressedMapX, _pressedMapY + 1) == getItem(_pressedMapX, _pressedMapY)) {
				_pressedMapY++;
				_destY = _gobDestY = _pressedMapY;
			}
		}
	}

	// Picking up: walk to the item's foot instead, if it is walkable.
	if (_gobAction == 4 && _destActionItem != 0 && itemToObject(_destActionItem) != -1 && objPickable(_destActionItem)) {
		Object &item = *obj(itemToObject(_destActionItem));
		item.animation = state0(item).animation;
		int16_t layer = curState(item).layer;
		Scenery &sc = _in._scenery;
		sc.updateAnim(layer, 0, item.animation, 0, item.xPos, item.yPos, false);

		int16_t tmpX = int16_t((sc.toRedrawRight + sc.toRedrawLeft) / 2);
		int16_t tmpY = sc.toRedrawBottom;

		int16_t tmpPosY = int16_t(tmpY / 6);
		if ((tmpY % 3) < 3 && tmpPosY > 0)
			tmpPosY--;
		int16_t tmpPosX = int16_t(tmpX / 12);
		if ((tmpX % 12) < 6 && tmpPosX > 0)
			tmpPosX--;
		if (itemPos(_destActionItem).orient == 0 || itemPos(_destActionItem).orient == -1)
			tmpPosX++;

		if (getPass(tmpPosX, tmpPosY) == 1) {
			_pressedMapX = _destX = _gobDestX = tmpPosX;
			_pressedMapY = _destY = _gobDestY = tmpPosY;
		}
	}
	_pressedMapX = clip<int16_t>(_pressedMapX, 0, kMapWidth - 1);
	_pressedMapY = clip<int16_t>(_pressedMapY, 0, kMapHeight - 1);
}

// The clicked cell; when acting, a click on a small object aims at its
// middle, and a click beside its item cells at the nearest of them.
void GoblinsV1::moveFindItem(int16_t posX, int16_t posY) {
	if (_gobAction != 3 && _gobAction != 4) {
		_pressedMapX = clip<int16_t>(int16_t(posX / 12), 0, kMapWidth - 1);
		_pressedMapY = clip<int16_t>(int16_t(posY / 6), 0, kMapHeight - 1);
		return;
	}

	int i;
	for (i = 0; i < 20; i++) {
		const Object *o = _objects[size_t(i)].get();
		if (!o || o->type != 0)
			continue;
		if (o->left > posX || o->right < posX || o->top > posY || o->bottom < posY)
			continue;
		if ((o->right - o->left) < 40)
			posX = int16_t((o->left + o->right) / 2);
		if ((o->bottom - o->top) < 40)
			posY = int16_t((o->top + o->bottom) / 2);
		break;
	}

	_pressedMapX = clip<int16_t>(int16_t(posX / 12), 0, kMapWidth - 1);
	_pressedMapY = clip<int16_t>(int16_t(posY / 6), 0, kMapHeight - 1);

	if (getItem(_pressedMapX, _pressedMapY) == 0 && i < 20) {
		const int16_t x = _pressedMapX, y = _pressedMapY;
		const bool right = x < kMapWidth - 1, below = y < kMapHeight - 1, left = x > 0, above = y > 0;
		if (below && getItem(x, y + 1) != 0) {
			_pressedMapY++;
		} else if (right && below && getItem(x + 1, y + 1) != 0) {
			_pressedMapX++;
			_pressedMapY++;
		} else if (right && getItem(x + 1, y) != 0) {
			_pressedMapX++;
		} else if (right && above && getItem(x + 1, y - 1) != 0) {
			_pressedMapX++;
			_pressedMapY--;
		} else if (above && getItem(x, y - 1) != 0) {
			_pressedMapY--;
		} else if (above && left && getItem(x - 1, y - 1) != 0) {
			_pressedMapY--;
			_pressedMapX--;
		} else if (left && getItem(x - 1, y) != 0) {
			_pressedMapX--;
		} else if (left && below && getItem(x - 1, y + 1) != 0) {
			_pressedMapX--;
			_pressedMapY++;
		}
	}
}

void GoblinsV1::moveCheckSelect(int16_t framesCount, Object &o, int16_t &gobIndex, int16_t &nextAct) {
	int mx = _in._interMouseX, my = _in._interMouseY;
	if (o.right > mx && o.left < mx && o.bottom > my && (o.bottom - 10) < my && _gobAction == 0) {
		// A click on the goblin's own feet: it turns towards the player.
		nextAct = (o.curLookDir & 4) ? 16 : 23;
		o.curFrame = int16_t(framesCount - 1);
		_pathExistence = 0;
	} else {
		gobIndex = peekGoblin(&o);
		if (gobIndex != 0) {
			_pathExistence = 0;
		} else if (_curGoblinX == _pressedMapX && _curGoblinY == _pressedMapY) {
			if (_gobAction != 0)
				_readyToAct = 1;
			_pathExistence = 0;
		}
	}
}

void GoblinsV1::moveInitStep(int16_t framesCount, int16_t action, int16_t cont, Object &o, int16_t &gobIndex,
                             int16_t &nextAct) {
	if (cont != 0 && _goesAtTarget == 0 && _readyToAct == 0 && _in._vars->slot(59) == 0 && o.type != 1 &&
	    o.state != 10 && o.state != 11) {
		if (o.state >= 40)
			o.curFrame = int16_t(framesCount - 1);

		_gobAction = action;
		_forceNextState[0] = -1;
		_forceNextState[1] = -1;
		_forceNextState[2] = -1;

		int16_t posX, posY;
		if (action == 3) {
			posX = int16_t(_in._interMouseX + 6);
			posY = int16_t(_in._interMouseY + 7);
		} else if (action == 4) {
			posX = int16_t(_in._interMouseX + 7);
			posY = int16_t(_in._interMouseY + 12);
		} else {
			posX = int16_t(_in._interMouseX);
			posY = int16_t(_in._interMouseY);
		}

		moveFindItem(posX, posY);
		adjustDest(posX, posY);
		adjustTarget();

		_destX = _gobDestX = _pressedMapX;
		_destY = _gobDestY = _pressedMapY;

		targetDummyItem(o);
		targetItem();
		initiateMove();
		moveCheckSelect(framesCount, o, gobIndex, nextAct);
	} else {
		if (_readyToAct != 0 && (_curGoblinX != _pressedMapX || _curGoblinY != _pressedMapY))
			_readyToAct = 0;
		if (o.type == 1)
			gobIndex = peekGoblin(&o);
	}
}

// Ladders (3) for everyone, ropes (6) for goblin 1 only: queue the climbing
// states the walk would otherwise miss.
void GoblinsV1::moveTreatRopeStairs(Object &o) {
	if (_currentGoblin != 1)
		return;
	const int16_t x = _curGoblinX, y = _curGoblinY;
	auto force = [&](int16_t s) {
		_forceNextState[0] = s;
		_forceNextState[1] = -1;
	};
	const int16_t look = o.curLookDir;

	if (o.nextState == 28 && getPass(x, y - 1) == 6)
		force(28);
	if (o.nextState == 29 && getPass(x, y + 1) == 6)
		force(29);
	if ((o.nextState == 28 || o.nextState == 29 || o.nextState == 20) && getPass(x, y) == 6) {
		if ((look == 0 || look == 4 || look == 2) && getPass(x, y - 1) == 6)
			force(28);
		else if ((look == 0 || look == 4 || look == 6) && getPass(x, y + 1) == 6)
			force(29);
	}

	if (o.nextState == 8 && getPass(x, y - 1) == 3)
		force(8);
	if (o.nextState == 9 && getPass(x, y + 1) == 3)
		force(9);
	if (o.nextState == 20 && getPass(x, y) == 3) {
		if ((look == 0 || look == 4 || look == 2) && getPass(x, y - 1) == 3)
			force(8);
		else if ((look == 0 || look == 4 || look == 6) && getPass(x, y + 1) == 3)
			force(9);
	}
}

// One tick of the current goblin: a new click target (cont != 0), then
// the path step and the animation. Returns the goblin (1-3) clicked on.
int16_t GoblinsV1::doMove(Object &o, int16_t cont, int16_t action) {
	int16_t nextAct = 0, gobIndex = 0;
	int16_t layer = state0(o).layer;
	int16_t fc = int16_t(framesCount(o.animation, layer));

	if (_in._vars->slot(59) == 0 && o.state != 30 && o.state != 31)
		o.order = int8_t(o.bottom / 24 + 3);

	if (_positionedGob != _currentGoblin) {
		_curGoblinX = _gobPositions[size_t(_currentGoblin)].x;
		_curGoblinY = _gobPositions[size_t(_currentGoblin)].y;
	}
	_positionedGob = _currentGoblin;

	o.animation = curState(o).animation;
	_gobStateLayer = curState(o).layer;

	moveInitStep(fc, action, cont, o, gobIndex, nextAct);
	moveTreatRopeStairs(o);
	moveAdvance(o, nextAct, fc);
	return gobIndex;
}

// ---------------------------------------------------------------------------
// Loading and freeing (goblin.cpp:1180-1250, goblin_v1.cpp:47-150)

void GoblinsV1::freeObjects() {
	for (int i = 0; i < kSounds; i++)
		_in.freeSoundSlot(kSoundSlotBase + i);
	_actDestItemDesc = nullptr;
	for (auto &g : _goblins)
		g.reset();
	for (auto &o : _objects)
		o.reset();
}

void GoblinsV1::freeAllObjects() {
	_objList.clear();
	_hasList = false;
	freeObjects();
}

void GoblinsV1::loadObjects(const std::string &source) {
	freeObjects();
	_itemToObject.fill(100);
	_objList.clear();
	_hasList = true;

	// "AVT00.AVJ" -> AVT00.AVO
	std::string base = source.substr(0, std::min<size_t>(source.size(), 14));
	base = base.size() >= 4 ? base.substr(0, base.size() - 4) : std::string();
	loadMapObjects(base + ".AVO");

	for (int i = 0; i < _gobsCount; i++)
		if (Object *g = gob(i))
			placeObject(*g, 0);
	for (int i = 0; i < _objCount; i++)
		if (Object *o = obj(i))
			placeObject(*o, 1);

	initVarPointers();
	_actDestItemDesc = nullptr;
	trace("loadObjects " + base + ".AVO: " + std::to_string(_gobsCount) + " goblins, " + std::to_string(_objCount) +
	      " objects");
}

// Goblin_v1::placeObject: resets the object to frame 0 of its state and
// adds it to the drawing list. Objects whose state is missing stay out.
void GoblinsV1::placeObject(Object &o, int8_t animated) {
	const StateMachine *m = o.stateMach;
	if (!m || o.state < 0 || o.state >= int(m->size()) || !(*m)[size_t(o.state)][0])
		return;

	o.animation = state0(o).animation;
	o.noTick = 0;
	o.toRedraw = 1;
	o.doAnim = animated;
	o.maxTick = 1;
	o.tick = 1;
	o.curFrame = 0;
	o.type = 0;
	o.actionStartState = 0;
	o.nextState = -1;
	o.multState = -1;
	o.stateColumn = 0;
	o.curLookDir = 0;
	o.visible = 1;
	o.pickable = 0;
	o.unk14 = 0;
	o.relaxTime = int16_t(random(30));

	int16_t layer = state0(o).layer;
	Scenery &sc = _in._scenery;
	sc.updateAnim(layer, 0, o.animation, 0, o.xPos, o.yPos, false);
	o.order = int8_t(sc.toRedrawBottom / 24 + 3);

	o.left = o.right = o.dirtyLeft = o.dirtyRight = o.xPos;
	o.top = o.bottom = o.dirtyTop = o.dirtyBottom = o.yPos;

	if (_hasList)
		_objList.push_back(&o);
}

void GoblinsV1::initVarPointers() {
	_varsBound = true;
	setVar(kItemInPocket, -2);
}

// The current goblin's record (and the acted-on object's) for the scripts.
void GoblinsV1::saveGobDataToVars(int16_t xPos, int16_t yPos, int16_t someVal) {
	setVar(kSome0Val, someVal);
	setVar(kCurGobXPos, xPos);
	setVar(kCurGobYPos, yPos);
	setVar(kItemInPocket, _itemIndInPocket);

	const Object *o = gob(_currentGoblin);
	if (o) {
		setVar(kCurGobState, o->state);
		setVar(kCurGobFrame, o->curFrame);
		setVar(kCurGobMultState, o->multState);
		setVar(kCurGobNextState, o->nextState);
		setVar(kCurGobScrX, o->xPos);
		setVar(kCurGobScrY, o->yPos);
		setVar(kCurGobLeft, o->left);
		setVar(kCurGobTop, o->top);
		setVar(kCurGobRight, o->right);
		setVar(kCurGobBottom, o->bottom);
		setVar(kCurGobDoAnim, o->doAnim);
		setVar(kCurGobOrder, o->order);
		setVar(kCurGobNoTick, o->noTick);
		setVar(kCurGobType, o->type);
		setVar(kCurGobMaxTick, o->maxTick);
		setVar(kCurGobTick, o->tick);
		setVar(kCurGobActStartState, o->actionStartState);
		setVar(kCurGobLookDir, o->curLookDir);
		setVar(kCurGobPickable, o->pickable);
		setVar(kCurGobRelax, o->relaxTime);
		setVar(kCurGobMaxFrame, getObjMaxFrame(*o));
	}

	if (!_actDestItemDesc)
		return;
	o = _actDestItemDesc;
	setVar(kDestItemState, o->state);
	setVar(kDestItemFrame, o->curFrame);
	setVar(kDestItemMultState, o->multState);
	setVar(kDestItemNextState, o->nextState);
	setVar(kDestItemScrX, o->xPos);
	setVar(kDestItemScrY, o->yPos);
	setVar(kDestItemLeft, o->left);
	setVar(kDestItemTop, o->top);
	setVar(kDestItemRight, o->right);
	setVar(kDestItemBottom, o->bottom);
	setVar(kDestItemDoAnim, o->doAnim);
	setVar(kDestItemOrder, o->order);
	setVar(kDestItemNoTick, o->noTick);
	setVar(kDestItemType, o->type);
	setVar(kDestItemMaxTick, o->maxTick);
	setVar(kDestItemTick, o->tick);
	setVar(kDestItemActStartSt, o->actionStartState);
	setVar(kDestItemLookDir, o->curLookDir);
	setVar(kDestItemPickable, o->pickable);
	setVar(kDestItemRelax, o->relaxTime);
	setVar(kDestItemMaxFrame, getObjMaxFrame(*o));
	_destItemState = o->state;
	_destItemType = o->type;
}

void GoblinsV1::loadGobDataFromVars() {
	_itemIndInPocket = int16_t(var(kItemInPocket));

	if (Object *o = gob(_currentGoblin)) {
		o->state = int16_t(var(kCurGobState));
		o->curFrame = int16_t(var(kCurGobFrame));
		o->multState = int16_t(var(kCurGobMultState));
		o->nextState = int16_t(var(kCurGobNextState));
		o->xPos = int16_t(var(kCurGobScrX));
		o->yPos = int16_t(var(kCurGobScrY));
		o->left = int16_t(var(kCurGobLeft));
		o->top = int16_t(var(kCurGobTop));
		o->right = int16_t(var(kCurGobRight));
		o->bottom = int16_t(var(kCurGobBottom));
		o->doAnim = int8_t(var(kCurGobDoAnim));
		o->order = int8_t(var(kCurGobOrder));
		o->noTick = int8_t(var(kCurGobNoTick));
		o->type = int8_t(var(kCurGobType));
		o->maxTick = int8_t(var(kCurGobMaxTick));
		o->tick = int8_t(var(kCurGobTick));
		o->actionStartState = int16_t(var(kCurGobActStartState));
		o->curLookDir = int16_t(var(kCurGobLookDir));
		o->pickable = int16_t(var(kCurGobPickable));
		o->relaxTime = int16_t(var(kCurGobRelax));
	}

	if (!_actDestItemDesc)
		return;
	Object *o = _actDestItemDesc;
	o->state = int16_t(var(kDestItemState));
	o->curFrame = int16_t(var(kDestItemFrame));
	o->multState = int16_t(var(kDestItemMultState));
	o->nextState = int16_t(var(kDestItemNextState));
	o->xPos = int16_t(var(kDestItemScrX));
	o->yPos = int16_t(var(kDestItemScrY));
	o->left = int16_t(var(kDestItemLeft));
	o->top = int16_t(var(kDestItemTop));
	o->right = int16_t(var(kDestItemRight));
	o->bottom = int16_t(var(kDestItemBottom));
	o->doAnim = int8_t(var(kDestItemDoAnim));
	o->order = int8_t(var(kDestItemOrder));
	o->noTick = int8_t(var(kDestItemNoTick));
	o->type = int8_t(var(kDestItemType));
	o->maxTick = int8_t(var(kDestItemMaxTick));
	o->tick = int8_t(var(kDestItemTick));
	o->actionStartState = int16_t(var(kDestItemActStartSt));
	o->curLookDir = int16_t(var(kDestItemLookDir));
	o->pickable = int16_t(var(kDestItemPickable));
	o->relaxTime = int16_t(var(kDestItemRelax));

	if (o->type != _destItemType)
		o->toRedraw = 1;
	if (o->state != _destItemState && o->type == 0)
		o->toRedraw = 1;
}

// ---------------------------------------------------------------------------
// Items (goblin.cpp:1385-1660)

void GoblinsV1::pickItem(int16_t indexToPocket, int16_t idToPocket) {
	Object *o = obj(indexToPocket);
	if (!o || o->pickable != 1)
		return;
	o->type = 3;
	_itemIndInPocket = indexToPocket;
	_itemIdInPocket = idToPocket;

	for (int y = 0; y < kMapHeight; y++)
		for (int x = 0; x < kMapWidth; x++) {
			if (_itemByteFlag == 1) {
				if (((getItem(x, y) & 0xFF00) >> 8) == idToPocket)
					setItem(x, y, int16_t(getItem(x, y) & 0xFF));
			} else if ((getItem(x, y) & 0xFF) == idToPocket) {
				setItem(x, y, int16_t(getItem(x, y) & 0xFF00));
			}
		}

	if (idToPocket >= 0 && idToPocket < 20)
		itemPos(_itemIdInPocket) = {};
}

void GoblinsV1::placeItem(int16_t indexInPocket, int16_t idInPocket) {
	Object *item = obj(indexInPocket);
	Object *g0 = gob(0);
	if (!item || !g0)
		return;
	int16_t lookDir = g0->curLookDir & 4;
	int16_t xPos = _gobPositions[0].x, yPos = _gobPositions[0].y;

	_itemIndInPocket = -1;
	_itemIdInPocket = 0;

	item->pickable = 1;
	item->type = 0;
	item->toRedraw = 1;
	item->curFrame = 0;
	item->order = g0->order;
	item->animation = state0(*item).animation;
	int16_t layer = curState(*item).layer;
	Scenery &sc = _in._scenery;
	sc.updateAnim(layer, 0, item->animation, 0, item->xPos, item->yPos, false);

	item->yPos = int16_t(item->yPos + (_gobPositions[0].y * 6) + 5 - sc.toRedrawBottom);
	if (lookDir == 4)
		item->xPos = int16_t(item->xPos + (_gobPositions[0].x * 12 + 14) - (sc.toRedrawLeft + sc.toRedrawRight) / 2);
	else
		item->xPos = int16_t(item->xPos + (_gobPositions[0].x * 12) - (sc.toRedrawLeft + sc.toRedrawRight) / 2);

	placeMapItem(xPos, yPos, idInPocket);
	if (yPos > 0)
		placeMapItem(xPos, int16_t(yPos - 1), idInPocket);
	if (lookDir == 4) {
		if (xPos < kMapWidth - 1) {
			placeMapItem(int16_t(xPos + 1), yPos, idInPocket);
			if (yPos > 0)
				placeMapItem(int16_t(xPos + 1), int16_t(yPos - 1), idInPocket);
		}
	} else if (xPos > 0) {
		placeMapItem(int16_t(xPos - 1), yPos, idInPocket);
		if (yPos > 0)
			placeMapItem(int16_t(xPos - 1), int16_t(yPos - 1), idInPocket);
	}

	if (idInPocket >= 0 && idInPocket < 20) {
		ItemPos &p = itemPos(idInPocket);
		p.x = _gobPositions[0].x;
		p.y = _gobPositions[0].y;
		p.orient = int8_t(lookDir);
		if (p.orient == 0) {
			if (getPass(p.x + 1, p.y) == 1)
				p.x++;
		} else if (getPass(p.x - 1, p.y) == 1) {
			p.x--;
		}
	}
}

void GoblinsV1::swapItems(int16_t indexToPick, int16_t idToPick) {
	Object *pickObj = obj(indexToPick);
	Object *placeObj = obj(_itemIndInPocket);
	Object *g0 = gob(0);
	if (!pickObj || !placeObj || !g0)
		return;

	int16_t idToPlace = _itemIdInPocket;
	pickObj->type = 3;
	_itemIndInPocket = indexToPick;
	_itemIdInPocket = idToPick;

	for (int y = 0; y < kMapHeight; y++)
		for (int x = 0; x < kMapWidth; x++) {
			if (_itemByteFlag == 0) {
				if ((getItem(x, y) & 0xFF) == idToPick)
					setItem(x, y, int16_t((getItem(x, y) & 0xFF00) + idToPlace));
			} else if (((getItem(x, y) & 0xFF00) >> 8) == idToPick) {
				setItem(x, y, int16_t((getItem(x, y) & 0xFF) + (idToPlace << 8)));
			}
		}

	if (idToPick >= 0 && idToPick < 20) {
		itemPos(idToPlace) = itemPos(_itemIdInPocket);
		itemPos(_itemIdInPocket) = {};
	}

	_itemIndInPocket = -1;
	_itemIdInPocket = 0;

	placeObj->type = 0;
	placeObj->nextState = -1;
	placeObj->multState = -1;
	placeObj->unk14 = 0;
	placeObj->toRedraw = 1;
	placeObj->curFrame = 0;
	placeObj->order = g0->order;
	placeObj->animation = state0(*placeObj).animation;
	int16_t layer = curState(*placeObj).layer;
	Scenery &sc = _in._scenery;
	sc.updateAnim(layer, 0, placeObj->animation, 0, placeObj->xPos, placeObj->yPos, false);

	placeObj->yPos = int16_t(placeObj->yPos + (_gobPositions[0].y * 6) + 5 - sc.toRedrawBottom);
	if (itemPos(idToPlace).orient == 4)
		placeObj->xPos =
		    int16_t(placeObj->xPos + (_gobPositions[0].x * 12 + 14) - (sc.toRedrawLeft + sc.toRedrawRight) / 2);
	else
		placeObj->xPos = int16_t(placeObj->xPos + (_gobPositions[0].x * 12) - (sc.toRedrawLeft + sc.toRedrawRight) / 2);
}

// At frame 9 of goblin 0's pick animation: pick, place or swap.
void GoblinsV1::treatItemPick(int16_t itemId) {
	Object *g = gob(_currentGoblin);
	if (!g || g->curFrame != 9 || !g->ownStates())
		return;

	_readyToAct = 0;
	_goesAtTarget = 0;

	int16_t itemIndex = itemToObject(itemId);
	if (itemId != 0 && itemIndex != -1 && !(obj(itemIndex) && obj(itemIndex)->pickable == 1))
		itemIndex = -1;
	if (_itemIndInPocket != -1 && _itemIndInPocket == itemIndex)
		itemIndex = -1;

	if (_itemIndInPocket != -1 && itemIndex != -1 && obj(itemIndex) && obj(itemIndex)->pickable == 1) {
		swapItems(itemIndex, itemId);
		_itemIndInPocket = itemIndex;
		_itemIdInPocket = itemId;
		return;
	}
	if (_itemIndInPocket != -1 && itemIndex == -1) {
		placeItem(_itemIndInPocket, _itemIdInPocket);
		return;
	}
	if (_itemIndInPocket == -1 && itemIndex != -1) {
		pickItem(itemIndex, itemId);
		return;
	}
}

// goblin 1009: what the goblin is doing with its target, for the script:
// -1 to use the pocket item, the item reached, or 0.
int16_t GoblinsV1::treatItem(int16_t action) {
	Object *g = gob(_currentGoblin);
	if (!g)
		return 0;
	int16_t state = g->state;
	if ((state == 10 || state == 11) && g->curFrame == 0)
		_readyToAct = 0;

	Pos pos = _gobPositions[size_t(clip<int16_t>(_currentGoblin, 0, 2))];
	if (action == 3 && _currentGoblin == 0 && (state == 10 || state == 11) && gob(0)->curFrame == 0) {
		saveGobDataToVars(pos.x, pos.y, 0);
		_goesAtTarget = 1;
		return -1;
	}

	if (_noPick == 0 && _currentGoblin == 0 && (state == 10 || state == 11)) {
		treatItemPick(_destActionItem);
		saveGobDataToVars(pos.x, pos.y, 0);
		return 0;
	}

	if (_goesAtTarget == 0) {
		saveGobDataToVars(pos.x, pos.y, 0);
		return 0;
	}

	if (itemToObject(_destActionItem) != 100 && _destActionItem != 0) {
		if (itemToObject(_destActionItem) == -1)
			_actDestItemDesc = nullptr;
		else
			_actDestItemDesc = obj(itemToObject(_destActionItem));
	}
	_goesAtTarget = 0;
	saveGobDataToVars(pos.x, pos.y, 0);
	return _destActionItem;
}

// ---------------------------------------------------------------------------
// Movement (goblin_v1.cpp:152-722)

void GoblinsV1::initiateMove() {
	findNearestToDest();
	findNearestToGob();
	optimizePoints();

	_pathExistence = int8_t(checkDirectPath(_curGoblinX, _curGoblinY, _pressedMapX, _pressedMapY));
	if (_pathExistence == 3) {
		if (checkLongPath(_curGoblinX, _curGoblinY, _pressedMapX, _pressedMapY, _nearestWayPoint, _nearestDest) == 0) {
			_pathExistence = 0;
		} else {
			_destX = wayPoint(_nearestWayPoint).x;
			_destY = wayPoint(_nearestWayPoint).y;
		}
	}
}

void GoblinsV1::movePathFind(Object &o, int16_t nextAct) {
	const Pos cur = _gobPositions[size_t(clip<int16_t>(_currentGoblin, 0, 2))];

	if (_pathExistence == 1) {
		_curGoblinX = cur.x;
		_curGoblinY = cur.y;
		if (_curGoblinX == _pressedMapX && _curGoblinY == _pressedMapY && _gobAction != 0) {
			_readyToAct = 1;
			_pathExistence = 0;
		}
		nextAct = getDirection(_curGoblinX, _curGoblinY, _destX, _destY);
		if (nextAct == kDirNone)
			_pathExistence = 0;
	} else if (_pathExistence == 3) {
		_curGoblinX = cur.x;
		_curGoblinY = cur.y;
		if (_curGoblinX == _gobDestX && _curGoblinY == _gobDestY) {
			_pathExistence = 1;
			_destX = _pressedMapX;
			_destY = _pressedMapY;
		} else {
			if (checkDirectPath(_curGoblinX, _curGoblinY, _gobDestX, _gobDestY) == 1) {
				_destX = _gobDestX;
				_destY = _gobDestY;
			} else if (_curGoblinX == _destX && _curGoblinY == _destY) {
				if (_nearestWayPoint > _nearestDest) {
					optimizePoints();
					_destX = wayPoint(_nearestWayPoint).x;
					_destY = wayPoint(_nearestWayPoint).y;
					if (_nearestWayPoint > _nearestDest)
						_nearestWayPoint--;
				} else if (_nearestWayPoint < _nearestDest) {
					optimizePoints();
					_destX = wayPoint(_nearestWayPoint).x;
					_destY = wayPoint(_nearestWayPoint).y;
					if (_nearestWayPoint < _nearestDest)
						_nearestWayPoint++;
				} else {
					if (checkDirectPath(_curGoblinX, _curGoblinY, _gobDestX, _gobDestY) == 3 &&
					    getPass(_pressedMapX, _pressedMapY) != 0) {
						_destX = wayPoint(_nearestWayPoint).x;
						_destY = wayPoint(_nearestWayPoint).y;
					} else {
						_pathExistence = 1;
						_destX = _pressedMapX;
						_destY = _pressedMapY;
					}
				}
			}
			nextAct = getDirection(_curGoblinX, _curGoblinY, _destX, _destY);
		}
	}

	if (_readyToAct != 0 && (_gobAction == 3 || _gobAction == 4))
		nextAct = 0x4DC8;

	const int16_t x = _curGoblinX, y = _curGoblinY;
	const bool rope = _currentGoblin == 1; // only goblin 1 climbs ropes
	switch (nextAct) {
	case kDirW:
		o.nextState = rotateState(o.curLookDir, 0);
		break;
	case kDirE:
		o.nextState = rotateState(o.curLookDir, 4);
		break;
	case 16:
		o.nextState = 16;
		break;
	case 23:
		o.nextState = 23;
		break;
	case kDirN:
		if (getPass(x, y - 1) == 6 && !rope) {
			_pathExistence = 0;
			break;
		}
		if (getPass(x, y) == 3) {
			o.nextState = 8;
			break;
		}
		if (getPass(x, y) == 6 && rope) {
			o.nextState = 28;
			break;
		}
		o.nextState = rotateState(o.curLookDir, 2);
		break;
	case kDirS:
		if (getPass(x, y + 1) == 6 && !rope) {
			_pathExistence = 0;
			break;
		}
		if (getPass(x, y) == 3) {
			o.nextState = 9;
			break;
		}
		if (getPass(x, y) == 6 && rope) {
			o.nextState = 29;
			break;
		}
		o.nextState = rotateState(o.curLookDir, 6);
		break;
	case kDirSE:
		if (getPass(x + 1, y + 1) == 6 && !rope) {
			_pathExistence = 0;
			break;
		}
		o.nextState = 5;
		if (o.curLookDir == 4)
			break;
		o.nextState = rotateState(o.curLookDir, 4);
		break;
	case kDirSW:
		if (getPass(x - 1, y + 1) == 6 && !rope) {
			_pathExistence = 0;
			break;
		}
		o.nextState = 7;
		if (o.curLookDir == 0)
			break;
		o.nextState = rotateState(o.curLookDir, 0);
		break;
	case kDirNW:
		if (getPass(x - 1, y - 1) == 6 && !rope) {
			_pathExistence = 0;
			break;
		}
		o.nextState = 1;
		if (o.curLookDir == 0)
			break;
		o.nextState = rotateState(o.curLookDir, 0);
		break;
	case kDirNE:
		if (getPass(x + 1, y - 1) == 6 && !rope) {
			_pathExistence = 0;
			break;
		}
		o.nextState = 3;
		if (o.curLookDir == 4)
			break;
		o.nextState = rotateState(o.curLookDir, 4);
		break;
	case 0x4DC8: // arrived to act on the target
		if (_currentGoblin == 0 && _gobAction == 3 && _itemIndInPocket == -1) {
			_destItemId = -1;
			_readyToAct = 0;
			break;
		}
		if (_currentGoblin == 0 && _gobAction == 4 && _itemIndInPocket == -1 && _destActionItem == 0) {
			o.multState = 104;
			_destItemId = -1;
			_readyToAct = 0;
			break;
		}
		if (_currentGoblin == 0 && _gobAction == 4 && _itemIndInPocket == -1 && _destActionItem != 0 &&
		    itemToObject(_destActionItem) != -1 && obj(itemToObject(_destActionItem)) &&
		    obj(itemToObject(_destActionItem))->pickable == 0) {
			o.multState = 104;
			_destItemId = -1;
			_readyToAct = 0;
			break;
		}
		switch (itemPos(_destActionItem).orient) {
		case 0:
		case -4:
			o.nextState = 10;
			o.curLookDir = 0;
			_destItemId = -1;
			break;
		case -1:
		case 4:
			o.nextState = 11;
			o.curLookDir = 4;
			_destItemId = -1;
			break;
		default:
			break;
		}
		break;
	default:
		if (getPass(x, y) == 3 || (getPass(x, y) == 6 && rope)) {
			o.nextState = 20;
			break;
		}
		switch (o.curLookDir) {
		case 2:
		case 4:
			o.nextState = 18;
			break;
		case 6:
		case 0:
			o.nextState = 19;
			break;
		default:
			break;
		}
		break;
	}
}

void GoblinsV1::moveAdvance(Object &o, int16_t nextAct, int16_t framesCount) {
	movePathFind(o, nextAct);

	o.curFrame++;
	if (o.curFrame == 1)
		o.actionStartState = o.state;

	if (_goesAtTarget == 0 && o.ownStates()) {
		switch (o.state) {
		case 0: case 1: case 7: case 13: case 16: case 27:
			o.curLookDir = 0;
			break;
		case 3: case 4: case 5: case 12: case 23: case 26:
			o.curLookDir = 4;
			break;
		case 28:
			if (_currentGoblin == 1)
				o.curLookDir = 2;
			break;
		case 2: case 8: case 15: case 22: case 25:
			o.curLookDir = 2;
			break;
		case 29:
			if (_currentGoblin == 1)
				o.curLookDir = 6;
			break;
		case 6: case 9: case 14: case 17: case 24:
			o.curLookDir = 6;
			break;
		default:
			break;
		}
	}

	// Footsteps.
	if (o.state >= 0 && o.state < 10 && o.ownStates() && (o.curFrame == 3 || o.curFrame == 6))
		speaker(10 * random(3) + 50, 5);

	// Goblin 0's pick/drop sound.
	if (_currentGoblin == 0 && o.ownStates() && (o.state == 10 || o.state == 11) && o.curFrame == 9) {
		_in.blasterStop(0);
		if (soundLoaded(14))
			_in.blasterPlay(kSoundSlotBase + 14, 1, _itemIndInPocket != -1 ? 9000 : 5000, 0);
	}

	if (_boreCounter++ == 120) {
		_boreCounter = 0;
		for (int16_t i = 0; i < 3; i++)
			showBoredom(i);
	}

	Scenery &sc = _in._scenery;
	auto stepCell = [&](int16_t state) {
		Pos &p = _gobPositions[size_t(clip<int16_t>(_currentGoblin, 0, 2))];
		switch (state) {
		case 0: p.x--; break;
		case 2: case 8: p.y--; break;
		case 4: p.x++; break;
		case 6: case 9: p.y++; break;
		case 1: p.x--; p.y--; break;
		case 3: p.x++; p.y--; break;
		case 5: p.x++; p.y++; break;
		case 7: p.x--; p.y++; break;
		case 38: p.y++; break;
		default: break;
		}
		if (_currentGoblin == 1) {
			if (state == 28)
				_gobPositions[1].y--;
			if (state == 29)
				_gobPositions[1].y++;
		}
	};
	auto standOnCurrentCell = [&]() {
		sc.updateAnim(_gobStateLayer, 0, o.animation, 0, o.xPos, o.yPos, false);
		o.yPos = int16_t((_curGoblinY + 1) * 6 - (sc.toRedrawBottom - sc.pieceTop));
		o.xPos = int16_t(_curGoblinX * 12 - (sc.toRedrawLeft - sc.pieceLeft));
	};

	if (o.multState != -1 && o.curFrame == framesCount && o.state != o.multState) {
		o.nextState = o.multState;
		o.multState = -1;
		const Scenery::Layer *l = sc.layer(o.animation, _gobStateLayer);
		int16_t newX = int16_t((l ? l->animDeltaX : 0) + o.xPos);
		int16_t newY = int16_t((l ? l->animDeltaY : 0) + o.yPos);
		_gobStateLayer = nextLayer(o);
		o.xPos = newX;
		o.yPos = newY;
		return;
	}

	// Mid-cycle step of a walk (or of goblin 1 on a rope).
	if (o.curFrame == 3 && o.ownStates() &&
	    (o.state < 10 || (_currentGoblin == 1 && (o.state == 28 || o.state == 29)))) {
		bool changed = false;
		if (_forceNextState[0] != -1) {
			o.nextState = _forceNextState[0];
			for (int i = 0; i < 9; i++)
				_forceNextState[size_t(i)] = _forceNextState[size_t(i + 1)];
		}
		_curGoblinX = _gobPositions[size_t(clip<int16_t>(_currentGoblin, 0, 2))].x;
		_curGoblinY = _gobPositions[size_t(clip<int16_t>(_currentGoblin, 0, 2))].y;

		if (o.nextState != o.state) {
			_gobStateLayer = nextLayer(o);
			changed = true;
		}
		stepCell(o.state);
		if (changed)
			standOnCurrentCell();
		if ((o.state == 10 || o.state == 11) && _currentGoblin != 0)
			_goesAtTarget = 1;
	}

	if (o.curFrame != framesCount)
		return;

	// End of the cycle: the next state, and its step.
	if (_forceNextState[0] != -1) {
		o.nextState = _forceNextState[0];
		for (int i = 0; i < 9; i++)
			_forceNextState[size_t(i)] = _forceNextState[size_t(i + 1)];
	}
	_curGoblinX = _gobPositions[size_t(clip<int16_t>(_currentGoblin, 0, 2))].x;
	_curGoblinY = _gobPositions[size_t(clip<int16_t>(_currentGoblin, 0, 2))].y;

	_gobStateLayer = nextLayer(o);
	if (o.ownStates())
		stepCell(o.nextState);
	standOnCurrentCell();
	if ((o.state == 10 || o.state == 11) && _currentGoblin != 0)
		_goesAtTarget = 1;
}

// ---------------------------------------------------------------------------
// Opcode helpers (inter_v1.cpp)

void GoblinsV1::manipulateMap(int16_t xPos, int16_t yPos, int16_t item) {
	for (int y = 0; y < kMapHeight; y++)
		for (int x = 0; x < kMapWidth; x++) {
			if ((getItem(x, y) & 0xFF) == item)
				setItem(x, y, int16_t(getItem(x, y) & 0xFF00));
			else if (((getItem(x, y) & 0xFF00) >> 8) == item)
				setItem(x, y, int16_t(getItem(x, y) & 0xFF));
		}

	// The item covers (x, y) and the cells right of and above it, in the
	// low byte if a high-byte item is already there.
	std::vector<std::pair<int16_t, int16_t>> cells{{xPos, yPos}};
	if (yPos > 0)
		cells.push_back({xPos, int16_t(yPos - 1)});
	if (xPos < kMapWidth - 1) {
		cells.push_back({int16_t(xPos + 1), yPos});
		if (yPos > 0)
			cells.push_back({int16_t(xPos + 1), int16_t(yPos - 1)});
	}
	bool high = false;
	for (auto [x, y] : cells)
		high = high || (getItem(x, y) & 0xFF00) != 0;
	for (auto [x, y] : cells)
		setItem(x, y, high ? int16_t((getItem(x, y) & 0xFF00) + item) : int16_t((getItem(x, y) & 0xFF) + (item << 8)));

	if (item < 0 || item >= 20)
		return;
	ItemPos &p = itemPos(item);
	if (xPos > 1 && getPass(xPos - 2, yPos) == 1) {
		p = {int8_t(xPos - 2), int8_t(yPos), 4};
		return;
	}
	if (xPos < kMapWidth - 2 && getPass(xPos + 2, yPos) == 1) {
		p = {int8_t(xPos + 2), int8_t(yPos), 0};
		return;
	}
	if (xPos < kMapWidth - 1 && getPass(xPos + 1, yPos) == 1) {
		p = {int8_t(xPos + 1), int8_t(yPos), 0};
		return;
	}
	if (xPos > 0 && getPass(xPos - 1, yPos) == 1) {
		p = {int8_t(xPos - 1), int8_t(yPos), 4};
		return;
	}
}

// goblin 2005: makes goblin 0 current and, with an item in the pocket,
// plays its drop animation, putting the item down at frame 10.
void GoblinsV1::initGoblin() {
	Object *g0 = gob(0);
	if (!g0)
		return;
	Scenery &sc = _in._scenery;

	if (_currentGoblin != 0) {
		Object &cur = *gob(_currentGoblin);
		cur.doAnim = 1;
		cur.nextState = 21;
		nextLayer(cur);
		_currentGoblin = 0;

		g0->doAnim = 0;
		g0->type = 0;
		g0->toRedraw = 1;

		_pressedMapX = _destX = _gobDestX = _gobPositions[0].x;
		_pressedMapY = _destY = _gobDestY = _gobPositions[0].y;
		setVar(kCurGob, 0);
		_pathExistence = 0;
		_readyToAct = 0;
	}

	if (g0->state != 10 && _itemIndInPocket != -1 && getObjMaxFrame(*g0) == g0->curFrame) {
		g0->stateMach = &g0->realStateMach;
		int16_t xPos = _gobPositions[0].x, yPos = _gobPositions[0].y;
		g0->nextState = 10;
		int16_t layer = nextLayer(*g0);
		sc.updateAnim(layer, 0, g0->animation, 0, g0->xPos, g0->yPos, false);
		g0->yPos = int16_t((yPos * 6 + 6) - (sc.toRedrawBottom - sc.pieceTop));
		g0->xPos = int16_t(xPos * 12 - (sc.toRedrawLeft - sc.pieceLeft));
	}

	if (g0->state != 10 || _itemIndInPocket == -1 || g0->curFrame != 10)
		return;

	Object *item = obj(_itemIndInPocket);
	if (!item)
		return;
	item->type = 0;
	item->toRedraw = 1;
	item->curFrame = 0;
	item->order = g0->order;
	item->animation = state0(*item).animation;
	int16_t layer = state0(*item).layer;
	sc.updateAnim(layer, 0, item->animation, 0, item->xPos, item->yPos, false);
	item->yPos = int16_t(item->yPos + (_gobPositions[0].y * 6 + 5) - sc.toRedrawBottom);
	if (g0->curLookDir == 4)
		item->xPos = int16_t(item->xPos + _gobPositions[0].x * 12 + 14 - (sc.toRedrawLeft + sc.toRedrawRight) / 2);
	else
		item->xPos = int16_t(item->xPos + _gobPositions[0].x * 12 - (sc.toRedrawLeft + sc.toRedrawRight) / 2);

	_itemIndInPocket = -1;
	_itemIdInPocket = -1;
	speaker(50, 50); // Util::beep(50)
}

// ---------------------------------------------------------------------------
// The goblin function (o1_goblinFunc and the opcodes it dispatches to)

bool GoblinsV1::goblinFunc(int16_t cmd, int16_t paramCount) {
	(void)paramCount;
	Script &s = _in.script();
	auto rd = [&]() { return s.readInt16(); };
	auto varSlot = [&](int16_t i) { return int32_t(_in._vars->slot(uint32_t(uint16_t(i)))); };
	auto ret = [&](int32_t v) { _in._vars->write32(kGobRet, uint32_t(v)); };

	// Operand decoding: 1-16 object setters (object, value), 91-106 the
	// same on goblins, 21-37 object getters, 111-127 goblin getters.
	Object *od = nullptr;
	bool objDescSet = false;
	int16_t extra = 0;
	if (cmd > 0 && cmd < 17) {
		objDescSet = true;
		od = obj(rd());
		extra = rd();
	}
	if (cmd > 90 && cmd < 107) {
		objDescSet = true;
		od = gob(rd());
		extra = rd();
		cmd = int16_t(cmd - 90);
	}
	if (cmd > 110 && cmd < 128) {
		objDescSet = true;
		od = gob(rd());
		cmd = int16_t(cmd - 90);
	} else if (cmd > 20 && cmd < 38) {
		objDescSet = true;
		od = obj(rd());
	}
	if (cmd < 40 && objDescSet && !od)
		return true;

	// Setters also update the acted-on object's variable record.
	auto destVar = [&](uint32_t off, int16_t v) {
		if (od == _actDestItemDesc)
			setVar(off, v);
	};
	Scenery &sc = _in._scenery;

	switch (cmd) {
	case 1: od->state = extra; destVar(kDestItemState, extra); return true;
	case 2: od->curFrame = extra; destVar(kDestItemFrame, extra); return true;
	case 3: od->nextState = extra; destVar(kDestItemNextState, extra); return true;
	case 4: od->multState = extra; destVar(kDestItemMultState, extra); return true;
	case 5: od->order = int8_t(extra); destVar(kDestItemOrder, extra); return true;
	case 6: od->actionStartState = extra; destVar(kDestItemActStartSt, extra); return true;
	case 7: od->curLookDir = extra; destVar(kDestItemLookDir, extra); return true;
	case 8:
		od->type = int8_t(extra);
		destVar(kDestItemType, extra);
		if (extra == 0)
			od->toRedraw = 1;
		return true;
	case 9: od->noTick = int8_t(extra); destVar(kDestItemNoTick, extra); return true;
	case 10: od->pickable = extra; destVar(kDestItemPickable, extra); return true;
	case 12: od->xPos = extra; destVar(kDestItemScrX, extra); return true;
	case 13: od->yPos = extra; destVar(kDestItemScrY, extra); return true;
	case 14: od->doAnim = int8_t(extra); destVar(kDestItemDoAnim, extra); return true;
	case 15: od->relaxTime = extra; destVar(kDestItemRelax, extra); return true;
	case 16: od->maxTick = int8_t(extra); destVar(kDestItemMaxTick, extra); return true;

	case 21: ret(od->state); return true;
	case 22: ret(od->curFrame); return true;
	case 23: ret(od->nextState); return true;
	case 24: ret(od->multState); return true;
	case 25: ret(od->order); return true;
	case 26: ret(od->actionStartState); return true;
	case 27: ret(od->curLookDir); return true;
	case 28: ret(od->type); return true;
	case 29: ret(od->noTick); return true;
	case 30: ret(od->pickable); return true;
	case 32: ret(getObjMaxFrame(*od)); return true;
	case 33: ret(od->xPos); return true;
	case 34: ret(od->yPos); return true;
	case 35: ret(od->doAnim); return true;
	case 36: ret(od->relaxTime); return true;
	case 37: ret(od->maxTick); return true;

	case 40: { // manipulateMap x, y, item
		int16_t x = rd(), y = rd(), item = rd();
		manipulateMap(x, y, item);
		return true;
	}
	case 41: { // getItem x, y
		int16_t x = rd(), y = rd();
		int16_t it = getItem(x, y);
		ret((it & 0xFF00) != 0 ? (it & 0xFF00) >> 8 : it);
		return true;
	}
	case 42: { // manipulateMapIndirect: the operands are variables
		int16_t x = rd(), y = rd(), item = rd();
		manipulateMap(int16_t(varSlot(x)), int16_t(varSlot(y)), int16_t(varSlot(item)));
		return true;
	}
	case 43: { // getItemIndirect
		int16_t x = rd(), y = rd();
		int16_t it = getItem(int16_t(varSlot(x)), int16_t(varSlot(y)));
		ret((it & 0xFF00) != 0 ? (it & 0xFF00) >> 8 : it);
		return true;
	}
	case 44: { // setPassMap x, y, value
		int16_t x = rd(), y = rd(), v = rd();
		setPass(x, y, int8_t(v));
		return true;
	}

	case 50: // setGoblinPosH: goblin, x, y in half resolution
	case 250: { // setGoblinPos: goblin, x, y
		int16_t item = rd(), x = rd(), y = rd();
		Object *g = gob(item);
		if (!g || item > 2)
			return true;
		int scale = cmd == 50 ? 2 : 1;
		_gobPositions[size_t(item)].x = int8_t(x * scale);
		_gobPositions[size_t(item)].y = int8_t(y * scale);
		g->nextState = 21;
		nextLayer(*g);
		int16_t layer = state0(*g).layer;
		sc.updateAnim(layer, 0, g->animation, 0, g->xPos, g->yPos, false);
		int16_t cy = cmd == 50 ? _gobPositions[size_t(item)].y : y;
		int16_t cx = cmd == 50 ? _gobPositions[size_t(item)].x : x;
		g->yPos = int16_t((cy * 6 + 6) - (sc.toRedrawBottom - sc.pieceTop));
		g->xPos = int16_t(cx * 12 - (sc.toRedrawLeft - sc.pieceLeft));
		g->curFrame = 0;
		g->state = 21;
		if (_currentGoblin == item) {
			setVar(kCurGobScrX, g->xPos);
			setVar(kCurGobScrY, g->yPos);
			setVar(kCurGobFrame, 0);
			setVar(kCurGobState, 18);
			_pressedMapX = _gobPositions[size_t(item)].x;
			_pressedMapY = _gobPositions[size_t(item)].y;
		}
		return true;
	}
	case 52: { // getGoblinPosXH
		int16_t item = rd();
		ret(gobPosition(item).x >> 1);
		return true;
	}
	case 53: { // getGoblinPosYH
		int16_t item = rd();
		ret(gobPosition(item).y >> 1);
		return true;
	}

	case 150: { // setGoblinMultState: goblin, state, 0 / goblin, x, y
		int16_t item = rd(), x = rd(), y = rd();
		Object *g = gob(item);
		if (!g)
			return true;
		if (y == 0) {
			g->multState = x;
			g->nextState = x;
			nextLayer(*g);
			int16_t layer = state0(*g).layer;
			if (const Scenery::Layer *l = sc.layer(g->animation, layer)) {
				g->xPos = l->posX;
				g->yPos = l->posY;
			}
			setVar(kCurGobScrX, g->xPos);
			setVar(kCurGobScrY, g->yPos);
			setVar(kCurGobFrame, 0);
			setVar(kCurGobState, g->state);
			setVar(kCurGobNextState, g->nextState);
			setVar(kCurGobMultState, g->multState);
			setVar(kCurGobMaxFrame, getObjMaxFrame(*g));
			_noPick = 1;
			return true;
		}
		g->multState = 21;
		g->nextState = 21;
		g->state = 21;
		nextLayer(*g);
		int16_t layer = state0(*g).layer;
		sc.updateAnim(layer, 0, g->animation, 0, g->xPos, g->yPos, false);
		g->yPos = int16_t((y * 6 + 6) - (sc.toRedrawBottom - sc.pieceTop));
		g->xPos = int16_t(x * 12 - (sc.toRedrawLeft - sc.pieceLeft));
		if (item >= 0 && item < 3) {
			_gobPositions[size_t(item)].x = int8_t(x);
			_gobPositions[size_t(item)].y = int8_t(y);
		}
		_pressedMapX = _curGoblinX = x;
		_pressedMapY = _curGoblinY = y;
		setVar(kCurGobScrX, g->xPos);
		setVar(kCurGobScrY, g->yPos);
		setVar(kCurGobFrame, 0);
		setVar(kCurGobState, 21);
		setVar(kCurGobNextState, 21);
		setVar(kCurGobMultState, -1);
		_noPick = 0;
		return true;
	}
	case 152: { // setGoblinUnk14: object, value
		int16_t item = rd(), v = rd();
		if (Object *o = obj(item))
			o->unk14 = int8_t(v);
		return true;
	}

	case 200: _itemIdInPocket = rd(); return true;
	case 201: _itemIndInPocket = rd(); return true;
	case 202: ret(_itemIdInPocket); return true;
	case 203: ret(_itemIndInPocket); return true;
	case 204: // setItemPos: item, x, y, orient
	case 605: {
		int16_t item = rd(), x = rd(), y = rd(), v = rd();
		itemPos(item) = {int8_t(x), int8_t(y), int8_t(v)};
		return true;
	}

	case 251: { // setGoblinState: goblin, state (the layer's own position)
		int16_t item = rd(), state = rd();
		Object *g = gob(item);
		if (!g)
			return true;
		g->nextState = state;
		nextLayer(*g);
		int16_t layer = state0(*g).layer;
		if (const Scenery::Layer *l = sc.layer(g->animation, layer)) {
			g->xPos = l->posX;
			g->yPos = l->posY;
		}
		if (item == _currentGoblin) {
			setVar(kCurGobScrX, g->xPos);
			setVar(kCurGobScrY, g->yPos);
			setVar(kCurGobFrame, 0);
			setVar(kCurGobState, g->state);
			setVar(kCurGobMultState, g->multState);
		}
		return true;
	}
	case 252: { // setGoblinStateRedraw: object, state
		int16_t item = rd(), state = rd();
		Object *o = obj(item);
		if (!o)
			return true;
		o->nextState = state;
		nextLayer(*o);
		int16_t layer = state0(*o).layer;
		if (const Scenery::Layer *l = sc.layer(o->animation, layer)) {
			o->xPos = l->posX;
			o->yPos = l->posY;
		}
		o->toRedraw = 1;
		o->type = 0;
		if (o == _actDestItemDesc) {
			setVar(kDestItemScrX, o->xPos);
			setVar(kDestItemScrY, o->yPos);
			setVar(kDestItemState, o->state);
			setVar(kDestItemNextState, -1);
			setVar(kDestItemMultState, -1);
			setVar(kDestItemFrame, 0);
		}
		return true;
	}

	case 500: { // decRelaxTime: object
		Object *o = obj(rd());
		if (!o)
			return true;
		o->relaxTime--;
		if (o->relaxTime < 0 && getObjMaxFrame(*o) == o->curFrame) {
			o->relaxTime = int16_t(random(100) + 50);
			o->curFrame = 0;
			o->toRedraw = 1;
		}
		return true;
	}
	case 502: { int16_t item = rd(); ret(gobPosition(item).x); return true; }
	case 503: { int16_t item = rd(); ret(gobPosition(item).y); return true; }
	case 600: _pathExistence = 0; return true;
	case 601: // setGoblinVisible
	case 602: { // setGoblinInvisible
		if (Object *g = gob(rd()))
			g->visible = cmd == 601 ? 1 : 0;
		return true;
	}
	case 603: // getObjectIntersect: object, goblin
	case 604: { // getGoblinIntersect: goblin, goblin
		int16_t a = rd(), b = rd();
		const Object *first = cmd == 603 ? obj(a) : gob(a);
		ret(objIntersected(first, gob(b)) ? 1 : 0);
		return true;
	}

	case 1000: { // loadObjects: string variable with the .AVJ name
		int16_t v = rd();
		loadObjects(_in._vars->readString(uint32_t(uint16_t(v)) * 4));
		return true;
	}
	case 1001: freeAllObjects(); return true;
	case 1002: animateObjects(); return true;
	case 1003:
		drawObjects();
		playBgMusic();
		return true;
	case 1004: loadMapsInitGobs(); return true;
	case 1005: { // moveGoblin: action variable, "use the pocket item" variable
		int16_t actionVar = rd(), useVar = rd();
		Object *g = gob(_currentGoblin);
		if (!g)
			return true;
		int16_t item = uint16_t(varSlot(useVar)) == 0 ? doMove(*g, 1, int16_t(uint16_t(varSlot(actionVar))))
		                                              : doMove(*g, 1, 3);
		if (item != 0)
			switchGoblin(item);
		return true;
	}
	case 1006: switchGoblin(0); return true;
	case 1008: loadGobDataFromVars(); return true;
	case 1009: { // writeTreatItem: action variable, result variable, "use the pocket item" variable
		int16_t actionVar = rd(), resultVar = rd(), useVar = rd();
		int16_t r = uint16_t(varSlot(useVar)) == 0 ? treatItem(int16_t(uint16_t(varSlot(actionVar)))) : treatItem(3);
		_in._vars->setSlot(uint32_t(uint16_t(resultVar)), uint32_t(int32_t(r)));
		return true;
	}
	case 1010:
		if (Object *g = gob(_currentGoblin))
			doMove(*g, 0, 0);
		return true;
	case 1011: { // setGoblinTarget: variable
		int16_t v = rd();
		_goesAtTarget = varSlot(v) != 0 ? 1 : 0;
		return true;
	}
	case 1015: { // setGoblinObjectsPos: x variable, y variable (object 10)
		int16_t x = int16_t(varSlot(rd()));
		int16_t y = int16_t(varSlot(rd()));
		if (Object *o = obj(10)) {
			o->xPos = x;
			o->yPos = y;
		}
		return true;
	}
	case 2005: initGoblin(); return true;

	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Snapshots (quick save, engine/levels_v1.cpp)

namespace {
constexpr uint16_t kStateVersion = 1; // bump when the serialized fields change
} // namespace

int16_t GoblinsV1::objectRef(const Object *o) const {
	for (int i = 0; o && i < kGoblins; i++)
		if (_goblins[size_t(i)].get() == o)
			return int16_t(i);
	for (int i = 0; o && i < kObjects; i++)
		if (_objects[size_t(i)].get() == o)
			return int16_t(100 + i);
	return -1;
}

GoblinsV1::Object *GoblinsV1::objectFromRef(int16_t ref) {
	if (ref >= 0 && ref < kGoblins)
		return gob(ref);
	if (ref >= 100 && ref < 100 + kObjects)
		return obj(ref - 100);
	return nullptr;
}

int16_t GoblinsV1::machineRef(const StateMachine *m) const {
	for (int i = 0; m && i < kGoblins; i++)
		if (_goblins[size_t(i)] && &_goblins[size_t(i)]->realStateMach == m)
			return int16_t(i);
	for (int i = 0; m && i < kObjects; i++)
		if (_objects[size_t(i)] && &_objects[size_t(i)]->realStateMach == m)
			return int16_t(100 + i);
	return -1;
}

GoblinsV1::StateMachine *GoblinsV1::machineFromRef(int16_t ref) {
	Object *o = objectFromRef(ref);
	return o ? &o->realStateMach : nullptr;
}

template <class IO>
bool GoblinsV1::serialize(IO &io) {
	constexpr bool reading = IO::kReading;
	bool ok = true;
	// Reading: a value that must equal what the loaded level has.
	auto expect = [&](auto value, auto actual) {
		io(value);
		if (value != actual)
			ok = false;
	};

	expect(kStateVersion, kStateVersion);
	expect(int16_t(_gobsCount), int16_t(_gobsCount));
	expect(int16_t(_objCount), int16_t(_objCount));

	for (auto &v : _passMap)
		io(v);
	for (auto &v : _itemsMap)
		io(v);
	for (WayPoint &w : _wayPoints) {
		io(w.x);
		io(w.y);
		io(w.notWalkable);
	}
	for (ItemPos &p : _itemPoses) {
		io(p.x);
		io(p.y);
		io(p.orient);
	}
	io(_dummyItemPos.x);
	io(_dummyItemPos.y);
	io(_dummyItemPos.orient);
	io(_nearestWayPoint);
	io(_nearestDest);
	io(_curGoblinX);
	io(_curGoblinY);
	io(_destX);
	io(_destY);
	io(_loadFromAvo);

	auto object = [&](std::unique_ptr<Object> &p) {
		expect(uint8_t(p ? 1 : 0), uint8_t(p ? 1 : 0));
		if (!ok || !p)
			return;
		Object &o = *p;
		expect(uint16_t(o.realStateMach.size()), uint16_t(o.realStateMach.size()));
		if (!ok)
			return;
		io(o.animation);
		io(o.state);
		io(o.stateColumn);
		io(o.curFrame);
		io(o.xPos);
		io(o.yPos);
		io(o.dirtyLeft);
		io(o.dirtyTop);
		io(o.dirtyRight);
		io(o.dirtyBottom);
		io(o.left);
		io(o.top);
		io(o.right);
		io(o.bottom);
		io(o.nextState);
		io(o.multState);
		io(o.actionStartState);
		io(o.curLookDir);
		io(o.pickable);
		io(o.relaxTime);
		int16_t mach = machineRef(o.stateMach);
		io(mach);
		if constexpr (reading) {
			o.stateMach = machineFromRef(mach);
			if (!o.stateMach)
				ok = false;
		}
		io(o.doAnim);
		io(o.order);
		io(o.noTick);
		io(o.toRedraw);
		io(o.type);
		io(o.maxTick);
		io(o.tick);
		io(o.multObjIndex);
		io(o.unk14);
		io(o.visible);
	};
	for (auto &g : _goblins)
		object(g);
	for (auto &o : _objects)
		object(o);
	if (!ok)
		return false;

	uint16_t listSize = uint16_t(_objList.size());
	io(listSize);
	if constexpr (reading) {
		if (listSize > kGoblins + kObjects)
			return false;
		_objList.clear();
	}
	for (uint16_t i = 0; i < listSize; i++) {
		int16_t ref = reading ? int16_t(-1) : objectRef(_objList[i]);
		io(ref);
		if constexpr (reading) {
			Object *o = objectFromRef(ref);
			if (!o)
				return false;
			_objList.push_back(o);
		}
	}
	io(_hasList);

	io(_currentGoblin);
	io(_gobStateLayer);
	io(_goesAtTarget);
	io(_readyToAct);
	io(_gobAction);
	for (Pos &p : _gobPositions) {
		io(p.x);
		io(p.y);
	}
	io(_gobDestX);
	io(_gobDestY);
	io(_pressedMapX);
	io(_pressedMapY);
	io(_pathExistence);
	io(_destItemType);
	io(_destItemState);
	for (auto &v : _itemToObject)
		io(v);
	io(_itemIndInPocket);
	io(_itemIdInPocket);
	io(_itemByteFlag);
	io(_destItemId);
	io(_destActionItem);
	int16_t acted = objectRef(_actDestItemDesc);
	io(acted);
	if constexpr (reading)
		_actDestItemDesc = objectFromRef(acted);
	for (auto &v : _forceNextState)
		io(v);
	io(_boreCounter);
	io(_positionedGob);
	io(_noPick);
	io(_varsBound);
	return ok;
}

std::vector<uint8_t> GoblinsV1::saveState() const {
	snap::Writer w;
	const_cast<GoblinsV1 *>(this)->serialize(w); // writing leaves the state as it is
	return w.take();
}

bool GoblinsV1::loadState(const std::vector<uint8_t> &data) {
	if (!loaded())
		return false;
	const std::vector<uint8_t> backup = saveState();
	snap::Reader r(data);
	if (serialize(r) && r.ok() && r.atEnd())
		return true;
	snap::Reader undo(backup);
	serialize(undo);
	return false;
}

void GoblinsV1::redrawAll() {
	Scenery &sc = _in._scenery;
	Draw &draw = _in._draw;
	if (Surface *anim = draw.sprite(draw::kAnimSurface).get()) {
		const int l = _in._multLeft, t = _in._multTop;
		const int r = l + _in._multWidth - 1, b = t + _in._multHeight - 1;
		draw.back().blit(*anim, l, t, r, b, l, t);
		draw.invalidateRect(l, t, r, b);
	}

	// Drawing order as drawObjects would sort, without changing the list.
	const std::vector<Object *> list = _objList;
	sortByOrder();
	for (Object *o : _objList) {
		if (o->type != 0 && o->type != 3) // hidden; type 3 is still on screen until the next draw
			continue;
		const int16_t layer = curState(*o).layer;
		if (o->visible == 0)
			sc.updateAnim(layer, o->curFrame, o->animation, 0, o->xPos, o->yPos, false);
		else
			sc.updateAnim(layer, o->curFrame, o->animation, 2, o->xPos, o->yPos, true);
		if (sc.toRedrawLeft == -12345)
			continue;
		o->left = sc.toRedrawLeft;
		o->top = sc.toRedrawTop;
		o->right = sc.toRedrawRight;
		o->bottom = sc.toRedrawBottom;
		if (o->visible == 0)
			continue;
		draw.invalidateRect(o->left, o->top, o->right, o->bottom);
		sc.updateStatic(o->order);
	}
	_objList = list;
}

} // namespace gob
