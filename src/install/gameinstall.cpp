// gob2native - GOG installer import
// SPDX-License-Identifier: GPL-3.0-or-later

#include "install/gameinstall.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace gob {

namespace {

std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	return s;
}

bool startsWith(const std::string &s, const std::string &prefix) {
	return s.compare(0, prefix.size(), prefix) == 0;
}

// A file in the game's folder ({app}), and not one of GOG's or ScummVM's.
bool isGameFile(const InnoFile &f, const std::string &path, bool floppy) {
	if (f.parts.empty() || f.merged || f.type != 0 || path.empty())
		return false;
	const std::string u = upper(path);
	const std::string top = u.substr(0, u.find('/'));
	static const std::set<std::string> skipFolders = {"SCUMMVM", "DOSBOX", "__REDIST", "__SUPPORT", "APP",
	                                                  "TMP", "COMMONAPPDATA", "DOCS", "CLOUD_SAVES"};
	if (top != u && (skipFolders.count(top) || (top == "FDD" && !floppy)))
		return false;
	if (top != u && top != "FDD")
		return false; // only the game's own top folder and FDD/
	const std::string name = u.substr(u.rfind('/') + 1);
	if (startsWith(name, "GOGGAME-") || startsWith(name, "UNINS") || startsWith(name, "GOG") ||
	    name == "SUPPORT.TXT" || name == "WEBCACHE.ZIP" || name == "EULA.TXT")
		return false;
	const size_t dot = name.rfind('.');
	const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot);
	static const std::set<std::string> skipExt = {".ICO", ".EXE", ".DLL", ".INI", ".URL", ".LNK", ".BAT"};
	return !skipExt.count(ext);
}

// Prefer the English file when the installer has one per language.
bool englishFirst(const InnoFile &f) {
	return f.languages.empty() || f.languages.find("en") != std::string::npos;
}

} // namespace

const GameInfo *identifyInstaller(const InnoSetup &setup, std::string &reason) {
	std::set<std::string> names; // the top level of the game's folder
	bool floppyOnly = false;
	for (const InnoFile &f : setup.files()) {
		if (f.merged || f.parts.empty())
			continue;
		const std::string path = upper(InnoSetup::relativePath(f.destination));
		if (path.find('/') == std::string::npos)
			names.insert(path);
		else if (startsWith(path, "FDD/"))
			floppyOnly = true;
	}
	auto has = [&](const char *n) { return names.count(n) != 0; };
	if (has("TOT.STK") && has("EXT.STK") && (has("USGOB3.ITK") || has("MUS_GOB3.LIC") || has("IMD.ITK"))) {
		reason = "TOT.STK, EXT.STK and the Goblins Quest 3 speech/music files";
		return &gameInfo(GameId::Gob3);
	}
	if (has("INTRO.STK") && has("GOBNEW.LIC")) {
		reason = "INTRO.STK and GOBNEW.LIC";
		return &gameInfo(GameId::Gob2);
	}
	if (has("INTRO.STK") && has("GOB.LIC")) {
		reason = "INTRO.STK and GOB.LIC";
		return &gameInfo(GameId::Gob1);
	}
	if (floppyOnly && !has("INTRO.STK") && !has("TOT.STK"))
		reason = "only a floppy version (FDD/), which isn't supported";
	else
		reason = "\"" + setup.appName() + "\" isn't Gobliiins, Gobliins 2 or Goblins Quest 3 (CD versions)";
	return nullptr;
}

