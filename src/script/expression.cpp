// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "script/expression.h"

#include <algorithm>
#include <cstdlib>

namespace gob {

using namespace tok;

namespace {

bool isOperand(uint8_t op) { return op >= ArrayInt8 && op <= Func; }

} // namespace

// Reads the dimension indices that follow an array token and folds them into
// a flat element offset. Each index is clamped to its dimension, as the
// original does.
int Expression::arrayOffset(const uint8_t *dims, int dimCount) {
	int offset = 0;
	for (int d = 0; d < dimCount; d++) {
		int idx = std::clamp<int>(parseVal(EndMarker), 0, std::max(0, dims[d] - 1));
		offset = offset * dims[d] + idx;
	}
	return offset;
}

// Optional prefix tokens (14/15) that add a base offset to the following
// variable access; used for structure-like data. With `mindStop`, returns
// true when the prefix itself is the whole reference.
bool Expression::varBase(uint32_t &base, bool mindStop, VarRef *ref) {
	base = 0;
	uint8_t op = _script->peekByte();
	while (op == VarBase || op == VarBaseArray) {
		_script->skip(1);
		if (op == VarBase) {
			base += uint32_t(_script->readInt16()) * 4;
			if (ref) {
				ref->size = _script->peekUint16();
				ref->type = VarBase;
			}
			_script->skip(2);
		} else {
			base += uint32_t(_script->readInt16()) * 4;
			uint16_t elemSize = _script->readUint16();
			if (ref) {
				ref->size = elemSize;
				ref->type = VarBaseArray;
			}
			uint8_t dimCount = _script->readByte();
			std::vector<uint8_t> dims(dimCount);
			for (auto &d : dims)
				d = _script->readByte();
			int idx = arrayOffset(dims.data(), dimCount);
			base += uint32_t(idx) * elemSize * 4;
		}

		if (_script->peekByte() != VarBaseCont) {
			if (mindStop)
				return true;
		} else {
			_script->skip(1);
		}
		op = _script->peekByte();
	}
	return false;
}

VarRef Expression::parseVarIndex() {
	VarRef ref;
	uint32_t base;
	if (varBase(base, true, &ref)) {
		ref.offset = base;
		return ref;
	}

	uint8_t op = _script->readByte();
	ref.size = 0;
	ref.type = op;

	switch (op) {
	case ArrayInt8:
	case ArrayInt32:
	case ArrayInt16:
	case ArrayStr: {
		int16_t start = _script->readInt16();
		uint8_t dimCount = _script->readByte();
		std::vector<uint8_t> dims(dimCount);
		for (auto &d : dims)
			d = _script->readByte();
		int offset = arrayOffset(dims.data(), dimCount);
		if (op == ArrayInt8)
			ref.offset = base + start + offset;
		else if (op == ArrayInt32)
			ref.offset = base + (start + offset) * 4;
		else if (op == ArrayInt16)
			ref.offset = base + (start + offset) * 2;
		else {
			int32_t s = start * 4;
			if (_script->peekByte() == StrIndex) {
				_script->skip(1);
				s += parseVal(EndMarker);
			}
			ref.offset = base + offset * 4 * _animDataSize + s;
		}
		break;
	}
	case LoadVarInt16:
		ref.offset = base + _script->readUint16() * 2;
		break;
	case LoadVarInt8:
		ref.offset = base + _script->readUint16();
		break;
	case LoadVarInt32:
	case LoadVarInt32AsInt16:
	case LoadVarStr: {
		uint32_t off = _script->readUint16() * 4;
		if (op == LoadVarStr && _script->peekByte() == StrIndex) {
			_script->skip(1);
			off += parseVal(EndMarker);
		}
		ref.offset = base + off;
		break;
	}
	default:
		ref.offset = 0;
		break;
	}
	return ref;
}

void Expression::loadValue(uint8_t op, uint32_t base, Stack &st) {
	Slot &s = st.at(0);
	s.str.clear();

	switch (op) {
	case ArrayInt8:
	case ArrayInt32:
	case ArrayInt16:
	case ArrayStr: {
		s.op = op == ArrayStr ? LoadImmStr : LoadImmInt16;
		int16_t start = _script->readInt16();
		uint8_t dimCount = _script->readByte();
		std::vector<uint8_t> dims(dimCount);
		for (auto &d : dims)
			d = _script->readByte();
		int offset = arrayOffset(dims.data(), dimCount);
		if (op == ArrayInt8)
			s.val = int8_t(_vars->read8(base + start + offset));
		else if (op == ArrayInt32)
			s.val = int32_t(_vars->read32(base + start * 4 + offset * 4));
		else if (op == ArrayInt16)
			s.val = int16_t(_vars->read16(base + start * 2 + offset * 2));
		else {
			uint32_t strOff = base + start * 4 + offset * _animDataSize * 4;
			s.str = _vars->readString(strOff);
			if (_script->peekByte() == StrIndex) {
				_script->skip(1);
				int idx = parseVal(EndMarker);
				s.op = LoadImmInt16;
				s.val = _vars->read8(base + start * 4 + offset * 4 * _animDataSize + idx);
			}
		}
		break;
	}
	case LoadVarInt16:
		s.op = LoadImmInt16;
		s.val = int16_t(_vars->read16(base + _script->readUint16() * 2));
		break;
	case LoadVarInt8:
		s.op = LoadImmInt16;
		s.val = int8_t(_vars->read8(base + _script->readUint16()));
		break;
	case LoadImmInt32:
		s.op = LoadImmInt16;
		s.val = _script->readInt32();
		break;
	case LoadImmInt16:
		s.op = LoadImmInt16;
		s.val = _script->readInt16();
		break;
	case LoadImmInt8:
		s.op = LoadImmInt16;
		s.val = _script->readInt8();
		break;
	case LoadImmStr:
		s.op = LoadImmStr;
		s.str = _script->readString();
		break;
	case LoadVarInt32:
		s.op = LoadImmInt16;
		s.val = int32_t(_vars->read32(base + _script->readUint16() * 4));
		break;
	case LoadVarInt32AsInt16:
		s.op = LoadImmInt16;
		s.val = int16_t(_vars->read16(base + _script->readUint16() * 4));
		break;
	case LoadVarStr: {
		s.op = LoadImmStr;
		uint32_t off = _script->readUint16() * 4;
		s.str = _vars->readString(base + off);
		if (_script->peekByte() == StrIndex) {
			_script->skip(1);
			off += parseVal(EndMarker);
			s.op = LoadImmInt16;
			s.val = _vars->read8(base + off);
		}
		break;
	}
	case Func: {
		uint8_t fn = _script->readByte();
		// Nested parse clobbers the result registers, as in the original.
		parse(EndExpr);
		int32_t v = _resultInt;
		switch (fn) {
		case FuncSqrt1:
		case FuncSqrt2:
		case FuncSqrt3: {
			int32_t cur = 1, prev = 1, prevPrev;
			do {
				prevPrev = prev;
				prev = cur;
				cur = (cur + v / cur) / 2;
			} while (cur != prev && cur != prevPrev);
			v = cur;
			break;
		}
		case FuncSqr: v = v * v; break;
		case FuncAbs: v = v < 0 ? -v : v; break;
		case FuncRand: v = _random ? _random(v) : _ownRandom.below(v); break;
		default: break;
		}
		_resultInt = v;
		// Re-fetch: the nested parse may have reused this slot's storage.
		Slot &out = st.at(0);
		out.op = LoadImmInt16;
		out.val = v;
		out.str.clear();
		return;
	}
	default:
		break;
	}
}

// String concatenation materializes into the result-string register; the
// stack slot then holds a copy (the original stored a pointer to it).
static void concat(std::string &resultStr, std::string &dst, const std::string &src) {
	resultStr = dst + src;
	dst = resultStr;
}

void Expression::simpleArithmetic1(Stack &st) {
	switch (st.at(-1).op) {
	case Add:
		if (st.at(-2).op == LoadImmStr) {
			concat(_resultStr, st.at(-2).str, st.at(0).str);
			st.pop(2);
		}
		break;
	case Mul: st.at(-2).val *= st.at(0).val; st.pop(2); break;
	case Div: if (st.at(0).val) st.at(-2).val /= st.at(0).val; st.pop(2); break;
	case Mod: if (st.at(0).val) st.at(-2).val %= st.at(0).val; st.pop(2); break;
	case BitAnd: st.at(-2).val &= st.at(0).val; st.pop(2); break;
	default: break;
	}
}

void Expression::simpleArithmetic2(Stack &st) {
	if (st.pos > 1) {
		if (st.at(-2).op == Neg) {
			st.at(-2).op = LoadImmInt16;
			st.at(-2).val = -st.at(-1).val;
			st.pop();
		} else if (st.at(-2).op == Not) {
			st.at(-2).op = (st.at(-1).op == False) ? True : False;
			st.pop();
		}
	}

	if (st.pos > 2) {
		switch (st.at(-2).op) {
		case Mul: st.at(-3).val *= st.at(-1).val; st.pop(2); break;
		case Div: if (st.at(-1).val) st.at(-3).val /= st.at(-1).val; st.pop(2); break;
		case Mod: if (st.at(-1).val) st.at(-3).val %= st.at(-1).val; st.pop(2); break;
		case BitAnd: st.at(-3).val &= st.at(-1).val; st.pop(2); break;
		default: break;
		}
	}
}

int Expression::compare(Stack &st) {
	Slot &a = st.at(-3);
	Slot &b = st.at(-1);
	if (a.op == LoadImmInt16)
		return int(a.val) - int(b.val);
	if (a.op == LoadImmStr) {
		_resultStr = a.str;
		return _resultStr.compare(b.str);
	}
	return 0;
}

bool Expression::complexArithmetic(Stack &st, int brackStart) {
	Slot &start = st.slots[brackStart];
	switch (st.at(-2).op) {
	case Add:
		if (start.op == LoadImmInt16)
			start.val += st.at(-1).val;
		else if (start.op == LoadImmStr)
			concat(_resultStr, start.str, st.at(-1).str);
		st.pop(2);
		break;
	case Sub: start.val -= st.at(-1).val; st.pop(2); break;
	case BitOr: start.val |= st.at(-1).val; st.pop(2); break;
	case Mul: st.at(-3).val *= st.at(-1).val; st.pop(2); break;
	case Div: if (st.at(-1).val) st.at(-3).val /= st.at(-1).val; st.pop(2); break;
	case Mod: if (st.at(-1).val) st.at(-3).val %= st.at(-1).val; st.pop(2); break;
	case BitAnd: st.at(-3).val &= st.at(-1).val; st.pop(2); break;
	case Or:
		if (st.at(-3).op == False)
			st.at(-3).op = st.at(-1).op;
		st.pop(2);
		break;
	case And:
		if (st.at(-3).op == True)
			st.at(-3).op = st.at(-1).op;
		st.pop(2);
		break;
	case Less:    st.at(-3).op = compare(st) <  0 ? True : False; st.pop(2); break;
	case Leq:     st.at(-3).op = compare(st) <= 0 ? True : False; st.pop(2); break;
	case Greater: st.at(-3).op = compare(st) >  0 ? True : False; st.pop(2); break;
	case Geq:     st.at(-3).op = compare(st) >= 0 ? True : False; st.pop(2); break;
	case Eq:      st.at(-3).op = compare(st) == 0 ? True : False; st.pop(2); break;
	case Neq:     st.at(-3).op = compare(st) != 0 ? True : False; st.pop(2); break;
	default:
		return true;
	}
	return false;
}

void Expression::setResult(const Slot &s, ExprType *type) {
	uint8_t t = s.op;
	switch (s.op) {
	case Not:
		t ^= 1;
		break;
	case LoadImmInt16:
		_resultInt = s.val;
		break;
	case LoadImmStr:
		_resultStr = s.str;
		break;
	case True:
	case False:
		break;
	default:
		_resultInt = 0;
		t = LoadImmInt16;
		break;
	}
	if (type)
		*type = ExprType(t);
}

ExprType Expression::parse(uint8_t stopToken) {
	Stack st;
	ExprType type = ExprType::Int;

	while (true) {
		uint32_t base;
		varBase(base, false, nullptr);

		if (st.pos >= int(std::size(st.slots)) - 2)
			return ExprType::Int; // malformed expression; bail out safely
		st.push();

		uint8_t op = _script->readByte();
		if (isOperand(op)) {
			loadValue(op, base, st);

			if (st.pos > 0 && (st.at(-1).op == Neg || st.at(-1).op == Not)) {
				st.pop();
				if (st.at(0).op == Neg) {
					st.at(0).op = LoadImmInt16;
					st.at(0).val = -st.at(1).val;
				} else {
					st.at(0).op = (st.at(1).op == False) ? True : False;
				}
			}
			if (st.pos <= 0)
				continue;
			simpleArithmetic1(st);
			continue;
		}

		if (op == stopToken || op == Or || op == And || op == EndExpr) {
			while (st.pos >= 2) {
				if (st.at(-2).op == BeginExpr && (op == EndExpr || op == stopToken)) {
					st.at(-2).op = st.at(-1).op;
					if (st.at(-2).op == LoadImmInt16 || st.at(-2).op == LoadImmStr) {
						st.at(-2).val = st.at(-1).val;
						st.at(-2).str = st.at(-1).str;
					}
					st.pop();
					simpleArithmetic2(st);
					if (op != stopToken)
						break;
				}

				int brackStart = st.pos - 2;
				while (brackStart > 0 && st.slots[brackStart].op < Or && st.slots[brackStart].op != BeginExpr)
					brackStart--;
				if (st.slots[brackStart].op >= Or || st.slots[brackStart].op == BeginExpr)
					brackStart++;

				if (complexArithmetic(st, brackStart))
					break;
			}

			if (op == Or || op == And) {
				if (st.at(-1).op == LoadImmInt16)
					st.at(-1).op = st.at(-1).val != 0 ? True : False;

				if ((op == Or && st.at(-1).op == True) || (op == And && st.at(-1).op == False)) {
					// Short-circuit: skip the rest of this (sub)expression.
					if (st.pos > 1 && st.at(-2).op == BeginExpr) {
						skip(EndExpr);
						st.at(-2).op = st.at(-1).op;
						st.pop(2);
					} else {
						skip(stopToken);
					}
					op = _script->peekByte(-1);
					if (st.pos > 0 && st.at(-1).op == Not) {
						st.at(-1).op = (st.at(0).op == False) ? True : False;
						st.pop();
					}
				} else {
					st.at(0).op = op;
				}
			} else {
				st.pop();
			}

			if (op != stopToken)
				continue;

			setResult(st.slots[0], &type);
			return type;
		}

		if (op < Neg || op > Not) {
			if (op < Less || op > Neq)
				continue;

			// Comparison: fold a pending additive operation first.
			if (st.pos > 2) {
				if (st.at(-2).op == Add) {
					if (st.at(-3).op == LoadImmInt16)
						st.at(-3).val += st.at(-1).val;
					else if (st.at(-3).op == LoadImmStr)
						concat(_resultStr, st.at(-3).str, st.at(-1).str);
					st.pop(2);
				} else if (st.at(-2).op == Sub) {
					st.at(-3).val -= st.at(-1).val;
					st.pop(2);
				} else if (st.at(-2).op == BitOr) {
					st.at(-3).val |= st.at(-1).val;
					st.pop(2);
				}
			}
		}
		st.at(0).op = op;
	}
}

int16_t Expression::parseVal(uint8_t stopToken) {
	parse(stopToken);
	return int16_t(_resultInt);
}

int32_t Expression::evalInt() {
	parse();
	return _resultInt;
}

bool Expression::evalBool() {
	ExprType t = parse();
	return t == ExprType::True || (t == ExprType::Int && _resultInt != 0);
}

std::string Expression::evalString() {
	parse();
	return _resultStr;
}

void Expression::skip(uint8_t stopToken) {
	int depth = 0;
	while (true) {
		if (_script->pos() >= _script->size())
			return;
		uint8_t op = _script->readByte();

		if (op >= VarBase && op <= Func) {
			switch (op) {
			case VarBase:
				_script->skip(4);
				if (_script->peekByte() == VarBaseCont)
					_script->skip(1);
				break;
			case LoadVarInt16:
			case LoadVarInt8:
			case LoadImmInt16:
			case LoadVarInt32:
			case LoadVarInt32AsInt16:
				_script->skip(2);
				break;
			case LoadImmInt32:
				_script->skip(4);
				break;
			case LoadImmInt8:
				_script->skip(1);
				break;
			case LoadImmStr:
				_script->skip(int32_t(_script->peekString().size()) + 1);
				break;
			case LoadVarStr:
				_script->skip(2);
				if (_script->peekByte() == StrIndex) {
					_script->skip(1);
					skip(EndMarker);
				}
				break;
			case VarBaseArray:
				_script->skip(2);
				[[fallthrough]];
			case ArrayInt8:
			case ArrayInt32:
			case ArrayInt16:
			case ArrayStr: {
				uint8_t dimCount = _script->peekByte(2);
				_script->skip(3 + dimCount);
				for (int d = 0; d < dimCount; d++)
					skip(EndMarker);
				if (op == ArrayStr && _script->peekByte() == StrIndex) {
					_script->skip(1);
					skip(EndMarker);
				}
				break;
			}
			case Func:
				_script->skip(1);
				skip(EndExpr);
				break;
			default:
				break;
			}
			continue;
		}

		if (op == BeginExpr) {
			depth++;
			continue;
		}
		if (op == Not || (op >= Neg && op <= BitAnd))
			continue;
		if (op >= Or && op <= Neq)
			continue;
		if (op == EndExpr)
			depth--;
		if (op != stopToken)
			continue;
		if (stopToken != EndExpr || depth < 0)
			return;
	}
}

void Expression::store(const VarRef &ref, uint32_t value) {
	switch (ref.type) {
	case ArrayInt8:
	case LoadVarInt8:
		_vars->write8(ref.offset, uint8_t(value));
		break;
	case LoadVarInt16:
	case LoadVarInt32AsInt16:
	case ArrayInt16:
		_vars->write16(ref.offset, uint16_t(value));
		break;
	default:
		_vars->write32(ref.offset, value);
		break;
	}
}

void Expression::storeString(const VarRef &ref, const std::string &value) {
	_vars->writeString(ref.offset, value);
}

} // namespace gob
