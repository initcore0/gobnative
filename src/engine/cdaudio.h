// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gob {

// CD audio timeline. Scripts start named "tracks" (ranges of CD sectors
// listed in a .LIC file) and poll the play position to synchronize
// animations with the music. Position is in CD frames (75 per second).
//
// This class only keeps time; actual audio output is a separate concern
// (the GOG release ships the CD audio as Track1.mp3).
class CdAudio {
public:
	struct Track {
		std::string name;   // upper case
		uint32_t start = 0; // HSG sector address
		uint32_t end = 0;
	};

	bool loadLic(const std::vector<uint8_t> &data);
	void freeLic() { _tracks.clear(); _current = nullptr; _playing = false; }

	// Starts a named track at time `nowMs`. Returns false if unknown.
	bool start(const std::string &name, uint32_t nowMs);
	void stop() { _playing = false; _current = nullptr; }

	// Current position in CD frames, or -1 when not playing. With a key
	// track that lies inside the playing one, returns -2 before the key's
	// range and -1 after it.
	int32_t position(const std::string &keyTrack, uint32_t nowMs) const;

	const std::vector<Track> &tracks() const { return _tracks; }
	const std::string &currentName() const { return _currentName; }
	const Track *current() const { return _current; }
	bool playing(uint32_t nowMs) const { return _playing && int32_t(nowMs - _stopTime) < 0; }

private:
	const Track *find(const std::string &name) const;

	std::vector<Track> _tracks;
	const Track *_current = nullptr;
	std::string _currentName;
	bool _playing = false;
	uint32_t _startTime = 0;
	uint32_t _stopTime = 0;
};

} // namespace gob
