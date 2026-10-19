// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The player's help over the game, off unless the player asks for it
// (Settings [hints]):
// - the first time a level with hints is played, a question: hints when
//   asked for, also the clickable spots, or no thanks;
// - the hint key (HintsControl::request) shows a card over the picture: a
//   nudge, then who does what where (the target glows and an arrow marks
//   the goblin), then the solution (engine/hints.h, assets/hints/*.txt);
//   it goes away by itself after a while, when the step is done, or when
//   the key is pressed after the solution;
// - the clickable spots, a soft pulsing glow over everything in the room
//   the goblins can act on (Interpreter::clickSpots: Gobliiins' items on
//   the level map, the other games' room hotspots);
// - the Hints tab: the switches, and the level's progress.

#include "engine/goblins_v1.h"
#include "engine/hints.h"
#include "engine/interpreter.h"
#include "frontend/overlay.h"
#include "frontend/settings.h"

#include "imgui.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace gob::embedded {
extern const unsigned char hintsGob1[];
extern const std::size_t hintsGob1Size;
} // namespace gob::embedded

namespace gob {

namespace {

constexpr int kScreenW = Draw::kScreenW, kScreenH = Draw::kScreenH;
constexpr uint32_t kCardMs = 25000; // the card goes away by itself after this long

const char *const kGoblinNames[] = {"the picker", "the fighter", "the wizard"};

std::string capitalised(std::string s) {
	if (!s.empty() && s[0] >= 'a' && s[0] <= 'z')
		s[0] = char(s[0] - 'a' + 'A');
	return s;
}

class Hints : public HintsControl {
public:
	Hints(Overlay &overlay, OverlayHost &host, std::string keyName)
		: _overlay(overlay), _host(host), _keyName(std::move(keyName)) {
		if (host.currentGame().id == GameId::Gob1) {
			std::string error;
			const std::string text(reinterpret_cast<const char *>(embedded::hintsGob1), embedded::hintsGob1Size);
			if (!_book.parse(text, error))
				std::fprintf(stderr, "hints: %s\n", error.c_str());
		}
	}

	// --- HintsControl

	bool available() override { return _host.settings().hintsOn && level(); }

	void request() override {
		const HintLevel *l = level();
		if (!l)
			return;
		const uint32_t now = SDL_GetTicks();
		if (!_shown) {
			_shown = true;
			_tier = std::max(_tier, 1);
		} else if (_tier < 3 && current(*l) < l->steps.size()) {
			_tier++;
		} else {
			_shown = false;
		}
		_shownAt = now;
	}

	// --- every frame (the game layer's test)

	bool update() {
		Interpreter *in = _host.engine();
		const HintLevel *l = level();
		// The step (or the level) has changed: start again from a nudge.
		const std::string key = l ? l->level + "/" + std::to_string(current(*l)) : std::string();
		if (key != _stepKey) {
			_stepKey = key;
			_shown = false;
			_tier = 0;
		}
		if (_shown && SDL_GetTicks() - _shownAt > kCardMs)
			_shown = false;
		maybeAsk(in, l);
		return in && ((_host.settings().showSpots) || (_shown && l));
	}

	void draw(ImDrawList &dl) {
		Interpreter *in = _host.engine();
		if (!in)
			return;
		const GlPresenter::Rect vp = _host.gameRect();
		const ImVec2 fb = ImGui::GetIO().DisplayFramebufferScale;
		_x0 = float(vp.x / fb.x);
		_y0 = float(vp.y / fb.y);
		_w = float(vp.w / fb.x);
		_h = float(vp.h / fb.y);
		const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 2.6f);
		const std::vector<Interpreter::ClickSpot> spots = in->clickSpots();
		if (_host.settings().showSpots)
			for (const Interpreter::ClickSpot &s : spots)
				drawRegion(dl, s.rects, withAlpha(colour::kAmber, int(26 + 30 * pulse)),
				           withAlpha(colour::kAmber, int(150 + 90 * pulse)));
		const HintLevel *l = level();
		if (!_shown || !l)
			return;
		const size_t cur = current(*l);
		if (cur < l->steps.size() && _tier >= 2)
			drawTarget(dl, *in, l->steps[cur], spots, pulse);
		// What the card should leave in sight: the goblins and the target.
		std::vector<ImVec4> avoid;
		if (const GoblinsV1 *g = in->goblinsV1())
			for (int i = 0; i < 3; i++)
				if (const GoblinsV1::Object *o = g->goblin(i); o && o->right > o->left)
					avoid.push_back(box(o->left, o->top - 16, o->right, o->bottom));
		if (cur < l->steps.size() && l->steps[cur].pointX >= 0)
			avoid.push_back(box(l->steps[cur].pointX - 12, l->steps[cur].pointY - 12, l->steps[cur].pointX + 12,
			                    l->steps[cur].pointY + 12));
		drawCard(dl, *l, cur, avoid);
	}

