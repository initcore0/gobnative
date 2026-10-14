// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Tools tab: a debug panel (engine state, variable watches, mult
// objects, the hotspot table, the trace log, a diagnostics report) and the
// hotspot highlighter, which outlines the room's hotspots over the game
// image while its key is held or always.

#include "engine/interpreter.h"
#include "frontend/overlay.h"
#include "frontend/settings.h"
#include "frontend/tracelog.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gob {

namespace {

constexpr int kScreenW = Draw::kScreenW, kScreenH = Draw::kScreenH;

// Hotspots worth outlining: enabled, on screen, and not one of the entries
// the scripts keep out of the player's way: parked off screen (negative or
// 1000+ coordinates; parked negative ones end up with right < left once
// clipped), a full-screen catch-all (right-click / cancel) or a 1x1 timer
// tick entry.
bool highlightable(const Interpreter::HotspotInfo &h) {
	if (h.disabled() || h.right < h.left || h.bottom < h.top)
		return false;
	if (h.right < 0 || h.bottom < 0 || h.left >= kScreenW || h.top >= kScreenH)
		return false;
	if (h.left <= 0 && h.top <= 0 && h.right >= kScreenW - 1 && h.bottom >= kScreenH - 1)
		return false;
	return !(h.right == h.left && h.bottom == h.top);
}

const char *hotspotTypeName(int type) {
	switch (type) {
	case 0: return "hover";
	case 1: return "click";
	case 2: return "click+leave";
	default: return type <= 10 ? "input" : "?";
	}
}

const char *buttonName(int button) {
	switch (button) {
	case 0: return "left";
	case 1: return "right";
	case 2: return "any";
	default: return "none";
	}
}

ImU32 hotspotColor(int type, int alpha = 255) {
	if (type == 0)
		return IM_COL32(90, 200, 255, alpha); // hover only
	if (type <= 2)
		return IM_COL32(255, 220, 60, alpha); // clickable
	return IM_COL32(110, 255, 130, alpha);    // text input
}

std::string formatClock(double ms) {
	long long t = (long long)ms;
	char buf[48];
	std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%03lld", t / 3600000, t / 60000 % 60, t / 1000 % 60, t % 1000);
	return buf;
}

// Printable copy of a variable string, at most `max` bytes.
std::string printable(const Variables &v, uint32_t off, size_t max) {
	std::string s;
	for (size_t i = 0; i < max && off + i < v.size(); i++) {
		uint8_t c = v.read8(off + uint32_t(i));
		if (!c)
			break;
		s.push_back(c >= 32 && c < 127 ? char(c) : '.');
	}
	return s;
}

class ToolsTab {
public:
	ToolsTab(Overlay &overlay, OverlayHost &host) : _overlay(overlay), _host(host) {
		// A few variables the scripts use everywhere (docs/levels.md).
		const std::pair<uint32_t, const char *> defaults[] = {
			{16, "hotspot result"}, {60, "room exit: 0 playing, 2 walking out, 1 leaving"},
			{1400, "active goblin"}, {476, "selected item (-1 none)"}, {3734, "next room MENU runs (string)"},
		};
		for (const auto &[index, name] : defaults) {
			Watch w;
			w.offset = index * 4;
			w.name = name;
			_watches.push_back(w);
		}
	}

	void draw() {
		Interpreter *in = _host.engine();
		if (std::string sub = _overlay.takeSubTab(); !sub.empty())
			_selectSub = sub;
		if (!in) {
			ImGui::TextDisabled("The game is not running.");
			return;
		}
		if (ImGui::BeginTabBar("tools")) {
			auto item = [&](const char *name) {
				ImGuiTabItemFlags f = _selectSub == name ? ImGuiTabItemFlags_SetSelected : 0;
				return ImGui::BeginTabItem(name, nullptr, f);
			};
			if (item("Overview")) {
				overview(*in);
				ImGui::EndTabItem();
			}
			// (tables of values in the fixed-width font)
			ImFont *mono = _overlay.fonts().mono;
			if (item("Variables")) {
				ImGui::PushFont(mono, 0.0f);
				variables(*in);
				ImGui::PopFont();
				ImGui::EndTabItem();
			}
			if (item("Objects")) {
				ImGui::PushFont(mono, 0.0f);
				objects(*in);
				ImGui::PopFont();
				ImGui::EndTabItem();
			}
			if (item("Hotspots")) {
				ImGui::PushFont(mono, 0.0f);
				hotspots(*in);
				ImGui::PopFont();
				ImGui::EndTabItem();
			}
			if (item("Trace")) {
				ImGui::PushFont(mono, 0.0f);
				trace();
				ImGui::PopFont();
				ImGui::EndTabItem();
			}
			_selectSub.clear();
			ImGui::EndTabBar();
		}
	}

