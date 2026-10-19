// gob2native - SDL2 front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: gobnative [<gamedir>] [--game gob1|gob2|gob3] [--start TOT] [--scale N] [--settings FILE]
//                  [--pref-root DIR] [--print-pref-dir] [--trace] [--trace-ops]
// (build/gob2native, the name before the rename, is a link to it.)
//
// The game (Gobliiins, Gobliins 2 or Goblins Quest 3) is detected from the
// folder; --game picks it instead. Without a folder the game library
// opens (frontend/library.h): a tile per game, played with a click when
// installed, imported from its GOG installer otherwise (into Games/<game>
// under the pref root, docs/install.md), the recent folders, and folders of
// the player's own; the overlay's "Back to library" returns to it. Each game keeps its settings, save games, HD packs,
// music and screenshots in its own folder (Gobliiins, Gobliins2, Goblins3)
// under the pref root, ~/Library/Application Support/gobnative/ on macOS,
// whose own settings.ini holds the recent folders (frontend/prefs.h). The
// first start of Gobliins 2 moves its folder from before the rename,
// gob2native/Gobliins2, there and leaves a link behind (docs/port-plan.md
// 5.4). --pref-root DIR uses DIR as the root instead (tests, so they never
// touch the real folders; DIR/../gob2native/Gobliins2 is what moves then).
// --print-pref-dir prints the game's folder (after the move) and quits.
//
// Runs the game in a window, presented with OpenGL. The backquote key opens
// the settings overlay (the game pauses while it is open). Escape closes
// the window when held for a second (a short press is passed to the game,
// and skips the intro, the ending or another scene during which the player
// has no control, see Interpreter::canSkip()). Holding Tab runs the game at turbo
// speed. Cmd+S / Cmd+L (Ctrl on other systems) quick save and quick load.
// F12 saves a screenshot (PNG, filtered, without the overlay) into the
// Screenshots folder next to settings.ini. On a touchscreen a tap clicks, a
// long press or a two-finger tap right-clicks, and a bar of buttons beside
// the game stands in for the keys (frontend/touch.h). Holding left Option (left Alt on
// other systems) outlines the room's hotspots. Settings are kept in
// settings.ini next to the save games; --scale applies to this run only.
//
// Debug options (times are wall-clock seconds since start):
//   --overlay-at T TAB          open the overlay on tab TAB at time T
//   --restart-at T              restart the game at time T
//   --level-at T ROOM           jump to room ROOM (e.g. GOB08) at time T
//   --quicksave-at T            quick save at time T
//   --save-dir DIR              keep save games in DIR instead
//   --music-dir DIR             take replacement music from DIR instead of
//                               the Music folder next to settings.ini
//   --quickload-at T            quick load at time T
//   --screenshot-after T FILE   write the window contents (with the overlay)
//                               to FILE at time T, then quit; PNG, or BMP if
//                               FILE ends in .bmp
//   --screenshot-dir DIR        save F12 screenshots in DIR instead
//   --key-at T KEY              press and release KEY (an SDL key name such as
//                               F12 or Space) at time T, as if typed (may be
//                               given several times)
//   --filter NAME               use filter NAME (nearest, smooth, sharp,
//                               scale2x, scale3x, xbr, crt) for this run
//   --bench-at T N              at time T, time N redraws of the frame with
//                               each filter, print ms per frame and quit
//   --show-hotspots             outline the hotspots all the time, as the
//                               Tools tab's "Always show hotspots" does
//   --sharp-text                sharp text on for this run
//   --hdpacks-dir DIR           look for HD packs in DIR instead of the
//                               HDPacks folder next to settings.ini
//   --hd-pack NAME              open HD pack NAME (or "none") for this run
//   --hd-tint                   tint what the HD pack doesn't cover: blue
//                               for original pixels (enlarged), red for
//                               areas repaired from the 320x200 frame
//   --hd-native-smooth          enlarge pictures without an HD version with
//                               Scale2x/3x (whatever hd_sprite_filter says)
//   --hd-stats                  print the HD compositor's frame times at exit
//   --hd-dump-at T PREFIX       write the HD frame and the 320x200 frame as
//                               PREFIX_hd.png and PREFIX_native.png at time T
//                               (may be given several times)
//   --hd-switch-at T NAME       switch to HD pack NAME (or "none") at time T,
//                               as choosing it in the Video tab does (may be
//                               given several times; not saved)
//   --hd-option-at T KEY VALUE  set hd_sprite_filter or hd_pack_scope to
//                               VALUE at time T, as the Video tab does
//   --mouse-at T X Y            move the mouse to game pixel (X, Y) at time T
//                               (may be given several times)
//   --click-at T X Y            the same with a left click there
//   --back-to-library-at T      leave the game for the library at time T, as
//                               the Gameplay tab's "Back to library" does
// Without a game folder, for the library (times since it opened):
//   --pick-at T DIR             add and play folder DIR at time T, as if typed
//                               (with --screenshot-after, the library's window
//                               is saved instead if that comes first)
//   --play-at T KEY             click the tile of game KEY (gob1, gob2, gob3)
//   --import-at T SETUP         import the GOG installer SETUP, as if dropped
//                               (waits for an import that is running)
//   --tile-import-at T KEY SETUP  click the tile of game KEY and choose SETUP
//                               in the file dialog
//   --cancel-import-at T        press the running import's Cancel button
//   --shot-at T FILE            save the library's window as FILE (PNG) at T
//                               and go on (may be given several times)

#include "data/datafiles.h"
#include "engine/hdrender.h"
#include "engine/interpreter.h"
#include "frontend/appicon.h"
#include "frontend/gl.h"
#include "frontend/overlay.h"
#include "frontend/library.h"
#include "frontend/prefs.h"
#include "frontend/presenter.h"
#include "frontend/settings.h"
#include "frontend/sharptext.h"
#include "frontend/touch.h"
#include "frontend/tracelog.h"
#include "gfx/hdpack.h"
#include "gfx/png.h"
#ifdef __EMSCRIPTEN__
#include "install/zip.h"
#endif

#include <SDL.h>

#ifdef _WIN32
#include "frontend/win32.h"
#elif defined(__EMSCRIPTEN__)
#include "frontend/web.h"
#include <emscripten.h>
#else
#include <unistd.h>
#endif
#ifdef GOB_WEBOS
#include "frontend/webos.h"
#include <fcntl.h>
#endif

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace gob;

namespace {

constexpr int W = Draw::kScreenW, H = Draw::kScreenH;

class App;

class SdlHost : public Host {
public:
	explicit SdlHost(App &app) : _app(app) {}

	bool traceEnabled = false;
	int mx = 152, my = 92, buttons = 0, clicks = 0;
	std::deque<int> keys;
	std::string saveDir;

	// The engine clock stops while the game is paused.
	uint32_t ticks() override { return (_paused ? _pauseStart : SDL_GetTicks()) - _pausedTotal; }
	void setPaused(bool paused) {
		if (paused == _paused)
			return;
		if (paused)
			_pauseStart = SDL_GetTicks();
		else
			_pausedTotal += SDL_GetTicks() - _pauseStart;
		_paused = paused;
	}
	bool paused() const { return _paused; }

	int mouseX() override { return mx; }
	int mouseY() override { return my; }
	int mouseButtons() override { return buttons; }
	int takeMouseClicks() override {
		int c = clicks;
		clicks = 0;
		return c;
	}
	int pollKey() override {
		if (keys.empty())
			return 0;
		int k = keys.front();
		keys.pop_front();
		return k;
	}
	std::string saveDirectory() override { return saveDir; }
	void warpMouse(int x, int y) override;
	void notify(const std::string &message) override;
	void trace(const std::string &line) override;

private:
	App &_app;
	bool _paused = false;
	uint32_t _pauseStart = 0, _pausedTotal = 0;
};

// Translates an SDL key to the BIOS-style code the scripts expect
// (scan code << 8 | ASCII; printable keys are plain ASCII).
int translateKey(const SDL_Keysym &k) {
	switch (k.sym) {
	case SDLK_RETURN: case SDLK_KP_ENTER: return 0x1C0D;
	case SDLK_ESCAPE: return 0x011B;
	case SDLK_BACKSPACE: return 0x0E08;
	case SDLK_SPACE: return 0x3920;
	case SDLK_UP: return 0x4800;
	case SDLK_DOWN: return 0x5000;
	case SDLK_LEFT: return 0x4B00;
	case SDLK_RIGHT: return 0x4D00;
	case SDLK_DELETE: return 0x5300;
	default: break;
	}
	if (k.sym >= SDLK_F1 && k.sym <= SDLK_F10)
		return (0x3B + (k.sym - SDLK_F1)) << 8;
	if (k.sym >= 32 && k.sym < 127) {
		int c = k.sym;
		if ((k.mod & KMOD_SHIFT) && c >= 'a' && c <= 'z')
			c -= 32;
		return c;
	}
	return 0;
}

// Quick save / quick load shortcuts: Cmd on macOS, Ctrl elsewhere (F5 and
// the other F-keys belong to the game).
#ifdef __APPLE__
constexpr Uint16 kShortcutMod = KMOD_GUI;
const char *const kQuickSaveShortcut = "Cmd+S", *const kQuickLoadShortcut = "Cmd+L";
#elif defined(GOB_WEBOS)
constexpr Uint16 kShortcutMod = KMOD_CTRL; // a USB keyboard's; on the remote both are in the overlay
const char *const kQuickSaveShortcut = "", *const kQuickLoadShortcut = "";
#else
constexpr Uint16 kShortcutMod = KMOD_CTRL;
const char *const kQuickSaveShortcut = "Ctrl+S", *const kQuickLoadShortcut = "Ctrl+L";
#endif

bool isOverlayKey(const SDL_Keysym &k) {
	return k.sym == SDLK_BACKQUOTE || k.scancode == SDL_SCANCODE_GRAVE;
}

// Held for turbo speed. The game's scripts never test Tab (hotspot keys are
// F1-F7, Enter and Escape; key comparisons are Escape, Backspace and text).
constexpr SDL_Scancode kTurboKey = SDL_SCANCODE_TAB;

#ifdef GOB_WEBOS
// A TV remote has no keyboard: its colour keys stand in for the desktop's
// keys, rewritten before anything looks at the event, and the tips bar
// under the picture names them (GameRun::drawTvBar). Red is Escape (skips
// the intro and cutscenes, leaves the game's menus; held, leaves the
// game), green the overlay key. Yellow shows and hides the tips, blue
// switches fast (turbo, held Tab on a keyboard) on and off; both in
// GameRun::handleEvents. Quick save and quick load are in the overlay.
void remapRemoteKey(SDL_Keysym &k) {
	switch (int(k.scancode)) {
	case SDL_SCANCODE_WEBOS_RED: k = {SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, KMOD_NONE, 0}; break;
	case SDL_SCANCODE_WEBOS_GREEN: k = {SDL_SCANCODE_GRAVE, SDLK_BACKQUOTE, KMOD_NONE, 0}; break;
	default: break;
	}
}

// The tips bar's height under the picture, in drawable pixels.
int tvBarHeight(int dh) { return dh * 7 / 100; }
#endif

// The hint key while hints are on and a level with hints is played
// (frontend/hints.cpp): H, which Gobliiins' levels never read (their keys
// are the F-keys, Enter and Escape; access codes are typed between levels);
// on a TV remote the yellow key (GameRun::handleEvents).
#ifdef GOB_WEBOS
const char *const kHintKeyName = "the yellow key";
#else
const char *const kHintKeyName = "H";
#endif

// Held to outline the hotspots. Option/Alt alone never meant anything to the
// game (it only reads keys with an ASCII or BIOS scan code); the left one is
// easy to reach next to Cmd and leaves right Alt (AltGr) to typing.
constexpr SDL_Scancode kHotspotKey = SDL_SCANCODE_LALT;
#ifdef __APPLE__
const char *const kHotspotKeyName = "left Option";
#else
const char *const kHotspotKeyName = "left Alt";
#endif

class App : public OverlayHost {
public:
	App(DataFiles &files, const GameInfo &game)
		: files(files), gameInfo(game), windowTitle(std::string(game.title) + " (gobnative)"), host(*this),
		  gameImages(files.gameDir()) {}

