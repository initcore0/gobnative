// gob2native - web build
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the web build (Emscripten) asks of the browser; the JavaScript side
// is web/library_gobnative.js. The app's folder (the pref root, kRoot) is
// kept in the browser's IndexedDB: loaded into memory before main() runs
// and written back after changes (sync(); also every few seconds and when
// the page is hidden). Files the player picks (<input type=file>) are read
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

} // namespace gob::web

#endif
