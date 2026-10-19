// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Evaluator for the Gob engine's postfix-ish bytecode expressions. The
// algorithm mirrors the original's operator-stack evaluation closely,
// including its precedence quirks, because game scripts rely on them.

#pragma once

#include "script/random.h"
#include "script/script.h"
#include "script/variables.h"

#include <cstdint>
#include <functional>
#include <string>

namespace gob {

namespace tok {
enum : uint8_t {
	Neg = 1, Add = 2, Sub = 3, BitOr = 4, Mul = 5, Div = 6, Mod = 7, BitAnd = 8,
	BeginExpr = 9, EndExpr = 10, Not = 11, EndMarker = 12, StrIndex = 13,
	VarBase = 14, VarBaseArray = 15,
	ArrayInt8 = 16, LoadVarInt16 = 17, LoadVarInt8 = 18, LoadImmInt32 = 19,
	LoadImmInt16 = 20, LoadImmInt8 = 21, LoadImmStr = 22, LoadVarInt32 = 23,
	LoadVarInt32AsInt16 = 24, LoadVarStr = 25, ArrayInt32 = 26, ArrayInt16 = 27,
	ArrayStr = 28, Func = 29,
	Or = 30, And = 31, Less = 32, Leq = 33, Greater = 34, Geq = 35, Eq = 36, Neq = 37,
	VarBaseCont = 97, // continuation marker after a var-base prefix
	Stop = 99,        // default expression terminator
};

// Boolean results reuse two operand codes, as in the original.
constexpr uint8_t True = 24;
constexpr uint8_t False = 23;

enum : uint8_t { FuncSqrt1 = 0, FuncSqrt2 = 1, FuncSqr = 5, FuncSqrt3 = 6, FuncAbs = 7, FuncRand = 10 };
} // namespace tok

// Result type of an evaluated expression.
enum class ExprType : uint8_t {
	Int = tok::LoadImmInt16,
	Str = tok::LoadImmStr,
	True = tok::True,
	False = tok::False,
};

struct VarRef {
	uint32_t offset = 0; // byte offset into the variable space
	uint8_t type = 0;    // the token that described it (decides access width)
	uint16_t size = 0;   // element size for var-base references
};

class Expression {
public:
	Expression(Script &script, Variables &vars) : _script(&script), _vars(&vars) {}

	// Re-targets the evaluator, e.g. when the interpreter switches TOT files.
	void bind(Script &script, Variables &vars) { _script = &script; _vars = &vars; }

	// Size in 32-bit slots of one element of a string array (TOT header value).
	void setAnimDataSize(uint16_t n) { _animDataSize = n; }
	void setRandom(std::function<int32_t(int32_t)> rnd) { _random = std::move(rnd); }

	// Full evaluation. Returns the result type; value in resultInt()/resultStr().
	ExprType parse(uint8_t stopToken = tok::Stop);
	int16_t parseVal(uint8_t stopToken = tok::Stop);
	VarRef parseVarIndex();

	// Convenience wrappers matching how opcodes consume expressions.
	int32_t evalInt();
	bool evalBool();
	std::string evalString();

	void skip(uint8_t stopToken = tok::Stop);

	int32_t resultInt() const { return _resultInt; }
	const std::string &resultStr() const { return _resultStr; }

	// Stores into a variable reference with the width its type implies.
	void store(const VarRef &ref, uint32_t value);
	void storeString(const VarRef &ref, const std::string &value);

private:
	struct Slot {
		uint8_t op = 0;
		int32_t val = 0;
		std::string str;
	};

	// Operand stack with a frame pointer; offsets are relative to `pos`.
	struct Stack {
		Slot slots[24];
		int pos = -1;

		Slot &at(int rel) { return slots[pos + rel]; }
		void push(int n = 1) { pos += n; }
		void pop(int n = 1) { pos -= n; }
	};

	bool varBase(uint32_t &base, bool mindStop, VarRef *ref);
	void loadValue(uint8_t op, uint32_t base, Stack &st);
	void simpleArithmetic1(Stack &st);
	void simpleArithmetic2(Stack &st);
	bool complexArithmetic(Stack &st, int brackStart);
	int compare(Stack &st);
	void setResult(const Slot &s, ExprType *type);
	int arrayOffset(const uint8_t *dims, int dimCount);

	Script *_script;
	Variables *_vars;
	uint16_t _animDataSize = 0;
	std::function<int32_t(int32_t)> _random;
	Random _ownRandom; // rand() when no generator is set (tools, tests)

	int32_t _resultInt = 0;
	std::string _resultStr;
};

} // namespace gob