	DataFiles &files;
	const GameInfo &gameInfo;
	const std::string windowTitle; // "Gobliins 2 (gobnative)"
	SdlHost host;
	Settings config;
	std::string settingsPath;
	std::string start = gameInfo.startTot;
	DebugOptions debug; // Tools tab, this run only
	TraceLog log;       // every trace line, for the Tools tab

	SDL_Window *window = nullptr;
	SDL_GLContext glContext = nullptr;
	SDL_AudioDeviceID audio = 0;
	GlPresenter presenter;
	Overlay overlay;

	std::unique_ptr<Interpreter> interp;
	Task<void> game;
	Language running = Language::American;
	Mixer *audioMixer = nullptr; // read by the audio callback, swapped under the device lock

	bool restartPending = false, quitPending = false;
	bool libraryPending = false; // quit, then start again without a game folder (the library)
	int scaleOverride = 0; // --scale: used for this run, not saved
	int savedScale = 3;    // the scale the settings file had
	bool turbo = false;
	bool fastOn = false; // switched by the TV remote's blue key
	std::string title;

	// --- OverlayHost
	const GameInfo &currentGame() const override { return gameInfo; }
	Settings &settings() override { return config; }
	void settingsChanged() override {
		if (scaleOverride && config.windowScale != scaleOverride)
			scaleOverride = 0; // picked in the overlay: save it
		filterOverride = false;
		sharpTextOverride = false;
		if (config.hdPack != hdPackOverridden)
			hdPackOverride = false;
		applyVideo(false);
		applyAudio();
		applyHdPack(false);
		if (hdr.attached())
			applyHdOptions();
		_settingsDirty = true;
	}
	Language runningLanguage() const override { return running; }
	void restartGame() override { restartPending = true; }
	void quit() override { quitPending = true; }
	void backToLibrary() override { libraryPending = quitPending = true; }
	bool canSkip() const override { return interp && interp->canSkip(); }
	void skipCutscene() override {
		if (interp)
			interp->skipCutscene();
		overlay.setOpen(false);
	}
	std::vector<LevelInfo> levels() override { return interp ? interp->levels() : std::vector<LevelInfo>(); }
	std::string currentLevel() const override { return interp ? interp->currentLevel() : std::string(); }
	void jumpToLevel(const std::string &tot) override {
		overlay.setOpen(false);
		if (interp)
			interp->requestLevel(tot);
	}
	void quickSave() override {
		overlay.setOpen(false);
		if (interp)
			interp->requestQuickSave();
	}
	void quickLoad() override {
		overlay.setOpen(false);
		if (interp)
			interp->requestQuickLoad();
	}
	bool hasQuickSave() const override { return interp && interp->hasQuickSave(); }
	const char *quickSaveShortcut() const override { return kQuickSaveShortcut; }
	const char *quickLoadShortcut() const override { return kQuickLoadShortcut; }
	void takeScreenshot() override { screenshotPending = true; }
	MusicPack &musicPack() override { return music; }
	void openMusicFolder() override { openFolder(music.dir()); }
	void openFolder(const std::string &dir) {
		namespace fs = std::filesystem;
		if (dir.empty())
			return;
		std::error_code ec;
		fs::create_directories(dir, ec);
		// file:// URL with everything but unreserved characters and '/' escaped.
		std::string url = "file://";
		for (unsigned char c : fs::absolute(dir, ec).string()) {
			if (std::isalnum(c) || std::strchr("/-._~", c)) {
				url += char(c);
			} else {
				char buf[4];
				std::snprintf(buf, sizeof(buf), "%%%02X", c);
				url += buf;
			}
		}
		if (SDL_OpenURL(url.c_str()) != 0)
			overlay.toast(std::string("Cannot open the folder: ") + SDL_GetError());
	}
	std::vector<CdAudio::Track> cdTracks() const override {
		return interp ? interp->cdTracks() : std::vector<CdAudio::Track>();
	}
	Interpreter *engine() override { return interp.get(); }
	TraceLog &traceLog() override { return log; }
	DebugOptions &debugOptions() override { return debug; }
	void debugOptionsChanged() override {
		if (interp)
			interp->traceOps = debug.traceOps;
	}
	GlPresenter::Rect gameRect() override {
		int dw, dh;
		return viewport(dw, dh);
	}
	bool hotspotKeyHeld() override {
		return (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) && SDL_GetKeyboardState(nullptr)[kHotspotKey];
	}
	const char *hotspotKeyName() const override { return kHotspotKeyName; }
	std::string hdPacksDir() const override { return hdPacksPath; }
	std::vector<std::string> hdPackNames() override { return HdPack::list(hdPacksPath); }
	HdPack &hdPack() override { return hd; }
	void reloadHdPack() override { applyHdPack(true); }
	void openHdPacksFolder() override { openFolder(hdPacksPath); }
	int sharpTextCells() const override { return sharp.cellCount(); }
	const HdRenderer &hdRenderer() const override { return hdr; }
	bool hdShown() const override { return hdr.attached() && config.filters.filter != VideoFilter::Crt; }
	bool hdTintOn() const override { return hdTint; }
	void setHdTint(bool on) override {
		hdTint = on;
		applyHdOptions();
	}

#ifdef __EMSCRIPTEN__
	// --- packs and music from the browser's file dialog (web.h)
	enum class WebImport { None, PackFolder, PackZip, Music } webImport = WebImport::None;
	std::unique_ptr<ZipExtractor> zip; // a pack's zip being extracted
	std::string zipFile, zipDest;

	void importHdPack(bool zipped) override {
		if (webImport != WebImport::None || hdPacksPath.empty())
			return;
		web::PickOptions o;
		if (zipped) {
			o.accept = ".zip";
			o.destDir = web::kTemp;
		} else {
			o.folder = true; // the folder keeps its name: HDPacks/<pack>/...
			o.destDir = hdPacksPath;
		}
		web::pick(o);
		webImport = zipped ? WebImport::PackZip : WebImport::PackFolder;
	}
	void importMusic() override {
		if (webImport != WebImport::None || music.dir().empty())
			return;
		web::PickOptions o;
		o.accept = ".mp3,.wav";
		o.multiple = true;
		o.destDir = music.dir();
		web::pick(o);
		webImport = WebImport::Music;
	}
	std::string importProgress() const override {
		if (zip) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "Unpacking %s: %d%%", std::filesystem::path(zipDest).filename().string().c_str(),
			              int(zip->fraction() * 100));
			return buf;
		}
		if (webImport == WebImport::None)
			return {};
		if (web::pickState() == web::PickState::Reading) {
			char buf[32];
			std::snprintf(buf, sizeof(buf), " (%d%%)", int(web::pickProgress() * 100));
			return "Copying " + web::pickCurrent() + buf;
		}
		return "Choose in the browser's dialog...";
	}
	// Once per frame: a pick that ended, a zip to unpack.
	void pollWebImport() {
		namespace fs = std::filesystem;
		if (zip) {
			const uint32_t t0 = SDL_GetTicks();
			while (SDL_GetTicks() - t0 < 20 && zip->step(1 << 20)) {
			}
			if (!zip->done() && zip->error().empty())
				return;
			const std::string name = fs::path(zipDest).filename().string();
			overlay.toast(zip->done() ? "HD pack " + name + " imported" : "Cannot unpack the zip file: " + zip->error(),
			              zip->done() ? 0 : 6000);
			zip.reset();
			std::error_code ec;
			fs::remove(zipFile, ec);
			web::setAutoSync(true);
			web::sync();
			return;
		}
		if (webImport == WebImport::None)
			return;
		const web::PickState st = web::pickState();
		if (st == web::PickState::Choosing || st == web::PickState::Reading)
			return;
		std::string error;
		const std::vector<std::string> files = web::takePicked(error);
		const WebImport what = std::exchange(webImport, WebImport::None);
		if (st == web::PickState::Cancelled)
			return;
		if (files.empty()) {
			overlay.toast("Cannot read the files" + (error.empty() ? std::string() : ": " + error), 6000);
			return;
		}
		if (what == WebImport::Music) {
			music.rescan();
			overlay.toast(std::to_string(files.size()) + " music file(s) imported");
		} else if (what == WebImport::PackFolder) {
			// HDPacks/<pack>/...: the pack's name is the folder's.
			const fs::path rel = fs::path(files.front()).lexically_relative(hdPacksPath);
			overlay.toast("HD pack " + (rel.begin() == rel.end() ? std::string("?") : rel.begin()->string()) +
			              " imported: choose it under Pack");
		} else {
			zip = std::make_unique<ZipExtractor>();
			zipFile = files.front();
			if (!zip->open(zipFile)) {
				overlay.toast("Cannot read the zip file: " + zip->error(), 6000);
				zip.reset();
				std::error_code ec;
				fs::remove(zipFile, ec);
				return;
			}
			const std::string name = zip->topFolder().empty() ? fs::path(zipFile).stem().string() : zip->topFolder();
			zipDest = (fs::path(hdPacksPath) / name).string();
			if (!zip->begin(zipDest)) {
				overlay.toast("Cannot unpack the zip file: " + zip->error(), 6000);
				zip.reset();
				return;
			}
			web::setAutoSync(false); // not half a pack
			return;
		}
		web::sync();
	}
