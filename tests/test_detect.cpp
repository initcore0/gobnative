// gob2native - game detection tests: fake game folders with generated STK
// archives (no game data needed)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "data/datafiles.h"
#include "engine/game.h"
#include "engine/rules.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

using namespace gob;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

using Entry = std::pair<std::string, std::vector<uint8_t>>;

void put(std::vector<uint8_t> &out, uint32_t v, int bytes) {
	for (int i = 0; i < bytes; i++)
		out.push_back(uint8_t(v >> (8 * i)));
}

// An STK archive: u16 count, records of name[13], u32 size, u32 offset,
// u8 packed, then the (stored) data.
void writeStk(const fs::path &path, const std::vector<Entry> &entries) {
	std::vector<uint8_t> out;
	put(out, uint32_t(entries.size()), 2);
	uint32_t offset = uint32_t(2 + entries.size() * 22);
	for (const Entry &e : entries) {
		char name[13] = {};
		std::strncpy(name, e.first.c_str(), 12);
		out.insert(out.end(), name, name + 13);
		put(out, uint32_t(e.second.size()), 4);
		put(out, offset, 4);
		out.push_back(0);
		offset += uint32_t(e.second.size());
	}
	for (const Entry &e : entries)
		out.insert(out.end(), e.second.begin(), e.second.end());
	std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char *>(out.data()), long(out.size()));
}

void touch(const fs::path &path) {
	std::ofstream(path, std::ios::binary).put('x');
}

// A script file that is only a header with the version string at 39.
std::vector<uint8_t> tot(const char *version) {
	std::vector<uint8_t> d(128, 0);
	std::memcpy(d.data() + 39, version, 3);
	return d;
}

const std::vector<uint8_t> kSmall = {1, 2, 3};

class Folder {
public:
	explicit Folder(const fs::path &root, const std::string &name) : path(root / name) {
		fs::create_directories(path);
	}
	fs::path path;
};

std::optional<GameId> detected(const fs::path &dir) {
	return detectGame(dir).game;
}

} // namespace

