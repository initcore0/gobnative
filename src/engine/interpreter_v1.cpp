// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The version 1 layer (Gobliiins): the opcodes whose operands or behaviour
// differ from version 2 (docs/port-plan.md 4.1, docs/opcodes.md 10). The
// hooks return true for an opcode they handled; anything else runs the
// shared implementation. Also the dispatch to the layer of the running
// game's version.

#include "engine/interpreter.h"

namespace gob {

// No co_await inside ?:, && or || in the engine: GCC 12 (the webOS
// toolchain) runs both arms of `c ? co_await a : co_await b`, so Gobliiins
// ran Goblins 3's handlers too and the other way round (wrong script
// positions, "unknown block type"). Plain statements instead.
Task<bool> Interpreter::execFuncVersion(uint8_t index, Params &p) {
	if (_ver == 1)
		co_return co_await execFuncV1(index, p);
	co_return co_await execFuncV3(index, p);
}

Task<bool> Interpreter::execDrawVersion(uint8_t op, uint32_t opStart) {
	if (_ver == 1)
		co_return co_await execDrawV1(op, opStart);
	co_return co_await execDrawV3(op, opStart);
}

Task<bool> Interpreter::goblinFuncVersion(int16_t cmd, int16_t paramCount) {
	if (_ver == 1)
		co_return co_await goblinFuncV1(cmd, paramCount);
	co_return co_await goblinFuncV3(cmd, paramCount);
}

// Version 1 switch: the variable is always read as 32 bits, every case
// expression is evaluated, and the last matching case wins.
uint32_t Interpreter::checkSwitchTableV1() {
	VarRef ref = readVar();
	int32_t value = int32_t(_vars->read32(ref.offset));

	uint32_t offset = 0;
	bool matched = false;
	int8_t len = script().readInt8();
	while (len != -5) {
		bool found = false;
		for (int i = 0; i < len; i++) {
			_expr.parse();
			if (_terminate)
				return offset;
			if (_expr.resultInt() == value)
				found = true;
		}
		if (found) {
			offset = script().pos();
			matched = true;
		}
		script().skipBlock();
		len = script().readInt8();
	}

	if ((script().peekByte() >> 4) != 4)
		return offset;

	script().skip(1);
	if (!matched)
		offset = script().pos();
	script().skipBlock();
	return offset;
}

// Draw_v1::printTotText: an absolute rectangle and its back colour, boxes
// (lines, frames, filled rectangles) ended by -1, then a stream of letters
// with commands 2 (move), 3 (font and colour) and 0xBA (insert a variable,
// described by the 23-byte records that follow the stream's end byte 1).
void Interpreter::printTotTextV1(int16_t id) {
	// The CD version starts the level's narration with its story text
	// (Sound::cdPlayMultMusic): one track per language (FR, DE, EN, ES, IT).
	static const char *const kNarration[][6] = {
		{"AVT005", "fra1", "all1", "ang1", "esp1", "ita1"}, {"AVT006", "fra2", "all2", "ang2", "esp2", "ita2"},
		{"AVT012", "fra3", "all3", "ang3", "esp3", "ita3"}, {"AVT016", "fra4", "all4", "ang4", "esp4", "ita4"},
		{"AVT019", "fra5", "all5", "ang5", "esp5", "ita5"}, {"AVT022", "fra6", "all6", "ang6", "esp6", "ita6"},
	};
	for (const auto &n : kNarration)
		if (currentTot() == n[0]) {
			uint32_t lang = _game.scriptLanguage(language);
			startCdTrack(n[(lang <= 4 ? lang : 2) + 1]);
			break;
		}

	if (!env().tot)
		return;
	auto item = env().tot->text(uint16_t(id));
	if (!item || item->size() < 9)
		return;
	const std::vector<uint8_t> &d = *item;
	auto at = [&](size_t o) -> uint8_t { return o < d.size() ? d[o] : 1; };
	auto u16 = [&](size_t o) -> int16_t { return int16_t(at(o) | (at(o + 1) << 8)); };

	int destX = uint16_t(u16(0)) & 0x7FFF;
	int destY = u16(2);
	int right = u16(4), bottom = u16(6);
	size_t ptr = 8;

	if ((_draw.renderFlags & draw::CapturePush) && right >= destX && bottom >= destY) {
		capturePush(destX, destY, right - destX + 1, bottom - destY + 1);
		_captureCounter++;
	}

	_draw.destSpriteX = int16_t(destX);
	_draw.destSpriteY = int16_t(destY);
	_draw.spriteRight = int16_t(right);
	_draw.spriteBottom = int16_t(bottom);
	_draw.destSurface = draw::kBackSurface;
	uint8_t back = at(ptr++);
	_draw.backColor = back < 16 ? back : 16; // version 1 clears only for colours 0-15
	_draw.transparency = 1;
	spriteOp(draw::ClearRect);

	_draw.backColor = 0;
	int16_t savedFlags = _draw.renderFlags;
	_draw.renderFlags &= ~draw::NoInvalidate;

	while (ptr + 1 < d.size() && u16(ptr) != -1) {
		_draw.destSpriteX = int16_t(u16(ptr) + destX);
		_draw.destSpriteY = int16_t(u16(ptr + 2) + destY);
		_draw.spriteRight = int16_t(u16(ptr + 4) + destX);
		_draw.spriteBottom = int16_t(u16(ptr + 6) + destY);
		ptr += 8;
		uint8_t cmd = at(ptr++);
		switch (cmd >> 4) {
		case 0: _draw.frontColor = cmd & 0xF; spriteOp(draw::DrawLine); break;
		case 1: _draw.frontColor = cmd & 0xF; spriteOp(draw::DrawBar); break;
		case 2: _draw.backColor = cmd & 0xF; spriteOp(draw::FillRectAbs); break;
		default: break;
		}
	}
	ptr += 2;

	// The variable records start after the stream's end byte.
	size_t vars = ptr;
	while (vars < d.size() && at(vars) != 1) {
		if (at(vars) == 3)
			vars++;
		if (at(vars) == 2)
			vars += 4;
		vars++;
	}
	vars++;

	auto charWidth = [&]() {
		const Font *f = _draw.font(_draw.fontIndex);
		return f ? f->width() : 0;
	};

	while (ptr < d.size() && at(ptr) != 1) {
		uint8_t c = at(ptr);
		if (c == 3) {
			_draw.fontIndex = uint16_t((at(ptr + 1) >> 4) & 0xF);
			_draw.frontColor = at(ptr + 1) & 0xF;
			ptr += 2;
			continue;
		}
		if (c == 2) {
			_draw.destSpriteX = int16_t(destX + u16(ptr + 1));
			_draw.destSpriteY = int16_t(destY + u16(ptr + 3));
			ptr += 5;
			continue;
		}
		if (c != 0xBA) {
			int x = _draw.destSpriteX;
			_draw.textToPrint = std::string(1, char(c));
			spriteOp(draw::PrintText);
			_draw.destSpriteX = int16_t(x + charWidth());
			ptr++;
			continue;
		}

		uint8_t format = at(vars + 17);
		uint32_t varOff = uint32_t(uint16_t(u16(vars + 18))) * 4;
		std::string buf;
		switch (format & 0x7F) {
		case 0:
			buf = std::to_string(int32_t(_vars->read32(varOff)));
			break;
		case 1:
			buf = _vars->readString(varOff).substr(0, 19);
			break;
		default: { // a fixed-point number: `format` decimals after a comma
			buf = std::to_string(int32_t(_vars->read32(varOff)));
			size_t decimals = std::min<size_t>(format, 18);
			size_t at0 = buf[0] == '-' ? 1 : 0;
			while (buf.size() - 1 < decimals)
				buf.insert(at0, "0");
			buf.insert(buf.size() - decimals, ",");
			break;
		}
		}

		int x = _draw.destSpriteX;
		_draw.textToPrint = buf;
		spriteOp(draw::PrintText);
		if (format & 0x80) {
			// Flowing text: the insert pushes the rest of the line along.
			if (at(ptr + 1) == ' ') {
				_draw.destSpriteX = int16_t(_draw.destSpriteX + charWidth());
				while (at(ptr + 1) == ' ')
					ptr++;
				if (at(ptr + 1) == 2 && u16(ptr + 4) == _draw.destSpriteY)
					ptr += 5;
			} else if (at(ptr + 1) == 2 && u16(ptr + 4) == _draw.destSpriteY) {
				ptr += 5;
				_draw.destSpriteX = int16_t(_draw.destSpriteX + charWidth());
			}
		} else {
			_draw.destSpriteX = int16_t(x + charWidth());
		}
		vars += 23;
		ptr++;
	}

	_draw.renderFlags = savedFlags;
	if (_draw.renderFlags & draw::Collisions)
		trace("printTotText: collision check not implemented");
	if ((_draw.renderFlags & draw::CapturePop) && _captureCounter > 0) {
		_captureCounter--;
		capturePop(true);
	}
}

Task<bool> Interpreter::execFuncV1(uint8_t index, Params &p) {
	(void)p;
	Script &s = script();
	switch (index) {
	case 0x02: // printTotText
		printTotTextV1(s.readInt16());
		co_return true;

	case 0x09: { // assign: no repeat count; only 32-bit and string destinations
		uint8_t destType = s.peekByte();
		VarRef dest = readVar();
		ExprType t = _expr.parse();
		switch (destType) {
		case tok::LoadVarInt32:
		case tok::ArrayInt32:
			_vars->write32(dest.offset, uint32_t(_expr.resultInt()));
			break;
		case tok::LoadVarStr:
		case tok::ArrayStr:
			if (t == ExprType::Int)
				_vars->write8(dest.offset, uint8_t(_expr.resultInt()));
			else
				_vars->writeString(dest.offset, _expr.resultStr());
			break;
		default:
			break;
		}
		co_return true;
	}

	case 0x18: // addHotspot, removeHotspot, getTotTextItemPart: version 2 only
	case 0x19:
	case 0x1A:
		stub(p.opStart);
		co_return true;

	case 0x39: { // stopSound: AdLib (none here) and the sound-effect voice
		blasterStop(evalVal());
		co_return true;
	}

	case 0x3A: { // loadSound: slot, resource id (0xFFFF: a 9-byte file name)
		int slot = evalVal();
		uint16_t id = s.readUint16();
		if (id == 0xFFFF) {
			s.skip(9);
			co_return true;
		}
		if (slot >= 0 && slot < kSoundSlots) {
			freeSoundSlot(slot);
			loadSoundResource(slot, int16_t(id));
		}
		co_return true;
	}

	case 0x3E: { // getFreeMem: v[16] is the language in version 1, keep it
		VarRef a = readVar();
		VarRef b = readVar();
		writeVarOffset(a.offset, 1000000);
		writeVarOffset(b.offset, 1000000);
		co_return true;
	}

	case 0x3F: { // checkData: a handle (50) or -1; no size in v[16]
		std::string file = evalString();
		VarRef dest = readVar();
		bool exists = _files.read(file).has_value();
		trace("checkData \"" + file + "\" -> " + (exists ? "found" : "not found"));
		writeVarOffset(dest.offset, exists ? 50 : uint32_t(-1));
		co_return true;
	}

	case 0x4D: { // readData: int16 size and offset; game files only
		std::string file = evalString();
		VarRef dest = readVar();
		int16_t size = evalVal();
		int16_t offset = evalVal();
		writeVar(1, 1);
		auto data = _files.read(file);
		if (!data) {
			trace("readData: \"" + file + "\" not found");
			co_return true;
		}
		animateCursor(4);
		size_t pos = offset < 0 ? size_t(std::max<int64_t>(0, int64_t(data->size()) + offset + 1)) : size_t(offset);
		if (dest.offset / 4 == 59 && size == 4) {
			uint32_t v = 0;
			for (int i = 0; i < 4 && pos + size_t(i) < data->size(); i++)
				v |= uint32_t((*data)[pos + size_t(i)]) << (8 * i);
			writeVar(59, v);
			co_return true;
		}
		int32_t got = 0;
		for (; got < size && pos + size_t(got) < data->size(); got++)
			_vars->write8(dest.offset + uint32_t(got), (*data)[pos + size_t(got)]);
		if (got == size)
			writeVar(1, 0);
		co_return true;
	}

	case 0x4E: { // writeData: Gobliiins writes no files
		std::string file = evalString();
		readVar();
		evalVal();
		evalVal();
		trace("writeData: ignoring write to \"" + file + "\"");
		writeVar(1, 0);
		co_return true;
	}

	default:
		co_return false;
	}
}

Task<bool> Interpreter::execDrawV1(uint8_t op, uint32_t opStart) {
	Script &s = script();
	switch (op) {
	case 0x00: // loadMult
		co_await loadMultV1();
		co_return true;

	case 0x01: { // playMult: one mult, the whole word is the escape flag
		int16_t checkEscape = s.readInt16();
		co_await playMultV1(checkEscape != 0);
		co_return true;
	}

	case 0x02: // freeMultKeys: the operand is ignored
		s.readInt16();
		freeMultKeysV1();
		co_return true;

	case 0x10: // loadAnim (after a playing CD track ends)
		co_await sceneryLoadAnimV1(false);
		co_return true;

	case 0x12: { // updateAnim: dx, dy, animation, layer, frame, i16 flags
		int dx = int(evalInt()), dy = int(evalInt());
		int animation = int(evalInt()), layer = int(evalInt()), frame = int(evalInt());
		int flags = s.readInt16();
		_scenery.updateAnim(int16_t(layer), int16_t(frame), int16_t(animation), flags, int16_t(dx), int16_t(dy), true);
		co_return true;
	}

	case 0x15: // freeMult
		freeMult();
		_m1.animDataAllocated = false;
		co_return true;

	case 0x16: // animate
		animateV1();
		co_return true;

	case 0x17: // loadMultObject
		loadMultObjectV1();
		co_return true;

	case 0x18: { // getAnimLayerInfo: anim, layer -> deltaX, deltaY, unknown0, frame count
		int anim = int(evalInt()), layer = int(evalInt());
		VarRef dx = readVar(), dy = readVar(), unk = readVar(), frames = readVar();
		if (const Scenery::Layer *l = _scenery.layer(anim, layer)) {
			writeVarOffset(dx.offset, uint32_t(int32_t(l->animDeltaX)));
			writeVarOffset(dy.offset, uint32_t(int32_t(l->animDeltaY)));
			writeVarOffset(unk.offset, uint32_t(int32_t(l->unknown0)));
			writeVarOffset(frames.offset, uint32_t(int32_t(l->framesCount)));
		}
		co_return true;
	}

	case 0x1A: // loadStatic
		sceneryLoadStatic(false);
		co_return true;

	case 0x1B: // freeStatic
		_scenery.freeStatic(int(evalInt()));
		co_return true;

	case 0x1C: { // renderStatic: index, layer
		int index = int(evalInt()), layer = int(evalInt());
		_scenery.renderStatic(int16_t(index), int16_t(layer), resourceLoader());
		co_return true;
	}

	case 0x1D: { // loadCurLayer: the static and layer redrawn over objects
		int st = int(evalInt()), layer = int(evalInt());
		_scenery.curStatic = int16_t(st);
		_scenery.curStaticLayer = int16_t(layer);
		co_return true;
	}

	case 0x0A: // setRenderFlags: a plain assignment
		_draw.renderFlags = evalVal();
		co_return true;

	case 0x21: { // getCDTrackPos: v[5] = position in CD frames, 32767 when silent
		// The original waits a tick here, which keeps polling loops from spinning.
		_frameBudget = 0;
		co_await _sched.nextFrame();
		int32_t cdPos = _cd.position({}, cdTicks());
		if (cdPos != -1 && _cd.current() && skipPending())
			cdPos = -1; // skipping: the track is over
		writeVar(5, uint32_t(cdPos == -1 ? 32767 : cdPos));
		co_return true;
	}

	// Version 2 additions that version 1 does not have.
	case 0x13:
	case 0x23:
	case 0x24:
	case 0x25:
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x50:
	case 0x51:
	case 0x52:
	case 0x53:
	case 0x54:
	case 0x55:
	case 0x56:
	case 0x80:
	case 0x81:
	case 0x82:
	case 0x83:
	case 0x84:
	case 0x85:
	case 0x86:
	case 0x87:
	case 0x88:
		stub(opStart);
		co_return true;

	default:
		co_return false;
	}
}

// Goblin functions run in GoblinsV1 (engine/goblins_v1.cpp). A command
// without a handler is skipped by its parameter count: every version 1
// goblin op reads exactly paramCount words (checked over all Gobliiins
// scripts with gobdis).
Task<bool> Interpreter::goblinFuncV1(int16_t cmd, int16_t paramCount) {
	uint32_t start = script().pos();
	if (_gobV1->goblinFunc(cmd, paramCount))
		co_return true;
	script().seek(start);
	script().skip(std::max<int16_t>(paramCount, 0) * 2);
	if (cmd >= 2000 && cmd <= 2004) // F3-F9 in every level; no handler in the original engine either
		trace("goblin " + std::to_string(cmd) + ": no handler, ignored");
	else
		trace("stub goblin " + std::to_string(cmd) + " (" + std::to_string(paramCount) + " params)");
	co_return true;
}

} // namespace gob
