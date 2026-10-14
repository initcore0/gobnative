// gob2native - GOG installer import
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Installs one of the games from its GOG installer (setup_*.exe, install/
// inno.h): finds out which game the installer holds from its file list,
// picks the files the game needs (its data, the CD audio, the manual;
// not ScummVM, GOG's support files or the installer's own files; the
// floppy version in FDD/ only on request), extracts them into a temporary
// folder next to the destination, checks the result with the game
// detection (engine/game.h) and renames it into place.
//
// The work is done in steps (step()), so the app can show a progress bar
// and stay responsive without threads; the command line tool gobinstall
// calls step() in a loop.

#pragma once

#include "engine/game.h"
#include "install/inno.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gob {

struct InstallOptions {
	bool floppy = false;     // also install the floppy version (FDD/)
	bool everything = false; // every file of the installer, named as innoextract names them (tests)
};

struct GameInstallPlan {
	const GameInfo *game = nullptr; // null with `everything` when no game was recognised
	std::string reason;             // which files identified the game
	struct File {
		uint32_t file;    // index into setup.files()
		std::string path; // relative, with '/'
		uint64_t size;
	};
	std::vector<File> files;          // what is installed
	std::vector<std::string> skipped; // what isn't
	uint64_t bytes = 0;
};

// The game an installer holds, from the names of its files; null (with
// `reason` saying why) when it isn't one of the three games.
const GameInfo *identifyInstaller(const InnoSetup &setup, std::string &reason);
bool planGameInstall(const InnoSetup &setup, const InstallOptions &options, GameInstallPlan &plan, std::string &error);

class GameInstaller {
public:
	// Reads the installer and plans the installation.
	bool open(const std::filesystem::path &setupExe, const InstallOptions &options = {});
	const InnoSetup &setup() const { return _setup; }
	const GameInstallPlan &plan() const { return _plan; }

	// Starts extracting into `dest`, which must not exist or be empty
	// (with `merge`, files are written into an existing folder directly).
	bool begin(const std::filesystem::path &dest, bool merge = false);
	// About `budget` bytes of work; false when finished or failed.
	bool step(uint64_t budget);
	// Stops and removes what was extracted so far.
	void cancel();

	bool done() const { return _done; }
	bool failed() const { return !_error.empty(); }
	const std::string &error() const { return _error; }
	double fraction() const;
	std::string current() const { return _extractor.current(); }
	const std::filesystem::path &destination() const { return _dest; }

private:
	bool fail(const std::string &message);
	bool finish();

	InstallOptions _options;
	InnoSetup _setup;
	GameInstallPlan _plan;
	InnoExtractor _extractor;
	std::filesystem::path _dest, _work; // _work: where files are written (a temporary folder unless merging)
	bool _merge = false, _started = false, _done = false;
	std::string _error;
};

} // namespace gob
