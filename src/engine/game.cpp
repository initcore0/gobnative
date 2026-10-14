// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/game.h"

#include "formats/stk.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace fs = std::filesystem;

namespace gob {

namespace {

// As the original sets them for VGA with a mouse, a speaker, a Sound Blaster
// and an AdLib card (the same for all three games).
constexpr uint32_t kUseMouse = 1;
constexpr uint32_t kSoundFlags = 0x4000 | 0x0001 | 0x0004 | 0x0008; // MIDI, speaker, blaster, AdLib
constexpr uint32_t kVideoModeVga = 0x13;

const GameInfo kGames[] = {
	{GameId::Gob1, "gob1", "Gobliiins", "Gobliiins", 1, "INTRO.STK", "INTRO",
	 // ScummVM's Init::_fontNames; only JEUCAR1 exists, the scripts load the rest.
	 {"JEULET1.LET", "JEULET2.LET", "JEUCAR1.LET", "JEUMATH.LET"}, "GOB.LIC", kUseMouse, kSoundFlags, kVideoModeVga,
	 // The texts are inside the scripts, selected by VAR 16 = 0..4.
	 {Language::British, Language::French, Language::German, Language::Spanish, Language::Italian}},
	{GameId::Gob2, "gob2", "Gobliins 2", "Gobliins2", 2, "INTRO.STK", "INTRO",
	 {"JEULET1.LET", "JEULET2.LET"}, nullptr, kUseMouse, kSoundFlags, kVideoModeVga,
	 {Language::American, Language::British, Language::French, Language::German, Language::Spanish,
	  Language::Italian}},
	{GameId::Gob3, "gob3", "Goblins Quest 3", "Goblins3", 3, "INTRO.STK", "INTRO",
	 {}, // none of the original's default fonts exist; the scripts load GOB31-33.LET
	 nullptr, kUseMouse, kSoundFlags, kVideoModeVga,
	 // No .USA texts: American reads the .ANG files.
	 {Language::American, Language::British, Language::French, Language::German, Language::Spanish,
	  Language::Italian}},
};

std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	return s;
}

// The regular files in `dir` by upper-case name.
std::map<std::string, fs::path> listFolder(const fs::path &dir) {
	std::map<std::string, fs::path> files;
	std::error_code ec;
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		if (it->is_regular_file(ec))
			files[upper(it->path().filename().string())] = it->path();
	return files;
}

bool hasSubfolder(const fs::path &dir, const std::string &name) {
	std::error_code ec;
	for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		if (it->is_directory(ec) && upper(it->path().filename().string()) == name)
			return true;
	return false;
}

// "1.2" from bytes 39-41 of a script file's header, or empty.
std::string totVersion(StkArchive &stk, const std::string &tot) {
	const StkEntry *e = stk.find(tot);
	if (!e)
		return {};
	std::vector<uint8_t> d = stk.read(*e);
	if (d.size() < 42)
		return {};
	return std::string(reinterpret_cast<const char *>(d.data()) + 39, 3);
}

} // namespace

uint32_t GameInfo::scriptLanguage(Language lang) const {
	// Goblins 1 selects its texts by VAR 16 among five languages; anything
	// else would select the wrong text (ScummVM plays English then).
	if (id == GameId::Gob1 && int(lang) > int(Language::Italian))
		return uint32_t(Language::British);
	return uint32_t(lang);
}

bool GameInfo::offersLanguage(Language lang) const {
	return std::find(languages.begin(), languages.end(), lang) != languages.end();
}

const GameInfo &gameInfo(GameId id) {
	for (const GameInfo &g : kGames)
		if (g.id == id)
			return g;
	return kGames[1];
}

const GameInfo *gameByKey(const std::string &key) {
	std::string k = key;
	std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	for (const GameInfo &g : kGames)
		if (k == g.key)
			return &g;
	return nullptr;
}

std::vector<const GameInfo *> allGames() {
	std::vector<const GameInfo *> v;
	for (const GameInfo &g : kGames)
		v.push_back(&g);
	return v;
}