#endif

	// --- HD packs (validated here, drawn by the HD compositor)
	GameImages gameImages; // the original pictures, for checking pack sizes
	HdPack hd;
	HdRenderer hdr;
	bool hdTint = false;          // --hd-tint / Tools tab (this run only)
	bool hdNativeSmoothArg = false; // --hd-native-smooth (this run only)
	std::vector<double> hdFrameMs; // compositor CPU time per frame (--hd-stats)
	std::vector<double> engineMs;  // runFrame time per frame (--hd-stats; includes the layers' mirroring)
	std::vector<double> uploadMs;  // HD frame upload (CPU side) per frame (--hd-stats)
	bool hdStats = false;
	std::string hdPacksPath;  // HDPacks/ in the SDL pref dir (--hdpacks-dir)
	std::string openedPack;   // the pack hd holds
	bool hdPackOverride = false; // --hd-pack: used for this run, not saved
	std::string hdPackOverridden, savedHdPack;

	bool openedSmoothEdges = false;
	void applyHdPack(bool force) {
		if (!force && config.hdPack == openedPack) {
			if (config.hdSmoothEdges != openedSmoothEdges && hd.isOpen()) {
				openedSmoothEdges = config.hdSmoothEdges;
				HdPackOptions opt = hd.options();
				opt.alpha = config.hdSmoothEdges ? HdAlpha::Smooth : HdAlpha::Nearest;
				hd.setOptions(opt); // drops the decoded pictures; applyHdOptions rebuilds the layers
			}
			return;
		}
		openedPack = config.hdPack;
		openedSmoothEdges = config.hdSmoothEdges;
		if (openedPack.empty() || hdPacksPath.empty()) {
			hd.close();
			attachHd(); // stops compositing: the original frame at once
			return;
		}
		HdPackOptions opt;
		opt.alpha = config.hdSmoothEdges ? HdAlpha::Smooth : HdAlpha::Nearest;
		hd.open(std::filesystem::path(hdPacksPath) / openedPack,
		        [this](const std::string &tot, int id) { return gameImages(tot, id); }, opt);
		host.trace("hd pack: " + openedPack + ", " + std::to_string(hd.size()) + " pictures at " +
		           std::to_string(hd.scale()) + "x, " + std::to_string(hd.errorCount()) + " errors");
		for (const HdIssue &i : hd.issues())
			if (i.severity != HdIssue::Info)
				host.trace("hd pack: " + (i.file.empty() ? std::string("(pack)") : i.file) + ": " + i.message);
		attachHd();
	}

	// Composites the running game with the open pack (or stops). The game
	// tracks pixel sources, so the layers are rebuilt and the new pack shows
	// at once, also in the middle of a room.
	void attachHd() {
		const bool want = interp && hd.isOpen() && hd.size() > 0 && hd.maxScale() > 0;
		if (!want) {
			hdr.detach();
			return;
		}
		hdr.detach();
		applyHdOptions(); // before attaching: one rebuild with the right options
		hdr.attach(interp->draw(), &hd, hd.maxScale(), [this] { return interp->currentTot(); });
		const HdRenderer::Stats &st = hdr.stats();
		char buf[160];
		std::snprintf(buf, sizeof(buf), "hd compositor: %dx, layers rebuilt in %.1f ms (%zu pictures, %zu pixels live)",
		              hdr.scale(), st.rebuildMs, st.rebuildPictures, st.rebuildLost);
		host.trace(buf);
		if (hdStats)
			std::printf("%s\n", buf);
	}
	void applyHdOptions() {
		HdRenderOptions o;
		o.filter = hdNativeSmoothArg ? HdSpriteFilter::Scale2x : config.hdSpriteFilter;
		o.roomsOnly = config.hdRoomsOnly;
		o.smoothSilhouettes = config.hdSmoothEdges;
		o.tint = hdTint;
		hdr.setOptions(o);
	}

	// --- sharp text
	SharpText sharp;
	std::vector<Draw::TextCell> textCells;
	bool sharpTextOverride = false; // --sharp-text: on for this run, not saved
	bool savedSharpText = false;

	bool sharpTextActive() const { return config.sharpText && config.filters.filter != VideoFilter::Crt; }

	// The text layer for the frame buildFrame() made.
	GlPresenter::TextLayer textLayer() {
		GlPresenter::TextLayer layer;
		if (!sharpTextActive() || sharp.empty())
			return layer;
		layer.map = sharp.map();
		layer.colors = sharp.colors();
		layer.atlas = sharp.atlas();
		layer.atlasDirty = sharp.atlasDirty();
		layer.atlasSize = SharpText::kAtlasSize;
		layer.cellStride = SharpText::kCellStride;
		layer.slotsPerRow = SharpText::kSlotsPerRow;
		layer.samples = SharpText::kSamples;
		layer.spread = float(SharpText::kSpread);
		sharp.atlasUploaded();
		return layer;
	}

	MusicPack music; // Music/ in the SDL pref dir (--music-dir)

	void applyAudio() {
		music.fullLength = config.musicFullLength;
		if (!interp)
			return;
		interp->mixer().setVolumes(config.mixerVolumes());
		interp->mixer().setEffectsQuality(config.effectsQuality);
		interp->speech = config.speech;
	}

	std::string screenshotDir; // Screenshots/ in the SDL pref dir
	bool screenshotPending = false;
	bool filterOverride = false; // --filter: used for this run, not saved
	FilterSettings savedFilters;

	// Saves the game image just drawn (before the overlay) as a PNG.
	void saveScreenshot(const GlPresenter::Rect &vp, int dh) {
		screenshotPending = false;
		namespace fs = std::filesystem;
		std::error_code ec;
		if (screenshotDir.empty() || (fs::create_directories(screenshotDir, ec), !fs::is_directory(screenshotDir, ec))) {
			overlay.toast("Screenshot failed: no screenshot folder");
			return;
		}
		char stamp[32];
		std::time_t now = std::time(nullptr);
		std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H.%M.%S", std::localtime(&now));
		std::string name;
		for (int n = 1; n < 100; n++) {
			name = std::string("gob2 ") + stamp + (n > 1 ? " (" + std::to_string(n) + ")" : "") + ".png";
			if (!fs::exists(fs::path(screenshotDir) / name, ec))
				break;
		}
		std::vector<uint8_t> rgb = presenter.readPixels(vp, dh);
		std::string path = (fs::path(screenshotDir) / name).string();
		if (!writePng(path, rgb.data(), vp.w, vp.h, 3)) {
			overlay.toast("Screenshot failed: cannot write " + name);
			return;
		}
		host.trace("screenshot: " + path);
#ifdef __EMSCRIPTEN__
		web::download(path, name, "image/png"); // the folder is in the browser's storage
		web::sync();
		overlay.toast("Screenshot: " + name);
#else
		overlay.toast("Screenshot saved: " + name);
#endif
	}

	void saveSettings() {
		Settings saved = config;
		if (scaleOverride)
			saved.windowScale = savedScale;
		if (filterOverride)
			saved.filters = savedFilters;
		if (sharpTextOverride)
			saved.sharpText = savedSharpText;
		if (hdPackOverride)
			saved.hdPack = savedHdPack;
		if (!settingsPath.empty() && !saved.save(settingsPath))
			std::fprintf(stderr, "cannot write %s\n", settingsPath.c_str());
		_settingsDirty = false;
#ifdef __EMSCRIPTEN__
		web::sync();
#endif
	}

	// Game speed for this frame: turbo while its key is held.
	void updateSpeed() {
		turbo = !overlay.isOpen() && (SDL_GetKeyboardState(nullptr)[kTurboKey] || fastOn);
		interp->speed = turbo ? config.turboSpeed : config.speed;
		interp->cutscenesAtNormalSpeed = config.cutscenesAtNormalSpeed;

		std::string t = windowTitle;
		if (interp->speed != 1.0) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "  -  %s%gx%s", turbo ? "turbo " : "", interp->speed,
			              interp->effectiveSpeed() != interp->speed ? " (cutscene at 1x)" : "");
			t += buf;
		}
		t += "  -  ` overlay";
		if (t != title) {
			title = t;
			SDL_SetWindowTitle(window, title.c_str());
			host.trace("window title: " + title);
		}
	}

	void startGame(const std::string &tot) {
		hdr.detach();
		interp = std::make_unique<Interpreter>(files, host, gameInfo);
		interp->traceOps = debug.traceOps;
		interp->language = running = config.language;
		interp->musicPack = &music;
		interp->draw().recordText = true; // cheap; lets sharp text be switched on at any time
		interp->draw().setTrackSources(true); // lets HD packs be switched at any time
		applyAudio();
		setAudioMixer(&interp->mixer());
		attachHd();
		game = interp->run(tot);
		game.start();
	}

	void stopGame() {
		hdr.detach(); // before the Draw it composites goes
		setAudioMixer(nullptr);
		game = Task<void>(); // destroys the suspended coroutine chain
		interp.reset();
		host.keys.clear();
		host.buttons = host.clicks = 0;
	}

	void setAudioMixer(Mixer *m) {
		if (audio)
			SDL_LockAudioDevice(audio);
		audioMixer = m;
		if (audio)
			SDL_UnlockAudioDevice(audio);
	}

	// The game pauses while the overlay is open (unless the Tools tab keeps
	// it running); the system cursor shows while the overlay is open (the
	// game draws its own).
	bool backgrounded = false; // a TV: another app or the home screen is in front
	void updatePause() {
		const bool open = overlay.isOpen();
		const bool paused = (open && !debug.runBehindOverlay) || backgrounded;
		overlay.dimGame = paused;
		if (open != _cursorShown) {
			_cursorShown = open;
			SDL_ShowCursor(open ? SDL_ENABLE : SDL_DISABLE);
			host.buttons = host.clicks = 0;
			if (!open && _settingsDirty)
				saveSettings();
		}
		if (paused == host.paused())
			return;
		host.setPaused(paused);
		if (audio)
			SDL_PauseAudioDevice(audio, paused ? 1 : 0);
		host.buttons = host.clicks = 0;
	}

	// Window size for the current scale (in window points).
	void windowSizeFor(int &w, int &h) const {
		w = W * config.windowScale;
		h = (config.aspectCorrection ? H * 6 / 5 : H) * config.windowScale;
	}

	void applyVideo(bool force) {
#ifdef GOB_WEBOS
		// The window always covers the TV's screen.
		(void)force;
		_applied = config;
		return;
#endif
		bool sizeChanged = force || config.windowScale != _applied.windowScale ||
		                   config.aspectCorrection != _applied.aspectCorrection;
		if (force || config.fullscreen != _applied.fullscreen) {
			SDL_SetWindowFullscreen(window, config.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
			sizeChanged |= !config.fullscreen;
		}
#ifdef __EMSCRIPTEN__
		sizeChanged = false; // the page sizes the canvas
#endif
		if (sizeChanged && !config.fullscreen) {
			int w, h;
			windowSizeFor(w, h);
			SDL_SetWindowSize(window, w, h);
			SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		}
		_applied = config;
	}

	GlPresenter::Rect viewport(int &dw, int &dh) const {
		SDL_GL_GetDrawableSize(window, &dw, &dh);
#ifdef GOB_WEBOS
		// The picture above the key tips bar, or the whole screen without it.
		return presenter.layout(dw, dh - (config.keyTips ? tvBarHeight(dh) : 0), config.aspectCorrection,
		                        config.integerScaling);
#else
		return presenter.layout(dw, dh, config.aspectCorrection, config.integerScaling);
#endif
	}

	// Window coordinates (points) <-> game screen coordinates, through the
	// letterboxed viewport (in drawable pixels, which differ on HiDPI).
	void windowToGame(int wx, int wy, int &gx, int &gy) const {
		int ww, wh, dw, dh;
		SDL_GetWindowSize(window, &ww, &wh);
		GlPresenter::Rect vp = viewport(dw, dh);
		double px = (wx + 0.5) * dw / std::max(ww, 1), py = (wy + 0.5) * dh / std::max(wh, 1);
		gx = std::clamp(int(std::floor((px - vp.x) * W / vp.w)), 0, W - 1);
		gy = std::clamp(int(std::floor((py - vp.y) * H / vp.h)), 0, H - 1);
	}
	// The game picture in window points.
	void gameRectPoints(float &x, float &y, float &w, float &h) const {
		int ww, wh, dw, dh;
		SDL_GetWindowSize(window, &ww, &wh);
		GlPresenter::Rect vp = viewport(dw, dh);
		const float sx = float(ww) / std::max(dw, 1), sy = float(wh) / std::max(dh, 1);
		x = vp.x * sx;
		y = vp.y * sy;
		w = vp.w * sx;
		h = vp.h * sy;
	}
	void gameToWindow(int gx, int gy, int &wx, int &wy) const {
		int ww, wh, dw, dh;
		SDL_GetWindowSize(window, &ww, &wh);
		GlPresenter::Rect vp = viewport(dw, dh);
		wx = int((vp.x + (gx + 0.5) * vp.w / W) * ww / std::max(dw, 1));
		wy = int((vp.y + (gy + 0.5) * vp.h / H) * wh / std::max(dh, 1));
	}

	// Palette-converted frame plus the cursor, as RGBA bytes.
	void buildFrame(std::vector<uint8_t> &rgba) {
		const Surface &scr = interp->screen();
		const Palette &pal = interp->screenPalette();
		auto put = [&](size_t i, uint8_t idx) {
			const Rgb &c = pal[idx];
			rgba[i * 4] = c.r;
			rgba[i * 4 + 1] = c.g;
			rgba[i * 4 + 2] = c.b;
			rgba[i * 4 + 3] = 0xFF;
		};
		for (size_t i = 0; i < size_t(W) * H; i++)
			put(i, scr.data()[i]);
		// Sharp text: take the text that is on screen as drawn out of the
		// frame (the presenter draws it over the filtered image).
		textCells.clear();
		if (sharpTextActive())
			interp->draw().visibleText(textCells);
		sharp.build(textCells, pal, rgba.data(), W, H);
		// Cursor overlay at the live mouse position; colour 0 is transparent.
		auto cur = interp->cursor();
		if (!cur || host.paused())
			return;
		int ox = host.mx - cur->hotX, oy = host.my - cur->hotY;
		for (int y = 0; y < cur->height; y++)
			for (int x = 0; x < cur->width; x++) {
				uint8_t idx = cur->strip->get(cur->frameX + x, y);
				int px = ox + x, py = oy + y;
				if (idx != 0 && px >= 0 && py >= 0 && px < W && py < H) {
					put(size_t(py) * W + px, idx);
					sharp.maskOut(px, py); // the cursor stays over the text
				}
			}
	}

	// The HD frame (after buildFrame, which also made the text maps): the
	// compositor's frame with the sharp text's ink taken out and the cursor
	// from the HD cursor strip. Returns the rectangles to upload.
	std::vector<HdRenderer::Rect> buildHdFrame() {
		auto t0 = std::chrono::steady_clock::now();
		hdr.update();
		const int n = hdr.scale(), fw = hdr.frameWidth();
		uint32_t *frame = hdr.frame();
		auto block = [&](int x, int y, uint32_t c) {
			for (int j = 0; j < n; j++)
				std::fill_n(frame + size_t(y * n + j) * fw + size_t(x) * n, n, c);
		};
		for (const Draw::TextCell &c : textCells) {
			for (int j = 0; j < c.h; j++)
				for (int i = 0; i < c.w; i++)
					if (c.inked(i, j))
						block(c.x + i, c.y + j, hdr.liveColour(c.clean[size_t(j * 8 + i)]));
			hdr.overlay(c.x, c.y, c.x + c.w - 1, c.y + c.h - 1);
		}
		auto cur = interp->cursor();
		if (cur && !host.paused()) {
			const int ox = host.mx - cur->hotX, oy = host.my - cur->hotY;
			const HdLayer *layer = cur->strip->hd();
			for (int y = 0; y < cur->height * n; y++)
				for (int x = 0; x < cur->width * n; x++) {
					const int px = ox * n + x, py = oy * n + y;
					if (px < 0 || py < 0 || px >= fw || py >= hdr.frameHeight())
						continue;
					const int sx = cur->frameX * n + x;
					if (layer && layer->scale == n) {
						const size_t o = size_t(y) * layer->w + size_t(sx);
						if (layer->idx[o])
							frame[size_t(py) * fw + px] = hdr.resolve(layer->px[o], layer->idx[o]);
					} else if (uint8_t idx = cur->strip->get(sx / n, y / n)) {
						frame[size_t(py) * fw + px] = hdr.liveColour(idx);
					}
				}
			hdr.overlay(ox, oy, ox + cur->width - 1, oy + cur->height - 1);
		}
		std::vector<HdRenderer::Rect> rects = hdr.changedRects();
		if (hdStats)
			hdFrameMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
		return rects;
	}

private:
	Settings _applied;
	bool _settingsDirty = false;
	bool _cursorShown = false;
};

