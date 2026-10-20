// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Read-only snapshots of the engine's state for debugging tools.

#include "engine/goblins_v1.h"
#include "engine/interpreter.h"

#include <algorithm>

namespace gob {

std::vector<Interpreter::EnvInfo> Interpreter::envStack() const {
	std::vector<EnvInfo> out;
	for (size_t i = 0; i < _envs.size(); i++) {
		const Env &e = *_envs[i];
		EnvInfo info;
		info.name = e.name;
		info.loaded = e.tot != nullptr;
		info.pos = e.script.pos();
		info.current = int(i) == _cur;
		out.push_back(std::move(info));
	}
	return out;
}

std::vector<Interpreter::HotspotInfo> Interpreter::hotspots() const {
	std::vector<HotspotInfo> out;
	for (int i = 0; i < kHotspotCount && !_hotspots[size_t(i)].isEnd(); i++) {
		const Hotspot &h = _hotspots[size_t(i)];
		HotspotInfo info;
		info.slot = i;
		info.id = h.id;
		info.flags = h.flags;
		info.key = h.key;
		info.left = int16_t(h.left);
		info.top = int16_t(h.top);
		info.right = int16_t(h.right);
		info.bottom = int16_t(h.bottom);
		info.hasEnter = h.funcEnter != 0;
		info.hasLeave = h.funcLeave != 0;
		info.expression = h.funcPos != 0;
		out.push_back(info);
	}
	return out;
}

std::vector<Interpreter::ObjectInfo> Interpreter::objects() const {
	std::vector<ObjectInfo> out;
	if (!_vars)
		return out;
	if (_gobV1 && _gobV1->loaded()) {
		// Gobliiins: the goblins (0-3) and the level's objects (100 + n),
		// which it draws itself; its mult objects are unused.
		auto add = [&](int index, const GoblinsV1::Object &o, bool goblin) {
			ObjectInfo info;
			info.index = index;
			info.x = o.xPos;
			info.y = o.yPos;
			info.animation = info.drawnAnimation = o.animation;
			info.layer = info.drawnLayer = _gobV1->layerOf(o);
			info.frame = info.drawnFrame = o.curFrame;
			info.animType = o.type;
			info.order = o.order;
			info.isPaused = o.doAnim == 0;
			const bool drawn = (o.type == 0 || o.type == 3) && o.visible && (o.right > o.left || o.bottom > o.top);
			info.left = drawn ? o.left : -1;
			info.top = drawn ? o.top : -1;
			info.right = drawn ? o.right : -1;
			info.bottom = drawn ? o.bottom : -1;
			info.isGoblin = goblin;
			if (goblin) {
				const GoblinsV1::Pos cell = _gobV1->gobPosition(index);
				info.cellX = cell.x;
				info.cellY = cell.y;
				info.state = o.state;
				info.nextState = o.nextState;
				info.lookDir = o.curLookDir;
				if (index == _gobV1->currentGoblin())
					info.pathExistence = _gobV1->pathExistence();
			}
			out.push_back(info);
		};
		for (int i = 0; i < GoblinsV1::kGoblins; i++)
			if (const GoblinsV1::Object *o = _gobV1->goblin(i))
				add(i, *o, true);
		for (int i = 0; i < GoblinsV1::kObjects; i++)
			if (const GoblinsV1::Object *o = _gobV1->object(i))
				add(100 + i, *o, false);
		return out;
	}
	for (int i = 0; i < int(_objects.size()); i++) {
		const MultObject &o = _objects[size_t(i)];
		ObjectInfo info;
		info.index = i;
		info.x = objPosX(i);
		info.y = objPosY(i);
		info.animation = animField(i, kAnimation);
		info.layer = animField(i, kLayer);
		info.frame = animField(i, kFrame);
		info.animType = animField(i, kAnimType);
		info.order = animField(i, kOrder);
		info.isPaused = animField(i, kIsPaused) != 0;
		info.isStatic = animField(i, kIsStatic) != 0;
		info.left = o.lastLeft;
		info.top = o.lastTop;
		info.right = o.lastRight;
		info.bottom = o.lastBottom;
		info.drawnAnimation = animField(i, kRedrawAnimation);
		info.drawnLayer = animField(i, kRedrawLayer);
		info.drawnFrame = animField(i, kRedrawFrame);
		info.isGoblin = o.isGoblin;
		if (o.isGoblin) {
			info.cellX = o.goblinX;
			info.cellY = o.goblinY;
			info.destX = o.destX;
			info.destY = o.destY;
			info.gobDestX = o.gobDestX;
			info.gobDestY = o.gobDestY;
			info.pathExistence = animField(i, kPathExistence);
			info.state = animField(i, kState);
			info.nextState = animField(i, kNextState);
			info.lookDir = animField(i, kCurLookDir);
			info.isBusy = animField(i, kIsBusy);
		}
		out.push_back(info);
	}
	return out;
}

std::vector<Interpreter::ClickSpot> Interpreter::clickSpots() const {
	std::vector<ClickSpot> out;
	if (!_vars || currentLevel().empty())
		return out;
	if (_gobV1) {
		if (!_gobV1->loaded())
			return out;
		constexpr int w = GoblinsV1::kCellW, h = GoblinsV1::kCellH;
		for (const GoblinsV1::ItemSpot &item : _gobV1->itemSpots()) {
			ClickSpot spot;
			spot.id = item.id;
			spot.object = item.object;
			for (int y = 0; y < GoblinsV1::kMapHeight; y++)
				for (int x = 0; x < GoblinsV1::kMapWidth; x++)
					if (item.has(x, y))
						spot.rects.push_back({x * w, y * h, x * w + w - 1, y * h + h - 1});
			out.push_back(std::move(spot));
		}
		return out;
	}
	if (_hotspotDepth > 1) // a block evaluated inside the room's own (the icon bar)
		return out;
	const int vx = _draw.viewX, vy = _draw.viewY;
	for (const HotspotInfo &h : hotspots()) {
		if (h.disabled() || h.type() > 2)
			continue;
		ClickSpot::Rect r{h.left - vx, h.top - vy, h.right - vx, h.bottom - vy};
		r.left = std::max(r.left, 0);
		r.top = std::max(r.top, 0);
		r.right = std::min(r.right, Draw::kScreenW - 1);
		r.bottom = std::min(r.bottom, Draw::kScreenH - 1);
		if (r.right - r.left < 2 || r.bottom - r.top < 2) // off screen, a tick or a strip
			continue;
		if (r.left == 0 && r.top == 0 && r.right == Draw::kScreenW - 1 && r.bottom == Draw::kScreenH - 1)
			continue;
		ClickSpot spot;
		spot.id = h.id & 0x7FFF;
		spot.rects.push_back(r);
		out.push_back(std::move(spot));
	}
	return out;
}

} // namespace gob
