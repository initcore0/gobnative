// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Dear ImGui overlay (SDL2 + OpenGL 3 backends), toggled with the backquote
// key. The overlay is a window with one tab per feature; tabs are plain
// callbacks registered with addTab(), so new features just add a tab.

#pragma once

#include "audio/musicpack.h"
#include "engine/cdaudio.h"
#include "engine/game.h"
#include "engine/hdrender.h"
#include "engine/levels.h"
#include "formats/tot.h"
#include "frontend/presenter.h"
#include "frontend/theme.h"
#include "gfx/hdpack.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

union SDL_Event;
struct SDL_Window;
struct ImDrawList;

namespace gob {

class Interpreter;
class TraceLog;
struct Settings;

// Debugging options of the Tools tab, for this run only (not saved).
struct DebugOptions {
	bool showHotspots = false;     // outline the hotspots all the time, not only while the key is held
	bool hotspotSlots = false;     // label the outlines with their slot in the hotspot table
	bool runBehindOverlay = false; // keep the game running while the overlay is open
	bool traceOps = false;         // trace every opcode (Interpreter::traceOps)
};

// What the overlay's tabs can ask of the front end.
class OverlayHost {
public:
	virtual ~OverlayHost() = default;
	// The game being played (its rules: gameRules(currentGame().id)).
	virtual const GameInfo &currentGame() const = 0;
	virtual Settings &settings() = 0;
	// Applies the (changed) settings now and saves them later.
	virtual void settingsChanged() = 0;
	// Language the running game was started with.
	virtual Language runningLanguage() const = 0;
	virtual void restartGame() = 0;
	virtual void quit() = 0;
	// Closes the game and shows the game library (frontend/library.h).
	virtual void backToLibrary() = 0;
	// Something can be skipped (the intro, the ending, or a scripted
	// sequence during which the player has no control); skipCutscene()
	// skips it.
	virtual bool canSkip() const = 0;
	virtual void skipCutscene() = 0;
	// Levels tab: the rooms in play order, the room running now (empty when
	// none is), jumping to a room and the quick save slot.
	virtual std::vector<LevelInfo> levels() = 0;
	virtual std::string currentLevel() const = 0;
	virtual void jumpToLevel(const std::string &tot) = 0;
	virtual void quickSave() = 0;
	virtual void quickLoad() = 0;
	virtual bool hasQuickSave() const = 0;
	// Keyboard shortcut text for quick save/load ("Cmd+S" on macOS).
	virtual const char *quickSaveShortcut() const = 0;
	virtual const char *quickLoadShortcut() const = 0;
	// Audio tab: the replacement music pack (folder and files found), opening
	// its folder in the file manager, and the running game's CD tracks.
	virtual MusicPack &musicPack() = 0;
	virtual void openMusicFolder() = 0;
	virtual std::vector<CdAudio::Track> cdTracks() const = 0;
	// Saves a PNG of the game image (filtered, without the overlay) into the
	// screenshot folder at the end of this frame (F12).
	virtual void takeScreenshot() = 0;
	// Tools tab: the running engine (null while none is), the trace log,
	// the debugging options (debugOptionsChanged() applies them), where the
	// game image is drawn (drawable pixels) and the hotspot highlight key.
	virtual Interpreter *engine() = 0;
	virtual TraceLog &traceLog() = 0;
	virtual DebugOptions &debugOptions() = 0;
	virtual void debugOptionsChanged() = 0;
	virtual GlPresenter::Rect gameRect() = 0;
	virtual bool hotspotKeyHeld() = 0;
	virtual const char *hotspotKeyName() const = 0;
	// Video tab: HD packs (the folder holding them, the packs found, the
	// one opened for Settings::hdPack and its validation), opening the
	// folder, and how many glyph cells sharp text draws this frame.
	virtual std::string hdPacksDir() const = 0;
	virtual std::vector<std::string> hdPackNames() = 0;
	virtual HdPack &hdPack() = 0;
	virtual void reloadHdPack() = 0;
	virtual void openHdPacksFolder() = 0;
	virtual int sharpTextCells() const = 0;
	// The HD compositor (attached while a pack is shown) and its coverage
	// tint (a debug view, this run only).
	virtual const HdRenderer &hdRenderer() const = 0;
	virtual bool hdShown() const = 0;
	virtual bool hdTintOn() const = 0;
	virtual void setHdTint(bool on) = 0;
	// The web build, which has no folders to open: an HD pack (a folder or
	// a zip file) or music files chosen in the browser's file dialog are
	// copied into the packs' or the music's folder; what is being copied
	// ("" when nothing is).
	virtual void importHdPack(bool zip) { (void)zip; }
	virtual void importMusic() {}
	virtual std::string importProgress() const { return {}; }
};

class Overlay {
public:
	bool init(SDL_Window *window, void *glContext);
	void shutdown();

