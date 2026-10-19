// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's help over the game, off unless the player asks for it
// (Settings [hints]): the clickable spots, a soft pulsing glow over
// everything in the room the goblins can act on (Interpreter::clickSpots:
// Gobliiins' items on the level map, the other games' room hotspots).

#include "engine/interpreter.h"
#include "frontend/overlay.h"
#include "frontend/settings.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <utility>

namespace gob {

namespace {

constexpr int kScreenW = Draw::kScreenW, kScreenH = Draw::kScreenH;

class Hints {
public:
	explicit Hints(OverlayHost &host) : _host(host) {}

	bool spotsActive() { return _host.settings().showSpots && _host.engine(); }

	// Each spot is filled, and outlined along its border only: a rectangle's
	// edge is left out where another rectangle of the spot continues it (a
	// Gobliiins item is a group of map cells). Edges are filled rectangles in
	// whole window points, so they are crisp at any scale.
	void drawSpots(ImDrawList &dl) {
		Interpreter *in = _host.engine();
		if (!in)
			return;
		const GlPresenter::Rect vp = _host.gameRect();
		const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
		auto px = [&](int gx) { return std::round(float((vp.x + double(gx) * vp.w / kScreenW) / fb.x)); };
		auto py = [&](int gy) { return std::round(float((vp.y + double(gy) * vp.h / kScreenH) / fb.y)); };
		const float pixel = float(vp.w) / kScreenW / fb.x; // one game pixel in points
		const float t = std::max(1.0f, std::round(pixel * 0.5f));
		const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 2.6f);
		const ImU32 fill = withAlpha(colour::kAmber, int(26 + 30 * pulse));
		const ImU32 edge = withAlpha(colour::kAmber, int(150 + 90 * pulse));
		const ImU32 shadow = IM_COL32(0, 0, 0, 110);

		for (const Interpreter::ClickSpot &spot : in->clickSpots()) {
			std::set<std::pair<int, int>> corners; // (left, top) of each rectangle
			for (const auto &r : spot.rects)
				corners.insert({r.left, r.top});
			auto has = [&](int left, int top) { return spot.rects.size() > 1 && corners.count({left, top}); };
			for (int pass = 0; pass < 3; pass++) // the fills, a dark line for contrast, the edges
				for (const auto &r : spot.rects) {
					const int w = r.right - r.left + 1, h = r.bottom - r.top + 1;
					const ImVec2 a(px(r.left), py(r.top)), b(px(r.right + 1), py(r.bottom + 1));
					if (pass == 0) {
						dl.AddRectFilled(a, b, fill);
						continue;
					}
					const float o = pass == 1 ? 1.0f : 0.0f;
					const ImU32 col = pass == 1 ? shadow : edge;
					if (!has(r.left, r.top - h))
						dl.AddRectFilled(ImVec2(a.x - o, a.y - o), ImVec2(b.x + o, a.y + t + o), col);
					if (!has(r.left, r.top + h))
						dl.AddRectFilled(ImVec2(a.x - o, b.y - t - o), ImVec2(b.x + o, b.y + o), col);
					if (!has(r.left - w, r.top))
						dl.AddRectFilled(ImVec2(a.x - o, a.y - o), ImVec2(a.x + t + o, b.y + o), col);
					if (!has(r.left + w, r.top))
						dl.AddRectFilled(ImVec2(b.x - t - o, a.y - o), ImVec2(b.x + o, b.y + o), col);
				}
		}
	}

private:
	OverlayHost &_host;
};

} // namespace

void addHints(Overlay &overlay, OverlayHost &host) {
	auto hints = std::make_shared<Hints>(host);
	overlay.addGameLayer([hints] { return hints->spotsActive(); }, [hints](ImDrawList &dl) { hints->drawSpots(dl); });
}

} // namespace gob
