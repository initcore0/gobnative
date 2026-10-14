// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which game a data folder holds, and what the engine needs to know about
// each game before its scripts run (docs/port-plan.md 5.1). The engine
// plays three games: Gobliiins (Goblins 1), Gobliins 2 and Goblins Quest 3,
// all CD versions. A game is detected from the file names in its folder and
// the file tables of its archives, without hashing, so other releases of the
// same game are recognised too.

#pragma once

#include "formats/tot.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gob {

enum class GameId { Gob1, Gob2, Gob3 };

struct GameInfo {
	GameId id;
	const char *key;        // "gob1", "gob2", "gob3": --game, file names of saves
	const char *title;      // "Gobliiins", "Gobliins 2", "Goblins Quest 3"
	const char *prefFolder; // per-game folder for settings, saves, packs: "Gobliiins", "Gobliins2", "Goblins3"
	int engineVersion;      // 1, 2 or 3: selects the opcode, goblin, mult and draw variants
	const char *startArchive; // opened before the first script runs
	const char *startTot;     // the first script
	// Fonts loaded into slots 0.. at startup, as the original does (missing
	// files leave their slot empty).
	std::vector<const char *> defaultFonts;
	// CD track table loaded at startup (Goblins 1: the scripts never load
	// it), or null.
	const char *startupLic;
	// Initial values of VAR 13 (mouse), VAR 14 (sound hardware) and VAR 15
	// (video mode), written before every script file starts.
	uint32_t useMouse, soundFlags, videoMode;
	// Languages the game has texts for, in menu order.
	std::vector<Language> languages;

	// The value of VAR 16 (the language) the scripts get for `lang`.
	uint32_t scriptLanguage(Language lang) const;
	bool offersLanguage(Language lang) const;
};

const GameInfo &gameInfo(GameId id);
// The game with key `key` ("gob1", "gob2", "gob3"; case-insensitive), or null.
const GameInfo *gameByKey(const std::string &key);
// All games, in release order.
std::vector<const GameInfo *> allGames();

struct Detection {
	std::optional<GameId> game;
	// Why: which files identified the game, or why none could be.
	std::string reason;
};
Detection detectGame(const std::filesystem::path &dir);

// For the app and the tools: the game named `key` when not empty, else the
// one detected in `dir`. Null when neither works; `message` says which game
// was picked and why, or what went wrong.
const GameInfo *selectGame(const std::filesystem::path &dir, const std::string &key, std::string &message);

// For the tools: selectGame(), reported on stderr ("game: ..."). A folder
// without a recognised game is treated as Gobliins 2, with a warning, so the
// tools still open partial or loose data. Null only for an unknown key.
const GameInfo *selectToolGame(const std::filesystem::path &dir, const std::string &key);

// Removes "--game KEY" from the command line (argc is adjusted) and returns
// KEY, or an empty string when the option isn't there.
std::string takeGameOption(int &argc, char **argv);

} // namespace gob
