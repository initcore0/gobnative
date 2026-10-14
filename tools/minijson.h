// gob2native - a minimal JSON reader for the tools' own files
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Enough JSON to read back what the tools write (objects.json): objects,
// arrays, strings (\uXXXX below 0x100 kept as one byte), numbers, true,
// false, null. Not a validating parser.

#pragma once

#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gob::json {

struct Value {
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;
	bool b = false;
	double n = 0;
	std::string s;
	std::vector<Value> a;
	std::map<std::string, Value> o;

	const Value &operator[](const std::string &key) const {
		static const Value none;
		auto it = o.find(key);
		return it != o.end() ? it->second : none;
	}
	int asInt(int def = 0) const { return type == Number ? int(n) : def; }
	std::string asString(const std::string &def = {}) const { return type == String ? s : def; }
};

class Parser {
public:
	explicit Parser(const std::string &text) : _t(text) {}

	bool parse(Value &out) {
		_ok = true;
		out = value();
		ws();
		return _ok && _p == _t.size();
	}

private:
	const std::string &_t;
	size_t _p = 0;
	bool _ok = true;

	void ws() {
		while (_p < _t.size() && std::isspace(static_cast<unsigned char>(_t[_p])))
			_p++;
	}
	bool eat(char c) {
		ws();
		if (_p < _t.size() && _t[_p] == c) {
			_p++;
			return true;
		}
		return false;
	}
	std::string str() {
		std::string s;
		if (!eat('"')) {
			_ok = false;
			return s;
		}
		while (_p < _t.size() && _t[_p] != '"') {
			char c = _t[_p++];
			if (c == '\\' && _p < _t.size()) {
				char e = _t[_p++];
				switch (e) {
				case 'n': s += '\n'; break;
				case 't': s += '\t'; break;
				case 'r': s += '\r'; break;
				case 'b': s += '\b'; break;
				case 'f': s += '\f'; break;
				case 'u':
					if (_p + 4 <= _t.size()) {
						unsigned v = unsigned(std::strtoul(_t.substr(_p, 4).c_str(), nullptr, 16));
						_p += 4;
						s += v < 0x100 ? char(v) : '?';
					}
					break;
				default: s += e;
				}
			} else
				s += c;
		}
		if (_p >= _t.size())
			_ok = false;
		_p++;
		return s;
	}
	Value value() {
		Value v;
		ws();
		if (_p >= _t.size()) {
			_ok = false;
			return v;
		}
		const char c = _t[_p];
		if (c == '{') {
			_p++;
			v.type = Value::Object;
			if (eat('}'))
				return v;
			do {
				std::string k = str();
				if (!eat(':')) {
					_ok = false;
					return v;
				}
				v.o[k] = value();
			} while (_ok && eat(','));
			if (!eat('}'))
				_ok = false;
		} else if (c == '[') {
			_p++;
			v.type = Value::Array;
			if (eat(']'))
				return v;
			do
				v.a.push_back(value());
			while (_ok && eat(','));
			if (!eat(']'))
				_ok = false;
		} else if (c == '"') {
			v.type = Value::String;
			v.s = str();
		} else if (_t.compare(_p, 4, "true") == 0) {
			v.type = Value::Bool;
			v.b = true;
			_p += 4;
		} else if (_t.compare(_p, 5, "false") == 0) {
			v.type = Value::Bool;
			_p += 5;
		} else if (_t.compare(_p, 4, "null") == 0) {
			_p += 4;
		} else {
			char *end = nullptr;
			v.type = Value::Number;
			v.n = std::strtod(_t.c_str() + _p, &end);
			if (end == _t.c_str() + _p)
				_ok = false;
			_p = size_t(end - _t.c_str());
		}
		return v;
	}
};

} // namespace gob::json
