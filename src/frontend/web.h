// gob2native - web build
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the web build (Emscripten) asks of the browser; the JavaScript side
// is web/library_gobnative.js. The app's folder (the pref root, kRoot) is
// kept in the browser's IndexedDB: loaded into memory before main() runs
// and written back after changes (sync(); also every few seconds and when
// the page is hidden). To spare memory, the games' big files, HD packs,
// music and screenshots stay in IndexedDB (empty files in their place)
// until a game starts (load()). Files the player picks (<input type=file>) are read
// asynchronously into the in-memory file system; the app polls for them
// once per frame. Nothing leaves the browser.

#pragma once

#ifdef __EMSCRIPTEN__

#include <string>
#include <vector>

namespace gob::web {

constexpr const char *kRoot = "/gobnative";            // persistent (IndexedDB)
constexpr const char *kTemp = "/tmp/gobnative-import"; // picked installers, in memory only

// The page is served from this computer (localhost): developer options
// (?installer=, ?arg=) are honoured.
bool isLocalhost();

// Writes what changed under kRoot to IndexedDB (soon; one write at a time,
// a request during a write makes another one after it).
void sync();
bool syncBusy();
// The periodic write (every few seconds); off while an import writes a
// partial folder.
void setAutoSync(bool on);

// Reads the files under `folders` that were left in the browser's storage
// (a game's folder and its settings folder, before the game opens); they
// stay in memory from then on. loading() is true until they are in.
void load(const std::vector<std::string> &folders);
bool loading();
// File `path` in kTemp (a picked installer) is read once from start to
// end from now on: what was read is dropped from memory as the reading
// goes on (reading it again fails).
void freeAsRead(const std::string &path);

enum class PickState { Idle, Choosing, Reading, Done, Cancelled, Failed };
struct PickOptions {
	std::string accept;        // e.g. ".exe"
	bool multiple = false;     // several files
	bool folder = false;       // a folder (webkitdirectory)
	bool topLevelOnly = false; // of a folder, only the files directly in it
	std::string destDir;       // where the files are written (a folder keeps its own name below it unless topLevelOnly)
};
// Opens the browser's file dialog (only works right after a click or a
// key press of the player).
void pick(const PickOptions &options);
PickState pickState();
float pickProgress();      // while Reading: 0..1
std::string pickCurrent(); // while Reading: the file being read
// After Done: the files written (absolute paths); after Failed, `error`
// says why. Returns to Idle.
std::vector<std::string> takePicked(std::string &error);

// Offers file `path` as a download named `name`.
void download(const std::string &path, const std::string &name, const std::string &mime);
// Shows file `path` (a PDF) in a new browser tab.
void openFile(const std::string &path, const std::string &mime);
// Files dropped onto the page since the last call (read into kTemp).
std::vector<std::string> takeDropped();
// Opens a web page in a new tab.
void openUrl(const std::string &url);
// Writes the storage, then loads the page again with `query` (e.g. "" for
// the library).
void reload(const std::string &query);
// The page's status line under the canvas (empty hides it).
void status(const std::string &text);
// The main pointer is a finger (a phone or a tablet).
bool isTouch();
// Phones and tablets (fingers, in a browser tab rather than a home-screen
// app): the page fills the screen, held sideways where the browser allows.
// Works only right after a tap or a click.
void enterFullscreen();
// Asks the browser to keep the storage (the games, saves) instead of
// clearing it when space runs low or the site isn't visited for a while.
void requestPersistentStorage();
// How much the storage holds and whether the browser keeps it ("" until
// known).
std::string storageInfo();
// The file dialog can choose a folder; not on phones, where the player
// chooses all the files in it instead (pick() does that by itself).
bool canPickFolders();
// A game is running (the page shows a phone held upright a hint to turn
// it).
void setPlaying(bool playing);
// Where the touch bar's Keys button is (window points; w 0 when it isn't
// shown). A phone shows its keyboard only for a text field focused during
// a tap, so the page catches taps on the button itself and focuses a
// hidden field, whose keys come back as SDL key and text events.
void setKeyboardButton(float x, float y, float w, float h);

} // namespace gob::web

#endif
