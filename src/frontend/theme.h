// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The front end's look, shared by the game library and the overlay: a warm
// dark theme (soft colours, rounded corners) and its fonts, Nunito (SIL
// Open Font License, third_party/fonts/nunito, compiled into the app) as
// the default font, its bold for headings and buttons, and ImGui's own
// fixed-width font for tables of values. ImGui 1.92 draws each at any
// size (ImGui::PushFont(font, size)).

#pragma once

#include <imgui.h>

namespace gob {

namespace colour {
constexpr ImU32 kBackground = IM_COL32(25, 21, 30, 255);
constexpr ImU32 kCard = IM_COL32(38, 33, 46, 255);
constexpr ImU32 kCardHover = IM_COL32(46, 40, 56, 255);
constexpr ImU32 kBorder = IM_COL32(60, 52, 72, 255);
constexpr ImU32 kText = IM_COL32(245, 239, 231, 255);
constexpr ImU32 kMuted = IM_COL32(176, 166, 184, 255);
constexpr ImU32 kFaint = IM_COL32(120, 110, 132, 255);
constexpr ImU32 kAmber = IM_COL32(242, 168, 70, 255);
constexpr ImU32 kAmberHover = IM_COL32(250, 188, 102, 255);
constexpr ImU32 kAmberActive = IM_COL32(222, 144, 46, 255);
constexpr ImU32 kOnAmber = IM_COL32(48, 28, 8, 255);
constexpr ImU32 kGreen = IM_COL32(84, 178, 96, 255);
constexpr ImU32 kGreenHover = IM_COL32(104, 198, 114, 255);
constexpr ImU32 kGreenActive = IM_COL32(70, 156, 82, 255);
constexpr ImU32 kGood = IM_COL32(126, 214, 134, 255);
constexpr ImU32 kBad = IM_COL32(240, 118, 102, 255);
} // namespace colour

inline ImVec4 vec(ImU32 c) {
	return ImGui::ColorConvertU32ToFloat4(c);
}

inline ImU32 withAlpha(ImU32 c, int alpha) {
	return (c & ~IM_COL32_A_MASK) | (ImU32(alpha) << IM_COL32_A_SHIFT);
}

struct UiFonts {
	ImFont *regular = nullptr; // the default (the first font added)
	ImFont *bold = nullptr;
	ImFont *mono = nullptr;
};

// Adds the three fonts to the atlas (call before the first frame).
UiFonts addUiFonts(ImFontAtlas &atlas);

// The colours and rounded corners (and a 16-point font size); the spacing
// is ImGui's, for the callers to change.
void applyUiStyle(ImGuiStyle &style);

} // namespace gob
