// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "formats/stk.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gob {

// Virtual file system over the game directory: loose files plus any number
// of STK archives. Archives live in slots, as in the original: a new archive
// takes the first free slot, and later slots take priority when a name is
// looked up. "Base" archives are the game's data files (manageDataFile);
// the others are the ITK archives of videos and speech (openItk), closed
// again by closeItk.
class DataFiles {
public:
	explicit DataFiles(std::filesystem::path gameDir);

	// Opens `name` (".STK" added when it has no extension).
	bool openArchive(const std::string &name, bool base = true);
	// Closes the archive in the last slot that is (not) a base archive.
	// Returns false when there is none.
	bool closeArchive(bool base);
	// True when the game directory holds a loose file `name`.
	bool hasLooseFile(const std::string &name) const { return findLoose(name).has_value(); }

	// Opens every STK and ITK archive in the game directory (not in
	// subfolders), INTRO.STK first, the rest in name order. For tools, which
	// look at all of a game's files at once; the engine opens archives as
	// the game does. Returns how many were opened.
	int openAllArchives();

	bool exists(const std::string &name) const;
	// The file name of the archive `name` is read from (the last slot that
	// has it), or empty for a loose file or none.
	std::string archiveOf(const std::string &name) const;
	std::optional<std::vector<uint8_t>> read(const std::string &name);

	// All file names known across archives (for tools).
	std::vector<std::string> list() const;

	const std::filesystem::path &gameDir() const { return _gameDir; }

private:
	std::optional<std::filesystem::path> findLoose(const std::string &name) const;

	struct Slot {
		std::unique_ptr<StkArchive> archive; // null: a free slot
		bool base = true;
	};
	std::filesystem::path _gameDir;
	std::vector<Slot> _archives;
};

} // namespace gob
