// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/cdaudio.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace gob {

namespace {

uint16_t le16(const std::vector<uint8_t> &d, size_t o) { return o + 1 < d.size() ? uint16_t(d[o] | (d[o + 1] << 8)) : 0; }
uint32_t le32(const std::vector<uint8_t> &d, size_t o) { return le16(d, o) | (uint32_t(le16(d, o + 2)) << 16); }

std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	return s;
}

constexpr size_t kEntrySize = 22;

} // namespace

// Layout: u16 version (3), u16 chunks to skip, u16 track count; at offset 50
// a sequence of length-prefixed chunks to skip; then one 22-byte entry per
// track: NUL-padded name in bytes 0..11, u32 start sector at 12, u32 end at 16.
bool CdAudio::loadLic(const std::vector<uint8_t> &data) {
	freeLic();
	if (data.size() < 50 || le16(data, 0) != 3)
		return false;

	uint16_t skipChunks = le16(data, 2);
	uint16_t count = le16(data, 4);

	size_t pos = 50;
	for (int i = 0; i < skipChunks; i++) {
		uint16_t len = le16(data, pos);
		pos += 2;
		if (len == 0)
			break;
		pos += len;
	}

	for (int i = 0; i < count && pos + kEntrySize <= data.size(); i++, pos += kEntrySize) {
		Track t;
		const char *name = reinterpret_cast<const char *>(&data[pos]);
		t.name = upper(std::string(name, strnlen(name, 12)));
		t.start = le32(data, pos + 12);
		t.end = le32(data, pos + 16);
		_tracks.push_back(std::move(t));
	}
	return !_tracks.empty();
}

const CdAudio::Track *CdAudio::find(const std::string &name) const {
	std::string key = upper(name);
	for (const Track &t : _tracks)
		if (t.name == key)
			return &t;
	return nullptr;
}

bool CdAudio::start(const std::string &name, uint32_t nowMs) {
	const Track *t = find(name);
	if (!t)
		return false;
	_current = t;
	_currentName = t->name;
	_playing = true;
	_startTime = nowMs;
	_stopTime = nowMs + (t->end - t->start + 1 + 150) * 40 / 3;
	return true;
}

int32_t CdAudio::position(const std::string &keyTrack, uint32_t nowMs) const {
	if (!playing(nowMs))
		return -1;

	uint32_t pos = (nowMs - _startTime) * 3 / 40;
	const Track *key = find(keyTrack);
	if (key && _current && key != _current && key->start >= _current->start && key->end <= _current->end) {
		if (key->start - _current->start > pos)
			return -2;
		if (key->end - _current->start < pos)
			return -1;
	}
	return int32_t(pos);
}

} // namespace gob
