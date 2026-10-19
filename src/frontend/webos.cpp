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
	case RemoteKey::Back: return IM_COL32(120, 112, 130, 255);
	}
	return IM_COL32_WHITE;
}

void drawKeyChip(ImDrawList &dl, RemoteKey key, ImVec2 centre, float height, int alpha) {
	const float w = height * 1.8f;
	dl.AddRectFilled(ImVec2(centre.x - w / 2, centre.y - height / 2), ImVec2(centre.x + w / 2, centre.y + height / 2),
	                 withAlpha(keyColour(key), alpha), height / 2);
	if (key == RemoteKey::Back) { // the remote's Back key: a grey chip with a hooked arrow
		const ImU32 ink = withAlpha(colour::kText, alpha);
		const float r = height * 0.22f, t = std::max(1.5f, height * 0.09f);
		const ImVec2 c(centre.x + height * 0.12f, centre.y);
		dl.PathArcTo(c, r, -1.5707963f, 1.5707963f, 12);
		dl.PathStroke(ink, 0, t);
		dl.AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x - height * 0.42f, c.y - r), ink, t);
		dl.AddTriangleFilled(ImVec2(c.x - height * 0.52f, c.y - r), ImVec2(c.x - height * 0.32f, c.y - r - height * 0.16f),
		                     ImVec2(c.x - height * 0.32f, c.y - r + height * 0.16f), ink);
	}
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

// A big button drawn in full: rounded, a colour key's chip at the left
// (when `key` >= 0), the label centred. Disabled: greyed, never clicked.
// With `focused` (the arrow keys' ring) a thick white ring, and `press`
// (OK) clicks it.
bool tvButton(const char *id, const std::string &label, ImVec2 size, ImFont *font, float fontSize, ImU32 base,
              ImU32 hover, ImU32 text, int key, bool enabled, bool focused, bool press) {
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const bool clicked = (ImGui::InvisibleButton(id, size) || (focused && press)) && enabled;
	const bool hovered = (ImGui::IsItemHovered() || focused) && enabled;
	const ImVec2 q(p.x + size.x, p.y + size.y);
	dl->AddRectFilled(p, q, enabled ? (hovered ? hover : base) : IM_COL32(64, 58, 72, 255), size.y * 0.22f);
	if (focused) {
		const float g = std::round(size.y * 0.08f);
		dl->AddRect(ImVec2(p.x - g, p.y - g), ImVec2(q.x + g, q.y + g), IM_COL32_WHITE, size.y * 0.22f + g, 0,
		            std::max(3.0f, size.y * 0.05f));
	} else if (hovered)
		dl->AddRect(p, q, IM_COL32(255, 255, 255, 150), size.y * 0.22f, 0, 2.0f);
	const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
	float tx = p.x + (size.x - ts.x) / 2;
	if (key >= 0) {
		const float chipH = fontSize * 0.8f, chipW = chipH * 1.8f, gap = fontSize * 0.5f;
		tx = p.x + (size.x - ts.x - chipW - gap) / 2 + chipW + gap;
		drawKeyChip(*dl, RemoteKey(key), ImVec2(tx - gap - chipW / 2, p.y + size.y / 2), chipH,
		                   enabled ? 255 : 90);
	}
	dl->AddText(font, fontSize, ImVec2(std::round(tx), std::round(p.y + (size.y - ts.y) / 2)),
	            enabled ? text : colour::kFaint, label.c_str());
	return clicked;
}

} // namespace gob::webos
