// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What gobnative needs from Windows itself: its file dialogs
// (IFileOpenDialog) for the game library, the counterpart of the
// AppleScript dialogs on macOS; the console of the command prompt that
// started it (it is a GUI program); starting itself again. Strings are
// UTF-8. Only built on Windows.

#pragma once

#include <string>
#include <vector>

struct SDL_Window;

namespace gob {

// A folder; empty when cancelled. The dialog is modal to `owner`.
std::string windowsChooseFolder(SDL_Window *owner, const std::string &title);

// A file matching `pattern` (e.g. "*.exe", described as `filterName`),
// starting in the Downloads folder; empty when cancelled.
std::string windowsChooseFile(SDL_Window *owner, const std::string &title, const std::string &filterName,
                              const std::string &pattern);

// Started from a command prompt: stdout and stderr go to its console
// (unless they are redirected already), so --help and the logs show.
void windowsAttachConsole();

// Starts this program again with `args` (UTF-8, without the program
// itself); false if it couldn't.
bool windowsStartSelf(const std::vector<std::string> &args);

} // namespace gob
