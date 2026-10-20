// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the engine hard-codes per game, beyond the scripts: which script
// files are cutscenes, what fast-forwarding may run through, how rooms are
// strung together (level select, quick save), the save format, the copy
// protection skip and fixes for bugs in the game data (docs/port-plan.md
// 5.2). A game without an implementation of a feature reports it as
// unavailable, and the front end hides it.

#pragma once

#include "engine/game.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gob {

// How a game strings its rooms together, which is what level select and
// quick save/load build on.
enum class LevelSystem {
	None,
	Gob2, // MENU.TOT runs the GOBnn rooms (engine/levels.cpp, docs/levels.md)
	Gob1, // INTER.TOT starts the AVT levels by number (engine/levels_v1.cpp, docs/levels.md 9)
	Gob3, // MENU.TOT runs the EMAP rooms named in s[580] (engine/levels_v3.cpp, docs/levels-gob3.md)
};

// How the scripts' save files map to save games on disk.
enum class SaveFormat {
	None, // no save files (the scripts see them as missing)
	Gob2, // cat.inf, bloc.inf, save.inf (engine/saves.cpp)
	Gob3, // cat.inf, ima.inf, intro.$$$, bloc.inf (engine/saves_v3.cpp)
};

struct GameRules {
	// Script files that are cutscenes as a whole: they run at 1x speed (they
	// follow the CD music) and Escape or Skip ends them. Any playMult is a
	// cutscene too.
	std::vector<const char *> cutsceneTots;
	// Script files between rooms that fast-forwarding runs through
	// (engine/skip.cpp); fast-forwarding needs the level system as well.
	std::vector<const char *> transitionTots;
	LevelSystem levels = LevelSystem::None;
	SaveFormat saves = SaveFormat::None;

	// callSub targets that show the copy protection screen; skipped.
	struct CallSkip {
		const char *tot;
		uint16_t offset;
	};
	std::vector<CallSkip> copyProtectionCalls;

	// switchTotSub calls ignored to work around script bugs.
	struct SwitchTotSkip {
		const char *tot; // the target environment
		int16_t index, function;
	};
	std::vector<SwitchTotSkip> ignoredSwitchTotSubs;

	// Speech archives the scripts open by language (openItk), and the one
	// opened when a release lacks the language's (Goblins 3: the GOG version
	// has only the English USGOB3.ITK; docs/port-plan.md 7).
	std::vector<const char *> voiceArchives;
	const char *voiceFallback = nullptr;
	bool isVoiceArchive(const std::string &archive) const;

	// CD tracks the scripts name differently from the track table (Goblins 3
	// says ECHEQUI1/2 for ECHIQUI1/2, ScummVM ticket #11335).
	struct TrackRename {
		const char *script, *track;
	};
	std::vector<TrackRename> trackRenames;
	// The name of CD track `name` in the track table.
	std::string cdTrackName(const std::string &name) const;

	// Gobliiins: items a pick takes as another one. The level data links a
	// second item to a pickable object (to act on it from elsewhere), and a
	// pick there would put the object in the pocket under that item's id,
	// which nothing accepts later (engine/goblins_v1.cpp, treatItemPick).
	struct PickAlias {
		const char *tot;
		int16_t item, as;
	};
	std::vector<PickAlias> pickAliases;
	// The item a pick on `item` in script file `tot` takes.
	int16_t pickedItem(const std::string &tot, int16_t item) const;

	bool isCutsceneTot(const std::string &tot) const;
	bool isTransitionTot(const std::string &tot) const;
	bool isCopyProtectionCall(const std::string &tot, uint16_t offset) const;
	bool ignoresSwitchTotSub(const std::string &tot, int16_t index, int16_t function) const;
	// The archive to open when speech archive `archive` ("FRGOB3.ITK") is
	// missing, or null.
	const char *archiveFallback(const std::string &archive) const;

	// Features the front end offers.
	bool hasLevelSelect() const { return levels != LevelSystem::None; }
	// Gobliins 2 quick saves into its own save slots; Gobliiins, which has no
	// saves, into an engine snapshot of the level (engine/levels_v1.cpp).
	bool hasQuickSave() const {
		return (levels == LevelSystem::Gob2 && saves == SaveFormat::Gob2) || levels == LevelSystem::Gob1 ||
		       (levels == LevelSystem::Gob3 && saves == SaveFormat::Gob3);
	}
	bool hasFastForward() const { return levels != LevelSystem::None && !transitionTots.empty(); }
	// The game has speech (Goblins 3), with its own volume.
	bool hasSpeech() const { return !voiceArchives.empty(); }
};

const GameRules &gameRules(GameId id);

} // namespace gob