	bool layerActive() {
		return _host.engine() && (_host.debugOptions().showHotspots || _host.hotspotKeyHeld() || _hoverHotspot >= 0 ||
		                          _hoverObject >= 0);
	}

	// Outlines over the game image, in window points rounded to whole points
	// (and drawn as filled rectangles, which ImGui doesn't anti-alias), so
	// the lines are crisp at any scale and on HiDPI screens.
	void drawLayer(ImDrawList &dl) {
		int hoverHotspot = std::exchange(_hoverHotspot, -1), hoverObject = std::exchange(_hoverObject, -1);
		Interpreter *in = _host.engine();
		if (!in)
			return;
		const GlPresenter::Rect vp = _host.gameRect();
		const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
		auto toPoints = [&](int gx0, int gy0, int gx1, int gy1, ImVec2 &p0, ImVec2 &p1) {
			gx0 = std::clamp(gx0, 0, kScreenW);
			gx1 = std::clamp(gx1, 0, kScreenW);
			gy0 = std::clamp(gy0, 0, kScreenH);
			gy1 = std::clamp(gy1, 0, kScreenH);
			p0.x = std::round((vp.x + double(gx0) * vp.w / kScreenW) / scale.x);
			p1.x = std::round((vp.x + double(gx1) * vp.w / kScreenW) / scale.x);
			p0.y = std::round((vp.y + double(gy0) * vp.h / kScreenH) / scale.y);
			p1.y = std::round((vp.y + double(gy1) * vp.h / kScreenH) / scale.y);
		};
		auto outline = [&](ImVec2 a, ImVec2 b, float t, ImU32 col) {
			dl.AddRectFilled(a, ImVec2(b.x, a.y + t), col);
			dl.AddRectFilled(ImVec2(a.x, b.y - t), b, col);
			dl.AddRectFilled(ImVec2(a.x, a.y + t), ImVec2(a.x + t, b.y - t), col);
			dl.AddRectFilled(ImVec2(b.x - t, a.y + t), ImVec2(b.x, b.y - t), col);
		};

		const DebugOptions &opts = _host.debugOptions();
		const bool all = opts.showHotspots || _host.hotspotKeyHeld();
		// Hotspots and objects are on the screen surface; a scrolled one
		// (Goblins 3) shows a window of it.
		const int vx = in->draw().viewX, vy = in->draw().viewY;
		for (Interpreter::HotspotInfo h : in->hotspots()) {
			h.left -= vx;
			h.right -= vx;
			h.top -= vy;
			h.bottom -= vy;
			const bool hovered = h.slot == hoverHotspot;
			if (!hovered && !(all && highlightable(h)))
				continue;
			ImVec2 a, b;
			toPoints(h.left, h.top, h.right + 1, h.bottom + 1, a, b);
			if (b.x - a.x < 2 || b.y - a.y < 2)
				continue;
			if (hovered)
				dl.AddRectFilled(a, b, hotspotColor(h.type(), 90));
			outline(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), 1, IM_COL32(0, 0, 0, 200)); // contrast
			outline(a, b, hovered ? 2 : 1, hotspotColor(h.type()));
			if (opts.hotspotSlots) {
				char label[8];
				std::snprintf(label, sizeof(label), "%d", h.slot);
				ImVec2 size = ImGui::CalcTextSize(label);
				dl.AddRectFilled(ImVec2(a.x + 1, a.y + 1), ImVec2(a.x + 4 + size.x, a.y + 1 + size.y), IM_COL32(0, 0, 0, 170));
				dl.AddText(ImVec2(a.x + 2.5f, a.y + 1), hotspotColor(h.type()), label);
			}
		}
		if (hoverObject >= 0)
			for (const Interpreter::ObjectInfo &o : in->objects())
				if (o.index == hoverObject && o.left >= 0 && o.right >= o.left) {
					ImVec2 a, b;
					toPoints(o.left - vx, o.top - vy, o.right + 1 - vx, o.bottom + 1 - vy, a, b);
					dl.AddRectFilled(a, b, IM_COL32(255, 120, 220, 70));
					outline(a, b, 2, IM_COL32(255, 120, 220, 255));
				}
	}

private:
	// --- Overview: engine state, options, diagnostics

