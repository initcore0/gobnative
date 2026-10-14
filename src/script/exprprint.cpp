// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "script/exprprint.h"

#include <cstdio>

namespace gob {

using namespace tok;

namespace {

std::string quote(const std::string &s) {
	std::string out = "\"";
	for (unsigned char c : s) {
		if (c == '"' || c == '\\') {
			out += '\\';
			out += char(c);
		} else if (c < 0x20 || c >= 0x7F) {
			char buf[8];
			std::snprintf(buf, sizeof(buf), "\\x%02X", c);
			out += buf;
		} else {
			out += char(c);
		}
	}
	return out + "\"";
}

const char *funcName(uint8_t fn) {
	switch (fn) {
	case FuncSqrt1: case FuncSqrt2: case FuncSqrt3: return "sqrt";
	case FuncSqr: return "sqr";
	case FuncAbs: return "abs";
	case FuncRand: return "rand";
	default: return "func?";
	}
}

std::string printIndices(Script &s, uint8_t dimCount) {
	std::string out = "(";
	for (int d = 0; d < dimCount; d++) {
		if (d)
			out += ", ";
		out += printExpr(s, EndMarker);
	}
	return out + ")";
}

// Optional base-offset prefixes (tokens 14/15). Returns the prefix text and
// whether, with `mindStop`, the prefix alone forms the reference.
std::string printVarBase(Script &s, bool mindStop, bool &complete) {
	std::string out;
	complete = false;
	uint8_t op = s.peekByte();
	while (op == VarBase || op == VarBaseArray) {
		s.skip(1);
		int16_t slot = s.readInt16();
		if (op == VarBase) {
			uint16_t size = s.readUint16();
			out += "base(v[" + std::to_string(slot) + "] sz " + std::to_string(size) + ")";
		} else {
			uint16_t size = s.readUint16();
			uint8_t dimCount = s.readByte();
			s.skip(dimCount);
			out += "base(v[" + std::to_string(slot) + "] sz " + std::to_string(size) + printIndices(s, dimCount) + ")";
		}
		if (s.peekByte() != VarBaseCont) {
			if (mindStop) {
				complete = true;
				return out;
			}
		} else {
			s.skip(1);
		}
		out += ".";
		op = s.peekByte();
	}
	return out;
}

std::string printOperand(Script &s, uint8_t op) {
	switch (op) {
	case LoadImmInt8: return std::to_string(s.readInt8());
	case LoadImmInt16: return std::to_string(s.readInt16());
	case LoadImmInt32: return std::to_string(s.readInt32());
	case LoadImmStr: return quote(s.readString());
	case LoadVarInt32: return "v[" + std::to_string(s.readUint16()) + "]";
	case LoadVarInt32AsInt16: return "v[" + std::to_string(s.readUint16()) + "].w";
	case LoadVarInt16: return "w@" + std::to_string(s.readUint16() * 2);
	case LoadVarInt8: return "b@" + std::to_string(s.readUint16());
	case LoadVarStr: {
		std::string out = "s[" + std::to_string(s.readUint16()) + "]";
		if (s.peekByte() == StrIndex) {
			s.skip(1);
			out += "{" + printExpr(s, EndMarker) + "}";
		}
		return out;
	}
	case ArrayInt8:
	case ArrayInt16:
	case ArrayInt32:
	case ArrayStr: {
		static const char *prefix[] = {"ab", "", "", "", "", "", "", "", "", "", "av", "aw", "as"};
		int16_t start = s.readInt16();
		uint8_t dimCount = s.readByte();
		s.skip(dimCount);
		std::string out = std::string(prefix[op - ArrayInt8]) + "[" + std::to_string(start) + "]" + printIndices(s, dimCount);
		if (op == ArrayStr && s.peekByte() == StrIndex) {
			s.skip(1);
			out += "{" + printExpr(s, EndMarker) + "}";
		}
		return out;
	}
	case Func: {
		uint8_t fn = s.readByte();
		return std::string(funcName(fn)) + "(" + printExpr(s, EndExpr) + ")";
	}
	default:
		return "?op" + std::to_string(op);
	}
}

const char *operatorText(uint8_t op) {
	switch (op) {
	case Neg: return "-";
	case Add: return " + ";
	case Sub: return " - ";
	case BitOr: return " | ";
	case Mul: return " * ";
	case Div: return " / ";
	case Mod: return " % ";
	case BitAnd: return " & ";
	case Not: return "!";
	case Or: return " || ";
	case And: return " && ";
	case Less: return " < ";
	case Leq: return " <= ";
	case Greater: return " > ";
	case Geq: return " >= ";
	case Eq: return " == ";
	case Neq: return " != ";
	default: return nullptr;
	}
}

} // namespace

std::string printExpr(Script &s, uint8_t stopToken) {
	std::string out;
	int depth = 0;
	while (s.pos() < s.size()) {
		bool complete;
		out += printVarBase(s, false, complete);

		uint8_t op = s.readByte();
		if (op >= ArrayInt8 && op <= Func) {
			out += printOperand(s, op);
			continue;
		}
		if (op == BeginExpr) {
			depth++;
			out += "(";
			continue;
		}
		if (const char *t = operatorText(op)) {
			out += t;
			continue;
		}
		if (op == EndExpr) {
			depth--;
			if (stopToken == EndExpr && depth < 0)
				return out;
			out += ")";
			continue;
		}
		if (op == stopToken)
			return out;
		out += " ?" + std::to_string(op) + " ";
	}
	return out + " <eof>";
}

std::string printVarIndex(Script &s) {
	bool complete;
	std::string out = printVarBase(s, true, complete);
	if (complete)
		return out;
	uint8_t op = s.readByte();
	switch (op) {
	case ArrayInt8:
	case ArrayInt16:
	case ArrayInt32:
	case ArrayStr:
	case LoadVarInt16:
	case LoadVarInt8:
	case LoadVarInt32:
	case LoadVarInt32AsInt16:
	case LoadVarStr:
		return out + printOperand(s, op);
	default:
		return out + "?var" + std::to_string(op);
	}
}

} // namespace gob
