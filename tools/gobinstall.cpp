// gob2native - install a game from its GOG installer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: gobinstall <setup.exe> [destdir] [--list] [--floppy] [--all] [--check] [--merge]
//
// Reads a GOG installer of Gobliiins, Gobliins 2 or Goblins Quest 3
// (setup_*.exe, Inno Setup), says which game it holds and installs the
// files the game needs into destdir (default: a folder named after the
// game in the current folder), ready for gobnative. Nothing is run; the
// installer is read as data (install/inno.h, docs/install.md).
//
//   --list    show the installer's files and what would be installed
//   --floppy  also install the floppy version (FDD/)
//   --all     extract every file of the installer, named as innoextract
//             names them (for comparisons; no game check)
//   --check   decompress everything and verify the checksums, write nothing
//   --merge   write into destdir even if it already holds files

#include "install/gameinstall.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#else
#include <unistd.h>
#endif

using namespace gob;
namespace fs = std::filesystem;

namespace {

std::string megabytes(uint64_t bytes) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.1f MB", double(bytes) / (1024.0 * 1024.0));
	return buf;
}

} // namespace

int main(int argc, char **argv) {
	std::string setupPath, dest;
	bool list = false, check = false, merge = false;
	InstallOptions options;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--list"))
			list = true;
		else if (!std::strcmp(argv[i], "--floppy"))
			options.floppy = true;
		else if (!std::strcmp(argv[i], "--all"))
			options.everything = true;
		else if (!std::strcmp(argv[i], "--check"))
			check = true;
		else if (!std::strcmp(argv[i], "--merge"))
			merge = true;
		else if (argv[i][0] == '-' && argv[i][1] == '-') {
			std::fprintf(stderr, "unknown option %s\n", argv[i]);
			return 2;
		} else if (setupPath.empty())
			setupPath = argv[i];
		else if (dest.empty())
			dest = argv[i];
		else {
			std::fprintf(stderr, "too many arguments\n");
			return 2;
		}
	}
	if (setupPath.empty()) {
		std::fprintf(stderr,
		             "usage: gobinstall <setup.exe> [destdir] [--list] [--floppy] [--all] [--check] [--merge]\n"
		             "Installs Gobliiins, Gobliins 2 or Goblins Quest 3 from its GOG installer.\n");
		return 2;
	}

	GameInstaller installer;
	if (!installer.open(setupPath, options)) {
		std::fprintf(stderr, "%s: %s\n", setupPath.c_str(), installer.error().c_str());
		return 1;
	}
	const InnoSetup &setup = installer.setup();
	const GameInstallPlan &plan = installer.plan();
	std::printf("installer: \"%s\" %s, setup data version %s, %zu files\n", setup.appName().c_str(),
	            setup.appVersion().c_str(), setup.versionString().c_str(), setup.files().size());
	if (plan.game)
		std::printf("game: %s (%s; found %s)\n", plan.game->title, plan.game->key, plan.reason.c_str());
	else
		std::printf("game: none (%s)\n", plan.reason.c_str());

	if (list) {
		static const char *compressions[] = {"stored", "zlib", "bzip2", "lzma", "lzma2"};
		std::printf("compression: %s, %zu data entries, data at 0x%llx\n", compressions[int(setup.compression())],
		            setup.data().size(), (unsigned long long)setup.dataOffset());
		for (const InnoFile &f : setup.files()) {
			if (f.merged)
				continue;
			bool filter = false;
			for (uint32_t p : f.parts)
				filter |= setup.data()[p].callFilter;
			std::string notes;
			if (!f.languages.empty())
				notes += "  [" + f.languages + "]";
			if (f.deflated)
				notes += "  (" + std::to_string(f.parts.size()) + " zlib parts)";
			if (filter)
				notes += "  (x86 filter)";
			std::printf("  %-60s %12llu%s\n", f.destination.c_str(), (unsigned long long)f.size, notes.c_str());
		}
		std::printf("would install %zu files, %s:\n", plan.files.size(), megabytes(plan.bytes).c_str());
		for (const GameInstallPlan::File &f : plan.files)
			std::printf("  %s\n", f.path.c_str());
		return 0;
	}

	if (check) {
		// Every file, verified and dropped.
		InnoExtractor extractor;
		std::vector<InnoExtractor::Item> items;
		for (uint32_t i = 0; i < setup.files().size(); i++)
			if (!setup.files()[i].merged && !setup.files()[i].parts.empty())
				items.push_back({i, {}, setup.files()[i].destination});
		extractor.begin(setup, std::move(items));
		while (extractor.step(64 << 20)) {
		}
		if (extractor.failed()) {
			std::fprintf(stderr, "%s\n", extractor.error().c_str());
			return 1;
		}
		std::printf("all %zu files are intact (%s decoded)\n", extractor.filesDone(),
		            megabytes(extractor.progress()).c_str());
		return 0;
	}

	if (dest.empty())
		dest = plan.game ? plan.game->prefFolder : "setup-files";
	if (!installer.begin(dest, merge)) {
		std::fprintf(stderr, "%s\n", installer.error().c_str());
		return 1;
	}
	std::printf("installing %zu files (%s) into %s\n", plan.files.size(), megabytes(plan.bytes).c_str(),
	            dest.c_str());
	const bool tty = isatty(2);
	const auto start = std::chrono::steady_clock::now();
	int shown = -1;
	while (installer.step(8 << 20)) {
		const int percent = int(installer.fraction() * 100);
		if (tty && percent != shown) {
			std::fprintf(stderr, "\r%3d%%  %-50s", percent, installer.current().c_str());
			shown = percent;
		}
	}
	if (tty)
		std::fprintf(stderr, "\r%-60s\r", "");
	if (installer.failed()) {
		std::fprintf(stderr, "installation failed: %s\n", installer.error().c_str());
		return 1;
	}
	const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::printf("done in %.1f s: %s\n", seconds, fs::absolute(dest).lexically_normal().string().c_str());
	return 0;
}
