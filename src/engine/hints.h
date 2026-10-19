// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Hints: for each level, the puzzle's steps in the order a player solves
// them, each with when it counts as done (facts the engine can see: a level
// object's state, the pocket, the items on the map, a variable), who does
// it and on what (for highlights), and three levels of help (a nudge, a
// pointer, the solution). The step to hint is the one after the last step
// that is done, worked out from the state each time, so it follows the
// player through solving out of order, quick loads and level jumps.
//
// Gobliiins only for now. The text format (assets/hints/gob1.txt) is
// described there. tests/gob1/*.txt check it against the walkthrough:
// before each step's actions, `hint ID` asserts that ID is the step hinted.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gob {

class Interpreter;

struct HintCondition {
	enum class Kind { ObjState, ObjType, Pocket, ItemGone, ItemPresent, ItemCells, Var, Exit };
	Kind kind = Kind::Exit;
	int index = 0;           // object, item or variable slot
	int32_t lo = 0, hi = 0;  // the value (lo..hi for Var; cells for ItemCells)
};

struct HintStep {
	std::string id;
	// Done when every condition of any one line holds.
	std::vector<std::vector<HintCondition>> done;
	int goblin = -1; // who does it (Gobliiins: 0 picker, 1 fighter, 2 wizard), -1: anyone
	enum class Action { None, Walk, Act, Pick, Use };
	Action action = Action::None;
	int item = -1;           // the item on the map to act on (highlighted), -1: none
	int pointX = -1, pointY = -1; // or a screen point (highlighted), -1: none
	std::string nudge, pointer, solve;
};

struct HintLevel {
	std::string level; // the level's script name (AVT003), as Interpreter::currentLevel()
	std::string title;
	std::vector<HintStep> steps;
};

class HintBook {
public:
	// Parses a hint file; false with the first error ("line N: ...").
	bool parse(const std::string &text, std::string &error);

	const std::vector<HintLevel> &levels() const { return _levels; }
	// The level `name` (case-insensitive), null when it has no hints.
	const HintLevel *level(const std::string &name) const;

	// Whether step `step` of `level` is done in the running game.
	static bool done(const HintStep &step, const Interpreter &in);
	// The step to hint in the running game: the one after the last step
	// that is done (steps.size() when the last one is). See HintTracker.
	static size_t current(const HintLevel &level, const Interpreter &in);
	// What a step's conditions look at, as now (the values they compare).
	static std::vector<int32_t> facts(const HintStep &step, const Interpreter &in);

private:
	std::vector<HintLevel> _levels;
};

// The step to hint in one run of a level. A level's script doesn't clear
// every variable it uses when it starts again (a jump to it, AGAIN after the
// goblins collapse), so a step can look done from the start: such a step
// counts only once something it looks at has changed in this run. A quick
// load, put in place after the level has started, counts in full.
class HintTracker {
public:
	// Once a frame: follows the level being played (Interpreter::
	// currentLevel, levelStarts) and, at a new run, notes what each step
	// looked at then.
	void update(const HintBook &book, const Interpreter &in);
	// The level being played, if it has hints (null otherwise).
	const HintLevel *level() const { return _shown; }
	// The step to hint: the one after the last step done in this run.
	size_t current(const Interpreter &in) const;

private:
	const HintLevel *_level = nullptr, *_shown = nullptr;
	uint32_t _run = 0;
	std::vector<std::vector<int32_t>> _start; // per step: facts() when the run began
	std::vector<char> _stale;                 // per step: done then, and nothing changed since
};

const char *hintActionName(HintStep::Action a);

} // namespace gob
