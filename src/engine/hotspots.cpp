// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hotspots, input polling, the mouse cursor and capture push/pop.
// Behaviour follows docs/graphics.md sections 3.5, 5 and 6.

#include "engine/interpreter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace gob {

namespace {

constexpr uint16_t kKeyEscape = 0x011B;
constexpr uint16_t kKeyReturn = 0x1C0D;
constexpr uint16_t kKeyBackspace = 0x0E08;

enum Button { kLeft = 1, kRight = 2 };

// Hotspot button field: 0 left, 1 right, 2 any, other none.
bool buttonMatches(uint16_t flags, int pressed) {
	switch ((flags >> 4) & 7) {
	case 0: return pressed == kLeft;
	case 1: return pressed == kRight;
	case 2: return pressed != 0;
	default: return false;
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Input

// Buttons are tracked from press/release edges, so that forceMouseUp() can
// make a still-held button invisible until it is released and pressed again.
void Interpreter::pollMouseButtons() {
	int held = _host.mouseButtons();
	int pressed = _host.takeMouseClicks();
	_eventButtons = (_eventButtons & held) | pressed;
}

void Interpreter::forceMouseUp(bool onlyWhenSynced) {
	if (onlyWhenSynced && _mouseButtons != _eventButtons)
		return;
	_mouseButtons = 0;
	_eventButtons = 0;
}

int Interpreter::checkKeys() {
	if (_ver == 3) {
		pollMouseV3();
	} else {
		_interMouseX = _host.mouseX();
		_interMouseY = _host.mouseY();
	}
	pollMouseButtons();
	_mouseButtons = (_eventButtons == 3) ? 0 : _eventButtons; // both together count as none
	return _host.pollKey();
}

void Interpreter::flushKeys() {
	while (_host.pollKey()) {
	}
}

Task<void> Interpreter::waitMouseRelease(bool drawCursor) {
	for (pollMouseButtons(); !_quit && _eventButtons != 0; pollMouseButtons()) {
		if (drawCursor)
			animateCursor(2);
		_frameBudget = 0;
		_waitingForPlayer = true; // a button held down (engine/skip.cpp)
		co_await _sched.nextFrame();
		_waitingForPlayer = false;
	}
}

void Interpreter::storeMouse() {
	writeVar(2, uint32_t(_interMouseX));
	writeVar(3, uint32_t(_interMouseY));
	writeVar(4, uint32_t(_mouseButtons));
}

Task<void> Interpreter::waitEndFrame() {
	uint32_t frameMs = 1000 / uint32_t(std::max(_frameRate, 1));
	do {
		_frameBudget = 0;
		co_await _sched.nextFrame();
	} while (!_quit && gameTicks() - _lastFrameTime < frameMs);
	uint32_t now = gameTicks();
	_lastFrameTime = (now - _lastFrameTime > 1000) ? now : _lastFrameTime + frameMs;
}

// ---------------------------------------------------------------------------
// Cursor

std::optional<Interpreter::CursorImage> Interpreter::cursor() {
	Surface *strip = _draw.cursorStrip();
	if (!strip || !_draw.cursorVisible())
		return std::nullopt;
	CursorImage c;
	c.strip = strip;
	c.width = _draw.cursorWidth;
	c.height = _draw.cursorHeight;
	c.frameX = _draw.cursorFrame * _draw.cursorWidth;
	c.hotX = _draw.cursorHotX;
	c.hotY = _draw.cursorHotY;
	if (c.frameX + c.width > strip->width())
		return std::nullopt;
	return c;
}

void Interpreter::animateCursor(int cursor) {
	if (!_draw.cursorStrip())
		return;
	// Version 1 starts from "visible, not being installed" (2) and ends
	// visible (3) once a cursor is installed (Draw_v1::animateCursor; it
	// sets 0 after the retrace that shows the cursor when nothing was
	// invalidated, which our presenter would read as hidden).
	_draw.showCursor = _draw.v1Cursor ? 2 : _draw.showCursor | 1;

	int c = cursor;
	if (c == -1) {
		c = findCursor(_interMouseX, _interMouseY);
		if (c < 0 || c >= 40 || _draw.cursorAnimLow[c] == -1)
			c = 1;
	}
	if (c < 0 || c >= 40 || _draw.cursorAnimLow[c] == -1) {
		_draw.blitCursor();
		if (!_draw.v1Cursor)
			_draw.showCursor &= ~1;
		return;
	}

	uint32_t now = gameTicks();
	int low = _draw.cursorAnimLow[c], high = _draw.cursorAnimHigh[c], delay = _draw.cursorAnimDelays[c];
	if (c == _draw.cursorIndex) {
		if (delay && now >= _draw.cursorTimeKey + uint32_t(delay) * 10) {
			_draw.cursorAnim++;
			if (_draw.cursorAnim < low || _draw.cursorAnim > high)
				_draw.cursorAnim = low;
			_draw.cursorTimeKey = now;
		}
	} else {
		_draw.cursorIndex = c;
		if (delay) {
			_draw.cursorAnim = low;
			_draw.cursorTimeKey = now;
		}
	}

	// Draw_v1 blits cell cursorIndex, whatever the animation frame (Gobliiins'
	// cursor 2, shown while a button is held, has an empty cell).
	_draw.cursorFrame = !_draw.v1Cursor && delay ? std::clamp(_draw.cursorAnim, low, std::max(low, high)) : c;
	if (_draw.cursorHotspotXVar != -1 && _vars) {
		_draw.cursorHotX = uint16_t(_vars->slot(uint32_t(_draw.cursorHotspotXVar + _draw.cursorIndex)));
		_draw.cursorHotY = uint16_t(_vars->slot(uint32_t(_draw.cursorHotspotYVar + _draw.cursorIndex)));
	} else {
		_draw.cursorHotX = _draw.cursorHotY = 0;
	}

	if (_draw.v1Cursor)
		_draw.showCursor = 3;
	if (_draw.anythingInvalidated()) {
		int saved = _draw.cursorIndex;
		_draw.cursorIndex = -1;
		_draw.blitInvalidated(); // copies bit 0 into bit 1: visible
		_draw.cursorIndex = saved;
	} else {
		_draw.showCursor = 3;
	}
	if (!_draw.v1Cursor)
		_draw.showCursor &= ~1;
}

// ---------------------------------------------------------------------------
// Hotspot table

int Interpreter::firstFreeHotspot() const {
	for (int i = 0; i < kHotspotCount; i++)
		if (_hotspots[i].isEnd())
			return i;
	return kHotspotCount;
}

void Interpreter::hotspotsClear() {
	for (Hotspot &h : _hotspots)
		h = Hotspot{};
	_currentKey = 0;
}

void Interpreter::hotspotAdd(const Hotspot &h) {
	for (Hotspot &slot : _hotspots) {
		bool sameId = (slot.id & ~0x4000) == (h.id & ~0x4000);
		if (slot.isEnd() || (!slot.isEnd() && sameId)) {
			uint16_t keepId = (!slot.isEnd() && sameId) ? slot.id : h.id;
			slot = h;
			slot.id = keepId;
			return;
		}
	}
	trace("hotspot table full");
}

void Interpreter::hotspotRemove(uint16_t id) {
	for (Hotspot &h : _hotspots)
		if (!h.isEnd() && h.id == id)
			h = Hotspot{};
}

void Interpreter::hotspotsPush(int all, bool force) {
	if (!_shouldPush && !force)
		return;

	HotspotSet set;
	for (Hotspot &h : _hotspots) {
		if (h.isEnd())
			break;
		bool take = all == 1 || (all == 0 && h.id >= 20) || (all == 2 && h.disabled());
		if (take) {
			set.entries.push_back(h);
			h = Hotspot{};
		}
	}
	// Compact so later scans don't stop at the holes.
	std::stable_partition(_hotspots.begin(), _hotspots.end(), [](const Hotspot &h) { return !h.isEnd(); });

	set.shouldPush = _shouldPush;
	set.key = _currentKey;
	set.id = _currentId;
	set.index = _currentIndex;
	set.x = _currentX;
	set.y = _currentY;
	_hotspotStack.push_back(std::move(set));

	_currentKey = _currentId = _currentIndex = 0;
	_currentX = _currentY = 0;
	_shouldPush = false;
}

void Interpreter::hotspotsPop() {
	if (_hotspotStack.empty())
		return;
	HotspotSet set = std::move(_hotspotStack.back());
	_hotspotStack.pop_back();

	int dst = firstFreeHotspot();
	for (const Hotspot &h : set.entries) {
		if (dst >= kHotspotCount) {
			trace("hotspot table full on pop");
			break;
		}
		_hotspots[dst++] = h;
	}
	_shouldPush = set.shouldPush;
	_currentKey = set.key;
	_currentId = set.id;
	_currentIndex = set.index;
	_currentX = set.x;
	_currentY = set.y;
}

// Re-evaluates the coordinate expressions of hotspots defined with
// expressions, so moving objects keep accurate hit areas.
void Interpreter::recalculateHotspots(bool force) {
	if (!env().tot)
		return;
	Script &s = script();
	for (Hotspot &h : _hotspots) {
		if (h.isEnd())
			break;
		if (h.funcPos == 0 || ((h.flags & 0x80) && !force))
			continue;

		s.push();
		s.seek(h.funcPos);
		int left = evalVal(), top = evalVal(), width = evalVal(), height = evalVal();
		if (h.state() == 0xA)
			h.flags = uint16_t(evalVal());
		s.pop(true);

		if (left < 0) { width += left; left = 0; }
		if (top < 0) { height += top; top = 0; }
		h.left = uint16_t(left);
		h.top = uint16_t(top);
		h.right = uint16_t(left + width - 1);
		h.bottom = uint16_t(top + height - 1);
	}
}

Task<void> Interpreter::hotspotCall(uint16_t offset) {
	_shouldPush = true;
	size_t depth = _hotspotStack.size();

	script().call(offset);
	co_await funcBlock(0);
	script().pop(true);

	while (_hotspotStack.size() > depth)
		hotspotsPop();
	_shouldPush = false;
	recalculateHotspots(false);
}

Task<void> Interpreter::hotspotEnter(int index) {
	if (index < 0 || index >= kHotspotCount)
		co_return;
	Hotspot h = _hotspots[index];
	if (h.state() == 9 || h.state() == 0xA)
		writeVar(17, uint32_t(-(h.id & 0xFFF)));
	_currentX = _interMouseX;
	_currentY = _interMouseY;
	if (h.funcEnter)
		co_await hotspotCall(h.funcEnter);
}

Task<void> Interpreter::hotspotLeave(int index) {
	if (index < 0 || index >= kHotspotCount)
		co_return;
	Hotspot h = _hotspots[index];
	if (h.state() == 9 || h.state() == 0xA)
		writeVar(17, uint32_t(h.id & 0xFFF));
	if (h.funcLeave)
		co_await hotspotCall(h.funcLeave);
}

bool Interpreter::hotspotValid(uint16_t key, int index) const {
	return key != 0 && index >= 0 && index < kHotspotCount && (_hotspots[index].state() & 8);
}

uint16_t Interpreter::checkMouse(bool click, uint16_t &id, uint16_t &index) {
	id = 0;
	index = 0;
	if (traceOps && click)
		for (int i = 0; i < kHotspotCount && !_hotspots[i].isEnd(); i++) {
			const Hotspot &h = _hotspots[i];
			char buf[160];
			std::snprintf(buf, sizeof(buf), "  slot %d id %04X type %d flags %04X rect %d,%d-%d,%d%s", i, h.id, h.type(),
			              h.flags, int16_t(h.left), int16_t(h.top), int16_t(h.right), int16_t(h.bottom),
			              h.contains(_interMouseX, _interMouseY) ? " <- mouse" : "");
			trace(buf);
		}
	for (int i = 0; i < kHotspotCount; i++) {
		const Hotspot &h = _hotspots[i];
		if (h.isEnd())
			break;
		if (h.disabled() || !h.contains(_interMouseX, _interMouseY))
			continue;
		if (!click) {
			if (h.type() > 1)
				continue;
			id = h.id;
			index = uint16_t(i);
			return h.key;
		}
		if (h.type() < 1 || !buttonMatches(h.flags, _mouseButtons))
			continue;
		id = h.id;
		index = uint16_t(i);
		return (h.type() == 1 || h.type() == 2) ? h.key : 0;
	}
	if (click && _mouseButtons != kLeft)
		return kKeyEscape; // right-click on nothing acts as Escape
	return 0;
}

int Interpreter::findCursor(int x, int y) const {
	int cursor = 0;
	for (const Hotspot &h : _hotspots) {
		if (h.isEnd())
			break;
		if (h.disabled() || !h.contains(x, y))
			continue;
		int nibble = h.flags >> 12;
		if (nibble == 0) {
			if (h.type() >= 3 && h.type() <= 10)
				return 3;
			if (((h.flags >> 4) & 7) != 1 && cursor == 0)
				cursor = 1;
		} else if (cursor == 0) {
			cursor = nibble;
		}
	}
	return cursor;
}

int Interpreter::findKey(int key) const {
	for (int i = 0; i < kHotspotCount; i++) {
		const Hotspot &h = _hotspots[i];
		if (h.isEnd())
			break;
		if ((h.state() & 0xC) == 8 && (h.key == key || h.key == 0x7FFF))
			return i;
	}
	for (int i = 0; i < kHotspotCount; i++) {
		const Hotspot &h = _hotspots[i];
		if (h.isEnd())
			break;
		if ((h.state() & 0xC) == 8 && h.key < 0x100 &&
		    std::toupper(h.key & 0xFF) == std::toupper(key & 0xFF))
			return i;
	}
	return -1;
}

Task<void> Interpreter::checkHotspotChanged() {
	uint16_t id, index;
	uint16_t key = checkMouse(false, id, index);
	if (key == _currentKey)
		co_return;
	if (hotspotValid(_currentKey, _currentIndex))
		co_await hotspotLeave(_currentIndex);
	_currentKey = key;
	_currentId = id;
	_currentIndex = index;
	if (hotspotValid(key, index))
		co_await hotspotEnter(index);
}

// Waits for a key, a click on a hotspot, or a timeout (delay < 0, in ms of
// game time, counted from the call). Hover enter/leave callbacks run while
// waiting.
Task<int> Interpreter::checkHotspots(int handleMouse, int delay, uint16_t *id, uint16_t *index) {
	uint32_t start = gameTicks();
	_timerLate = 0;
	if (delay >= -1) {
		_currentKey = 0;
		_currentId = 0;
		_currentIndex = 0;
	}
	*id = 0;
	*index = 0;

	if (handleMouse) {
		if (_draw.cursorIndex == -1 && _currentKey == 0) {
			uint16_t cid, cidx;
			uint16_t k = checkMouse(false, cid, cidx);
			if (hotspotValid(k, cidx)) {
				_currentKey = k;
				_currentId = cid;
				_currentIndex = cidx;
				co_await hotspotEnter(cidx);
			}
		}
		animateCursor(-1);
	}

	_draw.blitInvalidated();
	_frameBudget = 0;
	co_await _sched.nextFrame();

	// The outermost hotspot block of a room's main loop: level jumps and quick
	// saves happen here, between frames (engine/levels.cpp).
	const bool levelLoop = _levelBlock && _hotspotDepth == 1;

	int key = 0;
	while (true) {
		if (_rules.levels == LevelSystem::Gob1)
			serviceLevelRequestsV1(levelLoop); // may leave the script file (_terminate)
		if (_terminate || _quit) {
			if (handleMouse)
				_draw.blitCursor();
			co_return 0;
		}
		if (levelLoop && _rules.levels == LevelSystem::Gob3)
			co_await serviceLevelRequestsV3();
		else if (levelLoop && _rules.levels != LevelSystem::Gob1)
			serviceLevelRequests();

		co_await checkHotspotChanged();

		if (_draw.anythingInvalidated()) {
			if (handleMouse)
				animateCursor(-1);
			else
				_draw.blitInvalidated();
		}

		if (handleMouse && _ver == 3)
			evaluateScroll();

		key = checkKeys();

		if (!handleMouse && _mouseButtons) {
			co_await waitMouseRelease(false);
			key = 3;
		}

		if (key) {
			if (handleMouse & 1)
				_draw.blitCursor();
			*id = 0;
			*index = 0;
			if (hotspotValid(_currentKey, _currentIndex))
				co_await hotspotLeave(_currentIndex);
			_currentKey = 0;
			break;
		}

		if (handleMouse) {
			if (_mouseButtons) {
				animateCursor(2);
				if (delay > 0)
					co_await sleep(uint32_t(delay));
				else if (handleMouse & 1)
					co_await waitMouseRelease(true);
				animateCursor(-1);

				key = checkMouse(true, *id, *index);
				if (key || *id) {
					if ((handleMouse & 1) && (delay <= 0 || !_mouseButtons))
						_draw.blitCursor();
					if (hotspotValid(_currentKey, _currentIndex) && _currentIndex != *index)
						co_await hotspotLeave(_currentIndex);
					_currentKey = 0;
					break;
				}
				if (handleMouse & 4)
					co_return 0;

				if (hotspotValid(_currentKey, _currentIndex))
					co_await hotspotLeave(_currentIndex);
				_currentKey = checkMouse(false, _currentId, _currentIndex);
				if (hotspotValid(_currentKey, _currentIndex))
					co_await hotspotEnter(_currentIndex);
			} else {
				co_await checkHotspotChanged();
			}
		}

		if (delay == -2 && !key && !_mouseButtons) {
			*id = 0;
			*index = 0;
			break;
		}

		if (handleMouse)
			animateCursor(-1);

		if (delay < 0 && !key && !_mouseButtons && int64_t(gameTicks() - start) >= -int64_t(delay)) {
			_timerLate = uint32_t(int64_t(gameTicks() - start) + delay);
			*id = 0;
			*index = 0;
			break;
		}

		_frameBudget = 0;
		_atLevelLoop = levelLoop;
		if (levelLoop) { // the player has control (engine/skip.cpp)
			_roomLoopReached = true;
			_lastControlMs = _gameMs;
		} else {
			_waitingForPlayer = true;
		}
		co_await _sched.nextFrame();
		_atLevelLoop = false;
		_waitingForPlayer = false;
	}
	co_return key;
}

// Edits one text-input field in place: the cursor starts at the end of the
// text, typing inserts at the cursor, Return/Up/Down/F-keys end the edit,
// and the cursor blinks while waiting (docs/graphics.md 6.9).
Task<int> Interpreter::updateInput(const InputField &in, int handleMouse, uint16_t *id, uint16_t *index) {
	const Font *font = _draw.font(in.font);
	if (!font)
		co_return 0;
	const Hotspot h = _hotspots[size_t(in.hotspot)];
	int x = int16_t(h.left), y = int16_t(h.top);
	int width = int16_t(h.right) - x + 1, height = int16_t(h.bottom) - y + 1;
	int cw = font->width(), ch = font->height();
	size_t editSize = font->monospaced() ? size_t(std::max(1, width / cw)) : 64;

	std::string str = _vars->readString(in.var);
	size_t pos = str.size();
	Surface &back = _draw.back();
	int textY = y + (height - ch) / 2;

	auto redraw = [&](bool cursor) {
		std::string shown = str + " ";
		if (shown.size() > editSize)
			shown = str;
		back.fillRect(x, y, x + int(editSize) * cw - 1, y + height - 1, in.back);
		_draw.drawText(back, *font, shown, x, textY, in.front, in.back, true);
		if (cursor) // underline cursor below the current character cell
			back.fillRect(x + int(pos) * cw, textY + ch - 1, x + int(pos) * cw + cw - 1, textY + ch - 1, in.front);
		_draw.invalidateRect(x, y, x + int(editSize) * cw - 1, y + height - 1);
	};

	while (!_quit && !_terminate) {
		if (pos >= editSize && editSize > 0)
			pos = editSize - 1;
		pos = std::min(pos, str.size());
		writeVar(56, uint32_t(pos));

		int key = 0;
		for (bool on = true; !_quit && !_terminate; on = !on) {
			redraw(on);
			key = co_await checkHotspots(handleMouse, -300, id, index);
			if (key || *id)
				break;
		}
		redraw(false);
		if (key == 0 || *id != 0)
			co_return 0;

		switch (key) {
		case 0x4D00: // right
			if (pos < str.size() && pos + 1 < editSize) {
				pos++;
				continue;
			}
			co_return 0x5000;
		case 0x4B00: // left
			if (pos > 0) {
				pos--;
				continue;
			}
			co_return 0x4800;
		case kKeyBackspace:
			if (pos > 0) {
				str.erase(pos - 1, 1);
				pos--;
			} else if (pos < str.size()) {
				str.erase(pos, 1);
			}
			break;
		case 0x5300: // delete
			if (pos < str.size())
				str.erase(pos, 1);
			break;
		case kKeyReturn: case 0x4800: case 0x5000:
			co_return key;
		case kKeyEscape: // ignored while the mouse is in use
			continue;
		default: {
			if (key >= 0x3B00 && key <= 0x4400 && (key & 0xFF) == 0)
				co_return key; // F1..F10
			int c = key & 0xFF;
			if (c < ' ')
				continue;
			if (str.size() > editSize)
				continue;
			if (str.size() == editSize)
				str.pop_back();
			str.insert(std::min(pos, str.size()), 1, char(c));
			pos++;
			break;
		}
		}
		_vars->writeString(in.var, str);
	}
	co_return 0;
}

// Runs the block's text inputs. Gobliins 2 has a single one (the save name).
Task<int> Interpreter::handleInputs(std::vector<InputField> &inputs, int handleMouse, uint16_t *id, uint16_t *index) {
	struct Editing { // editingText() while the inputs run, however they end
		bool &flag;
		explicit Editing(bool &f) : flag(f) { flag = true; }
		~Editing() { flag = false; }
	} editing(_editingText);
	for (uint32_t v = 17; v <= 56; v++)
		writeVar(v, 0);
	size_t cur = 0;
	while (!_quit && !_terminate) {
		writeVar(55, uint32_t(cur));
		int key = co_await updateInput(inputs[cur], handleMouse, id, index);
		if (*id != 0 || key == 0)
			co_return key;
		if (key == 0x1C0D) {
			if (inputs.size() == 1 || cur + 1 >= inputs.size())
				co_return key;
			cur++;
			continue;
		}
		if (key == 0x4800 && cur > 0) { cur--; continue; }
		if (key == 0x5000 && cur + 1 < inputs.size()) { cur++; continue; }
		co_return key;
	}
	co_return 0;
}

// Hotspot block (type 2): builds the block's entries, waits for the player
// and continues at the chosen entry's leave block (docs/graphics.md 6.4).
Task<void> Interpreter::evaluateHotspots() {
	Script &s = script();
	hotspotsPush(0, false);

	struct DepthGuard {
		Interpreter &in;
		bool savedLevelBlock;
		explicit DepthGuard(Interpreter &i) : in(i), savedLevelBlock(i._levelBlock) { in._hotspotDepth++; }
		~DepthGuard() {
			in._hotspotDepth--;
			in._levelBlock = savedLevelBlock;
		}
	} depthGuard(*this);

	int endIndex = firstFreeHotspot();

	s.skip(1); // block type
	int count = s.readByte();
	int handleMouse = s.readByte();
	int durationByte = s.readByte();
	s.skip(1); // leave-window index (unused)
	int idx1 = s.readByte();
	int idx2 = s.readByte();
	bool recalc = s.readByte() != 0;

	int32_t duration = durationByte * 1000;
	if (idx1 || idx2)
		duration = durationByte == 100 ? 2 : durationByte * 10;
	int32_t timeVal = duration;

	writeVar(16, 0);

	std::vector<int16_t> ids(size_t(count), 0);
	int inputId = -1;
	std::vector<InputField> inputs;

	for (int i = 0; i < count; i++) {
		uint8_t type = s.readByte();
		if (type & 0x40) {
			s.skip(1); // window number (unused)
			type &= ~0x40;
		}

		Hotspot h;
		h.id = uint16_t(0x8000 | i);
		int left, top, width, height;
		if (type & 0x80) {
			h.funcPos = uint16_t(s.pos());
			left = evalVal(); top = evalVal(); width = evalVal(); height = evalVal();
		} else {
			left = s.readUint16(); top = s.readUint16(); width = s.readUint16(); height = s.readUint16();
		}
		type &= 0x7F;
		h.left = uint16_t(left);
		h.top = uint16_t(top);
		h.right = uint16_t(left + width - 1);
		h.bottom = uint16_t(top + height - 1);

		switch (type) {
		case 0:
			s.skip(6);
			h.funcEnter = uint16_t(s.pos()); s.skipBlock();
			h.funcLeave = uint16_t(s.pos()); s.skipBlock();
			h.key = uint16_t(0xA000 + i);
			h.flags = 0;
			hotspotAdd(h);
			break;
		case 1: {
			uint16_t key = s.readUint16();
			ids[size_t(i)] = s.readInt16();
			uint16_t btn = s.readUint16();
			h.funcEnter = uint16_t(s.pos()); s.skipBlock();
			h.funcLeave = uint16_t(s.pos()); s.skipBlock();
			h.key = key ? key : uint16_t(0xA000 + i);
			h.flags = uint16_t(1 | (btn << 4));
			hotspotAdd(h);
			break;
		}
		case 2:
		case 20:
		case 21: {
			h.key = s.readUint16();
			ids[size_t(i)] = s.readInt16();
			uint16_t btn = s.readUint16();
			if (type == 21) {
				h.funcEnter = uint16_t(s.pos());
				h.funcLeave = 0;
			} else {
				h.funcLeave = uint16_t(s.pos());
			}
			s.skipBlock();
			if (type == 20)
				inputId = i;
			h.flags = uint16_t(2 | (btn << 4));
			hotspotAdd(h);
			break;
		}
		case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: {
			flushKeys();
			VarRef var = readVar();
			int16_t fontIdx = s.readInt16();
			uint8_t backColor = s.readByte();
			uint8_t frontColor = s.readByte();
			if (type >= 5 && type <= 8) {
				uint16_t n = s.readUint16();
				s.skip(n);
			}
			if (left == 0xFFFF) {
				if (!(type & 1))
					s.skipBlock();
				break;
			}
			if (const Font *f = _draw.font(fontIdx); f && f->monospaced())
				h.right = uint16_t(left + width * f->width() - 1);
			h.key = uint16_t(var.offset);
			h.flags = type;
			if (!(type & 1)) {
				h.funcLeave = uint16_t(s.pos());
				s.skipBlock();
			}
			hotspotAdd(h);
			inputs.push_back({firstFreeHotspot() - 1, var.offset, fontIdx, backColor, frontColor, type});
			break;
		}
		case 11:
		case 12: {
			s.skip(6);
			uint16_t block = uint16_t(s.pos());
			int want = type == 11 ? 0xE : 0xD;
			for (Hotspot &e : _hotspots) {
				if (e.isEnd())
					break;
				if (e.state() == want) {
					e.id &= ~0x4000;
					e.funcEnter = e.funcLeave = block;
				}
			}
			s.skipBlock();
			break;
		}
		default:
			trace("unknown hotspot type " + std::to_string(type));
			break;
		}
	}

	if (recalc)
		recalculateHotspots(true);
	flushKeys();
	_levelBlock = _hotspotDepth == 1 && idx1 != 0 && inputs.empty() &&
	              (_rules.levels == LevelSystem::Gob1   ? levelRunningV1()
	               : _rules.levels == LevelSystem::Gob3 ? roomUnderMenuV3()
	                                                    : roomUnderMenu());

	uint16_t id = 0, index = 0;
	bool finishedByTimer = false;
	while (id == 0 && !_terminate && !_quit) {
		int key;
		if (!inputs.empty()) {
			key = co_await handleInputs(inputs, handleMouse, &id, &index);
			if (key == kKeyReturn) {
				for (int i = 0; i < kHotspotCount && !_hotspots[i].isEnd(); i++) {
					const Hotspot &e = _hotspots[i];
					if ((e.state() & 0xC) == 8 && e.type() >= 3 && e.type() <= 10 && e.funcLeave) {
						id = e.id;
						index = uint16_t(i);
						break;
					}
				}
				break;
			}
		} else {
			key = co_await checkHotspots(handleMouse, -duration, &id, &index);
		}
		if (traceOps && (key || id))
			trace("hotspot input: key " + std::to_string(key) + " id " + std::to_string(id) + " buttons " +
			      std::to_string(_mouseButtons) + " mouse " + std::to_string(_interMouseX) + "," +
			      std::to_string(_interMouseY));

		if (id == 0 && key != 0) {
			int found = findKey(key);
			if (found < 0)
				continue; // unmatched keys are dropped
			id = _hotspots[found].id;
			index = uint16_t(found);
		} else if (id == 0 && key == 0 && duration != 0) {
			if (idx1) {
				// Timed tick: run the idx1-th plain entry's leave block.
				int n = 0, target = -1;
				for (int i = endIndex; i < kHotspotCount && !_hotspots[i].isEnd(); i++)
					if (_hotspots[i].state() == 8 && ++n == idx1) {
						target = i;
						break;
					}
				if (target < 0)
					continue;
				storeMouse();
				if (_vars->slot(16) == 0) {
					int local = (_hotspots[target].id & 0xFFF);
					writeVar(16, uint32_t(int32_t(local < count ? ids[size_t(local)] : 0)));
					if (traceOps)
						trace("timed tick");
					uint32_t t0 = gameTicks();
					co_await hotspotCall(_hotspots[target].funcLeave);
					if (_levelBlock)
						_roomLoopPasses = 1; // the room has updated its state once
					int32_t spent = int32_t(gameTicks() - t0);
					// The timeout fired late by up to a frame; take that off the next
					// wait so the ticks keep their period on average at any speed.
					int32_t late = int32_t(std::min<uint32_t>(_timerLate, 1000));
					duration = std::clamp(timeVal - spent - late, int32_t(2), std::max(timeVal, int32_t(2)));
				}
				if (_vars->slot(16) != 0) {
					finishedByTimer = true;
					break;
				}
				id = 0;
				continue;
			}
			int nth = idx2 ? idx2 : 1, n = 0;
			for (int i = endIndex; i < kHotspotCount && !_hotspots[i].isEnd(); i++)
				if (_hotspots[i].state() == 8 && ++n == nth) {
					id = _hotspots[i].id;
					index = uint16_t(i);
					break;
				}
			if (!idx2 && hotspotValid(_currentKey, _currentIndex) && _hotspots[_currentIndex].funcLeave)
				co_await hotspotCall(_hotspots[_currentIndex].funcLeave);
			_currentKey = 0;
			if (id == 0)
				break;
		}

		if (id != 0 && _hotspots[index].funcLeave == 0) {
			// Entries without a leave block run their enter block and keep waiting.
			storeMouse();
			int local = id & 0xFFF;
			writeVar(16, uint32_t(int32_t((id >> 12) == 8 && local < count ? ids[size_t(local)] : local)));
			if (_hotspots[index].funcEnter)
				co_await hotspotCall(_hotspots[index].funcEnter);
			writeVar(16, 0);
			id = 0;
		}
	}

	if (handleMouse == 1)
		_draw.blitCursor();

	if (traceOps)
		trace("hotspot block done: id " + std::to_string(id) + " index " + std::to_string(index) +
		      (finishedByTimer ? " (timer)" : ""));

	if (id != 0 && !finishedByTimer && !_terminate && !_quit) {
		uint16_t leave = _hotspots[index].funcLeave;
		storeMouse();
		if (_vars->slot(16) == 0) {
			int local = id & 0xFFF;
			writeVar(16, uint32_t(int32_t((id >> 12) == 8 && local < count ? ids[size_t(local)] : local)));
		}
		(void)inputId;
		script().seek(leave); // the leave block continues the current function
	} else {
		script().setFinished(true);
	}

	for (int i = 0; i < count; i++)
		hotspotRemove(uint16_t(0x8000 | i));
	std::stable_partition(_hotspots.begin(), _hotspots.end(), [](const Hotspot &h) { return !h.isEnd(); });
	for (Hotspot &h : _hotspots) {
		if (h.isEnd())
			break;
		if (h.state() == 9 || h.state() == 0xA)
			h.id |= 0x4000;
	}
}

// ---------------------------------------------------------------------------
// Capture stack: save/restore rectangles of the back surface.

void Interpreter::capturePush(int left, int top, int width, int height) {
	if (_captures.size() >= 20) {
		trace("capture stack overflow");
		return;
	}
	int n = int(_captures.size());
	_captures.push_back({left, top, width, height});

	int l = left & ~15;
	int r = (left + width - 1) | 15;
	_draw.createSprite(draw::kCaptureSurface + n, r - l + 1, height);
	_draw.sourceSurface = draw::kBackSurface;
	_draw.destSurface = int16_t(draw::kCaptureSurface + n);
	_draw.spriteLeft = int16_t(l);
	_draw.spriteTop = int16_t(top);
	_draw.spriteRight = int16_t(r - l + 1);
	_draw.spriteBottom = int16_t(height);
	_draw.destSpriteX = 0;
	_draw.destSpriteY = 0;
	_draw.transparency = 0;
	spriteOp(draw::BlitSurf);
}

void Interpreter::capturePop(bool doDraw) {
	if (_captures.empty())
		return;
	Capture c = _captures.back();
	_captures.pop_back();
	int n = int(_captures.size());

	if (doDraw) {
		_draw.sourceSurface = int16_t(draw::kCaptureSurface + n);
		_draw.destSurface = draw::kBackSurface;
		_draw.spriteLeft = int16_t(c.left & 15);
		_draw.spriteTop = 0;
		_draw.spriteRight = int16_t(c.width);
		_draw.spriteBottom = int16_t(c.height);
		_draw.destSpriteX = int16_t(c.left);
		_draw.destSpriteY = int16_t(c.top);
		_draw.transparency = 0;
		spriteOp(draw::BlitSurf);
	}
	_draw.freeSprite(draw::kCaptureSurface + n);
}

} // namespace gob
