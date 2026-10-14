// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game library, shown when the app starts without a game folder: one
// tile per game (Gobliiins, Gobliins 2, Goblins Quest 3; a list of tiles in
// narrow windows), in the warm theme of frontend/theme.h. An installed
// game shows its title picture, read from its own data (nothing of the
// games is part of the app), and starts with "Play" or a click on the
// picture. A game that isn't installed is a tinted card (its name and
// year) whose "Install..." imports it from its GOG installer
// (setup_*.exe, chosen in the system's file dialog on macOS, or dropped
// onto the window): the installer is checked to be that game's, the
// game's files are extracted with a progress bar (install/gameinstall.h,
// on a worker thread) into the app's own Games/<game> folder under the
// pref root, and the tile becomes playable. In the web build the installer
// comes from the browser's file dialog and is extracted step by step in
// the frame loop, into the browser's storage (frontend/web.h).
//
// While a game is missing, a card explains how to get its installer;
// during an import a card shows its progress. Messages are in plain words,
// with the paths and reasons in their tooltip and under "More options".
//
// A game is installed when the app's Games/<game> folder or one of the
// recent folders holds it (the most recent such folder is played). "More
// options" (closed at first): "Add a game folder..." (or dropping a folder,
// or typing a path) adds a folder of the user's own to the recent list.

#pragma once

#include "frontend/prefs.h"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gob {

// For tests (gobnative without a game folder; times in seconds).
struct LibraryDebug {
	double screenshotAt = -1; // write the window to screenshotFile, then close it
	std::string screenshotFile;
	std::vector<std::pair<double, std::string>> shots;   // --shot-at T FILE: write the window, go on
	double pickAt = -1;                                   // --pick-at T DIR: play DIR, as if typed and opened
	std::string pickFolder;
	std::vector<std::pair<double, std::string>> imports; // --import-at T SETUP: import, as if dropped
	std::vector<std::pair<double, std::string>> plays;   // --play-at T KEY: click the game's tile
	// --tile-import-at T KEY SETUP: "KEY=SETUP", as if the tile of game KEY
	// was clicked and SETUP chosen in the file dialog
	std::vector<std::pair<double, std::string>> tileImports;
	double cancelAt = -1; // --cancel-import-at T: press the import's Cancel button
};

struct LibraryContext {
	std::filesystem::path prefRoot; // the app's folder (Games/ is made in it); empty: no imports
	AppSettings *shared = nullptr;  // the recent folders, updated (and saved to sharedPath)
	std::string sharedPath;
	std::string gameKey;            // --game: only that game can be played
};

// Where imported games go: <pref root>/Games/<the game's pref folder>.
std::filesystem::path libraryGameFolder(const std::filesystem::path &prefRoot, const std::string &prefFolder);

// The library one frame at a time (the web build runs it from the
// browser's frame loop): open() makes the window, GL and ImGui contexts
// (SDL's video must be initialised), frame() handles input and draws,
// returning false once a game was chosen or the window closed, and
// close() destroys them and returns the game folder to play (empty when
// none was chosen).
class LibraryScreen {
public:
	explicit LibraryScreen(LibraryContext &context, const LibraryDebug &debug = {});
	~LibraryScreen();
	bool open();
	bool frame();
	std::string close();

private:
	struct State;
	std::unique_ptr<State> _s;
};

// Runs the library to the end: returns the game folder to play, or an
// empty string when the window was closed. Its window, GL context and
// ImGui context are gone when it returns.
std::string runLibrary(LibraryContext &context, const LibraryDebug &debug = {});

} // namespace gob
