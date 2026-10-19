// gob2native - LG webOS TV build
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/webos.h"

#include "frontend/theme.h"

#include <SDL.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
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

std::vector<std::string> usbDrives() {
	std::vector<std::string> drives;
	std::error_code ec;
	for (const auto &disk : std::filesystem::directory_iterator("/tmp/usb", ec))
		for (const auto &part : std::filesystem::directory_iterator(disk.path(), ec))
			if (part.is_directory(ec))
				drives.push_back(part.path().string());
	std::sort(drives.begin(), drives.end());
	return drives;
}

ImU32 keyColour(RemoteKey key) {
	switch (key) {
	case RemoteKey::Red: return IM_COL32(214, 48, 49, 255);
	case RemoteKey::Green: return IM_COL32(39, 174, 96, 255);
	case RemoteKey::Yellow: return IM_COL32(241, 196, 15, 255);
	case RemoteKey::Blue: return IM_COL32(41, 128, 185, 255);
	}
	return IM_COL32_WHITE;
}

void drawKeyChip(ImDrawList &dl, RemoteKey key, ImVec2 centre, float height, int alpha) {
	const float w = height * 1.8f;
	dl.AddRectFilled(ImVec2(centre.x - w / 2, centre.y - height / 2), ImVec2(centre.x + w / 2, centre.y + height / 2),
	                 withAlpha(keyColour(key), alpha), height / 2);
}

void drawKeyTips(ImDrawList &dl, ImFont *font, const std::vector<KeyTip> &tips, float left, float right, float top,
                 float height) {
	const float size = std::round(height * 0.42f), keyH = size * 0.9f, keyW = keyH * 1.8f, gap = size * 0.5f,
	            spacing = size * 1.8f;
	float total = 0;
	for (const KeyTip &t : tips)
		total += keyW + gap + font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.label.c_str()).x + spacing;
	total -= spacing;
	float x = std::round(left + (right - left - total) / 2);
	const float cy = top + height / 2;
	for (const KeyTip &t : tips) {
		const int alpha = t.enabled ? 255 : 90;
		drawKeyChip(dl, t.key, ImVec2(x + keyW / 2, cy), keyH, alpha);
		if (t.lit)
			dl.AddRect(ImVec2(x - 3, cy - keyH / 2 - 3), ImVec2(x + keyW + 3, cy + keyH / 2 + 3), colour::kText,
			           keyH / 2 + 3, 0, 2.0f);
		x += keyW + gap;
		const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, t.label.c_str());
		dl.AddText(font, size, ImVec2(x, cy - ts.y / 2),
		           t.lit ? colour::kText : withAlpha(t.enabled ? colour::kText : colour::kFaint, alpha), t.label.c_str());
		x += ts.x + spacing;
	}
}

} // namespace gob::webos