void SdlHost::warpMouse(int x, int y) {
	mx = x;
	my = y;
	if (_app.window && !_paused) {
		int wx, wy;
		_app.gameToWindow(x, y, wx, wy);
		SDL_WarpMouseInWindow(_app.window, wx, wy);
	}
}

void SdlHost::trace(const std::string &line) {
	if (traceEnabled)
		std::printf("%8.3fs  %s\n", SDL_GetTicks() / 1000.0, line.c_str());
	_app.log.add(SDL_GetTicks(), line);
}

void SdlHost::notify(const std::string &message) {
	trace("notify: " + message);
	_app.overlay.toast(message);
}

void audioCallback(void *user, Uint8 *stream, int len) {
	App *app = static_cast<App *>(user);
	if (app->audioMixer)
		app->audioMixer->mix(reinterpret_cast<int16_t *>(stream), len / 4);
	else
		std::memset(stream, 0, size_t(len));
}

bool createGlWindow(App &app, std::string &error) {
#ifdef GOB_GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES); // WebGL 2, webOS
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

	int w, h;
#ifdef GOB_WEBOS
	// A TV: the whole screen, always.
	webos::screenSize(w, h);
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
#else
	app.windowSizeFor(w, h);
	Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE;
	if (app.config.fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
	// 3.3 core where available, else 3.2 (all the shaders need); OpenGL
	// ES 3.0 in a browser (WebGL 2) and on a TV.
#ifdef GOB_GLES
	for (int minor : {0}) {
#else
	for (int minor : {3, 2}) {
#endif
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
		app.window = SDL_CreateWindow(app.windowTitle.c_str(), SDL_WINDOWPOS_CENTERED,
		                              SDL_WINDOWPOS_CENTERED, w, h, flags);
		if (!app.window)
			break;
		gob::setWindowIcon(app.window);
		app.glContext = SDL_GL_CreateContext(app.window);
		if (app.glContext)
			break;
		SDL_DestroyWindow(app.window);
		app.window = nullptr;
	}
	if (!app.glContext) {
#ifdef __EMSCRIPTEN__
		error = std::string("WebGL 2 context: ") + SDL_GetError();
#elif defined(GOB_GLES)
		error = std::string("OpenGL ES 3.0 context: ") + SDL_GetError();
#else
		error = std::string("OpenGL 3.2 core context: ") + SDL_GetError();
#endif
		return false;
	}
	SDL_GL_MakeCurrent(app.window, app.glContext);
#if defined(__APPLE__) || defined(__EMSCRIPTEN__)
	// SDL2's Cocoa vsync waits on a display link that stalls while the window
	// is occluded; the window server composites anyway, so pace ourselves.
	// (A browser paces the frames itself.)
	SDL_GL_SetSwapInterval(0);
#else
	SDL_GL_SetSwapInterval(1);
#endif
	if (const char *missing = gl::load(SDL_GL_GetProcAddress)) {
		error = std::string("missing GL function ") + missing;
		return false;
	}
	return app.presenter.init(W, H, error);
}

// Times `frames` redraws of the current frame with each filter, waiting for
// the GPU after each (so an upper bound: includes the CPU-GPU round trip).
void benchmarkFilters(App &app, int dw, int dh, const GlPresenter::Rect &vp, int frames) {
	std::printf("filter timings at %dx%d (viewport %dx%d), %d frames each:\n", dw, dh, vp.w, vp.h, frames);
	FilterSettings f = app.config.filters;
	for (VideoFilter v : {VideoFilter::Nearest, VideoFilter::Smooth, VideoFilter::Sharp, VideoFilter::Scale2x,
	                      VideoFilter::Scale3x, VideoFilter::Xbr, VideoFilter::Crt}) {
		f.filter = v;
		app.presenter.redraw(dw, dh, vp, f); // warm up (shader and render target set-up)
		gl::Finish();
		auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < frames; i++) {
			app.presenter.redraw(dw, dh, vp, f);
			gl::Finish(); // else a tiled GPU may drop frames that the next clear overwrites
		}
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::printf("  %-10s %7.3f ms/frame\n", filterName(v), ms / frames);
	}
	std::fflush(stdout);
}

// A running game: the window, the engine and the test options, stepped one
// frame at a time (by main's loop, or by the browser in the web build).
struct GameRun {
	std::unique_ptr<DataFiles> files; // before app, which reads through it
	std::unique_ptr<App> app;

	double askLeaveAt = -1; // (webOS)
	double overlayAt = -1, restartAt = -1, libraryAt = -1, screenshotAt = -1, levelAt = -1, quickSaveAt = -1,
	       quickLoadAt = -1;
	double benchAt = -1;
	std::vector<std::pair<double, std::string>> keysAt; // --key-at
	int benchFrames = 0;
	struct MouseMove { double at; int x, y; bool click; };
	std::vector<MouseMove> mouseMoves;
	struct HdDump { double at; std::string prefix; };
	std::vector<HdDump> hdDumps, hdSwitches; // a switch's prefix is the pack name
	struct HdOption { double at; std::string key, value; };
	std::vector<HdOption> hdOptions;
	std::string overlayTab, screenshotFile, levelRoom;

	std::vector<uint8_t> rgba = std::vector<uint8_t>(size_t(W) * H * 4);
	uint32_t startTicks = 0;
	uint32_t escapeDown = 0;
	bool clickHeld = false, started = false;

	// Touchscreens: the fingers' gestures, the touch bar (laid out each
	// frame while it shows), the finger holding one of its buttons, the
	// buttons a tap pressed (released the next frame, as the game samples
	// the buttons once a frame) and when Load was tapped once.
	TouchMouse touch;
	std::vector<TouchBarButton> touchBar;
	std::shared_ptr<HintsControl> hints; // the hint key's (frontend/hints.cpp)
	SDL_FingerID barFinger = 0;
	bool barFingerDown = false;
	int touchRelease = 0;
#ifdef GOB_WEBOS
	// A click from the remote's keys (Back: the right button).
	void remoteClick(int bit) {
		app->host.buttons |= bit;
		app->host.clicks |= bit;
		touchRelease |= bit; // released at the next frame's events
	}
	// Long Back (the TV's Exit key, claimed in main()) asks whether to keep
	// playing, go back to the library or quit; the arrow keys choose, OK
	// answers, Back or red cancels.
	void askLeave();
#endif
	uint32_t loadArmedAt = 0;
	int exitCode = 0;
	std::string error; // why the game stopped (an engine error)

	bool due(double &at) const {
		if (at < 0 || SDL_GetTicks() - startTicks < at * 1000)
			return false;
		at = -1;
		return true;
	}
	// One frame: events, the engine, drawing. False when the game is over
	// (quit, finished, failed).
	bool frame();
	// Saves the settings and closes everything; returns the exit code.
	int finish();
	// The touch bar, over everything (Overlay::setTouchLayer).
	void drawTouchBar(ImDrawList &dl);
#ifdef GOB_WEBOS
	void drawTvBar(ImDrawList &dl);
#endif

private:
	void handleEvents();
	void handleFinger(const SDL_TouchFingerEvent &f);
	void applyTouch(const TouchAction &a);
	void pressTouchButton(TouchButton button);
	void debugActions();
	void draw();
};

bool GameRun::frame() {
	App &app = *this->app;
	try {
		if (!started) {
			started = true;
			app.startGame(app.start);
		}
		if (app.quitPending)
			return false;
		handleEvents();
		if (escapeDown && SDL_GetTicks() - escapeDown > 1000)
			app.quitPending = true;
#ifndef __EMSCRIPTEN__
		// Outside the window SDL reports no motion: follow the pointer
		// there too, held at the nearest edge, so pushing past the left
		// edge scrolls Goblins 3's wide rooms as reliably as the right.
		// (Not in scripted runs, whose --move positions must stay. Not in a
		// browser, which reports no pointer outside the page.)
		if (mouseMoves.empty() && !app.overlay.isOpen() && SDL_GetMouseFocus() != app.window &&
		    (SDL_GetWindowFlags(app.window) & SDL_WINDOW_INPUT_FOCUS)) {
			int gx, gy, wx, wy;
			SDL_GetGlobalMouseState(&gx, &gy);
			SDL_GetWindowPosition(app.window, &wx, &wy);
			app.windowToGame(gx - wx, gy - wy, app.host.mx, app.host.my);
		}
#endif
		debugActions();
#ifdef __EMSCRIPTEN__
		app.pollWebImport();
#endif
		if (app.restartPending) {
			app.restartPending = false;
			app.overlay.setOpen(false);
			app.stopGame();
			app.startGame(app.gameInfo.startTot);
		}
		app.updatePause();

		uint32_t frameStart = SDL_GetTicks();
		app.updateSpeed();
		if (!app.host.paused()) {
			auto t0 = std::chrono::steady_clock::now();
			app.interp->runFrame();
			if (app.hdStats)
				app.engineMs.push_back(
					std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
			if (app.game.done() || (app.interp->quitRequested() && !app.interp->scheduler().waiting()))
				return false;
		}
#ifdef GOB_WEBOS
		// The TV's on-screen keyboard while the game edits text (a save
		// name), and only then.
		if (const bool want = app.interp->editingText() && !app.overlay.isOpen(); want != bool(SDL_IsTextInputActive()))
			want ? SDL_StartTextInput() : SDL_StopTextInput();
#endif
#ifdef GOB_WEBOS
		if (app.backgrounded)
			SDL_Delay(100); // paused behind another app: little to draw
#endif
		draw();
#ifdef __EMSCRIPTEN__
		(void)frameStart; // the browser paces the frames
#else
		// Cap at ~60 fps even without vsync.
		uint32_t elapsed = SDL_GetTicks() - frameStart;
		if (elapsed < 16)
			SDL_Delay(16 - elapsed);
#endif
		return !app.quitPending;
	} catch (const std::exception &e) {
		std::fprintf(stderr, "engine error: %s (in %s)\n", e.what(),
		             app.interp ? app.interp->currentTot().c_str() : "-");
		error = e.what();
		exitCode = 1;
		return false;
	}
}

void GameRun::handleEvents() {
	App &app = *this->app;
	if (touchRelease) {
		app.host.buttons &= ~touchRelease;
		touchRelease = 0;
	}
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		const bool open = app.overlay.isOpen();
		// A dialog (the TV's Leave, the hints' first question): the arrow
		// keys move between its buttons, OK or Enter presses one, Escape
		// (red) closes it.
		if (app.overlay.dialogOpen() && (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)) {
			Overlay &o = app.overlay;
			if (ev.type == SDL_KEYDOWN)
				switch (int(ev.key.keysym.scancode)) {
				case SDL_SCANCODE_LEFT: o.dialogFocus = std::max(0, o.dialogFocus - 1); break;
				case SDL_SCANCODE_RIGHT: o.dialogFocus = std::min(o.dialogButtons - 1, o.dialogFocus + 1); break;
				case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: o.dialogPress = true; break;
#ifdef GOB_WEBOS
				case SDL_SCANCODE_WEBOS_RED:
#endif
				case SDL_SCANCODE_ESCAPE: o.setOpen(false); break;
				default: break;
				}
			continue;
		}
#ifdef GOB_WEBOS
		// Back (claimed from the TV in main()): closes the overlay, else a
		// right click where the pointer is (puts the held item away, as the
		// mouse's right button does).
		if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_WEBOS_BACK) {
			if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
				if (open)
					app.overlay.setOpen(false);
				else
					remoteClick(2);
			}
			continue;
		}
		if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_WEBOS_EXIT) {
			if (ev.type == SDL_KEYDOWN && !ev.key.repeat && !app.overlay.dialogOpen())
				askLeave();
			continue;
		}
		// Home, another app or input in front: the game pauses.
		if (ev.type == SDL_WINDOWEVENT) {
			const auto e = ev.window.event;
			if (e == SDL_WINDOWEVENT_FOCUS_LOST || e == SDL_WINDOWEVENT_HIDDEN || e == SDL_WINDOWEVENT_MINIMIZED)
				app.backgrounded = true;
			else if (e == SDL_WINDOWEVENT_FOCUS_GAINED || e == SDL_WINDOWEVENT_SHOWN || e == SDL_WINDOWEVENT_RESTORED)
				app.backgrounded = false;
			std::fprintf(stderr, "webos: window event %d, %s\n", int(e), app.backgrounded ? "in the background" : "in front");
		} else if (ev.type == SDL_APP_WILLENTERBACKGROUND || ev.type == SDL_APP_DIDENTERBACKGROUND) {
			app.backgrounded = true;
			std::fprintf(stderr, "webos: app in the background\n");
		} else if (ev.type == SDL_APP_WILLENTERFOREGROUND || ev.type == SDL_APP_DIDENTERFOREGROUND) {
			app.backgrounded = false;
			std::fprintf(stderr, "webos: app in front\n");
		}
		if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_WEBOS_BLUE) {
			if (ev.type == SDL_KEYDOWN && !ev.key.repeat && !open) {
				app.fastOn = !app.fastOn;
				app.overlay.toast(app.fastOn ? "Fast on" : "Fast off");
			}
			continue; // never reaches the game
		}
		if ((ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) && ev.key.keysym.scancode == SDL_SCANCODE_WEBOS_YELLOW) {
			if (ev.type == SDL_KEYDOWN && !ev.key.repeat && !open && hints && hints->available()) {
				hints->request(); // with hints on, yellow is the hint key while a level is played
			} else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
				app.config.keyTips = !app.config.keyTips;
				app.settingsChanged(); // kept in settings.ini
				if (!app.config.keyTips)
					app.overlay.toast("Key tips hidden: the yellow key shows them");
			}
			continue; // never reaches the game
		}
		if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP)
			remapRemoteKey(ev.key.keysym);
