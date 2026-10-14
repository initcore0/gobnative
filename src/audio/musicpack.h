// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Music replacement packs: a folder of MP3/WAV files named after the CD
// tracks of the game's .LIC file (MINE.mp3, campagne.wav; the name is not
// case sensitive). When the game starts a track that has a file here, the
// file plays instead of that stretch of the CD image.

#pragma once

#include "audio/cdsource.h"

#include <memory>
#include <string>
#include <vector>

namespace gob {

class MusicPack {
public:
	struct Entry {
		std::string track; // upper case, as the .LIC names it
		std::string file;  // file name in the folder
		std::string path;
		MusicFileInfo info; // format, or why it can't be played
	};

	// Looks for replacement files in `dir` (empty: none). A missing folder
	// is no error; it just holds no replacements.
	void scan(const std::string &dir);
	void rescan() { scan(_dir); }
	const std::string &dir() const { return _dir; }
	const std::vector<Entry> &entries() const { return _entries; }
	const Entry *find(const std::string &track) const;

	// The replacement for `track`, ready to play, or null when there is none
	// or it can't be read (then `error` says why).
	std::unique_ptr<CdSource> open(const std::string &track, std::string *error = nullptr) const;

	// Let a replacement play to its end instead of stopping it at the length
	// of the CD track it replaces.
	bool fullLength = false;

private:
	std::string _dir;
	std::vector<Entry> _entries; // sorted by track name
};

} // namespace gob
