// gob2native - LG webOS TV build
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the webOS build asks of the TV (SDL-webOS, Wayland): one window that
// covers the screen. The package is made by scripts/build-webos.sh
// (webos/appinfo.json; SDL2 from webosbrew's SDL-webOS in its lib/).

#pragma once

#ifdef GOB_WEBOS

#include <string>

namespace gob::webos {

// The pref root: data/ in the app's own folder (the program's, argv0's),
// the only place an app may write that keeps (the app runs jailed as
// "prisoner", with HOME there; installing an update replaces the package's
// files and leaves the rest). SDL's pref path would be the app's folder
// itself.
std::string dataFolder(const char *argv0);

// The size of a window that covers the screen: the TV's UI plane as SDL
// reports it (1920x1080 on most TVs, whatever the panel), else 1920x1080.
void screenSize(int &w, int &h);

} // namespace gob::webos

#endif