	void overview(Interpreter &in) {
		DebugOptions &opts = _host.debugOptions();
		bool changed = false;
		char keyLabel[96];
		std::snprintf(keyLabel, sizeof(keyLabel), "Always show hotspots (or hold %s)", _host.hotspotKeyName());
		changed |= ImGui::Checkbox(keyLabel, &opts.showHotspots);
		ImGui::SetItemTooltip("Outlines the room's active hotspots: blue hover only, yellow clickable, green text\n"
		                      "input. Hotspots parked off screen, full-screen catch-alls and 1x1 timer entries\n"
		                      "are left out.");
		changed |= ImGui::Checkbox("Label hotspots with their slot", &opts.hotspotSlots);
		changed |= ImGui::Checkbox("Keep the game running while the overlay is open", &opts.runBehindOverlay);
		ImGui::SetItemTooltip("For watching objects, variables and the trace live. The game gets no input\n"
		                      "while the overlay is open.");
		changed |= ImGui::Checkbox("Opcode trace", &opts.traceOps);
		ImGui::SetItemTooltip("Logs every executed opcode to the trace (very verbose; also to stdout with --trace).");
		if (changed)
			_host.debugOptionsChanged();

		ImGui::SeparatorText("Engine");
		const std::vector<Interpreter::EnvInfo> envs = in.envStack();
		ImGui::Text("TOT: %s", in.currentTot().empty() ? "-" : in.currentTot().c_str());
		std::string stack;
		for (const Interpreter::EnvInfo &e : envs) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%s%s%s @%04X", stack.empty() ? "" : " > ", e.current ? "*" : "",
			              e.name.empty() ? "-" : e.name.c_str(), e.pos);
			stack += buf;
		}
		ImGui::TextWrapped("Environments: %s", stack.c_str());
		ImGui::Text("Level: %s", levelText(in).c_str());
		ImGui::Text("Game clock: %s", formatClock(in.gameClockMs()).c_str());
		ImGui::Text("Speed: %gx%s", in.speed,
		            in.effectiveSpeed() != in.speed ? " (cutscene at 1x)" : "");
		ImGui::Text("Objects: %d   Hotspots: %d (%d pushed sets)   Variables: %d bytes", int(in.objects().size()),
		            int(in.hotspots().size()), in.hotspotStackDepth(), int(in.variables().size()));

		ImGui::Separator();
		if (ImGui::Button("Copy diagnostics")) {
			ImGui::SetClipboardText(diagnostics(in).c_str());
			_overlay.toast("Diagnostics copied to the clipboard");
		}
		ImGui::SetItemTooltip("Puts a short report (TOT, level, objects, recent trace lines, settings) on the\n"
		                      "clipboard, for bug reports.");
	}

	std::string levelText(Interpreter &in) {
		const std::string room = in.currentLevel();
		if (room.empty())
			return "none (intro, ending or between rooms)";
		if (_levels.empty() || _language != _host.runningLanguage()) {
			_levels = _host.levels();
			_language = _host.runningLanguage();
		}
		std::string name;
		int world = 0;
		for (const LevelInfo &l : _levels)
			if (l.tot == room) {
				name = l.name;
				world = l.world;
			}
		return room + (world ? ", world " + std::to_string(world) : std::string()) + (name.empty() ? "" : " (" + name + ")");
	}

	std::string diagnostics(Interpreter &in) {
		const GameInfo &game = _host.currentGame();
		std::string r = "gobnative diagnostics\nGame: " + std::string(game.title) + " (" + game.key + ")\n";
		char buf[256];
		r += "TOT: " + (in.currentTot().empty() ? std::string("-") : in.currentTot()) + "\nEnvironments:";
		for (const Interpreter::EnvInfo &e : in.envStack()) {
			std::snprintf(buf, sizeof(buf), " %s%s@%04X", e.current ? "*" : "", e.name.c_str(), e.pos);
			r += buf;
		}
		r += "\nLevel: " + levelText(in) + "\n";
		std::snprintf(buf, sizeof(buf), "Game clock: %s, speed %gx (effective %gx)\n", formatClock(in.gameClockMs()).c_str(),
		              in.speed, in.effectiveSpeed());
		r += buf;
		const Settings &s = _host.settings();
		std::snprintf(buf, sizeof(buf),
		              "Settings: language %s (running %s), filter %s, scale %dx%s%s%s, speed %gx, turbo %dx, "
		              "cutscenes at 1x %s, effects %s, music files %d%s\n",
		              languageName(s.language), languageName(_host.runningLanguage()), filterName(s.filters.filter),
		              s.windowScale, s.fullscreen ? " fullscreen" : "", s.aspectCorrection ? " 4:3" : "",
		              s.integerScaling ? " integer" : "", s.speed, s.turboSpeed, s.cutscenesAtNormalSpeed ? "on" : "off",
		              s.effectsQuality == Mixer::EffectsQuality::Enhanced ? "enhanced" : "original",
		              int(_host.musicPack().entries().size()), s.musicFullLength ? " (full length)" : "");
		r += buf;
		const auto hs = in.hotspots();
		std::snprintf(buf, sizeof(buf), "Hotspots: %d (%d pushed sets)\n", int(hs.size()), in.hotspotStackDepth());
		r += buf;
		const auto objs = in.objects();
		std::snprintf(buf, sizeof(buf), "Objects: %d\n", int(objs.size()));
		r += buf;
		for (const Interpreter::ObjectInfo &o : objs) {
			std::snprintf(buf, sizeof(buf), "  #%d pos %d,%d anim %d layer %d frame %d type %d order %d%s%s", o.index, o.x,
			              o.y, o.animation, o.layer, o.frame, o.animType, o.order, o.isStatic ? " static" : "",
			              o.isPaused ? " paused" : "");
			r += buf;
			if (o.isGoblin) {
				std::snprintf(buf, sizeof(buf), " goblin cell %d,%d dest %d,%d path %d state %d", o.cellX, o.cellY,
				              o.gobDestX, o.gobDestY, o.pathExistence, o.state);
				r += buf;
			}
			r += "\n";
		}
		const TraceLog &log = _host.traceLog();
		const uint64_t end = log.end(), first = std::max(log.begin(), end > 40 ? end - 40 : 0);
		r += "Recent trace:\n";
		for (uint64_t n = first; n < end; n++) {
			std::snprintf(buf, sizeof(buf), "  %8.3fs  ", log.at(n).ms / 1000.0);
			r += buf + log.at(n).text + "\n";
		}
		return r;
	}

	// --- Variable watches

	struct Watch {
		uint32_t offset = 0;
		bool slot = true; // added as v[index] (else as a byte offset)
		const char *name = "";
		std::array<char, 64> text{};
		bool editingText = false;
	};

	void variables(Interpreter &in) {
		Variables &vars = in.variables();
		ImGui::SetNextItemWidth(120);
		ImGui::InputInt("##index", &_newIndex);
		_newIndex = std::max(0, _newIndex);
		ImGui::SameLine();
		ImGui::RadioButton("v[index]", &_newIsOffset, 0);
		ImGui::SetItemTooltip("32-bit variable slot, as the scripts and docs number them (byte offset index * 4).");
		ImGui::SameLine();
		ImGui::RadioButton("byte offset", &_newIsOffset, 1);
		ImGui::SameLine();
		if (ImGui::Button("Add watch")) {
			Watch w;
			w.slot = !_newIsOffset;
			w.offset = uint32_t(_newIndex) * (w.slot ? 4 : 1);
			_watches.push_back(w);
		}
		ImGui::TextDisabled("Variable space: %d bytes. Edits apply at once.", int(vars.size()));

		const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
		                           ImGuiTableFlags_ScrollY;
		if (!ImGui::BeginTable("watches", 6, tf, ImVec2(0, 300)))
			return;
		ImGui::TableSetupColumn("Variable");
		ImGui::TableSetupColumn("int32");
		ImGui::TableSetupColumn("int16");
		ImGui::TableSetupColumn("int8");
		ImGui::TableSetupColumn("string");
		ImGui::TableSetupColumn("");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		int remove = -1;
		for (size_t i = 0; i < _watches.size(); i++) {
			Watch &w = _watches[i];
			ImGui::PushID(int(i));
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (w.slot)
				ImGui::Text("v[%u] @%u", w.offset / 4, w.offset);
			else
				ImGui::Text("@%u", w.offset);
			if (*w.name)
				ImGui::SetItemTooltip("%s", w.name);
			ImGui::TableNextColumn();
			int32_t v32 = int32_t(vars.read32(w.offset));
			ImGui::SetNextItemWidth(110);
			if (ImGui::InputScalar("##32", ImGuiDataType_S32, &v32))
				vars.write32(w.offset, uint32_t(v32));
			ImGui::TableNextColumn();
			int16_t v16 = int16_t(vars.read16(w.offset));
			ImGui::SetNextItemWidth(80);
			if (ImGui::InputScalar("##16", ImGuiDataType_S16, &v16))
				vars.write16(w.offset, uint16_t(v16));
			ImGui::TableNextColumn();
			int8_t v8 = int8_t(vars.read8(w.offset));
			ImGui::SetNextItemWidth(60);
			if (ImGui::InputScalar("##8", ImGuiDataType_S8, &v8))
				vars.write8(w.offset, uint8_t(v8));
			ImGui::TableNextColumn();
			if (!w.editingText) {
				std::string s = printable(vars, w.offset, w.text.size() - 1);
				std::snprintf(w.text.data(), w.text.size(), "%s", s.c_str());
			}
			ImGui::SetNextItemWidth(160);
			if (ImGui::InputText("##s", w.text.data(), w.text.size(), ImGuiInputTextFlags_EnterReturnsTrue))
				vars.writeString(w.offset, w.text.data());
			w.editingText = ImGui::IsItemActive();
			ImGui::SetItemTooltip("Press Enter to write the string (with its terminating zero).");
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("x"))
				remove = int(i);
			ImGui::PopID();
		}
		ImGui::EndTable();
		if (remove >= 0)
			_watches.erase(_watches.begin() + remove);
	}

	// --- Mult objects

	void objects(Interpreter &in) {
		const std::vector<Interpreter::ObjectInfo> objs = in.objects();
		if (objs.empty()) {
			ImGui::TextDisabled("No mult objects.");
			return;
		}
		ImGui::TextDisabled("Hover a row to outline the object where it was last drawn.");
		const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
		                           ImGuiTableFlags_ScrollY;
		if (ImGui::BeginTable("objects", 9, tf, ImVec2(0, 240))) {
			for (const char *c : {"#", "position", "anim", "layer", "frame", "animType", "order", "flags", "drawn at"})
				ImGui::TableSetupColumn(c);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			ImGuiListClipper clip;
			clip.Begin(int(objs.size()));
			while (clip.Step())
				for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
					const Interpreter::ObjectInfo &o = objs[size_t(i)];
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					rowLabel((std::to_string(o.index) + (o.isGoblin ? " gob" : "")).c_str(), _hoverObject, o.index);
					ImGui::TableNextColumn();
					ImGui::Text("%d,%d", o.x, o.y);
					ImGui::TableNextColumn();
					ImGui::Text("%d", o.animation);
					ImGui::TableNextColumn();
					ImGui::Text("%d", o.layer);
					ImGui::TableNextColumn();
					ImGui::Text("%d", o.frame);
					ImGui::TableNextColumn();
					ImGui::Text("%d", o.animType);
					ImGui::TableNextColumn();
					ImGui::Text("%d", o.order);
					ImGui::TableNextColumn();
					ImGui::Text("%s%s", o.isStatic ? "static " : "", o.isPaused ? "paused" : "");
					ImGui::TableNextColumn();
					if (o.left >= 0)
						ImGui::Text("%d,%d-%d,%d", o.left, o.top, o.right, o.bottom);
					else
						ImGui::TextDisabled("-");
				}
			ImGui::EndTable();
		}
		if (std::none_of(objs.begin(), objs.end(), [](const auto &o) { return o.isGoblin; }))
			return;
		ImGui::SeparatorText("Goblins");
		if (ImGui::BeginTable("goblins", 8, tf & ~ImGuiTableFlags_ScrollY)) {
			for (const char *c : {"#", "cell", "leg target", "destination", "pathExistence", "state", "next", "busy / look"})
				ImGui::TableSetupColumn(c);
			ImGui::TableHeadersRow();
			for (const Interpreter::ObjectInfo &o : objs) {
				if (!o.isGoblin)
					continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				rowLabel(std::to_string(o.index).c_str(), _hoverObject, o.index);
				ImGui::TableNextColumn();
				ImGui::Text("%d,%d", o.cellX, o.cellY);
				ImGui::TableNextColumn();
				ImGui::Text("%d,%d", o.destX, o.destY);
				ImGui::TableNextColumn();
				ImGui::Text("%d,%d", o.gobDestX, o.gobDestY);
				ImGui::TableNextColumn();
				ImGui::Text("%d", o.pathExistence);
				ImGui::TableNextColumn();
				ImGui::Text("%d", o.state);
				ImGui::TableNextColumn();
				ImGui::Text("%d", o.nextState);
				ImGui::TableNextColumn();
				ImGui::Text("%d / %d", o.isBusy, o.lookDir);
			}
			ImGui::EndTable();
		}
	}

	// First cell of a row that can be hovered anywhere: sets `hover` (shown
	// in the game layer next frame) while the mouse is over the row.
	static void rowLabel(const char *text, int &hover, int value) {
		ImGui::PushID(value);
		ImGui::Selectable(text, false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
		if (ImGui::IsItemHovered())
			hover = value;
		ImGui::PopID();
	}

	// --- Hotspot table

	void hotspots(Interpreter &in) {
		const std::vector<Interpreter::HotspotInfo> hs = in.hotspots();
		ImGui::Text("%d entries, %d pushed sets below (an outer block's or TOT's).", int(hs.size()), in.hotspotStackDepth());
		ImGui::TextDisabled("Hover a row to show the hotspot. Grey rows aren't outlined by the highlighter.");
		const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
		                           ImGuiTableFlags_ScrollY;
		if (!ImGui::BeginTable("hotspots", 8, tf, ImVec2(0, 320)))
			return;
		for (const char *c : {"slot", "id", "type", "button", "rect", "key", "flags", "blocks"})
			ImGui::TableSetupColumn(c);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		ImGuiListClipper clip;
		clip.Begin(int(hs.size()));
		while (clip.Step())
			for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
				const Interpreter::HotspotInfo &h = hs[size_t(i)];
				const bool shown = highlightable(h);
				if (!shown)
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				rowLabel(std::to_string(h.slot).c_str(), _hoverHotspot, h.slot);
				ImGui::TableNextColumn();
				ImGui::Text("%04X%s", h.id, h.disabled() ? " off" : "");
				ImGui::TableNextColumn();
				ImGui::Text("%d %s", h.type(), hotspotTypeName(h.type()));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(buttonName(h.button()));
				ImGui::TableNextColumn();
				ImGui::Text("%d,%d %dx%d", h.left, h.top, h.right - h.left + 1, h.bottom - h.top + 1);
				ImGui::TableNextColumn();
				ImGui::Text("%04X", h.key);
				ImGui::TableNextColumn();
				ImGui::Text("%04X", h.flags);
				ImGui::TableNextColumn();
				ImGui::Text("%s%s%s", h.hasEnter ? "enter " : "", h.hasLeave ? "leave " : "", h.expression ? "expr" : "");
				if (!shown)
					ImGui::PopStyleColor();
			}
		ImGui::EndTable();
	}

	// --- Trace log

	void trace() {
		const TraceLog &log = _host.traceLog();
		if (ImGui::Checkbox("Pause", &_paused) && _paused)
			_pausedEnd = log.end();
		ImGui::SetItemTooltip("Freezes the view; lines keep being recorded.");
		ImGui::SameLine();
		_filter.Draw("Filter", 300);
		ImGui::SetItemTooltip("Shows only lines containing the text (not case sensitive);\n"
		                      "\"a,b\" either, \"-c\" without c.");
		ImGui::SameLine();
		if (ImGui::Button("Copy"))
			ImGui::SetClipboardText(visibleText(log).c_str());
		ImGui::SetItemTooltip("Copies the lines shown (all of them, not only the visible part).");

		const uint64_t begin = log.begin(), end = _paused ? std::max(begin, _pausedEnd) : log.end();
		const bool filtering = _filter.IsActive();
		if (filtering)
			updateMatches(log, begin, end);
		const size_t rows = filtering ? _matches.size() : size_t(end - begin);
		ImGui::TextDisabled("%zu line%s%s (the last %zu are kept)", rows, rows == 1 ? "" : "s",
		                    filtering ? " match" : "", size_t(20000));

		if (ImGui::BeginChild("tracelines", ImVec2(720, 320), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
			const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
			ImGuiListClipper clip;
			clip.Begin(int(rows));
			while (clip.Step())
				for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
					const TraceLog::Line &l = log.at(filtering ? _matches[size_t(i)] : begin + uint64_t(i));
					ImGui::TextDisabled("%8.3fs", l.ms / 1000.0);
					ImGui::SameLine();
					ImGui::TextUnformatted(l.text.c_str());
				}
			if (atBottom && !_paused)
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
	}

	// Keeps _matches (line numbers passing the filter) up to date, testing
	// only lines not tested before.
	void updateMatches(const TraceLog &log, uint64_t begin, uint64_t end) {
		std::string f = _filter.InputBuf;
		if (f != _matchFilter || _scanned < begin) {
			_matchFilter = f;
			_matches.clear();
			_scanned = begin;
		}
		while (!_matches.empty() && _matches.front() < begin)
			_matches.pop_front();
		if (_scanned > end) { // the view was paused after lines past it were tested
			while (!_matches.empty() && _matches.back() >= end)
				_matches.pop_back();
			_scanned = end;
		}
		for (; _scanned < end; _scanned++)
			if (_filter.PassFilter(log.at(_scanned).text.c_str()))
				_matches.push_back(_scanned);
	}

	std::string visibleText(const TraceLog &log) {
		const uint64_t begin = log.begin(), end = _paused ? std::max(begin, _pausedEnd) : log.end();
		std::string out;
		char buf[24];
		for (uint64_t n = begin; n < end; n++) {
			const TraceLog::Line &l = log.at(n);
			if (_filter.IsActive() && !_filter.PassFilter(l.text.c_str()))
				continue;
			std::snprintf(buf, sizeof(buf), "%8.3fs  ", l.ms / 1000.0);
			out += buf + l.text + "\n";
		}
		return out;
	}

	Overlay &_overlay;
	OverlayHost &_host;
	std::string _selectSub;
	int _hoverHotspot = -1, _hoverObject = -1; // table rows under the mouse, shown in the game layer

	std::vector<LevelInfo> _levels;
	Language _language = Language::American;

	std::vector<Watch> _watches;
	int _newIndex = 0, _newIsOffset = 0;

	ImGuiTextFilter _filter;
	bool _paused = false;
	uint64_t _pausedEnd = 0;
	std::string _matchFilter;
	std::deque<uint64_t> _matches;
	uint64_t _scanned = 0;
};

} // namespace

void addToolsTab(Overlay &overlay, OverlayHost &host) {
	auto tab = std::make_shared<ToolsTab>(overlay, host);
	overlay.addTab("Tools", [tab] { tab->draw(); });
	overlay.setGameLayer([tab] { return tab->layerActive(); }, [tab](ImDrawList &dl) { tab->drawLayer(dl); });
}

} // namespace gob