#endif
		switch (ev.type) {
		case SDL_QUIT:
			app.quitPending = true;
			break;
		case SDL_KEYDOWN:
			if (ev.key.keysym.sym == SDLK_F12) { // screenshot; never reaches the game
				if (!ev.key.repeat)
					app.takeScreenshot();
			} else if ((ev.key.keysym.mod & kShortcutMod) &&
			    (ev.key.keysym.sym == SDLK_s || ev.key.keysym.sym == SDLK_l)) { // never reach the game
				if (!ev.key.repeat && !app.overlay.popupOpen()) {
					if (ev.key.keysym.sym == SDLK_s)
						app.quickSave();
					else
						app.quickLoad();
				}
			} else if (!open && ev.key.keysym.sym == SDLK_h && !(ev.key.keysym.mod & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)) &&
			           hints && hints->available()) { // the hint key; never reaches the game
				if (!ev.key.repeat)
					hints->request();
			} else if (isOverlayKey(ev.key.keysym)) { // never reaches the game
				if (!ev.key.repeat)
					app.overlay.setOpen(!open);
				escapeDown = 0;
			} else if (open) {
				if (ev.key.keysym.sym == SDLK_ESCAPE && !app.overlay.popupOpen())
					app.overlay.setOpen(false);
				else
					app.overlay.processEvent(ev);
			} else if (ev.key.keysym.scancode != kTurboKey && ev.key.keysym.scancode != kHotspotKey) {
				// turbo and the hotspot key never reach the game
				if (ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat) {
					escapeDown = SDL_GetTicks();
					// skips the intro or the ending (also before the animation
					// polls for Escape) or a scripted sequence; a no-op during play
					app.interp->skipCutscene();
				}
				int k = translateKey(ev.key.keysym);
#ifdef GOB_WEBOS
				if (k >= ' ' && k < 127 && SDL_IsTextInputActive())
					k = 0; // the same letter comes as text (SDL_TEXTINPUT)
#endif
				if (k && app.host.keys.size() < 16) {
					app.host.keys.push_back(k);
					if (app.host.traceEnabled) {
						char buf[32];
						std::snprintf(buf, sizeof(buf), "key 0x%04X to the game", k);
						app.host.trace(buf);
					}
				}
			}
			break;
		case SDL_KEYUP:
			if (ev.key.keysym.sym == SDLK_ESCAPE)
				escapeDown = 0;
			if (open && !isOverlayKey(ev.key.keysym))
				app.overlay.processEvent(ev);
			break;
		case SDL_TEXTINPUT:
			if (open && !std::strchr(ev.text.text, '`'))
				app.overlay.processEvent(ev);
#ifdef GOB_WEBOS
			// The TV's on-screen keyboard (shown while the game edits text):
			// its letters go to the game as keys.
			else if (!open)
				for (const char *c = ev.text.text; *c; c++)
					if (*c >= ' ' && *c < 127 && *c != '`' && app.host.keys.size() < 16)
						app.host.keys.push_back(*c);
#endif
			break;
		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
		case SDL_FINGERUP:
			handleFinger(ev.tfinger);
			break;
		case SDL_MOUSEMOTION:
			if (open)
				app.overlay.processEvent(ev);
			else if (ev.motion.which != SDL_TOUCH_MOUSEID) // the fingers' own events say more
				app.windowToGame(ev.motion.x, ev.motion.y, app.host.mx, app.host.my);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			if (ev.button.which != SDL_TOUCH_MOUSEID && ev.type == SDL_MOUSEBUTTONDOWN)
				app.overlay.touchMode = false; // a mouse again: no touch bar
			if (open) {
				app.overlay.processEvent(ev);
				break;
			}
			if (ev.button.which == SDL_TOUCH_MOUSEID)
				break;
			int bit = ev.button.button == SDL_BUTTON_LEFT ? 1 : ev.button.button == SDL_BUTTON_RIGHT ? 2 : 0;
			if (ev.type == SDL_MOUSEBUTTONDOWN) {
				app.host.buttons |= bit;
				app.host.clicks |= bit;
			} else
				app.host.buttons &= ~bit;
			app.windowToGame(ev.button.x, ev.button.y, app.host.mx, app.host.my);
			break;
		}
		default:
			app.overlay.processEvent(ev); // wheel, focus, window events
			break;
		}
	}
	if (app.overlay.isOpen()) {
		touch.cancel();
		touch.take();
		return;
	}
	touch.update(SDL_GetTicks());
	for (const TouchAction &a : touch.take())
		applyTouch(a);
}

void GameRun::handleFinger(const SDL_TouchFingerEvent &f) {
	App &app = *this->app;
	// Touchscreens only: a Mac's trackpad reports its fingers too.
	if (SDL_GetTouchDeviceType(f.touchId) != SDL_TOUCH_DEVICE_DIRECT)
		return;
	int ww, wh;
	SDL_GetWindowSize(app.window, &ww, &wh);
	const float x = f.x * ww, y = f.y * wh;
	const uint32_t now = SDL_GetTicks();
	if (f.type == SDL_FINGERDOWN)
		app.overlay.touchMode = true;
	if (app.overlay.isOpen())
		return; // ImGui has the mouse events SDL makes of the fingers
	if (f.type == SDL_FINGERDOWN) {
		barFingerDown = false;
		if (const TouchBarButton *b = touchBarHit(touchBar, x, y)) {
			barFinger = f.fingerId;
			barFingerDown = true;
			pressTouchButton(b->id);
		} else
			touch.down(f.fingerId, x, y, now);
	} else if (barFingerDown && f.fingerId == barFinger) {
		if (f.type == SDL_FINGERUP)
			barFingerDown = false;
	} else if (f.type == SDL_FINGERMOTION)
		touch.motion(f.fingerId, x, y, now);
	else
		touch.up(f.fingerId, x, y, now);
}

void GameRun::applyTouch(const TouchAction &a) {
	App &app = *this->app;
	SdlHost &host = app.host;
	switch (a.kind) {
	case TouchAction::Point:
		app.windowToGame(int(a.x), int(a.y), host.mx, host.my);
		break;
	case TouchAction::Click: {
		// Only on the picture: a finger beside it moves the pointer to the
		// edge (to scroll a wide room) but never clicks there.
		float gx, gy, gw, gh;
		app.gameRectPoints(gx, gy, gw, gh);
		if (a.x < gx || a.y < gy || a.x >= gx + gw || a.y >= gy + gh)
			break;
		app.windowToGame(int(a.x), int(a.y), host.mx, host.my);
		host.buttons |= a.button;
		host.clicks |= a.button;
		touchRelease |= a.button;
		if (host.traceEnabled)
			host.trace(std::string("touch: ") + (a.button == 2 ? "right" : "left") + " click at " +
			           std::to_string(host.mx) + "," + std::to_string(host.my));
		break;
	}
	case TouchAction::Lift:
		// A pointer left at the left or top edge would go on scrolling
		// Goblins Quest 3's wide rooms (Interpreter::evaluateScroll); a
		// finger held there scrolls, lifting it stops.
		host.mx = std::max(host.mx, 1);
		host.my = std::max(host.my, 1);
		break;
	}
}

void GameRun::pressTouchButton(TouchButton button) {
	App &app = *this->app;
	switch (button) {
	case TouchButton::Menu:
		app.overlay.setOpen(true);
		break;
	case TouchButton::Skip:
		if (app.canSkip())
			app.skipCutscene();
		break;
	case TouchButton::Right:
		touch.rightNext = !touch.rightNext;
		break;
	case TouchButton::Save:
		app.quickSave();
		break;
	case TouchButton::Load: // twice, as a stray tap would lose the game since the save
		if (!app.hasQuickSave())
			app.overlay.toast("No quick save yet");
		else if (loadArmedAt && SDL_GetTicks() - loadArmedAt < 3000) {
			loadArmedAt = 0;
			app.quickLoad();
		} else {
			loadArmedAt = SDL_GetTicks();
			app.overlay.toast("Tap Load again to load the quick save", 3000);
		}
		break;
	case TouchButton::Hint:
		if (hints)
			hints->request();
		break;
	case TouchButton::Keyboard:
		break; // the page shows the phone's keyboard itself (web/library_gobnative.js)
	}
}

void GameRun::drawTouchBar(ImDrawList &dl) {
	ImFont *font = app->overlay.fonts().bold;
	for (const TouchBarButton &b : touchBar) {
		const ImVec2 p0(b.x, b.y), p1(b.x + b.w, b.y + b.h);
		dl.AddRectFilled(p0, p1, b.lit ? withAlpha(colour::kAmber, 235) : withAlpha(colour::kCard, 200), 12.0f);
		dl.AddRect(p0, p1, withAlpha(b.lit ? colour::kAmberHover : colour::kBorder, 230), 12.0f, 0, 1.5f);
		const float size = std::min(15.0f, b.w * 0.3f);
		const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, b.label);
		const ImU32 text = b.lit ? colour::kOnAmber : b.enabled ? colour::kText : colour::kFaint;
		dl.AddText(font, size, ImVec2(b.x + (b.w - ts.x) / 2, b.y + (b.h - ts.y) / 2), text, b.label);
	}
}

