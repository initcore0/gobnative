// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "script/disasm.h"

#include "script/expression.h"
#include "script/exprprint.h"

#include <cstdio>
#include <cstring>

namespace gob {

namespace {

std::string hex(uint32_t v, int width = 4) {
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%0*X", width, v);
	return buf;
}

uint16_t le16(const std::vector<uint8_t> &d, size_t off) {
	return off + 1 < d.size() ? uint16_t(d[off] | (d[off + 1] << 8)) : 0;
}

struct OpSpec {
	const char *name;
	const char *spec; // nullptr = handled specially
};

// Spec tokens: u8 i8 u16 i16 i32 expr val var str (space separated).
// "val" and "expr" consume identical bytes; both are printed as expressions.
const OpSpec *funcSpec(uint8_t idx, int ver) {
	// Version 1 has no hotspot opcodes 0x18-0x1A.
	if (ver == 1 && idx >= 0x18 && idx <= 0x1A)
		return nullptr;
	static const std::map<uint8_t, OpSpec> table = {
		{0x02, {"printTotText", "i16"}},
		{0x03, {"loadCursor", "i16 i8"}},
		{0x0A, {"loadSpriteToPos", "i16 val val u8 u8"}},
		{0x14, {"keyFunc", "i16"}},
		{0x15, {"capturePush", "val val val val"}},
		{0x16, {"capturePop", ""}},
		{0x17, {"animPalInit", "i16 val val"}},
		{0x18, {"addHotspot", "val val val val val val i16"}},
		{0x19, {"removeHotspot", "val"}},
		{0x1A, {"getTotTextItemPart", "i16 var val"}},
		{0x1F, {"setcmdCount", "u8"}},
		{0x20, {"return", ""}},
		{0x21, {"renewTimeInVars", ""}},
		{0x22, {"speakerOn", "val"}},
		{0x23, {"speakerOff", ""}},
		{0x24, {"putPixel", "i16 val val val"}},
		{0x27, {"freeSprite", "i16"}},
		{0x30, {"break", ""}},
		{0x31, {"loadSpriteContent", "i16 i16 i16"}},
		{0x33, {"fillRect", "i16 val val val val val"}},
		{0x34, {"drawLine", "i16 val val val val val"}},
		{0x35, {"strToLong", "var var"}},
		{0x36, {"invalidate", "i16 val val val val"}},
		{0x37, {"setBackDelta", "val val"}},
		{0x38, {"playSound", "val val val"}},
		{0x39, {"stopSound", "val"}},
		{0x3B, {"freeSoundSlot", "val"}},
		{0x3C, {"waitEndPlay", ""}},
		{0x3D, {"playComposition", "var val"}},
		{0x3E, {"getFreeMem", "var var"}},
		{0x3F, {"checkData", "expr var"}},
		{0x41, {"cleanupStr", "var"}},
		{0x42, {"insertStr", "var expr val"}},
		{0x43, {"cutStr", "var val val"}},
		{0x44, {"strstr", "var expr var"}},
		{0x45, {"istrlen", "var var"}},
		{0x46, {"setMousePos", "val val"}},
		{0x47, {"setFrameRate", "val"}},
		{0x48, {"animatePalette", ""}},
		{0x49, {"animateCursor", ""}},
		{0x4A, {"blitCursor", ""}},
		{0x4B, {"loadFont", "expr i16"}},
		{0x4C, {"freeFont", "i16"}},
		{0x4D, {"readData", "expr var val expr"}},
		{0x4E, {"writeData", "expr var val expr"}},
		{0x4F, {"manageDataFile", "expr"}},
	};
	auto it = table.find(idx);
	return it == table.end() ? nullptr : &it->second;
}

// Version 1 draw ops whose name or layout differs from version 2, and the
// version 2 additions that version 1 lacks (nullptr names).
const OpSpec *drawSpecV1(uint8_t op, bool &known) {
	static const std::map<uint8_t, OpSpec> table = {
		{0x02, {"freeMultKeys", "i16"}},
		{0x13, {nullptr, nullptr}},
		{0x21, {"getCDTrackPos", ""}},
		{0x23, {nullptr, nullptr}},
		{0x24, {nullptr, nullptr}},
		{0x25, {nullptr, nullptr}},
		{0x30, {"loadFontToSprite", "i16 i16 i16 i16 i16"}},
		{0x40, {nullptr, nullptr}},
		{0x41, {nullptr, nullptr}},
		{0x42, {nullptr, nullptr}},
		{0x43, {nullptr, nullptr}},
	};
	auto it = table.find(op);
	known = it != table.end() || (op >= 0x50 && op <= 0x56) || (op >= 0x80 && op <= 0x88);
	if (it == table.end() || !it->second.name)
		return nullptr;
	return &it->second;
}

const OpSpec *drawSpec(uint8_t op) {
	static const std::map<uint8_t, OpSpec> table = {
		{0x01, {"playMult", "i16"}},
		{0x02, {"freeMultKeys", "u16"}},
		{0x07, {"initCursor", "var var i16 i16 i16"}},
		{0x08, {"initCursorAnim", "val i16 i16 i16"}},
		{0x09, {"clearCursorAnim", "val"}},
		{0x0A, {"setRenderFlags", "val"}},
		{0x11, {"freeAnim", "expr"}},
		{0x12, {"updateAnim", "expr expr expr expr expr i16"}},
		{0x13, {"multSub", "val val val val val"}},
		{0x14, {"initMult", "i16 i16 i16 i16 i16 var var var"}},
		{0x15, {"freeMult", ""}},
		{0x16, {"animate", ""}},
		{0x18, {"getAnimLayerInfo", "expr expr var var var var"}},
		{0x19, {"getObjAnimSize", "expr var var var var"}},
		{0x1B, {"freeStatic", "expr"}},
		{0x1C, {"renderStatic", "val val"}},
		{0x1D, {"loadCurLayer", "val val"}},
		{0x20, {"playCDTrack", "expr"}},
		{0x21, {"waitCDTrackEnd", ""}},
		{0x22, {"stopCD", ""}},
		{0x23, {"readLIC", "expr"}},
		{0x24, {"freeLIC", ""}},
		{0x25, {"getCDTrackPos", "var var"}},
		{0x30, {"loadFontToSprite", "i16 u8 u8 u8 u8 u8 u8 u8 u8"}},
		{0x31, {"freeFontToSprite", "i16"}},
		{0x41, {"switchTotSub", "i16 i16"}},
		{0x51, {"freeGoblins", ""}},
		{0x52, {"moveGoblin", "val val val"}},
		{0x53, {"writeGoblinPos", "var var val"}},
		{0x54, {"stopGoblin", "val"}},
		{0x55, {"setGoblinState", "val val val"}},
		{0x56, {"placeGoblin", "val val val val"}},
		{0x80, {"initScreen", "i16 val val"}},
		{0x81, {"scroll", "val val val val val val"}},
		{0x82, {"setScrollOffset", "val val"}},
		{0x83, {"playImd", "expr val val val val val val val val"}},
		{0x84, {"getImdInfo", "expr var var var var var"}},
		{0x85, {"openItk", "expr"}},
		{0x86, {"closeItk", ""}},
		{0x87, {"setImdFrontSurf", ""}},
		{0x88, {"resetImdFrontSurf", ""}},
	};
	auto it = table.find(op);
	return it == table.end() ? nullptr : &it->second;
}

} // namespace

Disassembler::Disassembler(const TotFile &tot)
	: _tot(tot), _ver(tot.header().versionMinor <= 1 ? 1 : tot.header().versionMinor >= 3 ? 3 : 2), _s(&tot.bytes()) {}

void Disassembler::emit(uint32_t offset, int depth, std::string text) {
	_out->push_back({offset, depth, std::move(text)});
}

void Disassembler::error(uint32_t offset, int depth, const std::string &what) {
	_stats.errors++;
	emit(offset, depth, "!! " + what);
}

std::string Disassembler::operands(const char *spec) {
	std::string out;
	const char *p = spec;
	while (*p) {
		while (*p == ' ')
			p++;
		const char *end = std::strchr(p, ' ');
		std::string t = end ? std::string(p, end) : std::string(p);
		p = end ? end : p + t.size();
		if (t.empty())
			break;

		if (!out.empty())
			out += ", ";
		if (t == "u8") out += std::to_string(_s.readByte());
		else if (t == "i8") out += std::to_string(_s.readInt8());
		else if (t == "u16") out += std::to_string(_s.readUint16());
		else if (t == "i16") out += std::to_string(_s.readInt16());
		else if (t == "i32") out += std::to_string(_s.readInt32());
		else if (t == "expr" || t == "val") out += printExpr(_s);
		else if (t == "var") out += printVarIndex(_s);
		else if (t == "str") out += "\"" + _s.readString() + "\"";
		else out += "?spec:" + t;
	}
	return out;
}

std::string Disassembler::sceneryDescriptor(bool isStatic) {
	std::string out = "scenery " + printExpr(_s);
	if (isStatic) {
		int16_t backCount = _s.readInt16();
		out += " backs[";
		for (int i = 0; i < backCount; i++) {
			const int16_t back = _s.readInt16();
			out += (i ? " " : "") + std::to_string(back);
			if (_ver == 1) { // Gobliiins' rooms and intro pictures: drawn into the back surface
				ResourceUse u;
				u.kind = ResourceUse::Sprite;
				u.resId = back;
				u.surface = 101;
				use(u);
			}
		}
		out += "]";
	}
	int16_t picsCount = _s.readInt16();
	int16_t resId = _s.readInt16();
	out += " res " + std::to_string(resId) + " pics " + std::to_string(picsCount);
	for (int i = 0; i < picsCount; i++) {
		ResourceUse u;
		u.kind = ResourceUse::AnimPicture;
		u.animResId = resId;
		u.picture = i;
		u.pictDescId = _s.readInt16();
		u.width = _s.readInt16();
		u.height = _s.readInt16();
		u.resId = _s.readInt16();
		if (!isStatic || _ver == 1) // the pieces of Gobliiins' static planes are cut out the same way
			use(u);
	}
	return out;
}

std::string Disassembler::loadSoundSearch() {
	int16_t id = _s.readInt16();
	if (id == -1) {
		std::string name;
		for (int i = 0; i < 9; i++)
			if (uint8_t c = _s.readByte())
				name.push_back(char(c));
		return "\"" + name + "\"";
	}
	return std::to_string(id);
}

// Mult loading interleaves script reads with the mult resource's contents,
// so the resource has to be parsed alongside (docs/opcodes.md 8.6).
std::string Disassembler::loadMult() {
	uint16_t raw = _s.readUint16();
	std::string out = "loadMult res " + std::to_string(raw & 0x7FFF);
	if (raw & 0x8000)
		out += " index " + std::to_string(_s.readByte());

	auto res = _tot.resource(raw & 0x7FFF);
	if (!res) {
		out += " (resource missing: cannot decode remainder)";
		_stats.errors++;
		return out;
	}
	const std::vector<uint8_t> &d = res->data;
	size_t rp = 0;
	auto r8 = [&]() { return rp < d.size() ? int8_t(d[rp++]) : int8_t(0); };
	auto r16 = [&]() { int16_t v = int16_t(le16(d, rp)); rp += 2; return v; };

	uint8_t staticCount = uint8_t(r8() + 1);
	uint8_t animCount = uint8_t(r8() + 1);
	bool hasImds = staticCount & 0x80;
	staticCount &= 0x7F;
	out += " statics " + std::to_string(staticCount) + " anims " + std::to_string(animCount);

	for (int i = 0; i < staticCount; i++) {
		out += " {" + sceneryDescriptor(true) + "}";
		rp += 14;
	}
	for (int i = 0; i < animCount; i++) {
		out += " {" + sceneryDescriptor(false) + "}";
		rp += 14;
	}

	r16(); // frame rate
	rp += size_t(std::max<int16_t>(r16(), 0)) * 4;
	for (int i = 0; i < 4; i++)
		rp += size_t(std::max<int16_t>(r16(), 0)) * 10;
	rp += 240;
	rp += size_t(std::max<int16_t>(r16(), 0)) * 7;
	rp += size_t(std::max<int16_t>(r16(), 0)) * 80;
	rp += size_t(std::max<int16_t>(r16(), 0)) * (4 + (hasImds ? 0 : 24));

	int16_t soundKeys = r16();
	std::vector<uint16_t> seen;
	int sounds = 0;
	for (int i = 0; i < soundKeys; i++) {
		r16(); // frame
		int16_t cmd = r16();
		rp += 6 + 2 + (hasImds ? 0 : 24);
		if (cmd == 1 || cmd == 4) {
			uint16_t id = _s.peekUint16();
			bool dup = false;
			for (uint16_t s : seen)
				dup |= s == id;
			seen.push_back(id);
			if (dup)
				_s.skip(2);
			else {
				loadSoundSearch();
				sounds++;
			}
		} else if (cmd == 3) {
			_s.skip(_ver == 1 ? 6 : 4);
		} else if (cmd == 5 && _ver == 1) {
			_s.skip(std::max<int16_t>(int16_t(le16(d, rp - 32)), 0) * 2); // freq words
		}
	}
	out += " sounds " + std::to_string(sounds);

	if (hasImds) {
		int16_t n = _s.readInt16();
		for (int i = 0; i < n; i++) {
			ResourceUse u;
			u.kind = ResourceUse::MultSprite;
			u.resId = _s.readInt16();
			u.surface = 21;
			use(u);
		}
		if (_tot.header().versionMinor >= 3) {
			rp += size_t(std::max<int16_t>(r16(), 0)) * 20;
			int16_t k = _s.readInt16();
			for (int i = 0; i < k; i++) { // IMD file names, 14 bytes each
				std::string name;
				for (int c = 0; c < 14; c++)
					if (char ch = char(_s.readByte()); ch && name.size() == size_t(c))
						name.push_back(ch);
				out += (i ? " " : " imds ") + name;
			}
		}
	}
	return out;
}

std::string Disassembler::printText() {
	std::string out = operands("val val val val val") + ", \"";
	auto isVarType = [](uint8_t b) { return (b >= 16 && b <= 18) || (b >= 23 && b <= 28); };
	while (_s.pos() < _s.size()) {
		int n = 0;
		while (_s.peekByte() != '.' && _s.peekByte() != 200 && n < 59 && _s.pos() < _s.size()) {
			uint8_t c = _s.readByte();
			if (c >= 0x20 && c < 0x7F && c != '"')
				out.push_back(char(c));
			else
				out += hex(c, 2);
			n++;
		}
		if (_s.peekByte() != 200) {
			_s.skip(1);
			if (isVarType(_s.peekByte()))
				out += "{" + printVarIndex(_s) + "}";
			_s.skip(1);
		}
		if (_s.peekByte() == 200)
			break;
	}
	_s.skip(1);
	return out + "\"";
}

void Disassembler::goblinFunc(uint32_t at, int depth) {
	int16_t cmd = _s.readInt16();
	int16_t paramCount = _s.readInt16();
	std::string text = "goblin " + std::to_string(cmd);
	// Goblins 3: op 100 is wobble (no operands); no handleGoblins, no protracker.
	const int op = _ver == 3 && (cmd == 500 || cmd == 501) ? -1 : _ver == 3 && cmd == 100 ? -100 : cmd;
	switch (op) {
	case -100: text += " wobble"; break;
	case 101: break;
	case 0: text += " loadInfogramesIns var " + std::to_string(_s.readInt16()); break;
	case 1: text += " startInfogrames"; _s.skip(2); break;
	case 2: text += " stopInfogrames"; _s.skip(2); break;
	case 10: text += " playInfogrames var " + std::to_string(_s.readInt16()); break;
	case 100: text += " handleGoblins " + operands("i16 i16 i16 i16 i16 i16"); break;
	case 500: text += " playProtracker"; break;
	case 501: text += " stopProtracker"; break;
	default:
		text += " (unimplemented, " + std::to_string(paramCount) + " params)";
		_s.skip(std::max<int16_t>(paramCount, 0) * 2);
		break;
	}
	emit(at, depth, text);
}

namespace {

// Version 1 goblin ops: the name and the number of int16 operands that follow
// the object prefix (docs/opcodes.md 10.3).
struct GobOpV1 {
	const char *name;
	int words;
};

const GobOpV1 *goblinOpV1(int16_t cmd) {
	static const std::map<int16_t, GobOpV1> table = {
		{1, {"setState", 0}}, {2, {"setCurFrame", 0}}, {3, {"setNextState", 0}}, {4, {"setMultState", 0}},
		{5, {"setOrder", 0}}, {6, {"setActionStartState", 0}}, {7, {"setCurLookDir", 0}}, {8, {"setType", 0}},
		{9, {"setNoTick", 0}}, {10, {"setPickable", 0}}, {12, {"setXPos", 0}}, {13, {"setYPos", 0}},
		{14, {"setDoAnim", 0}}, {15, {"setRelaxTime", 0}}, {16, {"setMaxTick", 0}},
		{21, {"getState", 0}}, {22, {"getCurFrame", 0}}, {23, {"getNextState", 0}}, {24, {"getMultState", 0}},
		{25, {"getOrder", 0}}, {26, {"getActionStartState", 0}}, {27, {"getCurLookDir", 0}}, {28, {"getType", 0}},
		{29, {"getNoTick", 0}}, {30, {"getPickable", 0}}, {32, {"getObjMaxFrame", 0}}, {33, {"getXPos", 0}},
		{34, {"getYPos", 0}}, {35, {"getDoAnim", 0}}, {36, {"getRelaxTime", 0}}, {37, {"getMaxTick", 0}},
		{40, {"manipulateMap", 3}}, {41, {"getItem", 2}}, {42, {"manipulateMapIndirect", 3}},
		{43, {"getItemIndirect", 2}}, {44, {"setPassMap", 3}},
		{50, {"setGoblinPosH", 3}}, {52, {"getGoblinPosXH", 1}}, {53, {"getGoblinPosYH", 1}},
		{150, {"setGoblinMultState", 3}}, {152, {"setGoblinUnk14", 2}},
		{200, {"setItemIdInPocket", 1}}, {201, {"setItemIndInPocket", 1}}, {202, {"getItemIdInPocket", 0}},
		{203, {"getItemIndInPocket", 0}}, {204, {"setItemPos", 4}},
		{250, {"setGoblinPos", 3}}, {251, {"setGoblinState", 2}}, {252, {"setGoblinStateRedraw", 2}},
		{500, {"decRelaxTime", 1}}, {502, {"getGoblinPosX", 1}}, {503, {"getGoblinPosY", 1}},
		{600, {"clearPathExistence", 0}}, {601, {"setGoblinVisible", 1}}, {602, {"setGoblinInvisible", 1}},
		{603, {"getObjectIntersect", 2}}, {604, {"getGoblinIntersect", 2}}, {605, {"setItemPos", 4}},
		{1000, {"loadObjects", 1}}, {1001, {"freeObjects", 0}}, {1002, {"animateObjects", 0}},
		{1003, {"drawObjects", 0}}, {1004, {"loadMap", 0}}, {1005, {"moveGoblin", 2}},
		{1006, {"switchGoblin", 0}}, {1008, {"loadGoblin", 0}}, {1009, {"writeTreatItem", 3}},
		{1010, {"moveGoblin0", 0}}, {1011, {"setGoblinTarget", 1}}, {1015, {"setGoblinObjectsPos", 2}},
		{2005, {"initGoblin", 0}},
	};
	auto it = table.find(cmd);
	return it == table.end() ? nullptr : &it->second;
}

} // namespace

// Version 1: cmd and paramCount, then an object prefix that depends on the
// cmd range (1-16: object, value; 21-37: object; 91-106: goblin, value;
// 111-127: goblin; the last two map to 1-16 and 21-37), then the op's own
// operands. Results of getters go to v[59].
void Disassembler::goblinFuncV1(uint32_t at, int depth) {
	int16_t cmd = _s.readInt16();
	int16_t paramCount = _s.readInt16();
	uint32_t operandStart = _s.pos();
	std::string text = "goblin " + std::to_string(cmd);
	std::string prefix;
	if (cmd > 0 && cmd < 17) {
		prefix = " object " + std::to_string(_s.readInt16());
		prefix += " = " + std::to_string(_s.readInt16());
	} else if (cmd > 90 && cmd < 107) {
		prefix = " goblin " + std::to_string(_s.readInt16());
		prefix += " = " + std::to_string(_s.readInt16());
		cmd -= 90;
	} else if (cmd > 110 && cmd < 128) {
		prefix = " goblin " + std::to_string(_s.readInt16());
		cmd -= 90;
	} else if (cmd > 20 && cmd < 38) {
		prefix = " object " + std::to_string(_s.readInt16());
	}

	if (cmd == 1000) { // loadObjects: the level's goblins and objects (the room is set up)
		ResourceUse u;
		u.kind = ResourceUse::LevelObjects;
		use(u);
	}
	if (const GobOpV1 *op = goblinOpV1(cmd)) {
		text += std::string(" ") + op->name + prefix;
		for (int i = 0; i < op->words; i++)
			text += (i ? ", " : " ") + std::to_string(_s.readInt16());
		// The original reads the operands per op; paramCount is only used to
		// skip unknown ops. Flag scripts where the two disagree.
		if (int(_s.pos() - operandStart) != paramCount * 2)
			text += " [paramCount " + std::to_string(paramCount) + "]";
	} else {
		text += prefix + " (unknown, " + std::to_string(paramCount) + " params)";
		_s.skip(std::max<int16_t>(paramCount, 0) * 2);
	}
	emit(at, depth, text);
}

void Disassembler::drawOp(uint32_t at, int depth) {
	uint8_t op = _s.readByte();

	bool v1Known = false;
	const OpSpec *spec = _ver == 1 ? drawSpecV1(op, v1Known) : nullptr;
	if (_ver == 1 && v1Known && !spec) {
		error(at, depth, "unknown draw op " + hex(op, 2) + " (not in version 1)");
		return;
	}
	if (!spec)
		spec = drawSpec(op);
	if (spec) {
		std::string args = operands(spec->spec);
		emit(at, depth, std::string(spec->name) + (args.empty() ? "" : " " + args));
		return;
	}

	switch (op) {
	case 0x00:
		emit(at, depth, loadMult());
		return;
	case 0x10:
		emit(at, depth, "loadAnim " + sceneryDescriptor(false));
		return;
	case 0x1A:
		emit(at, depth, "loadStatic " + sceneryDescriptor(true));
		return;
	case 0x17: {
		std::string text = "loadMultObject " + operands("val val val");
		for (int i = 0; i < 11; i++) {
			if (_ver == 1 ? _s.peekUint16() == tok::Stop : _s.peekByte() == tok::Stop) {
				_s.skip(1);
				text += ", _";
			} else {
				text += ", " + printExpr(_s);
			}
		}
		emit(at, depth, text);
		return;
	}
	case 0x40: {
		uint8_t lenOrFlag = _s.readByte();
		std::string name;
		if (lenOrFlag & 0x80)
			name = printExpr(_s);
		else {
			for (int i = 0; i < lenOrFlag; i++)
				name.push_back(char(_s.readByte()));
			name = "\"" + name + "\"";
		}
		uint8_t flags = _s.readByte();
		emit(at, depth, "totSub " + name + " flags " + std::to_string(flags));
		return;
	}
	case 0x42: {
		uint8_t count = _s.readByte();
		std::string text = "pushVars";
		for (int i = 0; i < count; i++) {
			uint8_t t = _s.peekByte();
			if (t == tok::LoadVarStr || t == tok::ArrayStr) {
				text += " " + printVarIndex(_s);
				_s.skip(1);
			} else {
				text += " " + printExpr(_s);
			}
		}
		emit(at, depth, text);
		return;
	}
	case 0x43: {
		uint8_t count = _s.readByte();
		std::string text = "popVars";
		for (int i = 0; i < count; i++)
			text += " " + printVarIndex(_s);
		emit(at, depth, text);
		return;
	}
	case 0x50: {
		std::string text = "loadMapObjects " + printVarIndex(_s);
		int16_t id = _s.readInt16();
		text += ", " + std::to_string(id);
		if (uint16_t(id) < 65520) {
			int16_t soundCount = _s.readInt16();
			text += " sounds";
			for (int i = 0; i < soundCount; i++)
				text += " " + loadSoundSearch();
		}
		emit(at, depth, text);
		return;
	}
	default:
		error(at, depth, "unknown draw op " + hex(op, 2));
		return;
	}
}

void Disassembler::funcOp(uint32_t at, uint8_t idx, int depth) {
	if (idx == 0x03 || idx == 0x31) { // loadCursor, loadSpriteContent: immediate operands
		ResourceUse u;
		u.kind = idx == 0x03 ? ResourceUse::Cursor : ResourceUse::Sprite;
		u.resId = _s.peekInt16();
		u.surface = idx == 0x03 ? int8_t(_s.peekByte(2)) : _s.peekInt16(2);
		u.transparency = idx == 0x03 ? 0 : _s.peekInt16(4);
		use(u);
	}
	if (const OpSpec *spec = funcSpec(idx, _ver)) {
		const int16_t resId = _s.peekInt16();
		std::string args = operands(spec->spec);
		if (idx == 0x0A && _ver != 2) {
			// loadSpriteToPos: Gobliiins draws its portraits, the skull menu
			// and INTER's pictures with it, Goblins 3 parts of its rooms. The
			// flags byte (second to last): bit 0 transparency, the rest the
			// surface + 1 (0: the back surface). (Gobliins 2's export is
			// left as it was.)
			const uint8_t flags = _s.peekByte(-2);
			ResourceUse u;
			u.kind = ResourceUse::Sprite;
			u.resId = resId;
			u.surface = (flags >> 1) ? (flags >> 1) - 1 : 101;
			u.transparency = flags & 1;
			use(u);
		}
		emit(at, depth, std::string(spec->name) + (args.empty() ? "" : " " + args));
		return;
	}

	switch (idx) {
	case 0x00:
	case 0x01: {
		uint16_t target = _s.readUint16();
		emit(at, depth, "call " + hex(target));
		if (target >= 128 && target < _tot.header().scriptEnd && !_functions.count(target))
			_pending.insert(target);
		return;
	}
	case 0x05: {
		emit(at, depth, "switch " + printVarIndex(_s));
		while (_s.pos() < _s.size()) {
			uint32_t caseAt = _s.pos();
			int8_t n = _s.readInt8();
			if (n == -5)
				break;
			std::string values;
			for (int i = 0; i < n; i++) {
				if (i)
					values += ", ";
				uint8_t t = _s.peekByte();
				if (t == tok::LoadImmInt32) { _s.skip(1); values += std::to_string(_s.readInt32()); _s.skip(1); }
				else if (t == tok::LoadImmInt16) { _s.skip(1); values += std::to_string(_s.readInt16()); _s.skip(1); }
				else if (t == tok::LoadImmInt8) { _s.skip(1); values += std::to_string(_s.readInt8()); _s.skip(1); }
				else values += printExpr(_s);
			}
			emit(caseAt, depth + 1, "case " + values + ":");
			codeBlock(depth + 2, nullptr);
		}
		if ((_s.peekByte() >> 4) == 4) {
			emit(_s.pos(), depth + 1, "default:");
			_s.skip(1);
			codeBlock(depth + 2, nullptr);
		}
		return;
	}
	case 0x06: {
		emit(at, depth, "repeat");
		codeBlock(depth + 1, nullptr);
		_s.skip(1); // marker
		emit(_s.pos(), depth, "until " + printExpr(_s));
		return;
	}
	case 0x07: {
		emit(at, depth, "while " + printExpr(_s));
		codeBlock(depth + 1, nullptr);
		return;
	}
	case 0x08: {
		emit(at, depth, "if " + printExpr(_s));
		codeBlock(depth + 1, nullptr);
		uint32_t markerAt = _s.pos();
		uint8_t marker = _s.readByte();
		if ((marker >> 4) == 0xC) {
			emit(markerAt, depth, "else");
			codeBlock(depth + 1, nullptr);
		}
		return;
	}
	case 0x09: {
		std::string dest = printVarIndex(_s);
		int count = 1;
		if (_ver >= 2 && _s.peekByte() == tok::Stop) { // version 1 has no repeat count
			_s.skip(1);
			count = _s.readByte();
		}
		std::string text = dest + " = ";
		if (count > 1)
			text += "[";
		for (int i = 0; i < count; i++)
			text += (i ? ", " : "") + printExpr(_s);
		if (count > 1)
			text += "]";
		emit(at, depth, text);
		return;
	}
	case 0x11:
		emit(at, depth, "printText " + printText());
		return;
	case 0x12: {
		std::string name;
		if (_s.peekByte() & 0x80) {
			_s.skip(1);
			name = printExpr(_s);
		} else {
			uint8_t len = _s.readByte();
			for (int i = 0; i < len; i++)
				name.push_back(char(_s.readByte()));
			name = "\"" + name + "\"";
		}
		emit(at, depth, "loadTot " + name);
		return;
	}
	case 0x13: {
		uint8_t cmd = _s.readByte();
		std::string text = "palLoad cmd " + std::to_string(cmd & 0x7F) + ((cmd & 0x80) ? " (fade now)" : " (apply at next blit)");
		switch (cmd & 0x7F) {
		case 48: case 52: _s.skip(48); break;
		case 49: _s.skip(18); break;
		case 50: _s.skip(16); break;
		case 51: case 55: _s.skip(2); break;
		case 53: {
			ResourceUse u;
			u.kind = ResourceUse::Palette;
			u.resId = _s.readInt16();
			use(u);
			text += " res " + std::to_string(u.resId);
			break;
		}
		case 56: text += " " + operands("u8 u8"); break;
		case 61: {
			ResourceUse u;
			u.kind = ResourceUse::Palette;
			u.resId = _s.peekInt16(2);
			use(u);
			text += " " + operands("u8 u8 i16");
			break;
		}
		default: break;
		}
		emit(at, depth, text);
		return;
	}
	case 0x1E:
		drawOp(at, depth);
		return;
	case 0x25:
		if (_ver == 1)
			goblinFuncV1(at, depth);
		else
			goblinFunc(at, depth);
		return;
	case 0x26: {
		bool raw = _s.peekByte(1) == 0;
		std::string args = raw ? operands("i16 i16 i16") : operands("val val val");
		emit(at, depth, "createSprite " + args + ", flags " + std::to_string(_s.readInt16()));
		return;
	}
	case 0x32: {
		std::string src = _s.peekByte(1) == 0 ? std::to_string(_s.readInt16()) : printExpr(_s);
		std::string dst = _s.peekByte(1) == 0 ? std::to_string(_s.readInt16()) : printExpr(_s);
		std::string rest = operands("val val val val val val i16");
		emit(at, depth, "copySprite " + src + " -> " + dst + ", " + rest);
		return;
	}
	case 0x3A: {
		std::string slot = printExpr(_s);
		emit(at, depth, "loadSound slot " + slot + ", " + loadSoundSearch());
		return;
	}
	default:
		error(at, depth, "unknown func op index " + hex(idx, 2) + " (byte " + hex(_s.peekByte(-1), 2) + ")");
		return;
	}
}

void Disassembler::instruction(int depth) {
	uint32_t at = _s.pos();
	_instruction = at;
	uint8_t b = _s.readByte();
	uint8_t hi = b >> 4;
	uint8_t idx = hi >= 12 ? uint8_t((16 - hi) * 16 + (b & 0xF)) : hi;
	_stats.instructions++;
	funcOp(at, idx, depth);
}

void Disassembler::codeBlock(int depth, const char *label) {
	uint32_t start = _s.pos();
	uint8_t type = _s.readByte();
	if (type != 1) {
		error(start, depth, "expected code block, found type " + std::to_string(type));
		return;
	}
	uint8_t count = _s.readByte();
	uint16_t len = _s.readUint16();
	uint32_t end = start + len + 2;
	_stats.codeBlocks++;
	if (label)
		emit(start, depth - 1, label);

	for (int i = 0; i < count && _s.pos() < end; i++)
		instruction(depth);

	if (_s.pos() != end)
		error(_s.pos(), depth, "block length mismatch: ended at " + hex(_s.pos()) + ", header says " + hex(end));
	_s.seek(end);
}

void Disassembler::hotspotBlock(int depth) {
	uint32_t start = _s.pos();
	_s.skip(1);
	uint8_t count = _s.readByte();
	uint8_t handleMouse = _s.readByte();
	uint8_t duration = _s.readByte();
	_s.skip(1); // leave window index (unused by Gob2)
	uint8_t index1 = _s.readByte();
	uint8_t index2 = _s.readByte();
	uint8_t recalc = _s.readByte();
	_stats.hotspotBlocks++;

	char head[128];
	std::snprintf(head, sizeof(head), "hotspots count %u mouse %u duration %u idx %u/%u%s", count, handleMouse,
	              duration, index1, index2, recalc ? " recalc" : "");
	emit(start, depth, head);

	for (int i = 0; i < count; i++) {
		uint32_t at = _s.pos();
		uint8_t type = _s.readByte();
		std::string text = "hotspot type ";
		std::string window;
		if (type & 0x40) {
			window = " window " + std::to_string(_s.readByte());
			type &= ~0x40;
		}
		std::string rect;
		if (type & 0x80)
			rect = "(" + operands("val val val val") + ")";
		else {
			uint16_t l = _s.readUint16(), t = _s.readUint16(), w = _s.readUint16(), h = _s.readUint16();
			rect = l == 0xFFFF ? "(no pos)" : "(" + std::to_string(l) + "," + std::to_string(t) + " " +
			                                      std::to_string(w) + "x" + std::to_string(h) + ")";
		}
		type &= 0x7F;
		text += std::to_string(type) + window + " " + rect;

		switch (type) {
		case 0:
			_s.skip(6);
			emit(at, depth + 1, text);
			codeBlock(depth + 3, "enter:");
			codeBlock(depth + 3, "leave:");
			break;
		case 1:
			emit(at, depth + 1, text + " key/id/flags " + operands("i16 i16 i16"));
			codeBlock(depth + 3, "enter:");
			codeBlock(depth + 3, "leave:");
			break;
		case 2:
		case 20:
			emit(at, depth + 1, text + " click key/id/flags " + operands("i16 i16 i16"));
			codeBlock(depth + 3, "leave:");
			break;
		case 21:
			emit(at, depth + 1, text + " click-enter key/id/flags " + operands("i16 i16 i16"));
			codeBlock(depth + 3, "enter:");
			break;
		case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: {
			text += " input " + operands("var i16 u8 u8");
			if (type >= 5 && type <= 8) {
				uint16_t n = _s.readUint16();
				_s.skip(n);
				text += " fmt[" + std::to_string(n) + "]";
			}
			emit(at, depth + 1, text);
			if ((type & 1) == 0)
				codeBlock(depth + 3, "leave:");
			break;
		}
		case 11:
		case 12:
			_s.skip(6);
			emit(at, depth + 1, text + (type == 11 ? " enable2" : " enable1"));
			codeBlock(depth + 3, "block:");
			break;
		default:
			error(at, depth + 1, "unknown hotspot type " + std::to_string(type));
			return;
		}
	}
}

void Disassembler::anyBlock(int depth, const char *label) {
	uint8_t type = _s.peekByte();
	if (type == 1)
		codeBlock(depth, label);
	else if (type == 2)
		hotspotBlock(depth);
	else
		error(_s.pos(), depth, "unknown block type " + std::to_string(type));
}

void Disassembler::use(ResourceUse u) {
	if (!_collect)
		return;
	u.offset = _instruction;
	u.function = _function;
	_uses.push_back(u);
}

void Disassembler::function(uint32_t entry) {
	_out = &_functions[entry];
	_function = entry;
	_stats.functions++;

	std::string label = "function " + hex(entry);
	const auto &fns = _tot.header().functions;
	for (size_t i = 0; i < fns.size(); i++)
		if (fns[i] == entry)
			label += "  [entry " + std::to_string(i) + "]";
	emit(entry, 0, label);

	_s.seek(entry);
	anyBlock(1, nullptr);
}

std::pair<std::string, uint32_t> Disassembler::decodeInstruction(uint32_t offset) {
	std::vector<DisasmLine> lines;
	_out = &lines;
	_s.seek(offset);
	bool collect = _collect;
	_collect = false;
	instruction(0);
	_collect = collect;
	_out = nullptr;
	_pending.clear();
	return {lines.empty() ? std::string("?") : lines.front().text, _s.pos()};
}

std::vector<DisasmLine> Disassembler::run() {
	_collect = true;
	_uses.clear();
	for (uint16_t f : _tot.header().functions)
		if (f >= 128 && f < _tot.header().scriptEnd)
			_pending.insert(f);

	while (!_pending.empty()) {
		uint32_t entry = *_pending.begin();
		_pending.erase(_pending.begin());
		if (_functions.count(entry))
			continue;
		function(entry);
	}

	std::vector<DisasmLine> all;
	for (auto &[offset, lines] : _functions) {
		all.insert(all.end(), lines.begin(), lines.end());
		all.push_back({offset, 0, ""});
	}
	return all;
}

} // namespace gob
