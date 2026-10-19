// gob2native - LG webOS TV build
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/webos.h"

#include <SDL.h>

#include <cstdio>
#include <filesystem>

namespace gob::webos {

void screenSize(int &w, int &h) {
	w = 1920;
	h = 1080;
	SDL_DisplayMode mode{};
	if (SDL_GetDesktopDisplayMode(0, &mode) == 0 && mode.w > 0 && mode.h > 0) {
		w = mode.w;
		h = mode.h;
	}
	int pw = 0, ph = 0;
	SDL_webOSGetPanelResolution(&pw, &ph);
	std::fprintf(stderr, "webos: window %dx%d, panel %dx%d\n", w, h, pw, ph);
}

std::string dataFolder(const char *argv0) {
	// Not SDL_GetBasePath(): in the jail /proc/self/exe names the folder as
	// seen from outside (/var/palm/jail/<app id>/...). The launcher starts
	// the program by its full path.
	std::filesystem::path dir = std::filesystem::path(argv0).parent_path();
	return ((dir.empty() ? std::filesystem::path(".") : dir) / "data").string();
}

} // namespace gob::webos
