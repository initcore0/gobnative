// gob2native - expression evaluator tests
// SPDX-License-Identifier: GPL-3.0-or-later

#include "script/expression.h"
#include "script/exprprint.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using namespace gob;
using namespace gob::tok;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static std::vector<uint8_t> code(std::initializer_list<int> bytes) {
	std::vector<uint8_t> v;
	for (int b : bytes)
		v.push_back(uint8_t(b));
	return v;
}

struct Harness {
	std::vector<uint8_t> bytes;
	Variables vars{1024};
	Script script;
	Expression expr{script, vars};

	// Real bytecode always lives after the 128-byte TOT header, and seeks into
	// the header are refused, so place test code at offset 128.
	explicit Harness(std::vector<uint8_t> b) : bytes(128, 0) {
		bytes.insert(bytes.end(), b.begin(), b.end());
		bytes.push_back(0);
		script.attach(&bytes);
		script.seek(128);
	}
};

static int32_t evalInt(std::vector<uint8_t> b, Variables *init = nullptr) {
	Harness h(std::move(b));
	if (init)
		h.vars = *init;
	return h.expr.evalInt();
}

static bool evalBool(std::vector<uint8_t> b) {
	Harness h(std::move(b));
	return h.expr.evalBool();
}

int main() {
	// 2 + 3 * 4 = 14 (multiplication binds tighter)
	CHECK(evalInt(code({LoadImmInt16, 2, 0, Add, LoadImmInt16, 3, 0, Mul, LoadImmInt16, 4, 0, Stop})) == 14);

	// (2 + 3) * 4 = 20
	CHECK(evalInt(code({BeginExpr, LoadImmInt16, 2, 0, Add, LoadImmInt16, 3, 0, EndExpr, Mul, LoadImmInt16, 4, 0, Stop})) == 20);

	// 10 - 3 - 2 = 5 (left to right)
	CHECK(evalInt(code({LoadImmInt8, 10, Sub, LoadImmInt8, 3, Sub, LoadImmInt8, 2, Stop})) == 5);

	// -5 + 1 = -4
	CHECK(evalInt(code({Neg, LoadImmInt8, 5, Add, LoadImmInt8, 1, Stop})) == -4);

	// 32-bit immediate
	CHECK(evalInt(code({LoadImmInt32, 0x40, 0x42, 0x0F, 0x00, Stop})) == 1000000);

	// Comparisons and logic
	CHECK(evalBool(code({LoadImmInt8, 3, Less, LoadImmInt8, 5, Stop})));
	CHECK(!evalBool(code({LoadImmInt8, 3, Greater, LoadImmInt8, 5, Stop})));
	CHECK(evalBool(code({LoadImmInt8, 1, Add, LoadImmInt8, 1, Eq, LoadImmInt8, 2, Stop})));
	CHECK(evalBool(code({LoadImmInt8, 1, Eq, LoadImmInt8, 2, Or, LoadImmInt8, 4, Eq, LoadImmInt8, 4, Stop})));
	CHECK(!evalBool(code({LoadImmInt8, 1, Eq, LoadImmInt8, 1, And, LoadImmInt8, 4, Eq, LoadImmInt8, 5, Stop})));
	CHECK(evalBool(code({Not, BeginExpr, LoadImmInt8, 1, Eq, LoadImmInt8, 2, EndExpr, Stop})));

	// Short-circuit must skip the rest, including a nested function call.
	{
		Harness h(code({LoadImmInt8, 1, Eq, LoadImmInt8, 1, Or, Func, FuncAbs, LoadImmInt8, 9, EndExpr, Eq, LoadImmInt8, 3, Stop, 0xAA}));
		CHECK(h.expr.evalBool());
		CHECK(h.script.peekByte() == 0xAA);
	}

	// Variables: var32[2] = 7, var16 at byte 20 = -3
	{
		Variables v(1024);
		v.write32(8, 7);
		v.write16(20, uint16_t(-3));
		CHECK(evalInt(code({LoadVarInt32, 2, 0, Mul, LoadImmInt8, 3, Stop}), &v) == 21);
		CHECK(evalInt(code({LoadVarInt16, 10, 0, Stop}), &v) == -3);
	}

	// Functions: abs, sqr, sqrt
	CHECK(evalInt(code({Func, FuncAbs, Neg, LoadImmInt8, 6, EndExpr, Stop})) == 6);
	CHECK(evalInt(code({Func, FuncSqr, LoadImmInt8, 7, EndExpr, Stop})) == 49);
	CHECK(evalInt(code({Func, FuncSqrt1, LoadImmInt16, 0x90, 0x01, EndExpr, Stop})) == 20);

	// Strings: "ab" + "cd" == "abcd"
	{
		Harness h(code({LoadImmStr, 'a', 'b', 0, Add, LoadImmStr, 'c', 'd', 0, Stop}));
		CHECK(h.expr.parse() == ExprType::Str);
		CHECK(h.expr.resultStr() == "abcd");
	}
	CHECK(evalBool(code({LoadImmStr, 'x', 0, Eq, LoadImmStr, 'x', 0, Stop})));

	// Skip leaves the cursor right after the terminator.
	{
		Harness h(code({BeginExpr, LoadImmStr, 'h', 'i', 0, Add, LoadVarInt32, 1, 0, EndExpr, Stop, 0x55}));
		h.expr.skip();
		CHECK(h.script.peekByte() == 0x55);
	}

	// Variable reference + store with width from the type token.
	{
		Harness h(code({LoadVarInt16, 5, 0}));
		VarRef r = h.expr.parseVarIndex();
		CHECK(r.offset == 10);
		h.expr.store(r, 0x12345678);
		CHECK(h.vars.read32(10) == 0x5678);
	}

	// Printer renders infix text and consumes exactly one expression.
	{
		Harness h(code({BeginExpr, LoadVarInt32, 3, 0, Add, LoadImmInt8, 1, EndExpr, Mul, Func, FuncAbs, Neg, LoadImmInt8, 2, EndExpr, Eq, LoadImmStr, 'o', 'k', 0, Stop, 0x77}));
		std::string text = printExpr(h.script);
		CHECK(text == "(v[3] + 1) * abs(-2) == \"ok\"");
		CHECK(h.script.peekByte() == 0x77);
	}
	{
		Harness h(code({LoadVarInt16, 5, 0, 0x66}));
		CHECK(printVarIndex(h.script) == "w@10");
		CHECK(h.script.peekByte() == 0x66);
	}

	if (failures == 0)
		std::printf("all expression tests passed\n");
	return failures ? 1 : 0;
}