#ifdef GOB_WEBOS
void GameRun::askLeave() {
	app->overlay.showDialog([this](float s) {
		App &app = *this->app;
		const UiFonts &fonts = app.overlay.fonts();
		ImGuiIO &io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, vec(IM_COL32(0, 0, 0, 110)));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::Begin("##leave", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
		                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav);
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
		const ImVec2 size(1160 * s, 310 * s);
		const ImVec2 p0(std::round((io.DisplaySize.x - size.x) / 2), std::round((io.DisplaySize.y - size.y) / 2));
		const ImVec2 p1(p0.x + size.x, p0.y + size.y);
		ImDrawList *dl = ImGui::GetWindowDrawList();
		dl->AddRectFilled(p0, p1, colour::kBackground, 20 * s);
		dl->AddRect(p0, p1, colour::kBorder, 20 * s, 0, 1.5f);
		const float pad = 44 * s;
		dl->AddText(fonts.bold, 44 * s, ImVec2(p0.x + pad, p0.y + 40 * s), colour::kText, "Leave the game?");
		dl->AddText(fonts.regular, 29 * s, ImVec2(p0.x + pad, p0.y + 110 * s), colour::kMuted,
		            "Progress since your last save is lost.");
		const char *const labels[] = {"Keep playing", "Back to library", "Quit"};
		const float bw = 340 * s, bh = 76 * s, gap = 24 * s, by = p1.y - pad - bh;
		app.overlay.dialogButtons = 3;
		int chosen = -1;
		for (int i = 0; i < 3; i++) {
			ImGui::SetCursorScreenPos(ImVec2(p1.x - pad - float(3 - i) * bw - float(2 - i) * gap, by));
			const bool focused = app.overlay.dialogFocus == i;
			const ImU32 base = i == 0 ? colour::kGreen : colour::kCardHover;
			const ImU32 hover = i == 0 ? colour::kGreenHover : IM_COL32(70, 62, 82, 255);
			if (webos::tvButton(labels[i], labels[i], ImVec2(bw, bh), fonts.bold, 30 * s, base, hover,
			                    i == 0 ? IM_COL32_WHITE : colour::kText, -1, true, focused,
			                    focused && app.overlay.dialogPress))
				chosen = i;
		}
		app.overlay.dialogPress = false;
		ImGui::End();
		if (chosen == 1)
			app.backToLibrary();
		else if (chosen == 2)
			app.quit();
		return chosen < 0;
	});
}

// The TV remote's colour keys and what they do now, as on the remote, in
// the strip under the picture: [red] Skip  [green] Menu  [yellow] Hide tips
// [blue] Fast. Skip is dimmed when there is nothing to skip; Fast is lit
// while on. Not drawn while hidden (the picture then takes the strip).
void GameRun::drawTvBar(ImDrawList &dl) {
	App &app = *this->app;
	int ww, wh;
	SDL_GetWindowSize(app.window, &ww, &wh);
	float gx, gy, gw, gh;
	app.gameRectPoints(gx, gy, gw, gh);
	const float barTop = gy + gh, barH = float(wh) - barTop;
	if (!app.config.keyTips || barH < 8)
		return;
	using webos::KeyTip;
	using webos::RemoteKey;
	const std::vector<KeyTip> tips = {
		{RemoteKey::Red, "Skip", app.canSkip()},
		{RemoteKey::Green, "Menu"},
		{RemoteKey::Yellow, hints && hints->available() ? "Hint" : "Hide tips"},
		{RemoteKey::Blue, app.fastOn ? "Fast: on" : "Fast", true, app.fastOn},
		{RemoteKey::Back, "Right click"},
	};
	webos::drawKeyTips(dl, app.overlay.fonts().bold, tips, 0, float(ww), barTop, barH);
}
#endif

void GameRun::debugActions() {
	App &app = *this->app;
#ifdef GOB_WEBOS
	if (due(askLeaveAt))
		askLeave();
#endif
	if (due(overlayAt)) {
		app.overlay.setOpen(true);
		app.overlay.selectTab(overlayTab);
	}
	if (due(restartAt))
		app.restartPending = true;
	if (due(libraryAt))
		app.backToLibrary();
	if (due(levelAt))
		app.jumpToLevel(levelRoom);
	for (HdDump &sw : hdSwitches)
		if (due(sw.at)) { // as if chosen in the Video tab, but not saved
			app.config.hdPack = app.hdPackOverridden = sw.prefix == "none" ? std::string() : sw.prefix;
			app.hdPackOverride = true;
			app.applyHdPack(false);
		}
	for (HdOption &o : hdOptions)
		if (due(o.at)) {
			if (o.key == "hd_pack_scope")
				app.config.hdRoomsOnly = o.value == "rooms";
			else if (o.key != "hd_sprite_filter" || !Settings::parseSpriteFilter(o.value, app.config.hdSpriteFilter))
				std::fprintf(stderr, "unknown HD option %s %s\n", o.key.c_str(), o.value.c_str());
			app.settingsChanged();
			if (app.hdStats)
				std::printf("hd option %s = %s: layers rebuilt in %.1f ms\n", o.key.c_str(), o.value.c_str(),
				            app.hdr.stats().rebuildMs);
		}
	if (due(quickSaveAt))
		app.quickSave();
	if (due(quickLoadAt))
		app.quickLoad();
	if (clickHeld) { // release a simulated click one frame later
		app.host.buttons &= ~1;
		clickHeld = false;
	}
	for (MouseMove &m : mouseMoves)
		if (due(m.at)) { // as if the mouse had moved there (and clicked)
			app.host.mx = std::clamp(m.x, 0, W - 1);
			app.host.my = std::clamp(m.y, 0, H - 1);
			if (m.click) {
				app.host.buttons |= 1;
				app.host.clicks |= 1;
				clickHeld = true;
			}
		}
	for (auto &[at, keyName] : keysAt) {
		if (!due(at))
			continue;
		// through the event queue, like a real key press
		SDL_Event key{};
		key.type = SDL_KEYDOWN;
		key.key.state = SDL_PRESSED;
		key.key.keysym.sym = SDL_GetKeyFromName(keyName.c_str());
		key.key.keysym.scancode = SDL_GetScancodeFromKey(key.key.keysym.sym);
		if (key.key.keysym.sym == SDLK_UNKNOWN)
			std::fprintf(stderr, "unknown key %s\n", keyName.c_str());
		SDL_PushEvent(&key);
		key.type = SDL_KEYUP;
		key.key.state = SDL_RELEASED;
		SDL_PushEvent(&key);
	}
}

void GameRun::draw() {
	App &app = *this->app;
	app.buildFrame(rgba);
	int dw, dh;
	GlPresenter::Rect vp = app.viewport(dw, dh);
	app.presenter.setTextLayer(app.textLayer());
	if (app.hdShown()) {
		if (app.presenter.sourceScale() != app.hdr.scale()) {
			app.presenter.setSourceScale(app.hdr.scale());
			app.hdr.invalidateAll();
		}
		std::vector<HdRenderer::Rect> changed = app.buildHdFrame();
		std::vector<GlPresenter::Rect> rects;
		for (const HdRenderer::Rect &r : changed)
			rects.push_back({r.x, r.y, r.w, r.h});
		auto u0 = std::chrono::steady_clock::now();
		app.presenter.upload(reinterpret_cast<const uint8_t *>(app.hdr.frame()), rects);
		if (app.hdStats)
			app.uploadMs.push_back(
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - u0).count());
		app.presenter.redraw(dw, dh, vp, app.config.filters);
	} else {
		app.presenter.setSourceScale(1);
		app.presenter.draw(rgba.data(), dw, dh, vp, app.config.filters);
	}
	if (app.screenshotPending)
		app.saveScreenshot(vp, dh);
	for (HdDump &d : hdDumps)
		if (due(d.at)) {
			auto rgbOf = [](const uint8_t *rgbaPx, size_t count) {
				std::vector<uint8_t> rgb(count * 3);
				for (size_t i = 0; i < count; i++)
					std::copy_n(rgbaPx + i * 4, 3, &rgb[i * 3]);
				return rgb;
			};
			std::vector<uint8_t> nat = rgbOf(rgba.data(), size_t(W) * H);
			writePng(d.prefix + "_native.png", nat.data(), W, H, 3);
			if (app.hdShown()) {
				const int fw = app.hdr.frameWidth(), fh = app.hdr.frameHeight();
				std::vector<uint8_t> hdRgb =
					rgbOf(reinterpret_cast<const uint8_t *>(app.hdr.frame()), size_t(fw) * fh);
				writePng(d.prefix + "_hd.png", hdRgb.data(), fw, fh, 3);
			}
			const HdRenderer::Coverage c = app.hdr.coverage();
			std::printf("hd dump %s: %zu HD, %zu live, %zu repaired game pixels; palette mode %d\n",
			            d.prefix.c_str(), c.hd, c.live, c.repaired, app.hdr.stats().mode);
			std::fflush(stdout);
		}
	if (due(benchAt)) {
		benchmarkFilters(app, dw, dh, vp, benchFrames);
		app.quitPending = true;
	}
	if (app.overlay.touchMode && !app.overlay.isOpen()) {
		int ww, wh;
		SDL_GetWindowSize(app.window, &ww, &wh);
		float gx, gy, gw, gh;
		app.gameRectPoints(gx, gy, gw, gh);
		TouchBarState state;
		state.canSkip = app.canSkip();
		state.hint = hints && hints->available();
		state.rightArmed = touch.rightNext;
#ifdef __EMSCRIPTEN__
		state.keyboard = true;
#endif
		touchBar = layoutTouchBar(float(ww), float(wh), gx, gy, gw, gh, state);
	} else
		touchBar.clear();
#ifdef __EMSCRIPTEN__
	const TouchBarButton *keys = nullptr;
	for (const TouchBarButton &b : touchBar)
		if (b.id == TouchButton::Keyboard)
			keys = &b;
	web::setKeyboardButton(keys ? keys->x : 0, keys ? keys->y : 0, keys ? keys->w : 0, keys ? keys->h : 0);
#endif
	app.overlay.render();
	if (due(screenshotAt)) {
		if (!app.presenter.screenshot(screenshotFile, dw, dh))
			std::fprintf(stderr, "cannot write %s\n", screenshotFile.c_str());
		app.quitPending = true;
	}
	SDL_GL_SwapWindow(app.window);
}

int GameRun::finish() {
	App &app = *this->app;
	if (exitCode == 0) {
		try {
			app.game.rethrowIfFailed();
		} catch (const std::exception &e) {
			std::fprintf(stderr, "engine error: %s (in %s)\n", e.what(),
			             app.interp ? app.interp->currentTot().c_str() : "-");
			error = e.what();
			exitCode = 1;
		}
	}

	auto printTimes = [](const char *what, std::vector<double> ms) {
		if (ms.empty())
			return;
		std::sort(ms.begin(), ms.end());
		double sum = 0;
		for (double v : ms)
			sum += v;
		std::printf("%s: %zu frames, mean %.3f ms, median %.3f ms, p95 %.3f ms, max %.3f ms\n", what, ms.size(),
		            sum / double(ms.size()), ms[ms.size() / 2], ms[ms.size() * 95 / 100], ms.back());
	};
	if (app.hdStats) {
		printTimes("engine frame", app.engineMs);
		printTimes("hd frame", app.hdFrameMs);
		printTimes("hd upload", app.uploadMs);
		const HdRenderer::Stats &st = app.hdr.stats();
		if (st.paletteRebuildMs > 0)
			std::printf("hd compositor: smoothed pictures redone for the room palette in %.1f ms (last time)\n",
			            st.paletteRebuildMs);
		std::printf("hd compositor: %zu HD and %zu original pictures, %zu pixels repaired\n", st.hdPictures,
		            st.nativePictures, st.repairedPixels);
	}
	app.saveSettings();
	if (app.audio)
		SDL_CloseAudioDevice(app.audio);
	app.audio = 0;
	app.stopGame();
	app.overlay.shutdown();
	app.presenter.shutdown();
	SDL_GL_DeleteContext(app.glContext);
	SDL_DestroyWindow(app.window);
	SDL_Quit();
	return exitCode;
}

