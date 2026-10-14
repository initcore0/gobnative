// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Static disassembler for TOT bytecode. Operand layouts follow
// docs/opcodes.md; the engine version comes from the TOT header (1.1 =
// Gobliiins, whose layouts differ in places: docs/opcodes.md section 10).

#pragma once

#include "formats/tot.h"
#include "script/script.h"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace gob {

struct DisasmLine {
	uint32_t offset = 0;
	int depth = 0;
	std::string text;
};

struct DisasmStats {
	int functions = 0;
	int codeBlocks = 0;
	int hotspotBlocks = 0;
	int instructions = 0;
	int errors = 0; // length mismatches, invalid opcodes, bad block types
};

// A resource the scripts load as an image or a palette, with the place in
// the bytecode (for tools that need to know which palette goes with which
// picture: gobexport).
struct ResourceUse {
	enum Kind {
		Palette,     // palLoad cmd 53 (resId) or 61
		Sprite,      // loadSpriteContent resId, surface, transparency
		Cursor,      // loadCursor resId, cursor slot (in `surface`)
		AnimPicture, // a picture of a loadAnim / loadMult scenery descriptor
		MultSprite,  // a mult's static-key sprite list entry (drawn into the back surface)
		LevelObjects, // Gobliiins: goblin 1000 loads the level's objects (no resource)
	};
	Kind kind = Sprite;
	uint32_t offset = 0;   // the instruction
	uint32_t function = 0; // entry of the function it is in
	int resId = -1;
	int surface = 0, transparency = 0;
	int animResId = -1, pictDescId = -1, picture = 0, width = 0, height = 0; // AnimPicture
};

class Disassembler {
public:
	explicit Disassembler(const TotFile &tot);

	// Disassembles every function reachable from the header table and from
	// call targets. Functions are returned in address order.
	std::vector<DisasmLine> run();

	const DisasmStats &stats() const { return _stats; }

	// Images and palettes loaded by the code that run() went through, in the
	// order they were decoded (function by function).
	const std::vector<ResourceUse> &resourceUses() const { return _uses; }

	// Decodes the single instruction whose opcode byte is at `offset`.
	// Returns its text and the offset just past it. Used by the interpreter to
	// log and skip opcodes it does not implement yet.
	std::pair<std::string, uint32_t> decodeInstruction(uint32_t offset);

private:
	void function(uint32_t entry);
	void anyBlock(int depth, const char *label);
	void codeBlock(int depth, const char *label);
	void hotspotBlock(int depth);
	void instruction(int depth);
	void funcOp(uint32_t at, uint8_t index, int depth);
	void drawOp(uint32_t at, int depth);
	void goblinFunc(uint32_t at, int depth);
	void goblinFuncV1(uint32_t at, int depth);

	// Regular operands described by a spec string (see disasm.cpp).
	std::string operands(const char *spec);
	std::string sceneryDescriptor(bool isStatic);
	std::string loadMult();
	std::string loadSoundSearch();
	std::string printText();

	void emit(uint32_t offset, int depth, std::string text);
	void error(uint32_t offset, int depth, const std::string &what);

	const TotFile &_tot;
	int _ver; // 1, 2 or 3, from the TOT version ("1.1", "1.2", "1.3")
	Script _s;
	std::map<uint32_t, std::vector<DisasmLine>> _functions;
	std::vector<DisasmLine> *_out = nullptr;
	std::set<uint32_t> _pending;
	DisasmStats _stats;
	std::vector<ResourceUse> _uses;
	uint32_t _function = 0, _instruction = 0;
	bool _collect = false;
	void use(ResourceUse u);
};

} // namespace gob
