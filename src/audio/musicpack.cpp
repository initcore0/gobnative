// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "audio/musicpack.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace gob {

namespace fs = std::filesystem;

namespace {

std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	return s;
}

} // namespace

void MusicPack::scan(const std::string &dir) {
	_dir = dir;
	_entries.clear();
	std::error_code ec;
	if (dir.empty() || !fs::is_directory(dir, ec))
		return;
	for (const fs::directory_entry &e : fs::directory_iterator(dir, ec)) {
		if (!e.is_regular_file(ec))
			continue;
		const fs::path &p = e.path();
		std::string ext = upper(p.extension().string());
		std::string name = p.filename().string();
		if ((ext != ".MP3" && ext != ".WAV") || name.empty() || name[0] == '.')
			continue;
		Entry entry{upper(p.stem().string()), name, p.string(), {}};
		// Both formats for one track: the WAV wins (lossless).
		auto same = std::find_if(_entries.begin(), _entries.end(), [&](const Entry &x) { return x.track == entry.track; });
		if (same != _entries.end()) {
			if (upper(fs::path(same->file).extension().string()) == ".WAV")
				continue;
			_entries.erase(same);
		}
		_entries.push_back(std::move(entry));
	}
	std::sort(_entries.begin(), _entries.end(), [](const Entry &a, const Entry &b) { return a.track < b.track; });
	for (Entry &e : _entries)
		e.info = probeMusicFile(e.path);
}

const MusicPack::Entry *MusicPack::find(const std::string &track) const {
	std::string key = upper(track);
	for (const Entry &e : _entries)
		if (e.track == key)
			return &e;
	return nullptr;
}

std::unique_ptr<CdSource> MusicPack::open(const std::string &track, std::string *error) const {
	const Entry *e = find(track);
	if (!e)
		return nullptr;
	std::string err;
	auto src = openMusicFile(e->path, &err);
	if (!src && error)
		*error = e->file + ": " + err;
	return src;
}

} // namespace gob