	// --- the Hints tab

	void tab() {
		Settings &s = _host.settings();
		bool changed = false;
		const std::string label = "Hints when I ask (" + keyName() + ")";
		changed |= ImGui::Checkbox(label.c_str(), &s.hintsOn);
		ImGui::SetItemTooltip("Each press shows more: a nudge, then who does what where, then the solution.");
		changed |= ImGui::Checkbox("Show clickable spots", &s.showSpots);
		ImGui::SetItemTooltip("A soft glow over everything in the room the goblins can act on.");
		if (changed)
			_host.settingsChanged();
		ImGui::Separator();
		if (_book.levels().empty()) {
			ImGui::TextWrapped("Hints are written for Gobliiins so far. The clickable spots work in every game.");
			return;
		}
		const HintLevel *l = level();
		if (!l) {
			ImGui::TextDisabled("Hints show while a level is being played.");
			return;
		}
		const size_t cur = current(*l);
		ImGui::Text("%s: %zu of %zu steps done", l->title.c_str(), std::min(cur, l->steps.size()), l->steps.size());
		if (cur >= l->steps.size())
			ImGui::TextWrapped("Everything here is done: on to the exit.");
		else if (s.hintsOn && ImGui::Button("Show a hint")) {
			_overlay.setOpen(false);
			request();
		}
	}

private:
	Overlay &_overlay;
	OverlayHost &_host;
	const std::string _keyName;
	HintBook _book;
	bool _shown = false;
	int _tier = 0; // 1 nudge, 2 also who and where, 3 also the solution
	uint32_t _shownAt = 0;
	std::string _stepKey;
	float _x0 = 0, _y0 = 0, _w = 0, _h = 0; // the picture, window points

	// The hint key as the player knows it: the touch bar's button while the
	// last pointer was a finger.
	std::string keyName() const { return _overlay.touchMode ? std::string("the Hint button") : _keyName; }

	// The level being played, if it has hints.
	const HintLevel *level() {
		Interpreter *in = _host.engine();
		return in && !_book.levels().empty() ? _book.level(in->currentLevel()) : nullptr;
	}
	size_t current(const HintLevel &l) { return HintBook::current(l, *_host.engine()); }

	// A rectangle of game pixels in window points (x0, y0, x1, y1).
	ImVec4 box(int left, int top, int right, int bottom) const {
		const ImVec2 a = point(float(left), float(top)), b = point(float(right + 1), float(bottom + 1));
		return ImVec4(a.x, a.y, b.x, b.y);
	}

	ImVec2 point(float gx, float gy) const {
		return ImVec2(std::round(_x0 + gx * _w / kScreenW), std::round(_y0 + gy * _h / kScreenH));
	}

