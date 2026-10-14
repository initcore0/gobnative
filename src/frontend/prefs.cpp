// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/prefs.h"

#include <algorithm>
#include <ctime>
#include <fstream>

namespace gob {

namespace fs = std::filesystem;

void (*prefsAfterMoveHook)(const fs::path &oldFolder) = nullptr;

namespace {

// `p` made absolute, without trailing separators (SDL's pref paths end in
// one), so that filename() and parent_path() name the folder itself.
fs::path trimmed(const fs::path &p) {
	std::error_code ec;
	fs::path a = fs::absolute(p, ec);
	std::string s = (ec ? p : a).lexically_normal().string();
	while (s.size() > 1 && (s.back() == '/' || s.back() == char(fs::path::preferred_separator)))
		s.pop_back();
	return s;
}

std::string quoted(const fs::path &p) {
	return "\"" + p.string() + "\"";
}

void appendLog(const fs::path &root, const std::vector<std::string> &lines) {
	std::error_code ec;
	if (lines.empty() || !fs::is_directory(root, ec))
		return;
	std::ofstream out(root / "migration.log", std::ios::app);
	if (!out)
		return;
	char stamp[32] = "";
	std::time_t now = std::time(nullptr);
	if (std::tm *t = std::localtime(&now))
		std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", t);
	for (const std::string &line : lines)
		out << stamp << "  " << line << "\n";
}

// Creates `dir` if needed; empty on failure (logged).
fs::path makeFolder(const fs::path &dir, std::vector<std::string> &log) {
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (!fs::is_directory(dir, ec)) {
		log.push_back("cannot create " + quoted(dir) + (ec ? ": " + ec.message() : std::string()));
		return {};
	}
	return dir;
}

// The migration of 5.4 for root/folder; see prefs.h.
PrefFolder migrate(const fs::path &root, const fs::path &oldRoot, const std::string &folder) {
	PrefFolder r;
	const fs::path dst = root / folder, src = oldRoot / folder;
	std::error_code ec;
	const fs::file_status dstStatus = fs::symlink_status(dst, ec);
	if (dstStatus.type() == fs::file_type::none) { // can't tell: leave everything alone
		r.log.push_back("cannot check " + quoted(dst) + ": " + ec.message());
		if (fs::is_directory(src, ec)) {
			r.dir = src;
			r.legacy = true;
			r.log.push_back("using the old folder " + quoted(src) + " in place");
		}
		return r;
	}
	const fs::file_status srcStatus = fs::symlink_status(src, ec);
	if (fs::exists(dstStatus)) {
		// Already migrated (src is our link then), or both made separately:
		// the new folder wins and the old one stays as it is.
		r.dir = dst;
		if (fs::is_directory(srcStatus))
			r.log.push_back("both " + quoted(dst) + " and the old " + quoted(src) +
			                " exist: using the new one, the old one is left as it is");
		return r;
	}
	if (srcStatus.type() == fs::file_type::not_found) { // a first start: nothing to move
		r.dir = makeFolder(dst, r.log);
		return r;
	}
	if (fs::is_symlink(srcStatus)) {
		if (fs::is_directory(src, ec)) {
			// The player's own link (to another disk, say): keep using it.
			r.dir = src;
			r.legacy = true;
			r.log.push_back("the old folder " + quoted(src) + " is a symbolic link to " +
			                quoted(fs::read_symlink(src, ec)) + ": using it in place, nothing moved");
			return r;
		}
		// A dangling link, e.g. ours after the new folder was removed: start
		// a new folder (which our link points at again).
		r.dir = makeFolder(dst, r.log);
		r.log.push_back("the old " + quoted(src) + " is a link to the missing " +
		                quoted(fs::read_symlink(src, ec)) + ": started a new folder " + quoted(dst));
		return r;
	}
	if (!fs::is_directory(srcStatus)) {
		r.dir = makeFolder(dst, r.log);
		r.log.push_back("the old " + quoted(src) + " is not a folder: left as it is");
		return r;
	}

	// Move the old folder: one rename, atomic, nothing copied.
	fs::create_directories(root, ec);
	fs::rename(src, dst, ec);
	if (ec) {
		const std::string why = ec.message();
		if (fs::is_directory(dst, ec)) {
			r.dir = dst; // another instance moved it (or made it) a moment ago
			r.log.push_back(quoted(dst) + " appeared meanwhile (" + why + "): using it");
			return r;
		}
		r.dir = src;
		r.legacy = true;
		r.log.push_back("could not move " + quoted(src) + " to " + quoted(dst) + " (" + why +
		                "): using the old folder in place");
		return r;
	}
	r.migrated = true;
	r.log.push_back("moved " + quoted(src) + " to " + quoted(dst));
	// The link back, relative, so it keeps working if the home folder moves.
	const fs::path target = fs::path("..") / root.filename() / folder;
	if (prefsAfterMoveHook)
		prefsAfterMoveHook(src);
	fs::create_directory_symlink(target, src, ec);
	if (!ec) {
		r.dir = dst;
		r.log.push_back("linked " + quoted(src) + " -> " + quoted(target) +
		                " for older gob2native builds");
		return r;
	}
	// Without the link an older build would start empty: move it back.
	const std::string why = ec.message();
	fs::rename(dst, src, ec);
	if (!ec) {
		r.dir = src;
		r.migrated = false;
		r.legacy = true;
		r.log.push_back("could not link " + quoted(src) + " (" + why +
		                "): moved the folder back and use it in place");
		return r;
	}
	r.dir = dst;
	r.log.push_back("could not link " + quoted(src) + " (" + why + ") nor move the folder back (" +
	                ec.message() + "): everything is in " + quoted(dst) +
	                "; an older gob2native would not find it");
	return r;
}

} // namespace

fs::path legacyPrefRoot(const fs::path &root) {
	return trimmed(root).parent_path() / "gob2native";
}

PrefFolder gamePrefFolder(const fs::path &rootIn, const std::string &folder, bool migrateLegacy) {
	const fs::path root = trimmed(rootIn);
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
	migrateLegacy = false; // gob2native never ran on Windows (and its links need privileges there) or in a browser
#endif
	if (!migrateLegacy) {
		PrefFolder r;
		r.dir = makeFolder(root / folder, r.log);
		return r;
	}
	PrefFolder r = migrate(root, legacyPrefRoot(root), folder);
	appendLog(root, r.log);
	return r;
}

bool AppSettings::load(const std::string &path) {
	if (!_ini.load(path))
		return false;
	recentFolders.clear();
	for (int i = 1; i <= kMaxRecent; i++) {
		std::string f = _ini.get("recent", "folder" + std::to_string(i));
		if (!f.empty())
			recentFolders.push_back(f);
	}
	lastGame = _ini.get("app", "last_game");
	return true;
}

bool AppSettings::save(const std::string &path) {
	for (int i = 1; i <= kMaxRecent; i++) {
		const std::string key = "folder" + std::to_string(i);
		if (i <= int(recentFolders.size()))
			_ini.set("recent", key, recentFolders[i - 1]);
		else
			_ini.remove("recent", key);
	}
	_ini.set("app", "last_game", lastGame);
	return _ini.save(path);
}

void AppSettings::addRecent(const std::string &folder, const std::string &gameKey) {
	std::string f = trimmed(folder).string();
	std::erase(recentFolders, f);
	recentFolders.insert(recentFolders.begin(), f);
	if (int(recentFolders.size()) > kMaxRecent)
		recentFolders.resize(kMaxRecent);
	lastGame = gameKey;
}

} // namespace gob