int main() {
	const fs::path root = fs::temp_directory_path() / ("gob_test_detect_" + std::to_string(getpid()));
	fs::remove_all(root);

	// Gobliins 2: INTRO.STK with GOB00 and MENU (and an INTER, as Gob1 has).
	Folder gob2(root, "gob2");
	writeStk(gob2.path / "INTRO.STK", {{"INTRO.TOT", tot("1.2")}, {"GOB00.TOT", kSmall}, {"MENU.TOT", kSmall},
	                                   {"INTER.TOT", kSmall}});
	touch(gob2.path / "GOBNEW.LIC");
	CHECK(detected(gob2.path) == GameId::Gob2);

	// Gobliiins: the AVT levels and INTER in INTRO.STK; lower-case file name.
	Folder gob1(root, "gob1");
	writeStk(gob1.path / "intro.stk", {{"INTRO.TOT", tot("1.1")}, {"INTER.TOT", kSmall}, {"AVT00.TOT", kSmall}});
	touch(gob1.path / "GOB.LIC");
	CHECK(detected(gob1.path) == GameId::Gob1);

	// Goblins 3: TOT.STK with EMAP rooms next to EXT.STK.
	Folder gob3(root, "gob3");
	writeStk(gob3.path / "INTRO.STK", {{"INTRO.TOT", tot("1.3")}});
	writeStk(gob3.path / "TOT.STK", {{"MENU.TOT", kSmall}, {"EMAP1000.TOT", kSmall}});
	writeStk(gob3.path / "EXT.STK", {{"EMAP1000.EXT", kSmall}});
	CHECK(detected(gob3.path) == GameId::Gob3);

	// Goblins 3 without EMAP rooms, but with its CD track table.
	Folder gob3b(root, "gob3b");
	writeStk(gob3b.path / "TOT.STK", {{"MENU.TOT", kSmall}});
	writeStk(gob3b.path / "EXT.STK", {{"DEMO.TOT", kSmall}});
	touch(gob3b.path / "MUS_GOB3.LIC");
	CHECK(detected(gob3b.path) == GameId::Gob3);

	// Unknown releases: the version of INTRO.TOT decides.
	const char *versions[] = {"1.1", "1.2", "1.3"};
	const GameId ids[] = {GameId::Gob1, GameId::Gob2, GameId::Gob3};
	for (int i = 0; i < 3; i++) {
		Folder f(root, std::string("version") + versions[i]);
		writeStk(f.path / "INTRO.STK", {{"INTRO.TOT", tot(versions[i])}});
		CHECK(detected(f.path) == ids[i]);
	}
	Folder v4(root, "version1.4");
	writeStk(v4.path / "INTRO.STK", {{"INTRO.TOT", tot("1.4")}});
	CHECK(!detected(v4.path));

	// Floppy versions are refused, with a reason that says so.
	Folder floppy(root, "floppy");
	writeStk(floppy.path / "INTRO.STK", {{"INTRO.TOT", tot("1.2")}, {"GOB00.TOT", kSmall}, {"MENU.TOT", kSmall}});
	writeStk(floppy.path / "DISK2.STK", {{"GOB01.TOT", kSmall}});
	Detection d = detectGame(floppy.path);
	CHECK(!d.game);
	CHECK(d.reason.find("floppy") != std::string::npos);

	Folder fddOnly(root, "fddonly");
	fs::create_directories(fddOnly.path / "FDD");
	d = detectGame(fddOnly.path);
	CHECK(!d.game);
	CHECK(d.reason.find("floppy") != std::string::npos);

	// Empty and missing folders, and an unreadable INTRO.STK.
	Folder empty(root, "empty");
	CHECK(!detected(empty.path));
	CHECK(!detected(root / "missing"));
	Folder broken(root, "broken");
	touch(broken.path / "INTRO.STK");
	CHECK(!detected(broken.path));

	// --game overrides detection; unknown keys are refused.
	std::string message;
	const GameInfo *g = selectGame(empty.path, "GOB1", message);
	CHECK(g && g->id == GameId::Gob1);
	CHECK(!selectGame(empty.path, "gob4", message));
	CHECK(!selectGame(empty.path, "", message));
	g = selectGame(gob2.path, "", message);
	CHECK(g && g->id == GameId::Gob2 && message.find("detected") != std::string::npos);

	// takeGameOption removes the option wherever it is.
	char a0[] = "tool", a1[] = "dir", a2[] = "--game", a3[] = "gob3", a4[] = "--all";
	char *argv[] = {a0, a1, a2, a3, a4, nullptr};
	int argc = 5;
	CHECK(takeGameOption(argc, argv) == "gob3");
	CHECK(argc == 3 && std::strcmp(argv[1], "dir") == 0 && std::strcmp(argv[2], "--all") == 0 && !argv[3]);
	CHECK(takeGameOption(argc, argv).empty() && argc == 3);

	// Game facts.
	CHECK(gameInfo(GameId::Gob2).engineVersion == 2 && std::strcmp(gameInfo(GameId::Gob2).key, "gob2") == 0);
	CHECK(gameInfo(GameId::Gob1).startupLic && !gameInfo(GameId::Gob2).startupLic);
	CHECK(gameInfo(GameId::Gob1).scriptLanguage(Language::American) == uint32_t(Language::British));
	CHECK(gameInfo(GameId::Gob1).scriptLanguage(Language::Italian) == uint32_t(Language::Italian));
	CHECK(gameInfo(GameId::Gob2).scriptLanguage(Language::American) == uint32_t(Language::American));
	CHECK(!gameInfo(GameId::Gob1).offersLanguage(Language::American));
	CHECK(allGames().size() == 3);

	// Rules: Gobliins 2 keeps its behaviour; Gobliiins has its own level
	// system; Goblins Quest 3 has none of it yet.
	const GameRules &r2 = gameRules(GameId::Gob2);
	CHECK(r2.isCutsceneTot("INTRO0") && r2.isCutsceneTot("FINAL") && !r2.isCutsceneTot("MENU"));
	CHECK(r2.isTransitionTot("INTER") && r2.isTransitionTot("MENU"));
	CHECK(r2.isCopyProtectionCall("INTRO0", 1722) && r2.isCopyProtectionCall("INTRO0", 1746) &&
	      !r2.isCopyProtectionCall("INTRO0", 1700) && !r2.isCopyProtectionCall("INTRO", 1722));
	CHECK(r2.ignoresSwitchTotSub("GOB06", -1, 7) && !r2.ignoresSwitchTotSub("GOB05", -1, 7));
	CHECK(r2.hasLevelSelect() && r2.hasQuickSave() && r2.hasFastForward());
	const GameRules &r1 = gameRules(GameId::Gob1);
	CHECK(r1.levels == LevelSystem::Gob1 && r1.saves == SaveFormat::None);
	CHECK(r1.hasLevelSelect() && r1.hasQuickSave() && r1.hasFastForward());
	CHECK(r1.isCutsceneTot("INTRO") && r1.isCutsceneTot("FINAL") && !r1.isCutsceneTot("INTER"));
	CHECK(r1.isTransitionTot("INTER") && !r1.isTransitionTot("AVT003"));
	for (GameId id : {GameId::Gob1, GameId::Gob3}) {
		const GameRules &r = gameRules(id);
		CHECK(!r.isCutsceneTot("INTRO0") && !r.isCopyProtectionCall("INTRO0", 1722));
	}
	const GameRules &r3 = gameRules(GameId::Gob3);
	CHECK(r3.levels == LevelSystem::Gob3 && r3.saves == SaveFormat::Gob3);
	CHECK(r3.hasLevelSelect() && r3.hasQuickSave() && r3.hasFastForward() && r3.hasSpeech());
	CHECK(!r1.hasSpeech() && !r2.hasSpeech());
	CHECK(r3.isCutsceneTot("DEMO") && r3.isCutsceneTot("FINAL") && !r3.isCutsceneTot("EMAP1000"));
	CHECK(r3.isTransitionTot("INTER3") && !r3.isTransitionTot("MENU"));
	CHECK(r3.cdTrackName("ECHEQUI1") == "ECHIQUI1" && r3.cdTrackName("echequi2") == "ECHIQUI2");
	CHECK(r3.cdTrackName("NEF1") == "NEF1" && r2.cdTrackName("ECHEQUI1") == "ECHEQUI1");

	// Speech archives: a missing voice ITK is the English one (Goblins 3 only).
	CHECK(r3.archiveFallback("FRGOB3.ITK") && std::string(r3.archiveFallback("frgob3.itk")) == "USGOB3.ITK");
	CHECK(r3.archiveFallback("DEGOB3.ITK") && r3.archiveFallback("ITGOB3.ITK"));
	CHECK(!r3.archiveFallback("USGOB3.ITK") && !r3.archiveFallback("IMD.ITK"));
	CHECK(r3.isVoiceArchive("usgob3.itk") && r3.isVoiceArchive("FRGOB3.ITK") && !r3.isVoiceArchive("IMD.ITK"));
	CHECK(!r2.archiveFallback("FRGOB3.ITK") && !r1.archiveFallback("FRGOB3.ITK"));

	// Archive slots: later slots win, closeArchive(false) closes the last
	// non-base archive, and a new archive takes the first free slot.
	Folder slots(root, "slots");
	writeStk(slots.path / "DATA.STK", {{"X.DAT", {1}}, {"Z.DAT", {1}}});
	writeStk(slots.path / "IMD.ITK", {{"X.DAT", {2}}});
	writeStk(slots.path / "VOICE.ITK", {{"Y.DAT", {3}}, {"Z.DAT", {3}}});
	writeStk(slots.path / "MORE.STK", {{"Z.DAT", {4}}});
	{
		DataFiles files(slots.path);
		auto byte = [&](const char *name) {
			auto d = files.read(name);
			return d && d->size() == 1 ? int((*d)[0]) : -1;
		};
		CHECK(files.openArchive("DATA"));
		CHECK(files.openArchive("VOICE.ITK", false));
		CHECK(files.openArchive("IMD.ITK", false));
		CHECK(!files.openArchive("MISSING.ITK", false));
		CHECK(byte("X.DAT") == 2 && byte("Y.DAT") == 3 && byte("Z.DAT") == 3);
		CHECK(files.closeArchive(false)); // IMD.ITK
		CHECK(byte("X.DAT") == 1 && byte("Y.DAT") == 3);
		CHECK(files.closeArchive(false)); // VOICE.ITK
		CHECK(byte("Y.DAT") == -1 && byte("Z.DAT") == 1);
		CHECK(!files.closeArchive(false));
		CHECK(files.openArchive("MORE")); // takes VOICE's slot, before the free IMD slot
		CHECK(files.openArchive("IMD.ITK", false));
		CHECK(byte("Z.DAT") == 4 && byte("X.DAT") == 2);
		CHECK(files.closeArchive(true)); // MORE
		CHECK(byte("Z.DAT") == 1 && files.exists("X.DAT") && !files.exists("Y.DAT"));
	}

	fs::remove_all(root);
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("detect: all tests passed\n");
	return 0;
}
