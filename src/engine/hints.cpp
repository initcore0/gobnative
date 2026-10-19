// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hint files and which step to hint (engine/hints.h).

#include "engine/hints.h"

#include "engine/goblins_v1.h"
#include "engine/interpreter.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace gob {

namespace {

std::string trim(const std::string &s) {
	size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
	return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string upper(std::string s) {
	for (char &c : s)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

std::vector<std::string> words(const std::string &s) {
	std::vector<std::string> out;
	std::istringstream in(s);
	for (std::string w; in >> w;)
		out.push_back(w);
	return out;
}

bool number(const std::string &s, int32_t &out) {
	if (s.empty())
		return false;
	char *end = nullptr;
	long v = std::strtol(s.c_str(), &end, 0);
	if (*end)
		return false;
	out = int32_t(v);
	return true;
}

// One condition: obj N state S | obj N type T | pocket ID | item ID gone |
// item ID present | item ID cells N | var SLOT V | var SLOT LO HI | exit.
bool parseCondition(const std::vector<std::string> &w, HintCondition &c) {
	int32_t a = 0, b = 0, d = 0;
	if (w.size() == 1 && w[0] == "exit") {
		c.kind = HintCondition::Kind::Exit;
		return true;
	}
	if (w.size() == 4 && w[0] == "obj" && number(w[1], a) && number(w[3], b) && (w[2] == "state" || w[2] == "type")) {
		c.kind = w[2] == "state" ? HintCondition::Kind::ObjState : HintCondition::Kind::ObjType;
		c.index = a;
		c.lo = c.hi = b;
		return true;
	}
	if (w.size() == 2 && w[0] == "pocket" && number(w[1], a)) {
		c.kind = HintCondition::Kind::Pocket;
		c.lo = c.hi = a;
		return true;
	}
	if (w.size() == 3 && w[0] == "item" && number(w[1], a) && (w[2] == "gone" || w[2] == "present")) {
		c.kind = w[2] == "gone" ? HintCondition::Kind::ItemGone : HintCondition::Kind::ItemPresent;
		c.index = a;
		return true;
	}
	if (w.size() == 4 && w[0] == "item" && number(w[1], a) && w[2] == "cells" && number(w[3], b)) {
		c.kind = HintCondition::Kind::ItemCells;
		c.index = a;
		c.lo = c.hi = b;
		return true;
	}
	if ((w.size() == 3 || w.size() == 4) && w[0] == "var" && number(w[1], a) && number(w[2], b) &&
	    (w.size() == 3 || number(w[3], d))) {
		c.kind = HintCondition::Kind::Var;
		c.index = a;
		c.lo = b;
		c.hi = w.size() == 4 ? d : b;
		return true;
	}
	return false;
}

// The value a condition compares (ItemGone/ItemPresent: the item's cells).
int32_t fact(const HintCondition &c, const Interpreter &in) {
	const GoblinsV1 *g = in.goblinsV1();
	switch (c.kind) {
	case HintCondition::Kind::ObjState:
	case HintCondition::Kind::ObjType: {
		const GoblinsV1::Object *o = g ? g->object(c.index) : nullptr;
		return !o ? -1000 : c.kind == HintCondition::Kind::ObjState ? o->state : o->type;
	}
	case HintCondition::Kind::Pocket:
		return g ? g->itemIdInPocket() : -1000;
	case HintCondition::Kind::ItemGone:
	case HintCondition::Kind::ItemPresent:
	case HintCondition::Kind::ItemCells: {
		int cells = 0;
		if (g)
			for (const GoblinsV1::ItemSpot &s : g->itemSpots())
				if (s.id == c.index)
					for (uint32_t r : s.rows)
						cells += std::popcount(r);
		return cells;
	}
	case HintCondition::Kind::Var:
		return int32_t(in.variables().slot(uint32_t(c.index)));
	case HintCondition::Kind::Exit:
		return int32_t(in.variables().slot(136));
	}
	return 0;
}

bool holds(const HintCondition &c, const Interpreter &in) {
	const GoblinsV1 *g = in.goblinsV1();
	switch (c.kind) {
	case HintCondition::Kind::ObjState:
	case HintCondition::Kind::ObjType: {
		const GoblinsV1::Object *o = g ? g->object(c.index) : nullptr;
		return o && (c.kind == HintCondition::Kind::ObjState ? o->state : o->type) == c.lo;
	}
	case HintCondition::Kind::Pocket:
		return g && g->itemIdInPocket() == c.lo;
	case HintCondition::Kind::ItemGone:
	case HintCondition::Kind::ItemPresent: {
		bool present = false;
		if (g)
			for (const GoblinsV1::ItemSpot &s : g->itemSpots())
				present |= s.id == c.index;
		return present == (c.kind == HintCondition::Kind::ItemPresent);
	}
	case HintCondition::Kind::ItemCells: {
		int cells = 0;
		if (g)
			for (const GoblinsV1::ItemSpot &s : g->itemSpots())
				if (s.id == c.index)
					for (uint32_t r : s.rows)
						cells += std::popcount(r);
		return cells == c.lo;
	}
	case HintCondition::Kind::Var: {
		const int32_t v = int32_t(in.variables().slot(uint32_t(c.index)));
		return v >= c.lo && v <= c.hi;
	}
	case HintCondition::Kind::Exit:
		return in.variables().slot(136) == 1; // the exit sign shows (engine/levels_v1.cpp)
	}
	return false;
}

} // namespace

const char *hintActionName(HintStep::Action a) {
	switch (a) {
	case HintStep::Action::Walk: return "walk";
	case HintStep::Action::Act: return "act";
	case HintStep::Action::Pick: return "pick";
	case HintStep::Action::Use: return "use";
	default: return "";
	}
}

bool HintBook::parse(const std::string &text, std::string &error) {
	_levels.clear();
	std::istringstream in(text);
	std::string raw;
	int n = 0;
	auto fail = [&](const std::string &why) {
		error = "line " + std::to_string(n) + ": " + why;
		_levels.clear();
		return false;
	};
	while (std::getline(in, raw)) {
		n++;
		const std::string line = trim(raw);
		if (line.empty() || line[0] == '#')
			continue;
		const size_t sp = line.find_first_of(" \t");
		const std::string key = line.substr(0, sp);
		const std::string rest = sp == std::string::npos ? std::string() : trim(line.substr(sp));
		if (key == "level") {
			const size_t sp2 = rest.find_first_of(" \t");
			HintLevel l;
			l.level = upper(rest.substr(0, sp2));
			l.title = sp2 == std::string::npos ? std::string() : trim(rest.substr(sp2));
			if (l.level.empty())
				return fail("level without a name");
			if (level(l.level))
				return fail("level " + l.level + " twice");
			_levels.push_back(std::move(l));
			continue;
		}
		if (_levels.empty())
			return fail("'" + key + "' before the first level");
		HintLevel &l = _levels.back();
		if (key == "step") {
			if (rest.empty() || rest.find_first_of(" \t") != std::string::npos)
				return fail("a step takes one id");
			for (const HintStep &s : l.steps)
				if (s.id == rest)
					return fail("step " + rest + " twice in " + l.level);
			l.steps.push_back({});
			l.steps.back().id = rest;
			continue;
		}
		if (l.steps.empty())
			return fail("'" + key + "' before the level's first step");
		HintStep &s = l.steps.back();
		if (key == "done") {
			std::vector<HintCondition> all;
			std::string part;
			std::istringstream parts(rest);
			while (std::getline(parts, part, ',')) {
				HintCondition c;
				if (!parseCondition(words(part), c))
					return fail("unknown condition '" + trim(part) + "'");
				all.push_back(c);
			}
			if (all.empty())
				return fail("done without a condition");
			s.done.push_back(std::move(all));
		} else if (key == "goblin") {
			int32_t v = 0;
			if (!number(rest, v) || v < 0 || v > 2)
				return fail("goblin takes 0, 1 or 2");
			s.goblin = v;
		} else if (key == "action") {
			static const std::pair<const char *, HintStep::Action> kActions[] = {
				{"walk", HintStep::Action::Walk}, {"act", HintStep::Action::Act},
				{"pick", HintStep::Action::Pick}, {"use", HintStep::Action::Use}};
			auto it = std::find_if(std::begin(kActions), std::end(kActions),
			                       [&](const auto &a) { return rest == a.first; });
			if (it == std::end(kActions))
				return fail("action takes walk, act, pick or use");
			s.action = it->second;
		} else if (key == "item") {
			int32_t v = 0;
			if (!number(rest, v) || v <= 0 || v > 255)
				return fail("item takes an item id");
			s.item = v;
		} else if (key == "at") {
			const std::vector<std::string> w = words(rest);
			int32_t x = 0, y = 0;
			if (w.size() != 2 || !number(w[0], x) || !number(w[1], y))
				return fail("at takes X Y");
			s.pointX = x;
			s.pointY = y;
		} else if (key == "nudge" || key == "pointer" || key == "solve") {
			std::string &t = key == "nudge" ? s.nudge : key == "pointer" ? s.pointer : s.solve;
			t += (t.empty() ? "" : " ") + rest; // a text may go on over several lines
		} else {
			return fail("unknown keyword '" + key + "'");
		}
	}
	for (const HintLevel &l : _levels)
		for (const HintStep &s : l.steps)
			if (s.done.empty() || s.nudge.empty() || s.pointer.empty() || s.solve.empty()) {
				error = l.level + " step " + s.id + ": needs done, nudge, pointer and solve";
				_levels.clear();
				return false;
			}
	return true;
}

const HintLevel *HintBook::level(const std::string &name) const {
	const std::string u = upper(name);
	for (const HintLevel &l : _levels)
		if (l.level == u)
			return &l;
	return nullptr;
}

bool HintBook::done(const HintStep &step, const Interpreter &in) {
	for (const std::vector<HintCondition> &line : step.done)
		if (std::all_of(line.begin(), line.end(), [&](const HintCondition &c) { return holds(c, in); }))
			return true;
	return false;
}

std::vector<int32_t> HintBook::facts(const HintStep &step, const Interpreter &in) {
	std::vector<int32_t> out;
	for (const std::vector<HintCondition> &line : step.done)
		for (const HintCondition &c : line)
			out.push_back(fact(c, in));
	return out;
}

void HintTracker::update(const HintBook &book, const Interpreter &in) {
	const std::string name = in.currentLevel();
	_shown = name.empty() ? nullptr : book.level(name);
	if (!_shown)
		return; // (between levels, or a moment the level isn't running: keep the run)
	const uint32_t run = in.levelStarts();
	const std::vector<HintStep> &steps = _shown->steps;
	if (_shown != _level || run != _run) {
		_level = _shown;
		_run = run;
		_start.clear();
		_stale.clear();
		for (const HintStep &s : steps) {
			_start.push_back(HintBook::facts(s, in));
			_stale.push_back(HintBook::done(s, in));
		}
		return;
	}
	for (size_t i = 0; i < steps.size(); i++)
		if (_stale[i] && HintBook::facts(steps[i], in) != _start[i])
			_stale[i] = false;
}

size_t HintTracker::current(const Interpreter &in) const {
	if (!_shown)
		return 0;
	for (size_t i = _shown->steps.size(); i > 0; i--)
		if (!_stale[i - 1] && HintBook::done(_shown->steps[i - 1], in))
			return i;
	return 0;
}

size_t HintBook::current(const HintLevel &level, const Interpreter &in) {
	for (size_t i = level.steps.size(); i > 0; i--)
		if (done(level.steps[i - 1], in))
			return i;
	return 0;
}

} // namespace gob