	bool isOpen() const { return _open; }
	void setOpen(bool open) {
		_open = open;
		if (!open)
			_dialog = nullptr;
	}

	void addTab(std::string name, std::function<void()> draw);
	// Selects tab `name`; "Tab/Sub" also asks for sub-tab "Sub", which the
	// tab takes with takeSubTab().
	void selectTab(const std::string &name);
	std::string takeSubTab() { return std::exchange(_selectSub, std::string()); }

	// Something drawn over the game image (under the overlay window) every
	// frame while `active()` returns true, also while the overlay is closed.
	// Coordinates are ImGui's (window points).
	void setGameLayer(std::function<bool()> active, std::function<void(ImDrawList &)> draw) {
		_layerActive = std::move(active);
		_layerDraw = std::move(draw);
	}
	// Dim the game behind the open overlay (off while it keeps running).
	bool dimGame = true;

	// A finger was the last pointer (frontend/touch.h): the open overlay
	// closes with a tap beside it, scrolls when dragged and has larger hit
	// boxes; while it is closed the touch layer (the touch bar) is drawn
	// over everything.
	bool touchMode = false;
	void setTouchLayer(std::function<void(ImDrawList &)> draw) { _touchDraw = std::move(draw); }
	// Drawn over the game while the overlay is closed, always (a TV's key
	// tips under the picture).
	void setBarLayer(std::function<void(ImDrawList &)> draw) { _barDraw = std::move(draw); }

	// A question over the paused game in place of the tabs (a TV's Leave
	// dialog): the overlay opens for it, `draw` is called every frame
	// (with the screen's scale) and returns false once answered, and the
	// overlay closes with it.
	void showDialog(std::function<bool(float scale)> draw) {
		_dialog = std::move(draw);
		_open = true;
	}
	bool dialogOpen() const { return bool(_dialog); }

	// True while a popup is open, which should get Escape instead of the
	// overlay closing.
	bool popupOpen() const;

	// Feeds an input event to ImGui (only while open).
	void processEvent(const SDL_Event &ev);

	// Shows a short message in the corner of the window for a moment, also
	// while the overlay is closed (`ms` milliseconds, 0 = the usual moment).
	void toast(std::string text, uint32_t ms = 0);

	// Builds and draws the overlay (and any toast) into the current GL
	// framebuffer.
	void render();

	// The fonts (frontend/theme.h): Nunito, and a fixed-width font for
	// tables of values.
	const UiFonts &fonts() const { return _fonts; }

private:
	struct Tab {
		std::string name;
		std::function<void()> draw;
	};
	struct Toast {
		std::string text;
		uint32_t shownAt;
		uint32_t ms;
	};
	void drawToasts();

	std::vector<Tab> _tabs;
	std::vector<Toast> _toasts;
	std::string _selectTab, _selectSub;
	std::function<bool()> _layerActive;
	std::function<void(ImDrawList &)> _layerDraw, _touchDraw, _barDraw;
	std::function<bool(float)> _dialog;
	float _scale = 1.0f; // sizes and fonts (a TV: larger)
	bool _open = false, _initialized = false;
	bool _tapBeside = false; // touchMode: the finger down went down beside the window
	UiFonts _fonts;
};

// The Video, Audio, Gameplay, Speed and Levels tabs.
void addSettingsTabs(Overlay &overlay, OverlayHost &host);
// The Tools tab (debug panel) and the hotspot highlighter.
void addToolsTab(Overlay &overlay, OverlayHost &host);

} // namespace gob
