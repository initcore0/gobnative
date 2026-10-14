// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where the app keeps its files (docs/port-plan.md 5.3, 5.4). Under the pref
// root (~/Library/Application Support/gobnative/ on macOS, or --pref-root
// DIR) each game has its own folder (Gobliiins, Gobliins2, Goblins3) with
// settings.ini, save games, HDPacks, Music and Screenshots; the root's own
// settings.ini holds what the games share: the recent game folders and the
// last game played.
//
// Before the app was renamed (gob2native -> gobnative), Gobliins 2's folder
// was gob2native/Gobliins2, next to the root. gamePrefFolder() moves it into
// the root the first time Gobliins 2 starts:
// - only when gobnative/Gobliins2 doesn't exist yet (if both exist, the new
//   one is used and the old one is left as it is);
// - with one rename(), which is atomic and copies nothing (both folders are
//   on the same volume; HD packs can be gigabytes);
// - then a symbolic link gob2native/Gobliins2 -> ../gobnative/Gobliins2, so
//   an older gob2native, running now or started later, still finds its
//   settings, saves and packs;
// - if the rename fails, the old folder is used where it is; if the link
//   can't be made, the folder is moved back and used where it is.
// Nothing is ever copied or deleted. What happened is logged (the returned
// lines, also appended to the root's migration.log).

#pragma once

#include "frontend/ini.h"

#include <filesystem>
#include <string>
#include <vector>

namespace gob {

struct PrefFolder {
	std::filesystem::path dir;    // the game's folder (created), or empty if it couldn't be made
	std::vector<std::string> log; // what was done and why, one line each; empty when nothing happened
	bool migrated = false;        // the old Gobliins 2 folder was moved into the root just now
	bool legacy = false;          // the old folder is used in place
};

// The pre-rename root next to `root`: <parent of root>/gob2native.
std::filesystem::path legacyPrefRoot(const std::filesystem::path &root);

// The folder named `folder` under `root`, created if needed. With
// `migrateLegacy` (Gobliins 2), the old folder legacyPrefRoot(root)/folder
// is moved first, as described above.
PrefFolder gamePrefFolder(const std::filesystem::path &root, const std::string &folder, bool migrateLegacy);

// For tests: called after the old folder was moved, before the link back
// is made (to make that fail).
extern void (*prefsAfterMoveHook)(const std::filesystem::path &oldFolder);

// The settings shared by the games, in the root's settings.ini.
struct AppSettings {
	static constexpr int kMaxRecent = 8;
	std::vector<std::string> recentFolders; // game folders, the most recent first
	std::string lastGame;                   // key of the last game started ("gob2")

	bool load(const std::string &path);
	bool save(const std::string &path);
	// Puts `folder` first in the recent list (dropping an older entry of it).
	void addRecent(const std::string &folder, const std::string &gameKey);

private:
	Ini _ini; // the file as loaded, so unknown keys are written back
};

} // namespace gob
