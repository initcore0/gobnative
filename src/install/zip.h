// gob2native - zip archives
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extracts a zip file (an HD pack or replacement music the player chose in
// the web build, which has no folders of the player's own): the central
// directory is read at open(), the files are written in steps (step()),
// so a page stays responsive without threads. Stored and deflated entries
// (inflated with the PNG reader's inflater), CRC-32 checked; no zip64, no
// encryption. Names with ".." or absolute paths are refused; macOS's
// __MACOSX/ folders and directory entries are skipped. When every file is
// in one top folder, that folder is left out (a zipped pack folder
// extracts into the destination itself).
//
// writeZip() makes one: a game folder exported for another device (a
// phone's browser, which can't choose folders).

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace gob {

class ZipExtractor {
public:
	struct Entry {
		std::string name; // relative path inside the destination ('/'), the common top folder taken off
		uint64_t offset = 0; // of the local header
		uint32_t compressedSize = 0, size = 0, crc = 0;
		uint16_t method = 0;
	};

	bool open(const std::filesystem::path &zip);
	const std::vector<Entry> &entries() const { return _entries; }
	// The top folder all files were in ("" when none).
	const std::string &topFolder() const { return _top; }

	// Writes the files below `dest` (created; existing files are replaced).
	bool begin(const std::filesystem::path &dest);
	// About `budget` bytes of work; false when finished or failed.
	bool step(uint64_t budget);

	bool done() const { return _done; }
	const std::string &error() const { return _error; }
	double fraction() const;
	std::string current() const { return _next < _entries.size() ? _entries[_next].name : std::string(); }

private:
	bool fail(const std::string &message);
	bool extract(const Entry &e);

	std::ifstream _in;
	uint64_t _fileSize = 0;
	std::vector<Entry> _entries;
	std::string _top;
	std::filesystem::path _dest;
	size_t _next = 0;
	uint64_t _bytesDone = 0, _bytesTotal = 0;
	bool _done = false;
	std::string _error;
};

// Writes the files below `folder` (not hidden ones; those `include`
// accepts, given their path relative to `folder`, when it is set) into a
// new zip file `zip`, below a top folder `top` ("Gobliins 2/INTRO.STK").
// Stored, not compressed (the games' files hardly compress); no zip64, so
// up to 4 GB.
bool writeZip(const std::filesystem::path &zip, const std::filesystem::path &folder, const std::string &top,
              std::string &error,
              const std::function<bool(const std::filesystem::path &)> &include = nullptr);

} // namespace gob