Detection detectGame(const fs::path &dir) {
	std::error_code ec;
	if (!fs::is_directory(dir, ec))
		return {std::nullopt, dir.string() + " is not a folder"};
	const auto files = listFolder(dir);
	auto has = [&](const char *name) { return files.count(name) != 0; };
	auto open = [&](const char *name, StkArchive &stk) { return has(name) && stk.open(files.at(name).string()); };

	// Goblins 3 keeps its scripts in TOT.STK and the rooms are EMAPnnnn.
	if (has("TOT.STK") && has("EXT.STK")) {
		StkArchive tot;
		if (open("TOT.STK", tot))
			for (const StkEntry &e : tot.entries())
				if (e.name.rfind("EMAP", 0) == 0 && e.name.size() > 4 &&
				    e.name.compare(e.name.size() - 4, 4, ".TOT") == 0)
					return {GameId::Gob3, "TOT.STK holds the EMAP rooms"};
		if (has("MUS_GOB3.LIC"))
			return {GameId::Gob3, "MUS_GOB3.LIC and TOT.STK"};
	}

	for (const auto &[name, path] : files)
		if (name.rfind("DISK", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".STK") == 0)
			return {std::nullopt, "this is a floppy version (" + name + "), which isn't supported; use the CD version"};

	StkArchive intro;
	if (!open("INTRO.STK", intro)) {
		if (hasSubfolder(dir, "FDD"))
			return {std::nullopt, "only the floppy version (FDD/) is here, which isn't supported"};
		return {std::nullopt, has("INTRO.STK") ? "INTRO.STK is unreadable" : "no INTRO.STK in " + dir.string()};
	}
	if (intro.find("GOB00.TOT") && intro.find("MENU.TOT"))
		return {GameId::Gob2, "INTRO.STK holds GOB00.TOT and MENU.TOT"};
	if (intro.find("AVT00.TOT") || intro.find("AVT001.TOT"))
		return {GameId::Gob1, "INTRO.STK holds the AVT levels"};

	// Another release: go by the script format version of INTRO.TOT.
	const std::string version = totVersion(intro, "INTRO.TOT");
	if (version == "1.1")
		return {GameId::Gob1, "INTRO.TOT is version 1.1"};
	if (version == "1.2")
		return {GameId::Gob2, "INTRO.TOT is version 1.2"};
	if (version == "1.3")
		return {GameId::Gob3, "INTRO.TOT is version 1.3"};
	if (version.empty())
		return {std::nullopt, "INTRO.STK has no readable INTRO.TOT"};
	return {std::nullopt, "INTRO.TOT is version " + version + ", not one of the three games"};
}

const GameInfo *selectGame(const fs::path &dir, const std::string &key, std::string &message) {
	if (!key.empty()) {
		const GameInfo *g = gameByKey(key);
		if (!g) {
			message = "unknown game \"" + key + "\" (gob1, gob2 or gob3)";
			return nullptr;
		}
		message = std::string(g->title) + " (" + g->key + ", chosen with --game)";
		return g;
	}
	Detection d = detectGame(dir);
	if (!d.game) {
		message = "no game detected: " + d.reason + " (--game gob1|gob2|gob3 overrides)";
		return nullptr;
	}
	const GameInfo &g = gameInfo(*d.game);
	message = std::string(g.title) + " (" + g.key + ", detected: " + d.reason + ")";
	return &g;
}

const GameInfo *selectToolGame(const fs::path &dir, const std::string &key) {
	std::string message;
	const GameInfo *g = selectGame(dir, key, message);
	if (g) {
		std::fprintf(stderr, "game: %s\n", message.c_str());
		return g;
	}
	if (!key.empty()) {
		std::fprintf(stderr, "%s\n", message.c_str());
		return nullptr;
	}
	std::fprintf(stderr, "warning: %s; assuming Gobliins 2\n", message.c_str());
	return &gameInfo(GameId::Gob2);
}

std::string takeGameOption(int &argc, char **argv) {
	std::string key;
	int out = 1;
	for (int i = 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--game") && i + 1 < argc) {
			key = argv[++i];
			continue;
		}
		argv[out++] = argv[i];
	}
	argc = out;
	argv[argc] = nullptr;
	return key;
}

} // namespace gob
