// gob2native - hint file tests: the shipped hint files parse, and malformed
// ones are refused with the line at fault (engine/hints.h)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/hints.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace gob;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

const char *kStep = "level avt003 The gate\n"
                    "step kick\n"
                    "  done obj 0 state 3\n"
                    "  done var 316 1 2, pocket 2\n"
                    "  goblin 1\n"
                    "  action act\n"
                    "  at 186 63\n"
                    "  nudge Something is\n"
                    "  nudge on the gate.\n"
                    "  pointer The fighter.\n"
                    "  solve Kick it.\n";

std::string refusal(const std::string &text) {
	HintBook book;
	std::string error;
	return book.parse(text, error) ? std::string() : error;
}

void testParse() {
	HintBook book;
	std::string error;
	CHECK(book.parse(kStep, error));
	const HintLevel *l = book.level("AVT003");
	CHECK(l && book.level("avt003") == l);
	if (!l || l->steps.size() != 1)
		return;
	const HintStep &s = l->steps[0];
	CHECK(l->title == "The gate");
	CHECK(s.id == "kick" && s.goblin == 1 && s.action == HintStep::Action::Act);
	CHECK(s.pointX == 186 && s.pointY == 63);
	CHECK(s.nudge == "Something is on the gate.");
	CHECK(s.done.size() == 2 && s.done[1].size() == 2);
	CHECK(s.done[1][0].kind == HintCondition::Kind::Var && s.done[1][0].lo == 1 && s.done[1][0].hi == 2);
	CHECK(s.done[1][1].kind == HintCondition::Kind::Pocket && s.done[1][1].lo == 2);
}

void testRefused() {
	CHECK(refusal("step x\n") == "line 1: 'step' before the first level");
	CHECK(refusal(std::string(kStep) + "  done obj 1 colour 2\n") == "line 12: unknown condition 'obj 1 colour 2'");
	CHECK(refusal(std::string(kStep) + "  goblin 3\n") == "line 12: goblin takes 0, 1 or 2");
	CHECK(refusal(std::string(kStep) + "step kick\n") == "line 12: step kick twice in AVT003");
	CHECK(refusal("level A\nstep s\n  done exit\n  nudge n\n") == "A step s: needs done, nudge, pointer and solve");
}

void testShipped(const char *path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	HintBook book;
	std::string error;
	CHECK(in && book.parse(text.str(), error));
	if (!error.empty())
		std::printf("%s: %s\n", path, error.c_str());
	CHECK(book.levels().size() == 22); // Gobliiins' levels, the ending aside
}

} // namespace

int main(int argc, char **argv) {
	testParse();
	testRefused();
	if (argc > 1)
		testShipped(argv[1]);
	if (failures)
		return 1;
	std::printf("hints: OK\n");
	return 0;
}