// What main() finds out before the game is known.
struct Startup {
	const char *self = nullptr;
	std::string gameKey, prefRoot;
	std::filesystem::path root;
	AppSettings shared;
	std::string sharedPath;
	bool printPrefDir = false;
	LibraryDebug libraryDebug;
};

// Opens the game in `args[1]` (args as on the command line, with the
// folder; an empty folder with --print-pref-dir and --game only): the
// game's pref folder, its window, settings and audio. Returns the exit
// code when there is nothing to run (`run` stays empty), else -1.
int openGame(Startup &st, std::vector<std::string> argStrings, std::unique_ptr<GameRun> &run) {
	std::vector<char *> args;
	for (std::string &a : argStrings)
		args.push_back(a.data());
	args.push_back(nullptr);
	int argc = int(args.size()) - 1;
	char **argv = args.data();
	const bool folderGiven = argc >= 2 && argv[1][0];

	std::string gameMessage;
	const GameInfo *game = !folderGiven ? gameByKey(st.gameKey) : selectGame(argv[1], st.gameKey, gameMessage);
	if (!game) {
		std::fprintf(stderr, "%s\n", gameMessage.empty() ? "unknown game" : gameMessage.c_str());
		return 1;
	}

	// The game's folder for settings, saves, packs, music and screenshots;
	// Gobliins 2's moves from gob2native/ on its first start (prefs.h).
	PrefFolder prefFolder;
	if (!st.root.empty())
		prefFolder = gamePrefFolder(st.root, game->prefFolder, game->id == GameId::Gob2);
	for (const std::string &line : prefFolder.log)
		std::fprintf(stderr, "settings folder: %s\n", line.c_str());
	if (st.printPrefDir) {
		std::printf("%s\n", prefFolder.dir.string().c_str());
		return prefFolder.dir.empty() ? 1 : 0;
	}

	run = std::make_unique<GameRun>();
	run->files = std::make_unique<DataFiles>(argv[1]);
	DataFiles &files = *run->files;
	if (!files.openArchive(game->startArchive)) {
		std::fprintf(stderr, "%s not found in %s\n", game->startArchive, argv[1]);
		run.reset();
		return 1;
	}
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
		std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		run.reset();
		return 1;
	}
#ifdef GOB_WEBOS
	SDL_StopTextInput(); // SDL starts with text input on; on a TV that is the on-screen keyboard
#endif
	if (!st.sharedPath.empty()) {
		std::error_code ec;
		st.shared.addRecent(std::filesystem::absolute(argv[1], ec).lexically_normal().string(), game->key);
		if (!st.shared.save(st.sharedPath))
			std::fprintf(stderr, "cannot write %s\n", st.sharedPath.c_str());
	}

	run->app = std::make_unique<App>(files, *game);
	App &app = *run->app;
	GameRun &r = *run;
	std::fprintf(stderr, "game: %s\n", gameMessage.c_str());
	app.host.trace("game: " + gameMessage); // for the Tools tab's trace log
	for (const std::string &line : prefFolder.log)
		app.host.trace("settings folder: " + line);
	std::string musicDir;
	std::string prefDir;
	if (!prefFolder.dir.empty())
		prefDir = prefFolder.dir.string() + "/";
	if (!prefDir.empty()) {
		app.host.saveDir = prefDir;
		app.settingsPath = prefDir + "settings.ini";
		app.screenshotDir = prefDir + "Screenshots";
		musicDir = prefDir + "Music";
		app.hdPacksPath = prefDir + "HDPacks";
		std::error_code ec;
		std::filesystem::create_directories(musicDir, ec); // so it is there to drop files into
		std::filesystem::create_directories(app.hdPacksPath, ec);
	}

	int scaleArg = 0;
	bool sharpTextArg = false;
	std::string filterArg, hdPackArg;
	for (int i = 2; i < argc; i++) {
		if (!std::strcmp(argv[i], "--start") && i + 1 < argc)
			app.start = argv[++i];
		else if (!std::strcmp(argv[i], "--pref-root") && i + 1 < argc)
			++i; // read above
		else if ((!std::strcmp(argv[i], "--pick-at") || !std::strcmp(argv[i], "--shot-at") ||
		          !std::strcmp(argv[i], "--import-at") || !std::strcmp(argv[i], "--play-at")) &&
		         i + 2 < argc)
			i += 2; // read above
		else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc)
			scaleArg = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--settings") && i + 1 < argc)
			app.settingsPath = argv[++i];
		else if (!std::strcmp(argv[i], "--trace"))
			app.host.traceEnabled = true;
		else if (!std::strcmp(argv[i], "--trace-ops"))
			app.debug.traceOps = true;
		else if (!std::strcmp(argv[i], "--show-hotspots"))
			app.debug.showHotspots = true;
		else if (!std::strcmp(argv[i], "--overlay-at") && i + 2 < argc) {
			r.overlayAt = std::atof(argv[++i]);
			r.overlayTab = argv[++i];
		}
#ifdef GOB_WEBOS
		else if (!std::strcmp(argv[i], "--ask-leave-at") && i + 1 < argc) // as if Back was held
			r.askLeaveAt = std::atof(argv[++i]);
#endif
		else if (!std::strcmp(argv[i], "--restart-at") && i + 1 < argc)
			r.restartAt = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--back-to-library-at") && i + 1 < argc)
			r.libraryAt = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--tile-import-at") && i + 3 < argc)
			i += 3; // read above
		else if (!std::strcmp(argv[i], "--cancel-import-at") && i + 1 < argc)
			++i; // read above
		else if (!std::strcmp(argv[i], "--save-dir") && i + 1 < argc)
			app.host.saveDir = argv[++i];
		else if (!std::strcmp(argv[i], "--music-dir") && i + 1 < argc)
			musicDir = argv[++i];
		else if (!std::strcmp(argv[i], "--level-at") && i + 2 < argc) {
			r.levelAt = std::atof(argv[++i]);
			r.levelRoom = argv[++i];
		} else if (!std::strcmp(argv[i], "--quicksave-at") && i + 1 < argc)
			r.quickSaveAt = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--quickload-at") && i + 1 < argc)
			r.quickLoadAt = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--screenshot-after") && i + 2 < argc) {
			r.screenshotAt = std::atof(argv[++i]);
			r.screenshotFile = argv[++i];
		} else if (!std::strcmp(argv[i], "--screenshot-dir") && i + 1 < argc)
			app.screenshotDir = argv[++i];
		else if (!std::strcmp(argv[i], "--key-at") && i + 2 < argc) {
			const double at = std::atof(argv[++i]);
			r.keysAt.emplace_back(at, argv[++i]);
		} else if (!std::strcmp(argv[i], "--filter") && i + 1 < argc)
			filterArg = argv[++i];
		else if (!std::strcmp(argv[i], "--sharp-text"))
			sharpTextArg = true;
		else if (!std::strcmp(argv[i], "--hdpacks-dir") && i + 1 < argc)
			app.hdPacksPath = argv[++i];
		else if (!std::strcmp(argv[i], "--hd-pack") && i + 1 < argc)
			hdPackArg = argv[++i];
		else if (!std::strcmp(argv[i], "--hd-tint"))
			app.hdTint = true;
		else if (!std::strcmp(argv[i], "--hd-native-smooth"))
			app.hdNativeSmoothArg = true;
		else if (!std::strcmp(argv[i], "--hd-stats"))
			app.hdStats = true;
		else if (!std::strcmp(argv[i], "--hd-dump-at") && i + 2 < argc) {
			GameRun::HdDump d;
			d.at = std::atof(argv[++i]);
			d.prefix = argv[++i];
			r.hdDumps.push_back(d);
		} else if (!std::strcmp(argv[i], "--hd-switch-at") && i + 2 < argc) {
			GameRun::HdDump d;
			d.at = std::atof(argv[++i]);
			d.prefix = argv[++i];
			r.hdSwitches.push_back(d);
		} else if (!std::strcmp(argv[i], "--hd-option-at") && i + 3 < argc) {
			GameRun::HdOption o;
			o.at = std::atof(argv[++i]);
			o.key = argv[++i];
			o.value = argv[++i];
			r.hdOptions.push_back(o);
		}
		else if (!std::strcmp(argv[i], "--mouse-at") && i + 3 < argc) {
			GameRun::MouseMove m;
			m.at = std::atof(argv[++i]);
			m.x = std::atoi(argv[++i]);
			m.y = std::atoi(argv[++i]);
			m.click = false;
			r.mouseMoves.push_back(m);
		} else if (!std::strcmp(argv[i], "--click-at") && i + 3 < argc) {
			GameRun::MouseMove m;
			m.at = std::atof(argv[++i]);
			m.x = std::atoi(argv[++i]);
			m.y = std::atoi(argv[++i]);
			m.click = true;
			r.mouseMoves.push_back(m);
		} else if (!std::strcmp(argv[i], "--bench-at") && i + 2 < argc) {
			r.benchAt = std::atof(argv[++i]);
			r.benchFrames = std::max(1, std::atoi(argv[++i]));
		}
	}
	if (!app.settingsPath.empty())
		app.config.load(app.settingsPath);
	if (!game->offersLanguage(app.config.language) && !game->languages.empty())
		app.config.language = game->languages.front(); // e.g. English (US) for Gobliiins: English (UK)
	app.savedScale = app.config.windowScale;
	app.music.scan(musicDir);
	for (const MusicPack::Entry &e : app.music.entries())
		app.host.trace("music: " + e.track + " -> " + e.file + " (" +
		               (e.info.error.empty() ? e.info.description : e.info.error) + ")");
	app.savedFilters = app.config.filters;
	if (!filterArg.empty()) {
		if (!parseFilter(filterArg, app.config.filters.filter))
			std::fprintf(stderr, "unknown filter %s\n", filterArg.c_str());
		app.filterOverride = true;
	}
	app.savedSharpText = app.config.sharpText;
	if (sharpTextArg) {
		app.config.sharpText = true;
		app.sharpTextOverride = true;
	}
	app.savedHdPack = app.config.hdPack;
	if (!hdPackArg.empty()) {
		app.config.hdPack = app.hdPackOverridden = hdPackArg == "none" ? std::string() : hdPackArg;
		app.hdPackOverride = true;
	}
	app.applyHdPack(true);
	if (scaleArg)
		app.config.windowScale = app.scaleOverride =
			std::clamp(scaleArg, int(Settings::kMinScale), int(Settings::kMaxScale));

	std::string error;
	if (!createGlWindow(app, error)) {
		std::fprintf(stderr, "%s\n", error.c_str());
		run.reset();
		SDL_Quit();
		return 1;
	}
	app.applyVideo(true);
	SDL_ShowCursor(SDL_DISABLE); // the game draws its own cursor
	if (!app.overlay.init(app.window, app.glContext))
		std::fprintf(stderr, "overlay: ImGui initialisation failed\n");
	addSettingsTabs(app.overlay, app);
	r.hints = addHints(app.overlay, app, kHintKeyName); // (its layer under the Tools tab's outlines)
	addToolsTab(app.overlay, app);
	app.overlay.setTouchLayer([&r](ImDrawList &dl) { r.drawTouchBar(dl); });
#ifdef GOB_WEBOS
	app.overlay.setBarLayer([&r](ImDrawList &dl) { r.drawTvBar(dl); });
#endif
#ifdef __EMSCRIPTEN__
	// A page knows of a touchscreen once a finger touched it (the library's
	// Play): show the touch bar from the start then.
	app.overlay.touchMode = SDL_GetNumTouchDevices() > 0;
	web::setPlaying(true);
