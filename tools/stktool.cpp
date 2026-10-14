// gob2native - STK archive inspection tool
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage:
//   stktool list <archive.stk>
//   stktool extract <archive.stk> <outdir>

#include "formats/stk.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static int list(gob::StkArchive &stk) {
	std::printf("%-14s %10s %10s %s\n", "name", "stored", "unpacked", "pack");
	for (const gob::StkEntry &e : stk.entries()) {
		size_t unpacked = e.size;
		std::string note;
		try {
			unpacked = stk.read(e).size();
		} catch (const std::exception &ex) {
			note = std::string(" ERROR: ") + ex.what();
		}
		std::printf("%-14s %10u %10zu %u%s\n", e.name.c_str(), e.size, unpacked, e.compression, note.c_str());
	}
	std::printf("%zu files\n", stk.entries().size());
	return 0;
}

static int extract(gob::StkArchive &stk, const fs::path &outDir) {
	fs::create_directories(outDir);
	int failures = 0;
	for (const gob::StkEntry &e : stk.entries()) {
		try {
			std::vector<uint8_t> data = stk.read(e);
			std::ofstream out(outDir / e.name, std::ios::binary);
			out.write(reinterpret_cast<const char *>(data.data()), data.size());
		} catch (const std::exception &ex) {
			std::fprintf(stderr, "%s: %s\n", e.name.c_str(), ex.what());
			failures++;
		}
	}
	std::printf("extracted %zu files to %s (%d failed)\n", stk.entries().size() - failures, outDir.string().c_str(), failures);
	return failures ? 1 : 0;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		std::fprintf(stderr, "usage: %s list <archive.stk>\n       %s extract <archive.stk> <outdir>\n", argv[0], argv[0]);
		return 2;
	}

	std::string cmd = argv[1];
	gob::StkArchive stk;
	if (!stk.open(argv[2])) {
		std::fprintf(stderr, "cannot open %s\n", argv[2]);
		return 1;
	}

	if (cmd == "list")
		return list(stk);
	if (cmd == "extract" && argc >= 4)
		return extract(stk, argv[3]);

	std::fprintf(stderr, "unknown command\n");
	return 2;
}
