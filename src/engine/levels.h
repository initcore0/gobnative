// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Level structures (docs/levels.md):
// - Gobliins 2: seven worlds of two to five rooms each, one script file
//   (GOB00-GOB22) per room. MENU.TOT runs the room named in s[3734] and loads
//   a world's item and place names from MONDEn.CAT when v[1353] asks for a
//   new world.
// - Gobliiins: 22 levels, one script file each (AVT00, AVT001-AVT022), which
//   INTER.TOT starts by number (v[286]); each level shows the access code of
//   the next one when it is solved.

#pragma once

#include <string>

namespace gob {

struct LevelInfo {
	std::string tot;  // room script, e.g. "GOB08"
	int world = 0;    // 1-7 (Gobliins 2), 0 for games without worlds
	bool worldStart = false; // the room a world begins in
	std::string name; // place name from MONDEn.CAT in the game language (UTF-8); may be empty
	int number = 0;   // Gobliiins: the level's number in play order (1-22)
	std::string code; // Gobliiins: its access code with full energy (empty for the first level)
	bool reached = false; // Goblins 3: a jump starts it as the player first reached it
};

// The world (1-7) room `tot` ("GOB08") belongs to, or 0 for other scripts.
int levelWorld(const std::string &tot);

} // namespace gob