	// Rectangles filled, and outlined along their border only: an edge is
	// left out where another rectangle continues it (a Gobliiins item is a
	// group of map cells). Edges are filled rectangles in whole points, so
	// they are crisp at any scale.
	void drawRegion(ImDrawList &dl, const std::vector<Interpreter::ClickSpot::Rect> &rects, ImU32 fill, ImU32 edge,
	                float thickness = 0) const {
		const float t = thickness > 0 ? thickness : std::max(1.0f, std::round(_w / kScreenW * 0.5f));
		const ImU32 shadow = IM_COL32(0, 0, 0, 110);
		std::set<std::pair<int, int>> corners; // (left, top) of each rectangle
		for (const auto &r : rects)
			corners.insert({r.left, r.top});
		auto has = [&](int left, int top) { return rects.size() > 1 && corners.count({left, top}); };
		for (int pass = 0; pass < 3; pass++) // the fills, a dark line for contrast, the edges
			for (const auto &r : rects) {
				const int w = r.right - r.left + 1, h = r.bottom - r.top + 1;
				const ImVec2 a = point(float(r.left), float(r.top)), b = point(float(r.right + 1), float(r.bottom + 1));
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

	// The step's target: the clickable spot under its point (or its item)
	// glowing green, else a ring at the point; an arrow over the goblin who
	// does it.
	void drawTarget(ImDrawList &dl, const Interpreter &in, const HintStep &step,
	                const std::vector<Interpreter::ClickSpot> &spots, float pulse) const {
		const ImU32 fill = withAlpha(colour::kGood, int(50 + 50 * pulse));
		const ImU32 edge = withAlpha(colour::kGood, int(200 + 55 * pulse));
		const float thick = std::max(2.0f, std::round(_w / kScreenW));
		bool drawn = false;
		for (const Interpreter::ClickSpot &s : spots) {
			bool hit = step.item > 0 && s.id == step.item;
			for (const auto &r : s.rects)
				hit |= step.pointX >= r.left && step.pointX <= r.right && step.pointY >= r.top && step.pointY <= r.bottom;
			if (hit) {
				drawRegion(dl, s.rects, fill, edge, thick);
				drawn = true;
				break; // the first one, as a click picks (a cell may hold two items)
			}
		}
		if (!drawn && step.pointX >= 0) {
			const ImVec2 c = point(float(step.pointX), float(step.pointY));
			const float r = (_w / kScreenW) * (8 + 3 * pulse);
			dl.AddCircle(c, r + 1, IM_COL32(0, 0, 0, 140), 0, thick + 2);
			dl.AddCircle(c, r, edge, 0, thick);
		}
		if (step.goblin < 0)
			return;
		const GoblinsV1 *g = in.goblinsV1();
		const GoblinsV1::Object *o = g ? g->goblin(step.goblin) : nullptr;
		if (!o || o->right <= o->left)
			return;
		const float bob = 3.0f * pulse * (_w / kScreenW);
		const ImVec2 tip = point(float(o->left + o->right) / 2, float(std::max<int>(o->top - 3, 8)));
		const float s = std::max(6.0f, _w / kScreenW * 5);
		const ImVec2 a(tip.x - s, tip.y - 1.6f * s - bob), b(tip.x + s, tip.y - 1.6f * s - bob), c(tip.x, tip.y - bob);
		dl.AddTriangleFilled(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y - 1), ImVec2(c.x, c.y + 2), IM_COL32(0, 0, 0, 160));
		dl.AddTriangleFilled(a, b, c, colour::kGood);
	}

	// The card in a corner of the picture (the top left one unless another
	// covers less of `avoid`): what the level is, then as much help as asked
	// for, then what the key does next.
	void drawCard(ImDrawList &dl, const HintLevel &l, size_t cur, const std::vector<ImVec4> &avoid) const {
		const UiFonts &fonts = _overlay.fonts();
		const float size = std::clamp(_h * 0.03f, 14.0f, 40.0f);
		const float pad = std::round(size * 0.8f), width = std::round(std::min(_w * 0.52f, size * 30));
		const float wrap = width - 2 * pad;
		struct Line {
			ImFont *font;
			float size;
			ImU32 colour;
			std::string text;
			float gap; // above it
		};
		std::vector<Line> lines;
		char head[160];
		const size_t n = l.steps.size();
		if (cur < n)
			std::snprintf(head, sizeof(head), "Hint: %s (step %zu of %zu)", l.title.c_str(), cur + 1, n);
		else
			std::snprintf(head, sizeof(head), "Hint: %s", l.title.c_str());
		lines.push_back({fonts.bold, size * 0.85f, colour::kAmber, head, 0});
		if (cur >= n) {
			lines.push_back({fonts.regular, size, colour::kText, "Everything here is done. On to the exit!", size * 0.4f});
		} else {
			const HintStep &s = l.steps[cur];
			lines.push_back({fonts.regular, size, colour::kText, s.nudge, size * 0.4f});
			if (_tier >= 2) {
				std::string who = s.goblin >= 0 ? capitalised(kGoblinNames[s.goblin]) : std::string("Anyone");
				if (s.action != HintStep::Action::None)
					who += std::string(", ") + hintActionName(s.action);
				lines.push_back({fonts.bold, size * 0.85f, colour::kGood, who, size * 0.6f});
				lines.push_back({fonts.regular, size, colour::kText, s.pointer, size * 0.15f});
			}
			if (_tier >= 3) {
				lines.push_back({fonts.bold, size * 0.85f, colour::kAmber, "Solution", size * 0.6f});
				lines.push_back({fonts.regular, size, colour::kText, s.solve, size * 0.15f});
			}
		}
		const bool more = cur < n && _tier < 3;
		const std::string foot = capitalised(keyName()) + (more ? (_tier == 1 ? ": who and where" : ": the solution") : ": hide");
		lines.push_back({fonts.regular, size * 0.8f, colour::kFaint, foot, size * 0.6f});

		float height = 2 * pad;
		for (const Line &line : lines)
			height += line.gap + line.font->CalcTextSizeA(line.size, FLT_MAX, wrap, line.text.c_str()).y;
		const float margin = std::round(size * 0.6f);
		// The bottom corners stay above Gobliiins' bar (the bottom 32 rows).
		const float right = _x0 + _w - margin - width, bottom = point(0, 168).y - margin - height;
		const ImVec2 corners[] = {{_x0 + margin, _y0 + margin}, {right, _y0 + margin}, {_x0 + margin, bottom},
		                          {right, bottom}};
		ImVec2 p0 = corners[0];
		float best = -1;
		for (const ImVec2 &c : corners) {
			float covered = 0;
			for (const ImVec4 &r : avoid)
				covered += std::max(0.0f, std::min(c.x + width, r.z) - std::max(c.x, r.x)) *
				           std::max(0.0f, std::min(c.y + height, r.w) - std::max(c.y, r.y));
			if (best < 0 || covered < best) {
				best = covered;
				p0 = c;
			}
		}
		const ImVec2 p1(p0.x + width, p0.y + height);
		dl.AddRectFilled(p0, p1, withAlpha(colour::kBackground, 236), size * 0.6f);
		dl.AddRect(p0, p1, withAlpha(colour::kAmber, 150), size * 0.6f, 0, 1.5f);
		float y = p0.y + pad;
		for (const Line &line : lines) {
			y += line.gap;
			dl.AddText(line.font, line.size, ImVec2(p0.x + pad, y), line.colour, line.text.c_str(), nullptr, wrap);
			y += line.font->CalcTextSizeA(line.size, FLT_MAX, wrap, line.text.c_str()).y;
		}
	}

	// --- the first-run question

	// Once, when a level with hints is first being played (not over the
	// intro): the answer goes to the settings, and so does having asked.
	void maybeAsk(Interpreter *in, const HintLevel *l) {
		Settings &s = _host.settings();
		if (s.hintsAsked || !l || !in || _overlay.isOpen() || !in->roomIdle())
			return;
		s.hintsAsked = true;
		_host.settingsChanged();
		_overlay.showDialog([this](float scale) { return askDialog(scale); });
		_overlay.dialogFocus = 1; // "Hints when I ask"
	}

	bool askDialog(float scale) {
		Settings &s = _host.settings();
		const UiFonts &fonts = _overlay.fonts();
		ImGuiIO &io = ImGui::GetIO();
		const float k = std::min(scale, io.DisplaySize.x / 1240.0f); // fits a narrow window
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, vec(IM_COL32(0, 0, 0, 110)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##hints", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		const float pad = 44 * k, width = 1160 * k, wrap = width - 2 * pad;
		const std::string body =
			"Gobliiins is a hard game. gobnative can help when you ask: press " + keyName() +
			" for a nudge, again for who does what where, and once more for the solution. "
			"You can change this any time in the menu's Hints tab.";
		const float bodyH = fonts.regular->CalcTextSizeA(29 * k, FLT_MAX, wrap, body.c_str()).y;
		const float bh = 76 * k, height = 40 * k + 56 * k + 24 * k + bodyH + 40 * k + bh + pad;
		const ImVec2 p0(std::round((io.DisplaySize.x - width) / 2), std::round((io.DisplaySize.y - height) / 2));
		const ImVec2 p1(p0.x + width, p0.y + height);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p0, p1, colour::kBackground, 20 * k);
		dl->AddRect(p0, p1, colour::kBorder, 20 * k, 0, 1.5f);
		dl->AddText(fonts.bold, 44 * k, ImVec2(p0.x + pad, p0.y + 40 * k), colour::kText, "Would you like hints?");
		dl->AddText(fonts.regular, 29 * k, ImVec2(p0.x + pad, p0.y + 120 * k), colour::kMuted, body.c_str(), nullptr,
		            wrap);
		const char *const labels[] = {"No thanks", "Hints when I ask", "Hints and clickable spots"};
		const float widths[] = {260 * k, 340 * k, 440 * k};
		const float gap = 24 * k, by = p1.y - pad - bh;
		_overlay.dialogButtons = 3;
		float x = p1.x - pad - (widths[0] + widths[1] + widths[2] + 2 * gap);
		int chosen = -1;
		for (int i = 0; i < 3; i++) {
			ImGui::SetCursorScreenPos(ImVec2(x, by));
			if (button(labels[i], ImVec2(widths[i], bh), fonts.bold, 30 * k, i == 1, _overlay.dialogFocus == i))
				chosen = i;
			x += widths[i] + gap;
		}
		_overlay.dialogPress = false;
		ImGui::End();
		if (chosen < 0)
			return true;
		s.hintsOn = chosen >= 1;
		s.showSpots = chosen == 2;
		_host.settingsChanged();
		if (chosen >= 1)
			_overlay.toast("Hints on: press " + keyName() + " when you're stuck", 4000);
		return false;
	}

	// A big rounded button, drawn in full at the cursor: clicked with the
	// pointer, or with OK while focused (a thick white ring).
	bool button(const char *label, ImVec2 size, ImFont *font, float fontSize, bool primary, bool focused) {
		const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + size.x, p0.y + size.y);
		const bool clicked = ImGui::InvisibleButton(label, size) || (focused && _overlay.dialogPress);
		const bool hovered = ImGui::IsItemHovered();
		const ImU32 base = primary ? (hovered ? colour::kGreenHover : colour::kGreen)
		                           : (hovered ? IM_COL32(70, 62, 82, 255) : colour::kCardHover);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const float r = size.y * 0.25f;
		dl->AddRectFilled(p0, p1, base, r);
		if (focused)
			dl->AddRect(ImVec2(p0.x - 4, p0.y - 4), ImVec2(p1.x + 4, p1.y + 4), IM_COL32_WHITE, r + 4, 0, 4.0f);
		const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label);
		dl->AddText(font, fontSize, ImVec2(std::round(p0.x + (size.x - ts.x) / 2), std::round(p0.y + (size.y - ts.y) / 2)),
		            primary ? IM_COL32_WHITE : colour::kText, label);
		return clicked;
	}
};

} // namespace

std::shared_ptr<HintsControl> addHints(Overlay &overlay, OverlayHost &host, std::string keyName) {
	auto hints = std::make_shared<Hints>(overlay, host, std::move(keyName));
	overlay.addTab("Hints", [hints] { hints->tab(); });
	// (update() runs every frame, as the layer is tested every frame)
	overlay.addGameLayer([hints] { return hints->update(); }, [hints](ImDrawList &dl) { hints->draw(dl); });
	return hints;
}

} // namespace gob
