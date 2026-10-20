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
	enum class Kind { ObjState, ObjType, Pocket, ItemGone, ItemPresent, ItemCells, Var, Exit, GoblinX, GoblinY };
	Kind kind = Kind::Exit;
	int index = 0;           // object, item, variable slot or goblin
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
	// Earlier steps this one can't be done without: when it is the step to
	// hint and one of them isn't done (the player went past it another
	// way), that one is hinted instead.
	std::vector<std::string> needs;
	std::string nudge, pointer, solve;
};

// A trap of the game itself: a state from which the level can no longer be
// won (the player has to start it again). Shown to the player, unasked,
// while it holds.
struct HintTrap {
	std::string id;
	std::vector<std::vector<HintCondition>> when; // holds when every condition of any one line does
	std::string text;
};

struct HintLevel {
	std::string level; // the level's script name (AVT003), as Interpreter::currentLevel()
	std::string title;
	std::vector<HintStep> steps;
	std::vector<HintTrap> traps;
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
	// that is done (steps.size() when the last one is), or the first step it
	// needs that isn't done. See HintTracker.
	static size_t current(const HintLevel &level, const Interpreter &in);
	// What a step's conditions look at, as now (the values they compare).
	static std::vector<int32_t> facts(const HintStep &step, const Interpreter &in);
	// The same for any list of condition lines (a step's done, a trap's when).
	static bool holds(const std::vector<std::vector<HintCondition>> &lines, const Interpreter &in);
	static std::vector<int32_t> facts(const std::vector<std::vector<HintCondition>> &lines, const Interpreter &in);

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
	// currentLevel, levelStarts) and, at a new run (as soon as the level's
	// script file has loaded), notes what each step looks at then.
	void update(const HintBook &book, const Interpreter &in);
	// The level being played, if it has hints (null otherwise).
	const HintLevel *level() const { return _shown; }
	// The step to hint: the one after the last step done in this run (or a
	// step it needs that isn't done).
	size_t current(const Interpreter &in) const;
	// The level's trap that holds in this run (as for steps, one that held
	// when the run began counts once something it looks at has changed), or
	// null.
	const HintTrap *trap(const Interpreter &in) const;

private:
	void begin(const HintLevel &level, const Interpreter &in); // a new run: what each step looks at now
	const HintLevel *_level = nullptr, *_shown = nullptr;
	std::vector<std::vector<int32_t>> _trapStart;
	std::vector<char> _trapStale;
	uint32_t _run = 0;
	std::vector<std::vector<int32_t>> _start; // per step: facts() when the run began
	std::vector<char> _stale;                 // per step: done then, and nothing changed since
};

const char *hintActionName(HintStep::Action a);

} // namespace gob
