// gob2native - LG webOS TV build
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the webOS build asks of the TV (SDL-webOS, Wayland): one window that
// covers the screen. The package is made by scripts/build-webos.sh
// (webos/appinfo.json; SDL2 from webosbrew's SDL-webOS in its lib/).

#pragma once

#ifdef GOB_WEBOS

#include <imgui.h>

#include <string>
#include <vector>

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

// USB storage the TV has mounted: /tmp/usb/<disk>/<partition>, which the
// app's jail sees too. Cheap: polled every few seconds.
std::vector<std::string> usbDrives();

// The remote's colour keys, as the app names what they do: a chip in the
// key's colour, then a label (the key tips under a game or the library).
enum class RemoteKey { Red, Green, Yellow, Blue };
ImU32 keyColour(RemoteKey key);
// A rounded chip `height` high (1.8 times as wide), centred at `centre`.
void drawKeyChip(ImDrawList &dl, RemoteKey key, ImVec2 centre, float height, int alpha = 255);
struct KeyTip {
	RemoteKey key;
	std::string label;
	bool enabled = true; // dimmed when false (nothing to do now)
	bool lit = false;    // outlined (a switch that is on)
};
// The tips in a row centred between `left` and `right` in the strip from
// `top`, `height` high (text about 0.42 of it).
void drawKeyTips(ImDrawList &dl, ImFont *font, const std::vector<KeyTip> &tips, float left, float right, float top,
                 float height);

} // namespace gob::webos

#endif
