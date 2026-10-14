// gob2native - TOT bytecode disassembler
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage:
//   gobdis <gamedir> <TOTBASE>     print the disassembly of one script file
//   gobdis <gamedir> --all         verify every TOT and print a summary
// The game is detected from the folder (printed to stderr); --game
// gob1|gob2|gob3 picks it.

#include "data/datafiles.h"
#include "engine/game.h"
#include "formats/tot.h"
#include "script/disasm.h"

#include <cstdio>
#include <string>

using namespace gob;

static bool disassemble(DataFiles &files, const std::string &base, bool print, DisasmStats &total) {
	TotFile tot;
	if (!tot.load(files, base)) {
		std::fprintf(stderr, "cannot load %s.TOT\n", base.c_str());
		return false;
	}

	Disassembler dis(tot);
	std::vector<DisasmLine> lines = dis.run();
	const DisasmStats &st = dis.stats();

	if (print) {
		for (const DisasmLine &l : lines) {
			if (l.text.empty()) {
				std::printf("\n");
				continue;
			}
			std::printf("%05X  %*s%s\n", l.offset, l.depth * 2, "", l.text.c_str());
		}
	} else {
		for (const DisasmLine &l : lines)
			if (l.text.rfind("!!", 0) == 0)
				std::printf("%s %05X %s\n", base.c_str(), l.offset, l.text.c_str());
	}

	std::fprintf(print ? stderr : stdout, "%-8s functions %4d  code blocks %5d  hotspot blocks %3d  instructions %6d  errors %d\n",
	             base.c_str(), st.functions, st.codeBlocks, st.hotspotBlocks, st.instructions, st.errors);

	total.functions += st.functions;
	total.codeBlocks += st.codeBlocks;
	total.hotspotBlocks += st.hotspotBlocks;
	total.instructions += st.instructions;
	total.errors += st.errors;
	return st.errors == 0;
}

int main(int argc, char **argv) {
	const std::string gameKey = takeGameOption(argc, argv);
	if (argc < 3) {
		std::fprintf(stderr, "usage: %s <gamedir> <TOTBASE | --all> [--game gob1|gob2|gob3]\n", argv[0]);
		return 2;
	}
	if (!selectToolGame(argv[1], gameKey))
		return 2;

	DataFiles files(argv[1]);
	if (files.openAllArchives() == 0) {
		std::fprintf(stderr, "no STK archives found in %s\n", argv[1]);
		return 1;
	}

	DisasmStats total;
	std::string target = argv[2];
	if (target != "--all")
		return disassemble(files, target, true, total) ? 0 : 1;

	bool ok = true;
	for (const std::string &name : files.list())
		if (name.size() > 4 && name.ends_with(".TOT"))
			ok &= disassemble(files, name.substr(0, name.size() - 4), false, total);

	std::printf("TOTAL    functions %4d  code blocks %5d  hotspot blocks %3d  instructions %6d  errors %d\n",
	            total.functions, total.codeBlocks, total.hotspotBlocks, total.instructions, total.errors);
	return ok ? 0 : 1;
}