#endif
	if (prefFolder.migrated || prefFolder.legacy) {
		std::string shown = prefFolder.dir.string();
		if (const char *home = std::getenv("HOME"); home && *home && shown.rfind(home, 0) == 0)
			shown = "~" + shown.substr(std::strlen(home));
		app.overlay.toast(prefFolder.migrated ? "Settings, saves and packs moved to\n" + shown
		                                      : "Using the old settings folder\n" + shown + "\n(see migration.log)",
		                  8000);
	}

	SDL_AudioSpec want{}, have{};
	want.freq = Mixer::kRate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;
	want.userdata = &app;
	want.callback = audioCallback;
	app.audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (app.audio)
		SDL_PauseAudioDevice(app.audio, 0);
	else
		std::fprintf(stderr, "no audio: %s\n", SDL_GetError());

	r.startTicks = SDL_GetTicks();
	return -1;
}

#ifdef __EMSCRIPTEN__
// The web build: the library, then the chosen game, in the browser's frame
// loop. Leaving the game (quit, "Back to library") loads the page again,
// as the desktop app starts itself again.
struct WebMain {
	Startup st;
	std::vector<std::string> args; // the game's arguments, without the folder
	LibraryContext library;
	std::unique_ptr<LibraryScreen> screen;
	std::unique_ptr<GameRun> run;
	std::string chosen; // the game folder chosen, while its files are read from the storage
	double nextFrame = 0;
	bool frameTimer = false; // --frame-timer: frames without the display (tests)
	double statStart = 0, statMs = 0;
	int statFrames = 0;
};

WebMain *webMain = nullptr;

void webGameOver(int exitCode, const std::string &error) {
	emscripten_cancel_main_loop();
	web::setPlaying(false);
	if (exitCode == 0) {
		web::reload(""); // the library
		return;
	}
	web::sync();
	web::status("The game stopped" + (error.empty() ? std::string() : ": " + error) +
	            ". Reload the page for the game library.");
}

void webFrame() {
	WebMain &w = *webMain;
	// About 60 frames a second whatever the display's rate: the engine
	// takes one step per frame, as on the desktop.
	const double now = emscripten_get_now();
	if (now + 2 < w.nextFrame)
		return;
	w.nextFrame = std::max(w.nextFrame + 1000.0 / 60, now);
	if (w.screen) {
		if (w.screen->frame())
			return;
		w.chosen = w.screen->close();
		w.screen.reset();
		if (w.chosen.empty()) {
			webGameOver(0, {});
			return;
		}
		// The library left the games' big files in the browser's storage:
		// this game's folder and its settings folder are read first.
		std::vector<std::string> folders{w.chosen};
		const GameInfo *game = w.st.gameKey.empty() ? nullptr : gameByKey(w.st.gameKey);
		if (const Detection d = detectGame(w.chosen); !game && d.game)
			game = &gameInfo(*d.game);
		if (game && !w.st.root.empty())
			folders.push_back((w.st.root / game->prefFolder).string());
		web::load(folders);
		web::status(std::string("Loading ") + (game ? game->title : "the game") + "...");
		return;
	}
	if (!w.chosen.empty()) {
		if (web::loading())
			return;
		web::status("");
		std::vector<std::string> args = w.args;
		args.insert(args.begin() + 1, std::exchange(w.chosen, {}));
		const int code = openGame(w.st, args, w.run);
		if (!w.run)
			webGameOver(code ? code : 1, "it could not be started");
		return;
	}
	if (!w.run)
		return;
	const bool more = w.run->frame();
	if (w.frameTimer) { // tests: how fast the frames go
		w.statFrames++;
		w.statMs += emscripten_get_now() - now;
		if (now - w.statStart > 10000) {
			if (w.statStart > 0)
				std::fprintf(stderr, "web frames: %.1f per second, %.2f ms each\n", w.statFrames * 1000.0 / (now - w.statStart),
				            w.statMs / std::max(1, w.statFrames));
			w.statStart = now;
			w.statFrames = 0;
			w.statMs = 0;
		}
	}
	if (more)
		return;
	const int code = w.run->finish();
	const std::string error = w.run->error;
	w.run.reset();
	webGameOver(code, error);
}

// The browser calls webFrame for every frame it shows. With --frame-timer
// as often as it can (posted messages, which the browser doesn't slow down
// as it does frames and timers of a page it doesn't show; for tests).
void webStartFrames() {
	emscripten_set_main_loop(webFrame, 0, false);
	if (webMain->frameTimer)
		emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 0);
}
#endif

} // namespace

// Starts the app again without a game folder, which shows the library
// (Gameplay tab, "Back to library"): a fresh process is the simplest clean
// way to leave a game, whose engine and front end keep state in many
// places. Keeps --pref-root and --game. Returns only if that fails, or on
// Windows (no exec there) once the new process is started.
void relaunchLibrary(const char *self, const std::string &prefRoot, const std::string &gameKey) {
	std::vector<std::string> args = {self};
	if (!prefRoot.empty())
		args.insert(args.end(), {"--pref-root", prefRoot});
	if (!gameKey.empty())
		args.insert(args.end(), {"--game", gameKey});
	std::fflush(nullptr);
#ifdef _WIN32
	// No exec on Windows: a new process (this program, found by its own
	// path), and this one ends.
	if (windowsStartSelf(std::vector<std::string>(args.begin() + 1, args.end())))
		return;
	std::fprintf(stderr, "cannot start the game library again\n");
#elif defined(__EMSCRIPTEN__)
	(void)args;
	web::reload("");
#else
	std::vector<char *> argv;
	for (std::string &a : args)
		argv.push_back(a.data());
	argv.push_back(nullptr);
	execvp(self, argv.data());
	std::perror("cannot start the game library again");
#endif
}

int main(int argc, char **argv) {
#ifdef GOB_WEBOS
	// Back comes to the app (a right click in the game, closing a dialog,
	// leaving the library) instead of opening the TV's launcher strip;
	// held, it is the Exit key (below).
	SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_BACK, "true");
	// Held, Back is the Exit key: a dialog asks to leave (GameRun::askLeave,
	// the library's), instead of the TV closing the app at once.
	SDL_SetHint(SDL_HINT_WEBOS_ACCESS_POLICY_KEYS_EXIT, "true");
#endif
#ifdef _WIN32
	// The taskbar's large icon from the .exe's own icon (win/icon.rc).
	SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
	SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
	// takeGameOption() moves the arguments within argv, and SDL2main on
	// Windows frees argv's strings after main returns: work on a copy.
	std::vector<char *> argvCopy(argv, argv + argc + 1);
#ifdef GOB_WEBOS
	// The TV's launcher passes its launch parameters (JSON) first.
	if (argc >= 2 && argv[1][0] == '{') {
		argvCopy.erase(argvCopy.begin() + 1);
		argc--;
	}
#endif
	argv = argvCopy.data();
#ifdef _WIN32
	windowsAttachConsole();
	// Window sizes in points and the drawable in pixels on high-DPI
	// displays, as on macOS (sharp instead of stretched by Windows).
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif
#ifdef __EMSCRIPTEN__
	webMain = new WebMain; // lives as long as the page
	Startup &st = webMain->st;
#else
	Startup startup;
	Startup &st = startup;
#endif
	st.self = argv[0];
	st.gameKey = takeGameOption(argc, argv);
	// What is needed before the game folder is known.
	const bool haveFolder = argc >= 2 && std::strncmp(argv[1], "--", 2) != 0;
	LibraryDebug &libraryDebug = st.libraryDebug;
	for (int i = haveFolder ? 2 : 1; i < argc; i++) {
		if (!std::strcmp(argv[i], "--pref-root") && i + 1 < argc)
			st.prefRoot = argv[++i];
		else if (!std::strcmp(argv[i], "--print-pref-dir"))
			st.printPrefDir = true;
		else if (!std::strcmp(argv[i], "--pick-at") && i + 2 < argc) {
			libraryDebug.pickAt = std::atof(argv[++i]);
			libraryDebug.pickFolder = argv[++i];
		} else if (!std::strcmp(argv[i], "--screenshot-after") && i + 2 < argc && !haveFolder) {
			libraryDebug.screenshotAt = std::atof(argv[++i]); // of the library (taken from the game once picked)
			libraryDebug.screenshotFile = argv[++i];
		} else if (!std::strcmp(argv[i], "--shot-at") && i + 2 < argc) {
			const double t = std::atof(argv[++i]);
			libraryDebug.shots.push_back({t, argv[++i]});
		} else if (!std::strcmp(argv[i], "--import-at") && i + 2 < argc) {
			const double t = std::atof(argv[++i]);
			libraryDebug.imports.push_back({t, argv[++i]});
		} else if (!std::strcmp(argv[i], "--play-at") && i + 2 < argc) {
			const double t = std::atof(argv[++i]);
			libraryDebug.plays.push_back({t, argv[++i]});
		} else if (!std::strcmp(argv[i], "--cancel-import-at") && i + 1 < argc)
			libraryDebug.cancelAt = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--tile-import-at") && i + 3 < argc) {
			const double t = std::atof(argv[++i]);
			const std::string key = argv[++i];
			libraryDebug.tileImports.push_back({t, key + "=" + argv[++i]});
		}
#ifdef __EMSCRIPTEN__
		else if (!std::strcmp(argv[i], "--frame-timer"))
			webMain->frameTimer = true;
#endif
		else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
			std::fprintf(stderr,
			             "usage: %s [<gamedir>] [--game gob1|gob2|gob3] [--start TOT] [--scale N] [--settings FILE] "
			             "[--pref-root DIR] [--print-pref-dir] [--trace]\n",
			             argv[0]);
			return 2;
		}
	}
	// The pref root and the settings the games share.
	if (!st.prefRoot.empty())
		st.root = st.prefRoot;
#ifdef __EMSCRIPTEN__
	else
		st.root = web::kRoot; // kept in the browser's storage (web.h)
#elif defined(GOB_WEBOS)
	else
		st.root = webos::dataFolder(argv[0]);
#else
	else if (char *pref = SDL_GetPrefPath("", "gobnative")) {
		st.root = pref; // ~/Library/Application Support/gobnative/ on macOS
		SDL_free(pref);
	}
#endif
	if (!st.root.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(st.root, ec);
#ifdef GOB_WEBOS
		// The TV's launcher shows no output: the last run's is kept in the
		// data folder.
		if (!isatty(2)) {
			const int log = open((st.root / "gobnative.log").string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (log >= 0) {
				dup2(log, 2);
				close(log);
			}
		}
#endif
		st.sharedPath = (st.root / "settings.ini").string();
		st.shared.load(st.sharedPath);
	}

	// The game's arguments: the folder (inserted as args[1]) and the rest.
	std::vector<std::string> args(argv, argv + argc);

	// No folder given: the library.
	if (!haveFolder && !(st.printPrefDir && !st.gameKey.empty())) {
		if (SDL_Init(SDL_INIT_VIDEO) != 0) {
			std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
			return 1;
		}
#ifdef __EMSCRIPTEN__
		LibraryContext &library = webMain->library;
#else
		LibraryContext library;
#endif
		library.prefRoot = st.root;
		library.shared = st.sharedPath.empty() ? nullptr : &st.shared;
		library.sharedPath = st.sharedPath;
		library.gameKey = st.gameKey;
#ifdef __EMSCRIPTEN__
		webMain->args = args;
		webMain->screen = std::make_unique<LibraryScreen>(library, libraryDebug);
		if (!webMain->screen->open()) {
			web::status("This browser cannot show the game (WebGL 2 is needed).");
			return 1;
		}
		webStartFrames();
		return 0;
#else
		const std::string pickedDir = runLibrary(library, libraryDebug);
		if (pickedDir.empty()) {
			SDL_Quit();
			return 0;
		}
		std::fprintf(stderr, "picked %s\n", pickedDir.c_str());
		args.insert(args.begin() + 1, pickedDir);
#endif
	} else if (!haveFolder)
		args.insert(args.begin() + 1, std::string()); // --print-pref-dir with --game only

	std::unique_ptr<GameRun> run;
	const int code = openGame(st, args, run);
	if (!run)
		return code;
#ifdef __EMSCRIPTEN__
	webMain->args = std::vector<std::string>(args.begin(), args.begin() + 1);
	webMain->run = std::move(run);
	webStartFrames();
	return 0;
#else
	while (run->frame()) {
	}
	const int exitCode = run->finish();
	const bool library = run->app->libraryPending;
	run.reset();
	if (library)
		relaunchLibrary(st.self, st.prefRoot, st.gameKey);
	return exitCode;
#endif
}
