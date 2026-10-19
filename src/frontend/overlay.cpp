// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/overlay.h"

#include "frontend/gl.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"

#include <SDL.h>

#include <algorithm>

// The keys that close the overlay, as the player knows them.
#ifdef GOB_WEBOS
#define GOB_OVERLAY_KEYS "the green or red key"
#else
#define GOB_OVERLAY_KEYS "` or Escape"
#endif

namespace gob {

bool Overlay::init(SDL_Window *window, void *glContext) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr; // our settings live in settings.ini
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	_fonts = addUiFonts(*io.Fonts);
	ImGuiStyle &style = ImGui::GetStyle();
	applyUiStyle(style); // the library's look
	style.WindowPadding = ImVec2(14, 12);
	style.FramePadding = ImVec2(8, 4);
	style.ItemSpacing = ImVec2(8, 6);
	style.WindowBorderSize = 1;
#ifdef GOB_WEBOS
	// Read from a sofa: everything larger.
	_scale = 1.7f;
	style.ScaleAllSizes(_scale);
	style.FontScaleMain = _scale;
#endif
#ifdef GOB_GLES
	const char *glsl = "#version 300 es"; // WebGL 2, webOS
#else
	const char *glsl = "#version 150";
#endif
	if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext) || !ImGui_ImplOpenGL3_Init(glsl)) {
		ImGui::DestroyContext();
		return false;
	}
	_initialized = true;
	return true;
}

void Overlay::shutdown() {
	if (!_initialized)
		return;
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	_initialized = false;
}

void Overlay::addTab(std::string name, std::function<void()> draw) {
	_tabs.push_back({std::move(name), std::move(draw)});
}

void Overlay::selectTab(const std::string &name) {
	size_t slash = name.find('/');
	_selectTab = name.substr(0, slash);
	_selectSub = slash == std::string::npos ? std::string() : name.substr(slash + 1);
}

bool Overlay::popupOpen() const {
	return _initialized && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void Overlay::processEvent(const SDL_Event &ev) {
	if (_initialized && _open)
		ImGui_ImplSDL2_ProcessEvent(&ev);
}

namespace {
constexpr uint32_t kToastMs = 1800, kToastFadeMs = 400;
}

void Overlay::toast(std::string text, uint32_t ms) {
	_toasts.clear(); // the newest message replaces the previous one
	_toasts.push_back({std::move(text), SDL_GetTicks(), ms ? ms : kToastMs});
}

void Overlay::drawToasts() {
	uint32_t now = SDL_GetTicks();
	std::erase_if(_toasts, [now](const Toast &t) { return now - t.shownAt >= t.ms; });
	ImDrawList *dl = ImGui::GetForegroundDrawList();
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	const float pad = 10.0f, margin = 16.0f;
	float y = margin;
	for (const Toast &t : _toasts) {
		uint32_t left = t.ms - (now - t.shownAt);
		float alpha = left < kToastFadeMs ? float(left) / kToastFadeMs : 1.0f;
		ImVec2 size = ImGui::CalcTextSize(t.text.c_str());
		ImVec2 p0(display.x - margin - size.x - 2 * pad, y), p1(display.x - margin, y + size.y + 2 * pad);
		dl->AddRectFilled(p0, p1, withAlpha(colour::kBackground, int(230 * alpha)), 10.0f);
		dl->AddRect(p0, p1, withAlpha(colour::kAmber, int(110 * alpha)), 10.0f);
		dl->AddText(ImVec2(p0.x + pad, p0.y + pad), withAlpha(colour::kText, int(255 * alpha)), t.text.c_str());
		y = p1.y + 6.0f;
	}
}

void Overlay::render() {
	const bool layer = _layerActive && _layerActive();
	const bool touchLayer = touchMode && _touchDraw;
	const bool barLayer = bool(_barDraw);
	if (!_initialized || (!_open && _toasts.empty() && !layer && !touchLayer && !barLayer))
		return;
	// While closed the game hides the system cursor; ImGui must leave it alone.
	ImGuiIO &io = ImGui::GetIO();
	if (_open)
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
	else
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	ImGui::GetStyle().TouchExtraPadding = touchMode ? ImVec2(6, 6) : ImVec2(0, 0);
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	if (!_open) {
		if (layer)
			_layerDraw(*ImGui::GetBackgroundDrawList());
		if (touchLayer)
			_touchDraw(*ImGui::GetForegroundDrawList());
		if (barLayer)
			_barDraw(*ImGui::GetForegroundDrawList());
		drawToasts();
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		return;
	}

	// Dim the paused game behind the window; draw the game layer on it.
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	if (dimGame)
		ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 110));
	if (layer)
		_layerDraw(*ImGui::GetBackgroundDrawList());

	if (_dialog) {
		if (!_dialog(display.y / 1080.0f)) {
			_dialog = nullptr;
			_open = false;
		}
		drawToasts();
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(440 * _scale, 0), ImGuiCond_Appearing);
	// Tabs differ in height: never taller than the window (the content
	// scrolls instead), and moved back up when a taller tab runs off it.
	ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(display.x, std::max(100.0f, display.y - 16.0f)));
	bool keepOpen = true;
	if (touchMode) {
		touchDragScroll();
		// A tap beside the window closes it, as Escape does (not one that
		// closes a popup, nor the release of the tap that opened it).
		const ImGuiIO &io = ImGui::GetIO();
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			_tapBeside = !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) && !popupOpen();
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (_tapBeside && io.MouseDragMaxDistanceSqr[0] <= io.MouseDragThreshold * io.MouseDragThreshold)
				keepOpen = false;
			_tapBeside = false;
		}
	}
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
	                               (touchMode ? ImGuiWindowFlags_NoMove : 0); // a drag scrolls instead
	if (ImGui::Begin("gobnative", &keepOpen, flags)) {
		ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
		if (pos.y + size.y > display.y - 8.0f && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			ImGui::SetWindowPos(ImVec2(pos.x, std::max(8.0f, display.y - 8.0f - size.y)));
		if (touchMode)
			ImGui::TextDisabled(dimGame ? "Game paused. Tap beside this window to resume."
			                            : "Game running (see Tools). Tap beside this window to close it.");
		else if (dimGame)
			ImGui::TextDisabled("Game paused. Press " GOB_OVERLAY_KEYS " to resume.");
		else
			ImGui::TextDisabled("Game running (see Tools). Press " GOB_OVERLAY_KEYS " to close.");
		if (ImGui::BeginTabBar("tabs")) {
			for (const Tab &t : _tabs) {
				ImGuiTabItemFlags flags = t.name == _selectTab ? ImGuiTabItemFlags_SetSelected : 0;
				if (ImGui::BeginTabItem(t.name.c_str(), nullptr, flags)) {
					t.draw();
					ImGui::EndTabItem();
				}
			}
			_selectTab.clear();
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
	if (!keepOpen)
		_open = false;
	drawToasts();

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace gob
