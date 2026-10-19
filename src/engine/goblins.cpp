// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Goblins and the level walk map (docs/goblins.md sections 1-2), plus the
// PC-speaker clicks the scripts use for footsteps.

#include "engine/interpreter.h"

#include <cmath>
#include <cstdlib>

namespace gob {

namespace {

int16_t le16s(const std::vector<uint8_t> &d, size_t o) {
	return o + 1 < d.size() ? int16_t(d[o] | (d[o + 1] << 8)) : 0;
}

// Turn/walk state from facing `from` to facing `to` (W, N, E, S rows/cols).
constexpr int kRotStates[4][4] = {
	{0, 18, 19, 20},
	{13, 2, 12, 14},
	{16, 15, 4, 17},
	{23, 21, 22, 6},
};

int rotateState(int from, int to) {
	if (from < 0 || from > 6 || to < 0 || to > 6)
		return 0;
	return kRotStates[from / 2][to / 2];
}

} // namespace

// Walking states: a cell move happens half-way through and at the end of
// their cycle. Version 3 adds the stairs (40-43) and the 19-ladder (26/27).
bool Interpreter::isMovement(int state) const {
	if ((state >= 0 && state <= 7) || state == 38 || state == 39)
		return true;
	return _ver >= 3 && ((state >= 40 && state <= 43) || state == 26 || state == 27);
}

// ---------------------------------------------------------------------------
// Map

int Interpreter::getPass(int x, int y) const {
	if (_passMap < 0 || x < 0 || x >= _mapW || y < 0 || y >= _mapH)
		return 0;
	return int8_t(_vars->read8(uint32_t(_passMap + y * _passStride + x)));
}

void Interpreter::setPass(int x, int y, int v) {
	if (_passMap < 0 || x < 0 || x >= _mapW || y < 0 || y >= _mapH)
		return;
	_vars->write8(uint32_t(_passMap + y * _passStride + x), uint8_t(v));
}

Interpreter::Direction Interpreter::getDirection(int x0, int y0, int x1, int y1) const {
	if (x0 == x1 && y0 == y1)
		return kDirNone;
	if (x1 < 0 || x1 > _mapW || y1 < 0 || y1 > _mapH)
		return kDirNone;

	bool up = y1 < y0, down = y1 > y0, left = x1 < x0, right = x1 > x0;
	auto walk = [&](Direction d) {
		int x = x0, y = y0;
		moveDirection(d, x, y);
		return getPass(x, y) != 0;
	};
	auto first = [&](std::initializer_list<Direction> c) {
		for (Direction d : c)
			if (walk(d))
				return d;
		return kDirNone;
	};

	if (left && !up && !down) return first({kDirW});
	if (right && !up && !down) return first({kDirE});
	if (up && !left && !right) return first({kDirN, kDirNW, kDirNE});
	if (down && !left && !right) return first({kDirS, kDirSW, kDirSE});
	if (right && up) return first({kDirNE, kDirN, kDirE});
	if (right && down) return first({kDirSE, kDirS, kDirE});
	if (left && up) return first({kDirNW, kDirN, kDirW});
	return first({kDirSW, kDirS, kDirW});
}

void Interpreter::moveDirection(Direction d, int &x, int &y) {
	switch (d) {
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

// Greedy walk: 1 if (x1, y1) is reachable step by step, 3 if it gets stuck.
int Interpreter::checkDirectPath(int x0, int y0, int x1, int y1) const {
	for (int guard = 0; guard < 10000; guard++) {
		if (x0 == x1 && y0 == y1)
			return 1;
		Direction d = getDirection(x0, y0, x1, y1);
		if (d == kDirNone)
			return 3;
		moveDirection(d, x0, y0);
	}
	return 3;
}

int Interpreter::findNearestWayPoint(int x, int y) const {
	int best = -1, length = 30000;
	for (size_t i = 0; i < _wayPoints.size(); i++) {
		const WayPoint &w = _wayPoints[i];
		if (w.x < 0 || w.x >= _mapW || w.y < 0 || w.y >= _mapH)
			break;
		int d = std::abs(x - w.x) + std::abs(y - w.y);
		if (d <= length) {
			length = d;
			best = int(i);
		}
	}
	return best;
}

void Interpreter::optimizePoints(int obj, int x, int y) {
	if (_wayPoints.empty())
		return;
	MultObject &o = _objects[size_t(obj)];
	if (o.nearestWayPoint < o.nearestDest) {
		for (int i = o.nearestWayPoint; i <= o.nearestDest && i < int(_wayPoints.size()); i++)
			if (checkDirectPath(x, y, _wayPoints[size_t(i)].x, _wayPoints[size_t(i)].y) == 1)
				o.nearestWayPoint = uint8_t(i);
	} else {
		for (int i = o.nearestWayPoint; i >= o.nearestDest && i < int(_wayPoints.size()); i--) {
			if (_wayPoints[size_t(i)].notWalkable == 1)
				break;
			if (checkDirectPath(x, y, _wayPoints[size_t(i)].x, _wayPoints[size_t(i)].y) == 1)
				o.nearestWayPoint = uint8_t(i);
		}
	}
}

void Interpreter::findNearestWalkable(int &cx, int &cy, int mouseX, int mouseY) const {
	const int mapW = _mapScreenW / _tilesW, mapH = 200 / _tilesH;
	int distance = 0, best = -1, dir = 0, i;

	for (i = 1; i <= cx; i++)
		if (getPass(cx - i, cy) != 0)
			break;
	if (i <= cx) {
		best = (i - 1) * _tilesW + mouseX % _tilesW + 1;
		dir = 1;
	}
	distance = i;

	for (i = 1; cx + i < mapW; i++)
		if (getPass(cx + i, cy) != 0)
			break;
	if (cx + i < mapW) {
		int cand = i * _tilesW - mouseX % _tilesW;
		if (best == -1 || cand < best) {
			best = cand;
			dir = 2;
			distance = i;
		}
	}

	for (i = 1; cy + i < mapH; i++)
		if (getPass(cx, cy + i) != 0)
			break;
	if (cy + i < mapH) {
		int cand = i * _tilesH - mouseY % _tilesH;
		if (best == -1 || cand < best) {
			best = cand;
			dir = 3;
			distance = i;
		}
	}

	for (i = 1; i <= cy; i++)
		if (getPass(cx, cy - i) != 0)
			break;
	if (i <= cy) {
		int cand = (i - 1) * _tilesH + mouseY % _tilesH + 1;
		if (best == -1 || cand < best) {
			best = cand;
			dir = 4;
			distance = i;
		}
	}

	switch (dir) {
	case 1: cx -= distance; break;
	case 2: cx += distance; break;
	case 3: cy += distance; break;
	case 4: cy -= distance; break;
	default: cx = -1; break;
	}
}

// Map resource (docs/goblins.md 1.3): header, packed pass map, way points,
// editor names, goblin-state blocks; the script then lists goblin sounds.
void Interpreter::loadMapObjects() {
	Script &s = script();
	VarRef var = readVar();
	int16_t id = s.readInt16();
	if (uint16_t(id) >= 65520) {
		trace("loadMapObjects special command " + std::to_string(uint16_t(id)) + " (unused by Gobliins 2)");
		return;
	}
	auto res = env().tot->resource(uint16_t(id));
	if (!res) {
		trace("map resource " + std::to_string(id) + " missing");
		return;
	}
	const std::vector<uint8_t> &d = res->data;
	size_t rp = 0;
	auto u8 = [&]() -> uint8_t { return rp < d.size() ? d[rp++] : 0; };
	auto i16 = [&]() { int16_t v = le16s(d, rp); rp += 2; return v; };

	uint8_t version = u8();
	int wpCount = u8();
	_tilesW = std::max<int16_t>(1, i16());
	_tilesH = std::max(1, i16() & 0xFF);
	// Version 3 (Goblins 3): the map covers 640 pixels, its pass map rows
	// are 65 bytes apart in variable space.
	_mapScreenW = version == 3 ? 640 : 320;
	_passStride = version == 3 ? 65 : kPassStride;
	_mapW = _mapScreenW / _tilesW;
	_mapH = 200 / _tilesH;

	size_t passStart = rp;
	rp += size_t(_mapW * _mapH);
	if (version == 1)
		wpCount = 40;
	_wayPoints.clear();
	for (int i = 0; i < wpCount; i++) {
		WayPoint w;
		w.x = int8_t(u8());
		w.y = int8_t(u8());
		w.notWalkable = int8_t(u8());
		_wayPoints.push_back(w);
	}

	// The pass map is spread into variable space with a 40-byte row stride.
	// A variable offset of 0 means "keep the current pass map" (re-entry).
	if (var.offset != 0) {
		_passMap = var.offset;
		for (int y = 0; y < _mapH; y++)
			for (int x = 0; x < _mapW; x++)
				setPass(x, y, int8_t(passStart + size_t(y * _mapW + x) < d.size() ? d[passStart + size_t(y * _mapW + x)] : 0));
	}

	int16_t n = i16();
	rp += size_t(std::max<int16_t>(n, 0)) * 14;
	n = i16();
	rp += size_t(std::max<int16_t>(n, 0)) * 14 + 28;

	int gobCount = i16();
	rp += size_t(std::max(gobCount, 0)) * 14;
	for (int g = 0; g < gobCount; g++) {
		int8_t indices[100];
		for (int8_t &idx : indices)
			idx = int8_t(u8());
		std::vector<std::optional<MultObject::GobState>> states(101);
		for (int8_t idx : indices) {
			if (idx == -1)
				continue;
			MultObject::GobState st;
			st.animation = i16();
			st.layer = i16();
			int count = u8();
			for (int k = 0; k < count; k++) {
				MultObject::GobSound snd;
				u8(); // record index
				snd.sndItem = int8_t(u8());
				u8();
				snd.sndFrame = u8();
				u8();
				snd.freq = i16();
				snd.repCount = int8_t(u8());
				snd.speaker = u8();
				st.sounds.push_back(snd);
			}
			if (idx >= 0 && idx <= 100)
				states[size_t(idx)] = std::move(st);
		}
		if (g < int(_objects.size())) {
			_objects[size_t(g)].goblinStates = std::move(states);
			_objects[size_t(g)].isGoblin = true;
		}
	}
	_gobsCount = gobCount;

	int16_t soundCount = s.readInt16();
	_gobSoundSlotsCount = 0;
	for (int i = 0; i < soundCount; i++) {
		int slot = loadSoundSearch();
		if (i < kSoundSlots) {
			_gobSoundSlots[size_t(i)] = slot;
			_gobSoundSlotsCount = i + 1;
		}
	}
}

void Interpreter::freeGoblins() {
	if (_gobsCount >= 0) {
		for (int i = 0; i < _gobsCount && i < int(_objects.size()); i++) {
			_objects[size_t(i)].goblinStates.clear();
			_objects[size_t(i)].isGoblin = false;
		}
		for (int i = 0; i < _gobSoundSlotsCount; i++)
			if (!(_gobSoundSlots[size_t(i)] & 0x8000))
				freeSoundSlot(_gobSoundSlots[size_t(i)]);
	}
	_gobSoundSlotsCount = 0;
	_gobsCount = -1;
}

// ---------------------------------------------------------------------------
// Goblin helpers

const Interpreter::MultObject::GobState *Interpreter::gobState(int obj, int state) const {
	if (obj < 0 || obj >= int(_objects.size()))
		return nullptr;
	const auto &states = _objects[size_t(obj)].goblinStates;
	if (state < 0 || state >= int(states.size()) || !states[size_t(state)])
		return nullptr;
	return &*states[size_t(state)];
}

// Height used to stand a sprite on its cell: the extent of the first piece
// of frame 0, as the original measures it.
int Interpreter::measureHeight(int animation, int layer, int x, int y) {
	_scenery.updateAnim(layer, 0, animation, 0, x, y, false);
	return _scenery.pieceBottom - _scenery.pieceTop;
}

void Interpreter::anchorToCell(int obj, int gx, int gy) {
	int h = measureHeight(animField(obj, kAnimation), animField(obj, kLayer), objPosX(obj), objPosY(obj));
	setObjPos(obj, gx * _tilesW, (gy + 1) * _tilesH - h);
}

void Interpreter::goblinSetState(int obj, int state) {
	const MultObject::GobState *st = gobState(obj, state);
	if (!st)
		return;
	setAnimField(obj, kLayer, st->layer);
	setAnimField(obj, kAnimation, st->animation);
	setAnimField(obj, kState, state);
	setAnimField(obj, kFrame, 0);
	setAnimField(obj, kIsPaused, 0);
	setAnimField(obj, kIsStatic, 0);
	if (const Scenery::Layer *l = _scenery.layer(st->animation, st->layer))
		setAnimField(obj, kNewCycle, l->framesCount);
	const MultObject &o = _objects[size_t(obj)];
	anchorToCell(obj, o.goblinX, o.goblinY);
}

void Interpreter::goblinPlaySounds(int obj) {
	const MultObject::GobState *st = gobState(obj, animField(obj, kState));
	if (!st)
		return;
	int frame = animField(obj, kFrame);
	for (const auto &snd : st->sounds) {
		if ((snd.sndItem == -1 && !snd.speaker) || frame != snd.sndFrame)
			continue;
		if (snd.speaker) {
			speakerOn(snd.freq);
			continue;
		}
		blasterStop(0);
		if (snd.sndItem >= 0 && snd.sndItem < _gobSoundSlotsCount)
			blasterPlay(_gobSoundSlots[size_t(snd.sndItem)] & 0x7FFF, snd.repCount, snd.freq, 0);
	}
}

// ---------------------------------------------------------------------------
// Movement

void Interpreter::goblinMove(int destX, int destY, int obj) {
	if (obj < 0 || obj >= int(_objects.size()))
		return;
	MultObject &o = _objects[size_t(obj)];
	o.gobDestX = int8_t(destX);
	o.gobDestY = int8_t(destY);
	setAnimField(obj, kDestX, destX);
	setAnimField(obj, kDestY, destY);

	if (animField(obj, kIsBusy) != 0 && destX == -1 && destY == -1) {
		int cx = _interMouseX / _tilesW, cy = _interMouseY / _tilesH;
		if (getPass(cx, cy) == 0)
			findNearestWalkable(cx, cy, _interMouseX, _interMouseY);
		if (cx == -1) {
			o.gobDestX = int8_t(o.goblinX);
			o.gobDestY = int8_t(o.goblinY);
		} else {
			o.gobDestX = int8_t(cx);
			o.gobDestY = int8_t(cy);
		}
		setAnimField(obj, kDestX, o.gobDestX);
		setAnimField(obj, kDestY, o.gobDestY);
	}

	writeVar(56, 0);

	int p = getPass(o.gobDestX, o.gobDestY);
	// Wide stairs (map version 3): don't keep walking on them.
	if (p == 11 && _mapScreenW == 640) {
		o.gobDestY++;
		setAnimField(obj, kDestY, o.gobDestY);
	}
	// Don't stop in the middle of a tall ladder: snap to its nearer end.
	if (p == 19 || p == 20) {
		int down = 0, up = 0;
		while (getPass(o.gobDestX, o.gobDestY + down) == p)
			down++;
		while (getPass(o.gobDestX, o.gobDestY - up) == p)
			up++;
		o.gobDestY = int8_t(up <= down ? o.gobDestY - up : o.gobDestY + down);
		setAnimField(obj, kDestY, o.gobDestY);
	}

	initiateMove(obj);
}

void Interpreter::initiateMove(int obj) {
	MultObject &o = _objects[size_t(obj)];
	o.destX = uint8_t(o.gobDestX);
	o.destY = uint8_t(o.gobDestY);

	int nd = findNearestWayPoint(o.destX, o.destY);
	if (nd != -1)
		o.nearestDest = uint8_t(nd);
	int ng = findNearestWayPoint(o.goblinX, o.goblinY);
	if (ng != -1)
		o.nearestWayPoint = uint8_t(ng);

	optimizePoints(obj, o.goblinX, o.goblinY);
	int path = checkDirectPath(o.goblinX, o.goblinY, o.gobDestX, o.gobDestY);
	setAnimField(obj, kPathExistence, path);
	if (path == 3 && o.nearestWayPoint < _wayPoints.size()) {
		o.destX = uint8_t(_wayPoints[o.nearestWayPoint].x);
		o.destY = uint8_t(_wayPoints[o.nearestWayPoint].y);
	}
}

void Interpreter::movePathFind(int obj) {
	MultObject &o = _objects[size_t(obj)];
	const Scenery::Layer *l = _scenery.layer(animField(obj, kAnimation), animField(obj, kLayer));
	setAnimField(obj, kNewCycle, l ? l->framesCount : 0);
	setAnimField(obj, kDestX, o.gobDestX);
	setAnimField(obj, kDestY, o.gobDestY);
	setAnimField(obj, kOrder, o.goblinY);

	int gx = o.goblinX, gy = o.goblinY;
	int dx = o.destX, dy = o.destY;
	int fx = o.gobDestX, fy = o.gobDestY;
	Direction dir = kDirNone;
	int path = animField(obj, kPathExistence);

	if (path == 1) {
		dir = getDirection(gx, gy, dx, dy);
		if (dir == kDirNone)
			path = 0;
		if (gx == fx && gy == fy)
			path = 4;
	} else if (path == 3) {
		if (gx == fx && gy == fy) {
			path = 4;
			dx = fx;
			dy = fy;
			dir = kDirNone;
		} else {
			if (checkDirectPath(gx, gy, fx, fy) == 1) {
				dx = fx;
				dy = fy;
			} else if (gx == dx && gy == dy) {
				auto wp = [&](int i, int &x, int &y) {
					if (i >= 0 && i < int(_wayPoints.size())) {
						x = _wayPoints[size_t(i)].x;
						y = _wayPoints[size_t(i)].y;
					}
				};
				if (o.nearestWayPoint > o.nearestDest) {
					optimizePoints(obj, gx, gy);
					wp(o.nearestWayPoint, dx, dy);
					if (checkDirectPath(gx, gy, dx, dy) == 3) {
						writeVar(56, 1);
						path = 0;
					}
					if (o.nearestWayPoint > o.nearestDest)
						o.nearestWayPoint--;
				} else if (o.nearestWayPoint < o.nearestDest) {
					optimizePoints(obj, gx, gy);
					wp(o.nearestWayPoint, dx, dy);
					if (checkDirectPath(gx, gy, dx, dy) == 3) {
						writeVar(56, 1);
						path = 0;
					}
					if (o.nearestWayPoint < o.nearestDest)
						o.nearestWayPoint++;
				} else {
					if (checkDirectPath(gx, gy, fx, fy) == 3 && getPass(fx, fy) != 0) {
						wp(o.nearestWayPoint, dx, dy);
						writeVar(56, 1);
					} else {
						path = 1;
						dx = fx;
						dy = fy;
					}
				}
			}
			dir = getDirection(gx, gy, dx, dy);
		}
	}

	setAnimField(obj, kPathExistence, path);
	o.destX = uint8_t(dx);
	o.destY = uint8_t(dy);

	int look = animField(obj, kCurLookDir);
	int next = animField(obj, kNextState);
	// Map version 3 (640 wide): stairs (pass 10, two cells up or down per
	// step: states 40-43), ladders (pass 20: 38/39) and pass 19 (26/27).
	const bool wide = _mapScreenW == 640;
	const int here = getPass(gx, gy);
	auto stairs = [&](int ddx, int ddy) { return getPass(gx + ddx, gy + ddy) == 10; };
	switch (dir) {
	case kDirNW:
		next = 1;
		if (wide && here == 10 && stairs(-1, -2))
			next = 40;
		break;
	case kDirN:
		next = look == 2 ? 2 : rotateState(look, 2);
		if (wide) {
			if (here == 10)
				next = stairs(-1, -2) ? 40 : stairs(1, -2) ? 42 : 2;
			if (here == 20)
				next = 38;
			if (here == 19)
				next = 26;
		}
		break;
	case kDirNE:
		next = 3;
		if (wide && here == 10 && stairs(1, -2))
			next = 42;
		break;
	case kDirW: next = rotateState(look, 0); break;
	case kDirE: next = rotateState(look, 4); break;
	case kDirSW:
		next = 7;
		if (wide && here == 10 && stairs(-1, 2))
			next = 41;
		break;
	case kDirS:
		next = look == 6 ? 6 : rotateState(look, 6);
		if (wide) {
			if (here == 20)
				next = 39;
			if (here == 19)
				next = 27;
		}
		break;
	case kDirSE:
		next = 5;
		if (wide && here == 10 && stairs(1, 2))
			next = 43;
		break;
	default:
		if (look == 0) next = 8;
		else if (look == 2) next = 29;
		else if (look == 4) next = 9;
		else if (look == 6) next = 28;
		break;
	}
	setAnimField(obj, kNextState, next);
}

void Interpreter::advMovement(int state, uint8_t &x, uint8_t &y) const {
	switch (state) {
	case 0: x--; break;
	case 1: x--; y--; break;
	case 2: case 38: y--; break;
	case 3: x++; y--; break;
	case 4: x++; break;
	case 5: x++; y++; break;
	case 6: case 39: y++; break;
	case 7: x--; y++; break;
	default:
		if (_ver < 3)
			break;
		switch (state) { // Goblin_v3::advMovement: the 19-ladder and two-cell stairs
		case 26: y--; break;
		case 27: y++; break;
		case 40: x--; y -= 2; break;
		case 41: x--; y += 2; break;
		case 42: x++; y -= 2; break;
		case 43: x++; y += 2; break;
		default: break;
		}
		break;
	}
}

// Per-tick goblin step (docs/goblins.md 2.6): path finding, sounds, frame
// advance, facing, and the two cell moves per walk cycle.
void Interpreter::goblinMoveAdvance(int obj) {
	MultObject &o = _objects[size_t(obj)];
	if (o.goblinStates.empty())
		return;

	movePathFind(obj);
	goblinPlaySounds(obj);

	const Scenery::Layer *l = _scenery.layer(animField(obj, kAnimation), animField(obj, kLayer));
	int fc = l ? l->framesCount : 0;
	if (!animField(obj, kIsPaused))
		setAnimField(obj, kFrame, animField(obj, kFrame) + 1);
	int frame = animField(obj, kFrame);

	switch (animField(obj, kStateType)) {
	case 0: case 1: setAnimField(obj, kIsPaused, 0); break;
	case 4: if (frame == 0) setAnimField(obj, kIsPaused, 1); break;
	case 6: if (frame >= fc) setAnimField(obj, kIsPaused, 1); break;
	default: break;
	}

	int state = animField(obj, kState);
	switch (state) {
	case 0: case 1: case 7: case 13: case 16: case 23: case 40: case 41:
		setAnimField(obj, kCurLookDir, 0); break;
	case 2: case 15: case 18: case 21: case 26: case 38:
		setAnimField(obj, kCurLookDir, 2); break;
	case 3: case 4: case 5: case 12: case 19: case 22: case 42: case 43:
		setAnimField(obj, kCurLookDir, 4); break;
	case 6: case 14: case 17: case 20: case 27: case 39:
		setAnimField(obj, kCurLookDir, 6); break;
	case 8: case 9: case 28: case 29:
		if (animField(obj, kPathExistence) == 4)
			setAnimField(obj, kPathExistence, 5);
		break;
	default:
		break;
	}

	// Version 3: a state queued in newState takes over at the end of the
	// cycle, moved by the layer's delta (Goblin_v2::moveAdvance).
	if (_ver >= 3) {
		int ns = animField(obj, kNewState);
		if (ns != -1 && frame == fc && ns != state) {
			const MultObject::GobState *nst = gobState(obj, ns);
			setAnimField(obj, kNextState, ns);
			setAnimField(obj, kNewState, -1);
			setAnimField(obj, kState, ns);
			if (l)
				setObjPos(obj, objPosX(obj) + l->animDeltaX, objPosY(obj) + l->animDeltaY);
			if (nst) {
				setAnimField(obj, kLayer, nst->layer);
				setAnimField(obj, kAnimation, nst->animation);
			}
			setAnimField(obj, kFrame, 0);
			return;
		}
	}

	auto switchTo = [&](int s, int gx, int gy) {
		const MultObject::GobState *st = gobState(obj, s);
		if (!st)
			return;
		setAnimField(obj, kLayer, st->layer);
		setAnimField(obj, kAnimation, st->animation);
		setAnimField(obj, kFrame, 0);
		setAnimField(obj, kState, s);
		int h = measureHeight(st->animation, st->layer, objPosX(obj), objPosY(obj));
		setObjPos(obj, gx * _tilesW, gy * _tilesH - h);
	};

	// Half-step in the middle of a walk cycle.
	if (isMovement(state) && frame == (fc + 1) / 2) {
		int s = animField(obj, kNextState);
		int gx = o.goblinX, gy = o.goblinY + 1;
		advMovement(s, o.goblinX, o.goblinY);
		if (state != s)
			switchTo(s, gx, gy);
	}

	if (frame < fc)
		return;

	// End of cycle: switch to the next state and step again.
	int s = animField(obj, kNextState);
	int gx = o.goblinX, gy = o.goblinY + 1;
	advMovement(s, o.goblinX, o.goblinY);
	const MultObject::GobState *st = gobState(obj, s);
	if (st) {
		switchTo(s, gx, gy);
	} else {
		setAnimField(obj, kFrame, 0);
		int h = measureHeight(animField(obj, kAnimation), animField(obj, kLayer), objPosX(obj), objPosY(obj));
		setObjPos(obj, gx * _tilesW, gy * _tilesH - h);
	}
}

// animType 101: state animation that loops, holding the last frame for
// maxFrame ticks between loops.
void Interpreter::goblinAnimate(int obj) {
	if (animField(obj, kIsStatic))
		return;
	const MultObject::GobState *st = gobState(obj, animField(obj, kState));
	const Scenery::Layer *l = st ? _scenery.layer(st->animation, st->layer) : nullptr;
	int fc = l ? l->framesCount : 0;
	setAnimField(obj, kNewCycle, fc);
	goblinPlaySounds(obj);

	if (!animField(obj, kIsPaused))
		setAnimField(obj, kFrame, animField(obj, kFrame) + 1);
	int frame = animField(obj, kFrame);
	switch (animField(obj, kStateType)) {
	case 0: case 1: setAnimField(obj, kIsPaused, 0); break;
	case 4: if (frame == 0) setAnimField(obj, kIsPaused, 1); break;
	case 6: if (frame >= fc) setAnimField(obj, kIsPaused, 1); break;
	default: break;
	}

	if (animField(obj, kNewState) == -1 && frame >= fc) {
		int left = animField(obj, kFramesLeft);
		if (left <= 0) {
			setAnimField(obj, kFramesLeft, animField(obj, kMaxFrame));
			setAnimField(obj, kFrame, 0);
		} else {
			setAnimField(obj, kFramesLeft, left - 1);
		}
	}
	if (animField(obj, kFrame) >= fc) {
		int ns = animField(obj, kNewState);
		if (ns != -1) {
			const MultObject::GobState *nst = gobState(obj, ns);
			if (nst && l) {
				setObjPos(obj, objPosX(obj) + l->animDeltaX, objPosY(obj) + l->animDeltaY);
				setAnimField(obj, kLayer, nst->layer);
				setAnimField(obj, kAnimation, nst->animation);
				setAnimField(obj, kState, ns);
				setAnimField(obj, kFrame, 0);
				setAnimField(obj, kNewState, -1);
			}
		} else {
			setAnimField(obj, kFrame, animField(obj, kFrame) - 1);
		}
	}
}

// Gob op 100: per-frame goblin housekeeping (docs/goblins.md 2.7).
void Interpreter::handleGoblins(int noTurn1, int noTurn2, int relax1, int relax2, int busy1, int busy2) {
	if (_objects.size() < 2 || _objects[0].goblinStates.empty() || _objects[1].goblinStates.empty())
		return;
	bool gobNoTurn[2] = {_vars->slot(uint32_t(noTurn1)) != 0, _vars->slot(uint32_t(noTurn2)) != 0};
	bool gobBusy[2] = {_vars->slot(uint32_t(busy1)) != 0, _vars->slot(uint32_t(busy2)) != 0};
	int relaxVar[2] = {relax1, relax2};
	int standW[2] = {45, 49}, standE[2] = {44, 48};

	for (int g = 0; g < 2; g++) {
		// 1. Boredom while not selected.
		if (!animField(g, kIsBusy)) {
			if (!gobBusy[g] && animField(g, kIsStatic) == 0) {
				uint32_t relax = _vars->slot(uint32_t(relaxVar[g]));
				if (relax == 0 && animField(g, kState) == 28) {
					goblinSetState(g, 24 + random(3));
					writeVar(uint32_t(relaxVar[g]), 100);
				} else {
					writeVar(uint32_t(relaxVar[g]), relax - 1);
				}
			}
			int st = animField(g, kState);
			if (st == 8 || st == 9 || st == 29)
				setAnimField(g, kCurLookDir, 6);
		}
		// 2.
		if (animField(g, kIsBusy) == 1 && animField(g, kIsStatic) == 0) {
			int st = animField(g, kState);
			if (st == 28 || st == 29)
				setAnimField(g, kCurLookDir, 0);
		}
		// 3. While a scripted sequence runs on this goblin's track.
		int track = int32_t(_vars->slot(uint32_t(18 + g)));
		if (track != -1) {
			int layer = animField(g, kLayer);
			if (layer == standE[g])
				setAnimField(g, kCurLookDir, 4);
			else if (layer == standW[g])
				setAnimField(g, kCurLookDir, 0);
			if (animField(g, kIsBusy) == 0)
				setAnimField(g, kCurLookDir, 6);
		} else if (!gobNoTurn[g]) {
			// 4. Turn towards the remembered facing once arrived.
			int layer = animField(g, kLayer), look = animField(g, kCurLookDir);
			if (layer == standW[g] && look == 4 && animField(g, kPathExistence) == 5)
				goblinSetState(g, 19);
			if (layer == standE[g] && look == 0 && animField(g, kPathExistence) == 5)
				goblinSetState(g, 16);
		}
	}

	// 5. Ladders, ropes and stairs use dedicated climbing layers.
	static const int kClimb[2][7][2] = {
		{{2, 8}, {6, 9}, {17, 26}, {18, 32}, {21, 22}, {22, 20}, {23, 21}},
		{{2, 10}, {6, 11}, {17, 29}, {18, 35}, {21, 25}, {22, 23}, {23, 24}},
	};
	for (int g = 0; g < 2; g++) {
		const MultObject &o = _objects[size_t(g)];
		if (getPass(o.goblinX, o.goblinY) > 17) {
			int st = animField(g, kState);
			for (const auto &m : kClimb[g])
				if (m[0] == st)
					setAnimField(g, kLayer, m[1]);
		}
	}

	// 6. A destination on a ladder is moved to a walkable cell beside its end.
	for (int g = 0; g < 2; g++) {
		int dx = int8_t(animField(g, kDestX)), dy = int8_t(animField(g, kDestY));
		if (dx < 0 || dx > 39 || dy < 0 || dy > 39) {
			if (g == 0)
				return;
			continue;
		}
		if (getPass(dx, dy) <= 17)
			continue;
		const MultObject &o = _objects[size_t(g)];
		int step = o.goblinY > dy ? -1 : 1;
		int guard = 0;
		while (getPass(dx, dy) > 17 && guard++ < 64)
			dy += step;
		dy -= step;
		if (getPass(dx - 1, dy) == 0) {
			if (getPass(dx + 1, dy) != 0)
				dx++;
		} else {
			dx--;
		}
		goblinMove(dx, dy, g);
	}
}

void Interpreter::placeGoblin(int obj, int x, int y, int state) {
	if (obj < 0 || obj >= int(_objects.size()))
		return;
	MultObject &o = _objects[size_t(obj)];
	if (_ver >= 3) {
		if (o.goblinStates.empty())
			return;
		// Goblin_v3::placeObject: the standing states set the look direction.
		if (gobState(obj, state)) {
			if (state == 8)
				setAnimField(obj, kCurLookDir, 0);
			else if (state == 9)
				setAnimField(obj, kCurLookDir, 4);
			else if (state == 28)
				setAnimField(obj, kCurLookDir, 6);
			else if (state == 29)
				setAnimField(obj, kCurLookDir, 2);
		}
		// WORKAROUND (ScummVM bug #5760): in EMAP1018, where Blount and
		// Were-Blount are apart, Blount can be left inactive when the room
		// is entered again; mark him as a walking goblin.
		const MultObject::GobState *st = gobState(obj, state);
		if (currentTot() == "EMAP1018" && obj == 0 && x == 3 && y == 33 && state == 9 && st && st->layer == 27 &&
		    st->animation == 0 && animField(obj, kAnimType) == 1)
			setAnimField(obj, kAnimType, 100);
	}
	o.goblinX = uint8_t(x);
	o.goblinY = uint8_t(y);
	setAnimField(obj, kOrder, y);

	if (state == -1) {
		setAnimField(obj, kFrame, 0);
		setAnimField(obj, kIsPaused, 0);
		setAnimField(obj, kIsStatic, 0);
		setAnimField(obj, kNewCycle, 0);
		anchorToCell(obj, x, y);
		return;
	}
	if (const MultObject::GobState *st = gobState(obj, state)) {
		setAnimField(obj, kState, state);
		setAnimField(obj, kLayer, st->layer);
		setAnimField(obj, kAnimation, st->animation);
		setAnimField(obj, kFrame, 0);
		setAnimField(obj, kIsPaused, 0);
		setAnimField(obj, kIsStatic, 0);
		if (const Scenery::Layer *l = _scenery.layer(st->animation, st->layer))
			setAnimField(obj, kNewCycle, l->framesCount);
		anchorToCell(obj, x, y);
	}
	initiateMove(obj);
}

void Interpreter::setGoblinState(int obj, int state, int type) {
	if (obj < 0 || obj >= int(_objects.size()))
		return;
	setAnimField(obj, kStateType, type);
	const MultObject::GobState *st = gobState(obj, state);
	if (!st)
		return;
	setAnimField(obj, kFrame, 0);
	setAnimField(obj, kState, state);
	setAnimField(obj, kLayer, st->layer);
	setAnimField(obj, kAnimation, st->animation);
	setAnimField(obj, kIsPaused, 0);
	setAnimField(obj, kIsStatic, 0);
	const Scenery::Layer *l = _scenery.layer(st->animation, st->layer);
	if (l)
		setAnimField(obj, kNewCycle, l->framesCount);
	if (type == 0) {
		if (l)
			setObjPos(obj, l->posX, l->posY);
	} else {
		// Type 11 (the only other one Gobliins 2 uses): stand on the cell.
		const MultObject &o = _objects[size_t(obj)];
		anchorToCell(obj, o.goblinX, o.goblinY);
	}
}

// loadMultObject with goblin anim types (docs/goblins.md 2.2).
void Interpreter::goblinPlaceObject(int obj, int animType) {
	MultObject &o = _objects[size_t(obj)];
	int state = animField(obj, kLayer); // the layer slot carries the state
	const MultObject::GobState *st = gobState(obj, state);
	if (!st)
		return;

	if (animType == 100) {
		o.goblinX = o.destX = uint8_t(objPosX(obj) % 256);
		o.goblinY = o.destY = uint8_t(objPosY(obj) % 256);
		o.gobDestX = int8_t(o.goblinX);
		o.gobDestY = int8_t(o.goblinY);
		setAnimField(obj, kFramesLeft, animField(obj, kMaxFrame));
		setAnimField(obj, kNextState, -1);
		setAnimField(obj, kNewState, -1);
		setAnimField(obj, kPathExistence, 0);
		setAnimField(obj, kIsBusy, 0);
		setAnimField(obj, kState, state);
		setAnimField(obj, kLayer, st->layer);
		setAnimField(obj, kAnimation, st->animation);
		anchorToCell(obj, o.goblinX, o.goblinY);
	} else {
		setAnimField(obj, kNextState, -1);
		setAnimField(obj, kNewState, -1);
		setAnimField(obj, kState, state);
		setAnimField(obj, kLayer, st->layer);
		setAnimField(obj, kAnimation, st->animation);
		if (objPosX(obj) == 1000 && objPosY(obj) == 1000)
			if (const Scenery::Layer *l = _scenery.layer(st->animation, st->layer))
				setObjPos(obj, l->posX, l->posY);
	}
}

// ---------------------------------------------------------------------------
// PC speaker: square-wave tones; 50 Hz is the 5 ms footstep click.

void Interpreter::speakerOn(int freq) {
	if (freq <= 0 || _ffSkip) // silent while skipping
		return;
	bool click = freq == 50;
	_mixer.playSpeaker(freq, click ? 5 : 0);
	_ignoreSpeakerOff = click;
}

void Interpreter::speakerOff() {
	if (_ignoreSpeakerOff) {
		_ignoreSpeakerOff = false;
		return;
	}
	_mixer.stopSample(Mixer::kSpeakerChannel);
}

} // namespace gob
