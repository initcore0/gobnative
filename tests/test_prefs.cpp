// gob2native - pref folder tests: the Gobliins 2 folder migration from
// gob2native/ to gobnative/ and the shared app settings, in a scratch folder
// (never the real ~/Library/Application Support)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/prefs.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace gob;
namespace fs = std::filesystem;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

[[maybe_unused]] void write(const fs::path &path, const std::string &text) {
	fs::create_directories(path.parent_path());
	std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path &path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

void testFresh(const fs::path &base) {
	const fs::path root = base / "fresh/gobnative";
	PrefFolder p = gamePrefFolder(root, "Gobliins2", true);
	CHECK(p.dir == root / "Gobliins2");
	CHECK(fs::is_directory(p.dir));
	CHECK(p.log.empty() && !p.migrated && !p.legacy);
	CHECK(!fs::exists(base / "fresh/gob2native")); // nothing made on the old side
	CHECK(!fs::exists(root / "migration.log"));
	// Another game: no migration, just the folder (trailing slash as SDL gives it).
	PrefFolder g1 = gamePrefFolder(root.string() + "/", "Gobliiins", false);
	CHECK(g1.dir == root / "Gobliiins" && fs::is_directory(g1.dir));
}

// The migration from gob2native/ exists only where gob2native ran (macOS);
// on Windows gamePrefFolder() never migrates (prefs.cpp).
#ifndef _WIN32
ino_t inode(const fs::path &path) {
	struct stat st {};
	return lstat(path.c_str(), &st) == 0 ? st.st_ino : 0;
}

// A Gobliins 2 folder the way a player's looks.
void makeOldFolder(const fs::path &dir) {
	write(dir / "settings.ini", "[video]\nwindow_scale=4\nhd_pack=Mine\n");
	write(dir / "gob2-slot00.sav", "G2NS slot 0");
	write(dir / "gob2-slot14.sav", "G2NS quick");
	write(dir / "gob2-notes.sav", "notes");
	write(dir / "HDPacks/Mine/pack.json", "{\"scale\": 4}");
	write(dir / "HDPacks/Mine/GOB01/sprite_01.png", "png");
	write(dir / "HDPacks/Mine/GOB01/deep/nested/frame.png", "png2");
	write(dir / "HDPacks/Comparisons/GOB01.png", "cmp");
	write(dir / "Screenshots/shot1.png", "shot");
	fs::create_directories(dir / "Music");
}

bool sameAsOld(const fs::path &dir) {
	return read(dir / "settings.ini") == "[video]\nwindow_scale=4\nhd_pack=Mine\n" &&
	       read(dir / "gob2-slot00.sav") == "G2NS slot 0" && read(dir / "gob2-slot14.sav") == "G2NS quick" &&
	       read(dir / "gob2-notes.sav") == "notes" && read(dir / "HDPacks/Mine/pack.json") == "{\"scale\": 4}" &&
	       read(dir / "HDPacks/Mine/GOB01/sprite_01.png") == "png" &&
	       read(dir / "HDPacks/Mine/GOB01/deep/nested/frame.png") == "png2" &&
	       read(dir / "HDPacks/Comparisons/GOB01.png") == "cmp" && read(dir / "Screenshots/shot1.png") == "shot" &&
	       fs::is_directory(dir / "Music");
}

void testMigrate(const fs::path &base) {
	const fs::path root = base / "move/gobnative", old = base / "move/gob2native/Gobliins2";
	makeOldFolder(old);
	write(base / "move/gob2native/other.txt", "left alone");
	const ino_t before = inode(old);
	// An older build running now has a file open in the folder.
	std::ofstream running(old / "running.txt");
	running << "before ";

	PrefFolder p = gamePrefFolder(root.string() + "/", "Gobliins2", true);
	CHECK(p.migrated && !p.legacy);
	CHECK(p.dir == root / "Gobliins2");
	CHECK(inode(root / "Gobliins2") == before); // moved, not copied
	CHECK(sameAsOld(root / "Gobliins2"));
	CHECK(fs::is_symlink(old));
	CHECK(fs::read_symlink(old) == fs::path("../gobnative/Gobliins2"));
	CHECK(fs::equivalent(old, root / "Gobliins2"));
	CHECK(sameAsOld(old)); // what an older build sees through the link
	CHECK(read(base / "move/gob2native/other.txt") == "left alone");
	CHECK(p.log.size() == 2);
	const std::string log = read(root / "migration.log");
	CHECK(log.find("moved \"" + old.string() + "\" to \"" + (root / "Gobliins2").string()) != std::string::npos);
	CHECK(log.find("linked") != std::string::npos);

	// The running build's open file and its later writes by path land in the new folder.
	running << "after";
	running.close();
	CHECK(read(root / "Gobliins2/running.txt") == "before after");
	write(old / "settings.ini", "[video]\nwindow_scale=5\n"); // an older build saving its settings
	CHECK(read(root / "Gobliins2/settings.ini") == "[video]\nwindow_scale=5\n");
	CHECK(fs::is_symlink(old)); // still the link, not replaced by a file
	// An older build making its folders (SDL's mkdir, create_directories) goes through the link.
	CHECK(mkdir(old.c_str(), 0700) != 0);
	fs::create_directories(old / "HDPacks");
	CHECK(fs::is_directory(root / "Gobliins2/HDPacks/Mine"));
	write(old / "settings.ini", "[video]\nwindow_scale=4\nhd_pack=Mine\n");

	// The second start does nothing.
	const std::string logBefore = read(root / "migration.log");
	PrefFolder again = gamePrefFolder(root, "Gobliins2", true);
	CHECK(again.dir == root / "Gobliins2" && !again.migrated && !again.legacy && again.log.empty());
	CHECK(read(root / "migration.log") == logBefore);
	CHECK(fs::is_symlink(old) && inode(root / "Gobliins2") == before);
}

void testBothExist(const fs::path &base) {
	const fs::path root = base / "both/gobnative", old = base / "both/gob2native/Gobliins2";
	makeOldFolder(old);
	write(root / "Gobliins2/settings.ini", "new");
	PrefFolder p = gamePrefFolder(root, "Gobliins2", true);
	CHECK(p.dir == root / "Gobliins2" && !p.migrated && !p.legacy);
	CHECK(read(root / "Gobliins2/settings.ini") == "new");
	CHECK(fs::is_directory(old) && !fs::is_symlink(old) && sameAsOld(old));
	CHECK(p.log.size() == 1);
}

void testRenameFails(const fs::path &base) {
	// The new root can't take the folder (read-only): use the old one in place.
	const fs::path root = base / "readonly/gobnative", old = base / "readonly/gob2native/Gobliins2";
	makeOldFolder(old);
	fs::create_directories(root);
	fs::permissions(root, fs::perms::owner_read | fs::perms::owner_exec);
	PrefFolder p = gamePrefFolder(root, "Gobliins2", true);
	CHECK(p.legacy && !p.migrated);
	CHECK(p.dir == old);
	CHECK(fs::is_directory(old) && !fs::is_symlink(old) && sameAsOld(old));
	CHECK(!fs::exists(root / "Gobliins2"));
	CHECK(p.log.size() == 1 && p.log[0].find("could not move") != std::string::npos);
	fs::permissions(root, fs::perms::owner_all);
	// Once it can, the next start moves it.
	PrefFolder later = gamePrefFolder(root, "Gobliins2", true);
	CHECK(later.migrated && later.dir == root / "Gobliins2" && sameAsOld(later.dir) && fs::is_symlink(old));
}

void testLinkFails(const fs::path &base) {
	// An older build makes an empty folder in the moment between the move
	// and the link: the link fails, the folder is moved back (over the empty
	// one) and used in place.
	const fs::path root = base / "nolink/gobnative", old = base / "nolink/gob2native/Gobliins2";
	makeOldFolder(old);
	const ino_t before = inode(old);
	prefsAfterMoveHook = [](const fs::path &src) { fs::create_directory(src); };
	PrefFolder p = gamePrefFolder(root, "Gobliins2", true);
	CHECK(p.legacy && !p.migrated && p.dir == old);
	CHECK(inode(old) == before && !fs::is_symlink(old) && sameAsOld(old));
	CHECK(!fs::exists(root / "Gobliins2"));
	CHECK(p.log.size() == 2 && p.log[1].find("moved the folder back") != std::string::npos);
	// Something that can't be replaced takes the place: everything stays in the new folder.
	prefsAfterMoveHook = [](const fs::path &src) { write(src, "a file"); };
	PrefFolder q = gamePrefFolder(root, "Gobliins2", true);
	prefsAfterMoveHook = nullptr;
	CHECK(q.dir == root / "Gobliins2" && !q.legacy && q.migrated);
	CHECK(inode(root / "Gobliins2") == before && sameAsOld(root / "Gobliins2"));
	CHECK(read(old) == "a file");
	CHECK(q.log.size() == 2 && q.log[1].find("nor move the folder back") != std::string::npos);
}

void testLinks(const fs::path &base) {
	const fs::path root = base / "links/gobnative", oldRoot = base / "links/gob2native";
	// The player's own link to a folder elsewhere: used in place.
	makeOldFolder(base / "links/elsewhere/Gobliins2");
	fs::create_directories(oldRoot);
	fs::create_directory_symlink(base / "links/elsewhere/Gobliins2", oldRoot / "Gobliins2");
	PrefFolder p = gamePrefFolder(root, "Gobliins2", true);
	CHECK(p.legacy && p.dir == oldRoot / "Gobliins2" && sameAsOld(p.dir));
	CHECK(!fs::exists(root / "Gobliins2"));
	// Our link whose target was removed: a new folder, which the link reaches again.
	fs::remove(oldRoot / "Gobliins2");
	fs::create_directory_symlink("../gobnative/Gobliins2", oldRoot / "Gobliins2");
	PrefFolder q = gamePrefFolder(root, "Gobliins2", true);
	CHECK(q.dir == root / "Gobliins2" && fs::is_directory(q.dir) && !q.legacy && !q.migrated);
	CHECK(fs::equivalent(oldRoot / "Gobliins2", root / "Gobliins2"));
	// A file where the old folder was: left alone.
	const fs::path root2 = base / "file/gobnative";
	write(base / "file/gob2native/Gobliins2", "a file");
	PrefFolder f = gamePrefFolder(root2, "Gobliins2", true);
	CHECK(f.dir == root2 / "Gobliins2" && fs::is_directory(f.dir));
	CHECK(read(base / "file/gob2native/Gobliins2") == "a file");
}

#endif // _WIN32

void testAppSettings(const fs::path &base) {
	const std::string path = (base / "app/settings.ini").string();
	fs::create_directories(base / "app");
	AppSettings a;
	CHECK(!a.load(path));
	for (int i = 0; i < 10; i++)
		a.addRecent("/games/g" + std::to_string(i), "gob2");
	a.addRecent("/games/g3/", "gob3"); // again, with a slash: moves to the front
	CHECK(int(a.recentFolders.size()) == AppSettings::kMaxRecent);
	// Folders are kept absolute (D:\games\g3 on Windows).
	auto absolute = [](const char *p) { return fs::absolute(p).lexically_normal().string(); };
	CHECK(a.recentFolders[0] == absolute("/games/g3") && a.recentFolders[1] == absolute("/games/g9"));
	CHECK(a.save(path));
	AppSettings b;
	CHECK(b.load(path));
	CHECK(b.recentFolders == a.recentFolders && b.lastGame == "gob3");
	b.recentFolders.resize(2);
	CHECK(b.save(path));
	AppSettings c;
	CHECK(c.load(path) && c.recentFolders.size() == 2);
	CHECK(read(path).find("folder3") == std::string::npos); // no empty entries left behind
}

} // namespace

int main() {
	const fs::path base = fs::temp_directory_path() / ("gobnative_test_prefs_" + std::to_string(getpid()));
	fs::remove_all(base);
	fs::create_directories(base);
	testFresh(base);
#ifndef _WIN32
	testMigrate(base);
	testBothExist(base);
	if (geteuid() != 0) // root ignores the read-only folder (a rooted TV)
		testRenameFails(base);
	testLinkFails(base);
	testLinks(base);
#endif
	testAppSettings(base);
	std::error_code ec;
	fs::remove_all(base, ec);
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("prefs: all tests passed\n");
	return 0;
}