bool planGameInstall(const InnoSetup &setup, const InstallOptions &options, GameInstallPlan &plan,
                     std::string &error) {
	plan = GameInstallPlan();
	plan.game = identifyInstaller(setup, plan.reason);
	if (!plan.game && !options.everything) {
		error = plan.reason;
		return false;
	}
	// One entry per path: the English one when there are several, else the
	// last (as a later file overwrites an earlier one when installing).
	std::map<std::string, size_t> chosen;
	std::vector<std::string> order;
	const auto &files = setup.files();
	for (size_t i = 0; i < files.size(); i++) {
		const InnoFile &f = files[i];
		const std::string path = InnoSetup::relativePath(f.destination, options.everything);
		const bool take = options.everything ? !f.parts.empty() && !f.merged && !path.empty() : isGameFile(f, path, options.floppy);
		if (!take) {
			if (!path.empty())
				plan.skipped.push_back(path);
			continue;
		}
		const std::string key = upper(path);
		auto it = chosen.find(key);
		if (it == chosen.end()) {
			chosen[key] = i;
			order.push_back(key);
		} else if (englishFirst(f) || !englishFirst(files[it->second]))
			it->second = i;
	}
	for (const std::string &key : order) {
		const InnoFile &f = files[chosen[key]];
		plan.files.push_back({uint32_t(chosen[key]), InnoSetup::relativePath(f.destination, options.everything), f.size});
		plan.bytes += f.size;
	}
	if (plan.files.empty()) {
		error = "nothing to install";
		return false;
	}
	return true;
}

bool GameInstaller::fail(const std::string &message) {
	if (_error.empty())
		_error = message;
	return false;
}

bool GameInstaller::open(const fs::path &setupExe, const InstallOptions &options) {
	_options = options;
	_error.clear();
	_started = _done = false;
	if (!_setup.open(setupExe))
		return fail(_setup.error());
	std::string error;
	if (!planGameInstall(_setup, options, _plan, error))
		return fail(error);
	return true;
}

bool GameInstaller::begin(const fs::path &dest, bool merge) {
	if (_plan.files.empty())
		return fail("nothing to install");
	_dest = dest;
	_merge = merge;
	std::error_code ec;
	if (!merge && fs::exists(dest, ec) && !(fs::is_directory(dest, ec) && fs::is_empty(dest, ec)))
		return fail(dest.string() + " already exists");
	_work = merge ? dest : fs::path(dest.string() + ".partial");
	if (!merge && fs::exists(_work, ec))
		fs::remove_all(_work, ec); // our own leftover from an interrupted import
	fs::create_directories(_work, ec);
	if (ec)
		return fail("cannot create " + _work.string() + ": " + ec.message());
	std::vector<InnoExtractor::Item> items;
	for (const GameInstallPlan::File &f : _plan.files)
		items.push_back({f.file, {_work / fs::path(f.path)}, f.path});
	if (!_extractor.begin(_setup, std::move(items)))
		return fail(_extractor.error());
	_started = true;
	return true;
}

double GameInstaller::fraction() const {
	if (_done)
		return 1.0;
	return _extractor.total() ? double(_extractor.progress()) / double(_extractor.total()) : 0.0;
}

bool GameInstaller::step(uint64_t budget) {
	if (!_started || _done || failed())
		return false;
	if (_extractor.step(budget))
		return true;
	if (_extractor.failed()) {
		fail(_extractor.error());
		cancel();
		return false;
	}
	if (!finish()) {
		cancel();
		return false;
	}
	_done = true;
	return false;
}

bool GameInstaller::finish() {
	// The game detection must find the game in what was extracted.
	if (_plan.game) {
		const Detection d = detectGame(_work);
		if (!d.game || *d.game != _plan.game->id)
			return fail("the extracted files don't hold " + std::string(_plan.game->title) + " (" + d.reason + ")");
	}
	if (_merge)
		return true;
	std::error_code ec;
	if (fs::exists(_dest, ec))
		fs::remove(_dest, ec); // an empty folder (checked in begin())
	fs::rename(_work, _dest, ec);
	if (ec)
		return fail("cannot rename " + _work.string() + " to " + _dest.string() + ": " + ec.message());
	return true;
}

void GameInstaller::cancel() {
	if (!_started)
		return;
	_started = false;
	_extractor = InnoExtractor(); // closes the files being written
	std::error_code ec;
	if (!_merge && !_done && fs::exists(_work, ec))
		fs::remove_all(_work, ec); // the temporary folder only
	if (_error.empty())
		_error = "cancelled";
}

} // namespace gob
