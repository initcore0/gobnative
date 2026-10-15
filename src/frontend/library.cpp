// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/library.h"

#include "data/datafiles.h"
#include "engine/game.h"
#include "formats/tot.h"
#include "frontend/appicon.h"
#include "frontend/gl.h"
#include "frontend/theme.h"
#ifdef __EMSCRIPTEN__
#include "frontend/web.h"
#endif
#ifdef _WIN32
#include "frontend/win32.h"
#endif
#include "gfx/image.h"
#include "gfx/png.h"
#include "install/gameinstall.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h> // ShadeVertsLinearColorGradientKeepAlpha
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#ifndef __EMSCRIPTEN__
#include <thread>
#endif

namespace gob {

namespace {

namespace fs = std::filesystem;

std::string trim(const std::string &s) {
	size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
	return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// "~/x" -> "$HOME/x"; paths pasted from Finder may come quoted.
std::string expand(std::string path) {
	path = trim(path);
	if (path.size() >= 2 && (path.front() == '"' || path.front() == '\'') && path.back() == path.front())
		path = path.substr(1, path.size() - 2);
	if (path.rfind("~/", 0) == 0 || path.rfind("~\\", 0) == 0) {
#ifdef _WIN32
		const char *home = std::getenv("USERPROFILE");
#else
		const char *home = std::getenv("HOME");
#endif
		if (home)
			path = home + path.substr(1);
	}
	return path;
}

bool isInstallerName(const std::string &path) {
	std::string ext = fs::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
	return ext == ".exe";
}

#ifdef __APPLE__
std::string runDialog(const std::string &script) {
	const std::string command = "osascript -e '" + script + "' 2>/dev/null";
	FILE *p = popen(command.c_str(), "r");
	if (!p)
		return {};
	std::string out;
	char buf[512];
	while (std::fgets(buf, sizeof(buf), p))
		out += buf;
	pclose(p);
	return trim(out);
}
#endif

#ifndef __EMSCRIPTEN__
// The system's dialogs (modal to the library's window): empty when
// cancelled or unavailable (the web build has the browser's, web.h).
std::string browseFolder(SDL_Window *owner) {
	(void)owner;
#ifdef __APPLE__
	return runDialog("POSIX path of (choose folder with prompt \"Choose the folder of a Goblins game\")");
#elif defined(_WIN32)
	return windowsChooseFolder(owner, "Choose the folder of a Goblins game");
#else
	return {};
#endif
}

std::string browseInstaller(SDL_Window *owner, const GameInfo *game) {
	(void)owner;
#if defined(__APPLE__) || defined(_WIN32)
	const std::string what = game ? std::string(game->title) : "Gobliiins, Gobliins 2 or Goblins Quest 3";
#endif
#ifdef __APPLE__
	return runDialog("POSIX path of (choose file with prompt \"Choose the GOG installer of " + what +
	                 " (setup_...exe)\" default location (path to downloads folder))");
#elif defined(_WIN32)
	return windowsChooseFile(owner, "Choose the GOG installer of " + what + " (setup_...exe)", "GOG installers",
	                         "*.exe");
#else
	(void)game;
	return {};
#endif
}

#endif

// Files can be dropped onto the window (or, in a browser, the page).
constexpr bool kCanDrop = true;
constexpr bool kWeb =
#ifdef __EMSCRIPTEN__
    true;
#else
    false;
#endif

// Where players download the games' offline installers.
constexpr const char *kGogLibrary = "https://www.gog.com/en/account";

void openUrl(const char *url) {
#ifdef __EMSCRIPTEN__
	web::openUrl(url);
#else
	SDL_OpenURL(url);
#endif
}

#ifndef __EMSCRIPTEN__
constexpr bool kHaveDialogs =
#if defined(__APPLE__) || defined(_WIN32)
    true;
#else
    false;
#endif
#endif

// The title picture shown on an installed game's tile, taken from the
// game's own data: (script file, picture, palette) as gobexport finds
// them (docs/hdpacks.md).
struct TitleArt {
	GameId game;
	const char *tot;
	uint16_t picture, palette;
};
const TitleArt kTitleArt[] = {
    {GameId::Gob1, "INTRO", 30012, 2},
    {GameId::Gob2, "INTRO0", 2, 3},
    {GameId::Gob3, "DEMO", 30000, 0},
};

// RGB pixels of the game's title picture, or nothing.
std::optional<std::vector<uint8_t>> loadTitleArt(const GameInfo &game, const fs::path &folder, int &w, int &h) {
	const TitleArt *art = nullptr;
	for (const TitleArt &a : kTitleArt)
		if (a.game == game.id)
			art = &a;
	if (!art)
		return std::nullopt;
	DataFiles files(folder);
	files.openAllArchives();
	TotFile tot;
	if (!tot.load(files, art->tot))
		return std::nullopt;
	auto pic = tot.resource(art->picture);
	auto pal = tot.resource(art->palette);
	if (!pic || !pal)
		return std::nullopt;
	auto image = decodeSprite(*pic);
	auto palette = decodeVgaPalette(pal->data);
	if (!image || !palette || image->width <= 0 || image->height <= 0)
		return std::nullopt;
	w = image->width;
	h = image->height;
	std::vector<uint8_t> rgb(size_t(w) * size_t(h) * 3);
	for (size_t i = 0; i < image->pixels.size(); i++) {
		const Rgb &c = (*palette)[image->pixels[i]];
		rgb[i * 3] = c.r;
		rgb[i * 3 + 1] = c.g;
		rgb[i * 3 + 2] = c.b;
	}
	return rgb;
}

struct Tile {
	const GameInfo *game = nullptr;
	std::string folder;   // where the game is installed, or empty
	bool imported = false; // in the app's Games folder
	gl::GLuint texture = 0;
	int texW = 0, texH = 0;
	std::string textureFolder; // the folder the picture is from
	std::string manual;        // the game's Manual.pdf, if it has one
};

struct Recent {
	std::string folder;
	std::string game; // the game's title, or why there is none
	const GameInfo *info = nullptr;
};

// An import running on a worker thread (or step by step in the frame loop
// where there are no threads: the web build).
struct Import {
	GameInstaller installer;
	const GameInfo *game = nullptr;
	fs::path setup, dest;
	std::string note; // said when it is done (an old folder moved aside)
	std::string temporary; // the installer, removed at the end (a copy the browser made)
	uint32_t startedAt = SDL_GetTicks();
	std::atomic<bool> running{false}, cancel{false};
	std::atomic<float> fraction{0};
	std::mutex mutex;
	std::string current;
#ifndef __EMSCRIPTEN__
	std::thread worker;
#endif

	void work(uint64_t budget) {
		if (cancel) {
			installer.cancel();
			running = false;
			return;
		}
		const bool more = installer.step(budget);
		fraction = float(installer.fraction());
		{
			std::lock_guard<std::mutex> lock(mutex);
			current = installer.current();
		}
		if (!more)
			running = false;
	}
	void start() {
		running = true;
#ifndef __EMSCRIPTEN__
		worker = std::thread([this] {
			while (running)
				work(4 << 20);
		});
#endif
	}
	// Frame loop: where there is no worker thread, steps for about 20 ms
	// (the page stays responsive; a GOG Galaxy part is inflated in one step).
	void frame() {
#ifdef __EMSCRIPTEN__
		const uint32_t t0 = SDL_GetTicks();
		while (running && SDL_GetTicks() - t0 < 20)
			work(256 << 10);
#endif
	}
	void join() {
#ifndef __EMSCRIPTEN__
		if (worker.joinable())
			worker.join();
#endif
	}
	~Import() {
		cancel = true;
		join();
		std::error_code ec;
		if (!temporary.empty())
			fs::remove(temporary, ec);
	}
};

void writeScreenshot(SDL_Window *window, const std::string &file) {
	int dw = 0, dh = 0;
	SDL_GL_GetDrawableSize(window, &dw, &dh);
	std::vector<uint8_t> rgb(size_t(dw) * size_t(dh) * 3);
	gl::ReadBuffer(gl::BACK);
	gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
	gl::ReadPixels(0, 0, dw, dh, gl::RGB, gl::UNSIGNED_BYTE, rgb.data());
	std::vector<uint8_t> flipped(rgb.size());
	for (int y = 0; y < dh; y++)
		std::memcpy(&flipped[size_t(y) * dw * 3], &rgb[size_t(dh - 1 - y) * dw * 3], size_t(dw) * 3);
	if (!writePng(file, flipped.data(), dw, dh, 3))
		std::fprintf(stderr, "cannot write %s\n", file.c_str());
}

// The library's look (frontend/theme.h) with more room around everything.
void applyStyle(ImGuiStyle &style) {
	applyUiStyle(style);
	style.FontSizeBase = 17.0f;
	style.WindowPadding = ImVec2(32, 26);
	style.FramePadding = ImVec2(14, 7);
	style.ItemSpacing = ImVec2(10, 10);
	style.ItemInnerSpacing = ImVec2(8, 6);
	style.WindowBorderSize = 0;
	style.ChildBorderSize = 0;
	style.FrameRounding = 9;
	style.ChildRounding = 14;
	style.PopupRounding = 12;
	style.GrabRounding = 9;
}

// How each game's tile looks: its year, and the colours of the card shown
// while it isn't installed (no art of a game that isn't here).
struct TileLook {
	GameId game;
	int year;
	ImU32 top, bottom;
	const char *numeral;
};
const TileLook kLooks[] = {
    {GameId::Gob1, 1991, IM_COL32(52, 128, 150, 255), IM_COL32(26, 48, 78, 255), "1"},
    {GameId::Gob2, 1992, IM_COL32(118, 78, 172, 255), IM_COL32(44, 30, 88, 255), "2"},
    {GameId::Gob3, 1993, IM_COL32(184, 74, 62, 255), IM_COL32(76, 26, 40, 255), "3"},
};
const TileLook &lookOf(const GameInfo &g) {
	for (const TileLook &l : kLooks)
		if (l.game == g.id)
			return l;
	return kLooks[0];
}

ImVec2 textSize(ImFont *font, float size, const char *text, float wrap = 0.0f) {
	return font->CalcTextSizeA(size, FLT_MAX, wrap, text);
}

// A rounded rectangle with a diagonal gradient.
void gradientRect(ImDrawList *dl, ImVec2 a, ImVec2 b, ImU32 from, ImU32 to, float rounding, ImDrawFlags flags) {
	const int first = dl->VtxBuffer.Size;
	dl->AddRectFilled(a, b, IM_COL32_WHITE, rounding, flags);
	ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, first, dl->VtxBuffer.Size, a, b, from, to);
}

// A soft shadow under a card.
void shadow(ImDrawList *dl, ImVec2 a, ImVec2 b, float rounding) {
	for (int i = 3; i >= 1; i--) {
		const float g = float(i) * 3.0f;
		dl->AddRectFilled(ImVec2(a.x - g * 0.5f, a.y + g * 0.6f), ImVec2(b.x + g * 0.5f, b.y + g * 1.3f),
		                  IM_COL32(0, 0, 0, 22), rounding + g);
	}
}

// A big coloured button (the tile's one obvious action).
bool primaryButton(const char *label, ImVec2 size, ImFont *font, ImU32 base, ImU32 hover, ImU32 active, ImU32 text) {
	ImGui::PushStyleColor(ImGuiCol_Button, vec(base));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(hover));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(active));
	ImGui::PushStyleColor(ImGuiCol_Text, vec(text));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
	ImGui::PushFont(font, 18.0f);
	const bool clicked = ImGui::Button(label, size);
	ImGui::PopFont();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor(4);
	return clicked;
}

// A small quiet text button (secondary actions).
bool quietButton(const char *label) {
	ImGui::PushStyleColor(ImGuiCol_Button, vec(IM_COL32(0, 0, 0, 0)));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(IM_COL32(255, 255, 255, 18)));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(IM_COL32(255, 255, 255, 30)));
	ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kMuted));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
	ImGui::PushFont(nullptr, 14.5f);
	const bool clicked = ImGui::Button(label);
	ImGui::PopFont();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(4);
	return clicked;
}

// A rounded progress bar with the percentage after it.
void progressBar(ImFont *bold, float fraction, float width, float height = 12.0f) {
	ImDrawList *dl = ImGui::GetWindowDrawList();
	char pct[16];
	std::snprintf(pct, sizeof(pct), "%d%%", int(std::clamp(fraction, 0.0f, 1.0f) * 100));
	const ImVec2 ts = textSize(bold, 17.0f, "100%");
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float barW = std::max(40.0f, width - ts.x - 12);
	const float lineH = std::max(height, ts.y);
	const ImVec2 a(p.x, p.y + (lineH - height) / 2), b(a.x + barW, a.y + height);
	dl->AddRectFilled(a, b, IM_COL32(20, 17, 24, 255), height / 2);
	if (fraction > 0)
		gradientRect(dl, a, ImVec2(a.x + std::max(height, barW * std::min(fraction, 1.0f)), b.y), colour::kAmberHover,
		             colour::kAmber, height / 2, 0);
	dl->AddText(bold, 17.0f, ImVec2(p.x + width - textSize(bold, 17.0f, pct).x, p.y + (lineH - ts.y) / 2),
	            colour::kText, pct);
	ImGui::Dummy(ImVec2(width, lineH));
}

// A numbered circle (the steps of getting a game).
void stepNumber(ImFont *bold, int n) {
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 p = ImGui::GetCursorScreenPos();
	const float r = 14.0f;
	dl->AddCircleFilled(ImVec2(p.x + r, p.y + r), r, colour::kAmber, 32);
	char digit[4];
	std::snprintf(digit, sizeof(digit), "%d", n);
	const ImVec2 ts = textSize(bold, 17.0f, digit);
	dl->AddText(bold, 17.0f, ImVec2(p.x + r - ts.x / 2, p.y + r - ts.y / 2), colour::kOnAmber, digit);
}

#ifndef __EMSCRIPTEN__
// A file:// URL for SDL_OpenURL (the manual, opened in the system's viewer).
std::string fileUrl(const fs::path &file) {
	std::string path = file.generic_string(), url = "file://";
	if (path.empty() || path[0] != '/')
		url += '/';
	for (unsigned char c : path) {
		if (std::isalnum(c) || std::strchr("/-_.~:", c))
			url += char(c);
		else {
			char hex[4];
			std::snprintf(hex, sizeof(hex), "%%%02X", c);
			url += hex;
		}
	}
	return url;
}
#endif


} // namespace

fs::path libraryGameFolder(const fs::path &prefRoot, const std::string &prefFolder) {
	return prefRoot / "Games" / prefFolder;
}

// Where a tile is drawn: the card (p0-p1), its cover (p0-c1, rounded at
// `corners`), and the top left corner and width of the text and buttons.
struct TileBox {
	ImVec2 p0, p1, c1, body;
	ImDrawFlags corners = 0;
	float innerW = 0, buttonH = 40;
};

struct LibraryScreen::State {
	State(LibraryContext &context, const LibraryDebug &dbg)
		: ctx(context), debug(dbg), screenshotAt(dbg.screenshotAt), pickAt(dbg.pickAt), cancelAt(dbg.cancelAt),
		  shots(dbg.shots), imports(dbg.imports), plays(dbg.plays), tileImports(dbg.tileImports) {}

	LibraryContext &ctx;
	const LibraryDebug debug;
	SDL_Window *window = nullptr;
	SDL_GLContext context = nullptr;
	bool imguiReady = false;

	std::vector<Tile> tiles;
	std::vector<Recent> recent;
	std::string chosen, message, detail;
	bool messageIsError = false;
	UiFonts fonts;
	float width = 0;      // of the content, this frame
	bool moreOpen = false; // "More options" (the last frame)
	float lastCoverH = -1, coverFit = -1; // the tiles' cover height, and the one that fits (last frame)
	char path[1024] = "";
	int pathId = 0; // a new id shows a path set from outside in the field
	std::unique_ptr<Import> import;

	uint32_t start = 0;
	bool quit = false;
	double screenshotAt, pickAt, cancelAt;
	std::vector<std::pair<double, std::string>> shots, imports, plays, tileImports;

#ifdef __EMSCRIPTEN__
	// A file dialog of the browser is open, or its files are being read.
	enum class Picking { None, Installer, Folder } picking = Picking::None;
	const GameInfo *pickFor = nullptr; // the tile whose installer is picked
	bool savePending = false;          // an import is being written to the browser's storage
#endif

	// Which folder each game is in: the most recent folder that holds it,
	// else the app's Games folder; the title pictures follow.
	void refresh() {
		recent.clear();
		if (ctx.shared)
			for (const std::string &f : ctx.shared->recentFolders) {
				Recent r;
				r.folder = f;
				std::error_code ec;
				const Detection d = fs::is_directory(f, ec) ? detectGame(f) : Detection{std::nullopt, "folder not found"};
				if (d.game) {
					r.info = &gameInfo(*d.game);
					r.game = r.info->title;
				} else
					r.game = fs::is_directory(f, ec) ? "no game found" : "folder not found";
				recent.push_back(r);
			}
		for (Tile &t : tiles) {
			t.folder.clear();
			t.imported = false;
			for (const Recent &r : recent)
				if (r.info == t.game) {
					t.folder = r.folder;
					break;
				}
			if (t.folder.empty() && !ctx.prefRoot.empty()) {
				const fs::path dir = libraryGameFolder(ctx.prefRoot, t.game->prefFolder);
				const Detection d = detectGame(dir);
				if (d.game && *d.game == t.game->id)
					t.folder = dir.string();
			}
			t.manual.clear();
			if (!t.folder.empty()) {
				std::error_code ec;
				const fs::path manual = fs::path(t.folder) / "Manual.pdf";
				if (fs::is_regular_file(manual, ec))
					t.manual = manual.string();
			}
			if (!t.folder.empty() && !ctx.prefRoot.empty()) {
				std::error_code ec;
				t.imported = fs::equivalent(t.folder, libraryGameFolder(ctx.prefRoot, t.game->prefFolder), ec);
			}
			if (t.textureFolder != t.folder) {
				if (t.texture)
					gl::DeleteTextures(1, &t.texture);
				t.texture = 0;
				t.textureFolder = t.folder;
				int w = 0, h = 0;
				if (!t.folder.empty())
					if (auto rgb = loadTitleArt(*t.game, t.folder, w, h)) {
						gl::GenTextures(1, &t.texture);
						gl::BindTexture(gl::TEXTURE_2D, t.texture);
						gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
						gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
						gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
						gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
						gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
#ifdef __EMSCRIPTEN__
						// WebGL 2 wants the sized format that matches RGB data.
						gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGB8, w, h, 0, gl::RGB, gl::UNSIGNED_BYTE, rgb->data());
#else
						gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, w, h, 0, gl::RGB, gl::UNSIGNED_BYTE, rgb->data());
#endif
						t.texW = w;
						t.texH = h;
					}
			}
		}
	}

	// A message for the player in plain words; `detail` (paths, the
	// reason) shows in its tooltip and under "More options".
	void say(const std::string &text, bool error, const std::string &why = {}) {
		message = text;
		messageIsError = error;
		detail = why;
		std::fprintf(stderr, "game library: %s%s%s\n", text.c_str(), why.empty() ? "" : " | ", why.c_str());
	}
	void setPath(const std::string &p) {
		std::snprintf(path, sizeof(path), "%s", p.c_str());
		pathId++;
	}
	bool allowed(const GameInfo *g) const { return ctx.gameKey.empty() || gameByKey(ctx.gameKey) == g; }
	void remember(const std::string &folder, const GameInfo &g) {
		if (!ctx.shared)
			return;
		std::error_code ec;
		ctx.shared->addRecent(fs::absolute(folder, ec).lexically_normal().string(), g.key);
		if (!ctx.sharedPath.empty() && !ctx.shared->save(ctx.sharedPath))
			std::fprintf(stderr, "cannot write %s\n", ctx.sharedPath.c_str());
	}
	void play(const std::string &folder) {
		std::string why;
		if (selectGame(folder, ctx.gameKey, why))
			chosen = folder;
		else
			say("That folder doesn't hold a Goblins game.", true, folder + ": " + why);
	}
	// A folder of the player's own: checked, then put into the recent list.
	void addFolder(const std::string &input) {
		const std::string folder = expand(input);
		std::error_code ec;
		std::string why;
		if (folder.empty())
			say("Type or choose a folder first.", true);
		else if (!fs::is_directory(folder, ec))
			say("That folder doesn't exist.", true, folder);
		else if (const GameInfo *g = selectGame(folder, ctx.gameKey, why)) {
			remember(folder, *g);
			refresh();
			say(std::string(g->title) + " was added from your folder. Press Play to start!", false, folder);
		} else
			say("No Goblins game was found in that folder.", true, folder + ": " + why);
	}

	// Moves whatever is at `dest` (when it isn't an empty folder) to
	// "<dest> (old)"; the note says so. False (with a message) if that fails.
	bool moveAside(const fs::path &dest, std::string &note) {
		std::error_code ec;
		if (!fs::exists(dest, ec) || (fs::is_directory(dest, ec) && fs::is_empty(dest, ec)))
			return true;
		// Something that isn't the game is in the way: kept, under another name.
		fs::path aside;
		for (int i = 1; i < 100; i++) {
			aside = dest.string() + (i == 1 ? std::string(" (old)") : " (old " + std::to_string(i) + ")");
			if (!fs::exists(aside, ec))
				break;
		}
		fs::rename(dest, aside, ec);
		if (ec) {
			say("Couldn't make room for the game.", true, "cannot rename " + dest.string() + ": " + ec.message());
			return false;
		}
		note = "what was in that folder before is now in " + aside.string();
		return true;
	}

	void startImport(const std::string &input, const GameInfo *expected, bool temporary = false) {
		const std::string setup = expand(input);
		if (import)
			return;
		// A picked installer (web) is a copy in memory: gone once used.
		struct Discard {
			std::string file;
			~Discard() {
				std::error_code ec;
				if (!file.empty())
					fs::remove(file, ec);
			}
		} discard{temporary ? setup : std::string()};
		if (ctx.prefRoot.empty()) {
			say("There's no folder to install games into.", true, "no pref folder");
			return;
		}
		auto im = std::make_unique<Import>();
		if (!im->installer.open(setup)) {
			say("That file isn't a GOG installer of a Goblins game.", true,
			    fs::path(setup).filename().string() + ": " + im->installer.error());
			return;
		}
		const GameInfo *g = im->installer.plan().game;
		if (expected && g != expected) {
			bool have = false; // the game of this installer
			for (const Tile &t : tiles)
				have |= t.game == g && !t.folder.empty();
			say(std::string("That's the installer of ") + g->title + ", not of " + expected->title +
			        ". Choose the installer of " + expected->title +
			        (kCanDrop && !have ? std::string(", or drop this one ") + (kWeb ? "onto the page" : "onto the window") +
			                                 " to install " + g->title + "."
			                           : "."),
			    true, setup);
			return;
		}
		if (!allowed(g)) {
			say("This window was started for another game.", true, "--game " + ctx.gameKey);
			return;
		}
		const fs::path dest = libraryGameFolder(ctx.prefRoot, g->prefFolder);
		const Detection there = detectGame(dest);
		if (there.game && *there.game == g->id) {
			say(std::string(g->title) + " is already installed.", true, dest.string());
			return;
		}
		std::error_code ec;
		fs::create_directories(dest.parent_path(), ec);
		if (!moveAside(dest, im->note))
			return;
		if (!im->installer.begin(dest)) {
			say("Couldn't start installing.", true, im->installer.error());
			return;
		}
		im->game = g;
		im->setup = setup;
		im->dest = dest;
		std::swap(im->temporary, discard.file); // removed when the import ends
		std::fprintf(stderr, "game library: importing %s from %s into %s\n", g->title, setup.c_str(),
		             dest.string().c_str());
		message.clear();
		import = std::move(im);
		import->start();
#ifdef __EMSCRIPTEN__
		web::setAutoSync(false); // not the partial folder
#endif
	}
	void finishImport() {
		import->join();
		GameInstaller &in = import->installer;
		std::fprintf(stderr, "game library: import ended after %.1f s\n", (SDL_GetTicks() - import->startedAt) / 1000.0);
		if (in.done()) {
			remember(import->dest.string(), *import->game);
			refresh();
			say(std::string(import->game->title) + " is installed. Press Play to start!", false,
			    import->dest.string() + (import->note.empty() ? "" : " (" + import->note + ")"));
		} else if (import->cancel)
			say("Installation cancelled. Nothing was installed.", false);
		else
			say("The installation failed.", true, in.error());
		import.reset();
#ifdef __EMSCRIPTEN__
		web::setAutoSync(true);
		web::sync();
		savePending = true;
#endif
	}
	void openPath(const std::string &input) {
		const std::string p = expand(input);
		std::error_code ec;
		if (!p.empty() && !fs::is_directory(p, ec) && isInstallerName(p))
			startImport(p, nullptr);
		else
			addFolder(p);
	}

#ifdef __EMSCRIPTEN__
	// The browser's file dialogs: the installer goes to memory (kTemp), a
	// game folder's files straight into Games/.incoming in the storage.
	fs::path incomingFolder() const { return ctx.prefRoot / "Games" / ".incoming"; }
	void pickInstaller(const GameInfo *game) {
		if (picking != Picking::None || import)
			return;
		web::PickOptions o;
		o.accept = ".exe";
		o.destDir = web::kTemp;
		web::pick(o);
		picking = Picking::Installer;
		pickFor = game;
		message.clear();
	}
	void pickFolder() {
		if (picking != Picking::None || import)
			return;
		std::error_code ec;
		fs::remove_all(incomingFolder(), ec); // a copy left by an earlier page
		web::PickOptions o;
		o.folder = true;
		o.topLevelOnly = true;
		o.destDir = incomingFolder().string();
		web::pick(o);
		picking = Picking::Folder;
		message.clear();
	}
	// Deletes an installed game's folder from the browser's storage.
	void removeGame(const Tile &t) {
		const std::string folder = t.folder, title = t.game->title;
		std::error_code ec;
		fs::remove_all(folder, ec);
		if (ctx.shared) {
			std::erase(ctx.shared->recentFolders, folder);
			if (!ctx.sharedPath.empty())
				ctx.shared->save(ctx.sharedPath);
		}
		refresh();
		if (ec)
			say("Couldn't remove " + title + ".", true, ec.message());
		else
			say(title + " was removed from this browser.", false);
		web::sync();
		savePending = true;
	}

	// A game folder copied into the storage: put where an import would
	// put it, if it is a game that isn't installed yet.
	void takeFolder() {
		const fs::path incoming = incomingFolder();
		std::error_code ec;
		std::string why;
		const GameInfo *g = selectGame(incoming, ctx.gameKey, why);
		if (!g) {
			say("No Goblins game was found in that folder. Choose the folder that holds the game's files.", true,
			    why + " (look for INTRO.STK and Track1.mp3)");
			fs::remove_all(incoming, ec);
			return;
		}
		const fs::path dest = libraryGameFolder(ctx.prefRoot, g->prefFolder);
		const Detection there = detectGame(dest);
		std::string note;
		if (there.game && *there.game == g->id) {
			say(std::string(g->title) + " is already installed.", true);
			fs::remove_all(incoming, ec);
			return;
		}
		if (!moveAside(dest, note)) {
			fs::remove_all(incoming, ec);
			return;
		}
		fs::rename(incoming, dest, ec);
		if (ec) {
			say("Couldn't store the game in this browser.", true, ec.message());
			fs::remove_all(incoming, ec);
			return;
		}
		remember(dest.string(), *g);
		refresh();
		say(std::string(g->title) + " was added. Press Play to start!", false, note);
		web::sync();
		savePending = true;
	}
	void pollPicker() {
		if (picking == Picking::None)
			return;
		const web::PickState st = web::pickState();
		if (st == web::PickState::Choosing || st == web::PickState::Reading)
			return;
		std::string error;
		const std::vector<std::string> files = web::takePicked(error);
		const Picking what = std::exchange(picking, Picking::None);
		if (st == web::PickState::Cancelled) {
			std::error_code ec;
			fs::remove_all(incomingFolder(), ec);
			return;
		}
		if (st == web::PickState::Failed || files.empty()) {
			say(error.empty() ? "Nothing was chosen." : "Couldn't read the files.", true, error);
			return;
		}
		if (what == Picking::Installer)
			startImport(files.front(), pickFor, true);
		else
			takeFolder();
	}
#endif

	bool openWindow();
	void debugActions(double now);
	void draw();
	void drawHeader();
	void drawBanner();
	void drawTiles(bool busy);
	void drawTile(Tile &t, int index, const TileBox &box, bool busy);
	void installFor(const GameInfo *game);
	void drawSteps();
	void drawProgress();
	void drawMore();
	bool frame();
	void shutdown();
};

bool LibraryScreen::State::openWindow() {
#ifdef __EMSCRIPTEN__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	window = SDL_CreateWindow("gobnative", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 940, 680,
	                          SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
	setWindowIcon(window);
	context = window ? SDL_GL_CreateContext(window) : nullptr;
	if (!context || gl::load(SDL_GL_GetProcAddress)) {
		std::fprintf(stderr, "game library: cannot open a window: %s\n", SDL_GetError());
		if (context)
			SDL_GL_DeleteContext(context);
		if (window)
			SDL_DestroyWindow(window);
		context = nullptr;
		window = nullptr;
		return false;
	}
	SDL_GL_MakeCurrent(window, context);
	SDL_GL_SetSwapInterval(0);

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	fonts = addUiFonts(*io.Fonts);
	applyStyle(ImGui::GetStyle());
	ImGui_ImplSDL2_InitForOpenGL(window, context);
#ifdef __EMSCRIPTEN__
	ImGui_ImplOpenGL3_Init("#version 300 es");
#else
	ImGui_ImplOpenGL3_Init("#version 150");
#endif
	imguiReady = true;

	for (const GameInfo *g : allGames()) {
		Tile t;
		t.game = g;
		tiles.push_back(t);
	}
	refresh();
	start = SDL_GetTicks();
	int installed = 0;
	for (const Tile &t : tiles)
		installed += !t.folder.empty();
	std::fprintf(stderr, "game library: %d of %zu games installed\n", installed, tiles.size());
	return true;
}

// The test options (LibraryDebug), `now` seconds after the library opened.
void LibraryScreen::State::debugActions(double now) {
	if (pickAt >= 0 && now >= pickAt) {
		pickAt = -1;
		setPath(debug.pickFolder);
		addFolder(debug.pickFolder);
		if (!messageIsError)
			play(expand(debug.pickFolder));
	}
	for (auto it = imports.begin(); it != imports.end();)
		if (now >= it->first && !import) {
			startImport(it->second, nullptr);
			it = imports.erase(it);
		} else
			++it;
	for (auto it = tileImports.begin(); it != tileImports.end();)
		if (now >= it->first && !import) {
			const size_t eq = it->second.find('=');
			startImport(it->second.substr(eq + 1), gameByKey(it->second.substr(0, eq)));
			it = tileImports.erase(it);
		} else
			++it;
	if (import && cancelAt >= 0 && now >= cancelAt) {
		cancelAt = -1;
		import->cancel = true;
	}
}

bool LibraryScreen::State::frame() {
	if (quit || !chosen.empty())
		return false;
	ImGuiIO &io = ImGui::GetIO();
	SDL_Event ev;
	const bool busy = import || !shots.empty() || !imports.empty() || !plays.empty() || !tileImports.empty() ||
	                  pickAt >= 0 || screenshotAt >= 0 || cancelAt >= 0;
#ifdef __EMSCRIPTEN__
	(void)busy;
	const bool any = SDL_PollEvent(&ev); // the browser paces the frames
#else
	const bool any = SDL_WaitEventTimeout(&ev, busy ? 16 : 50);
#endif
	if (any) {
		do {
			ImGui_ImplSDL2_ProcessEvent(&ev);
			if (ev.type == SDL_QUIT || (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE))
				quit = true;
			else if (ev.type == SDL_DROPFILE) {
				const std::string dropped = ev.drop.file;
				SDL_free(ev.drop.file);
				setPath(dropped);
				openPath(dropped);
			}
		} while (SDL_PollEvent(&ev));
	}
	const double now = (SDL_GetTicks() - start) / 1000.0;
	debugActions(now);
#ifdef __EMSCRIPTEN__
	pollPicker();
	if (!import)
		for (const std::string &dropped : web::takeDropped())
			openPath(dropped);
#endif
	if (import) {
		import->frame();
		if (!import->running)
			finishImport();
	}
	for (auto it = plays.begin(); it != plays.end();)
		if (now >= it->first && !import) {
			for (const Tile &t : tiles)
				if (gameByKey(it->second) == t.game) {
					if (t.folder.empty())
						say(std::string(t.game->title) + " isn't installed.", true);
					else
						play(t.folder);
				}
			it = plays.erase(it);
		} else
			++it;

	ImGui::GetStyle().TouchExtraPadding =
		io.MouseSource == ImGuiMouseSource_TouchScreen ? ImVec2(6, 6) : ImVec2(0, 0); // fingers miss by more
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	touchDragScroll();
	draw();
	ImGui::Render();

	int dw = 0, dh = 0;
	SDL_GL_GetDrawableSize(window, &dw, &dh);
	gl::Viewport(0, 0, dw, dh);
	gl::ClearColor(0.1f, 0.1f, 0.12f, 1.0f);
	gl::Clear(gl::COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	for (auto it = shots.begin(); it != shots.end();)
		if (now >= it->first) {
			writeScreenshot(window, it->second);
			it = shots.erase(it);
		} else
			++it;
	if (screenshotAt >= 0 && now >= screenshotAt) {
		screenshotAt = -1;
		writeScreenshot(window, debug.screenshotFile);
		quit = true;
	}
	SDL_GL_SwapWindow(window);
	(void)io;
	return !quit && chosen.empty();
}

// The tiles' sizes: at least kMinTile wide (narrower windows get fewer
// columns), at most kMaxTile; below the 4:3 cover, kBodyH for the title,
// the status, the button and the small actions.
constexpr float kMinTile = 200, kMaxTile = 330, kBodyH = 140, kPad = 16, kRound = 14;
constexpr float kListH = 132; // the height of a tile in the list of narrow windows

// The library's window: the header, the latest message, the tiles, then
// what to do next (an install's progress, or how to get the missing
// games), and "More options" at the bottom (folders of the player's own,
// a path field, the recent folders, the details of the latest message).
void LibraryScreen::State::draw() {
	ImGuiIO &io = ImGui::GetIO();
	const bool narrow = io.DisplaySize.x < 600;
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, narrow ? ImVec2(16, 18) : ImVec2(32, 26));
	ImGui::Begin("##library", nullptr,
	             ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
	                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
	                 ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::PopStyleVar();
	// The content: at most 1080 wide, centred.
	const float avail = ImGui::GetContentRegionAvail().x;
	width = std::min(avail, 1080.0f);
	const float indent = std::floor((avail - width) / 2);
	if (indent > 0)
		ImGui::Indent(indent);
#ifdef __EMSCRIPTEN__
	const bool busy = import != nullptr || picking != Picking::None;
#else
	const bool busy = import != nullptr;
#endif

	drawHeader();
	drawTiles(busy);
#ifdef __EMSCRIPTEN__
	if (savePending) {
		if (web::syncBusy()) {
			ImGui::PushFont(nullptr, 14.5f);
			ImGui::TextDisabled("Saving to this browser's storage...");
			ImGui::PopFont();
		} else
			savePending = false;
	}
#endif
	bool missing = false;
	for (const Tile &t : tiles)
		missing |= t.folder.empty() && allowed(t.game);
	if (busy)
		drawProgress();
	else if (missing)
		drawSteps();
	drawMore();

	if (indent > 0)
		ImGui::Unindent(indent);
#ifndef __EMSCRIPTEN__
	if (!import && ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput &&
	    !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
		quit = true;
#endif
	ImGui::End();
}

void LibraryScreen::State::drawHeader() {
	const bool narrow = width < 560;
	ImGui::PushFont(fonts.bold, narrow ? 28.0f : 36.0f);
	ImGui::TextUnformatted("Goblins Trilogy");
	ImGui::PopFont();
	if (!message.empty()) { // in place of the subtitle
		drawBanner();
		return;
	}
	ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kMuted));
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
	ImGui::TextWrapped(kWeb ? "Gobliiins, Gobliins 2 and Goblins Quest 3, right in your browser, from your own GOG "
	                          "copies."
	                        : "Gobliiins, Gobliins 2 and Goblins Quest 3: the classic puzzle adventures by Coktel "
	                          "Vision.");
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0, 6));
}

// The latest message, in a coloured box; its details in the tooltip.
void LibraryScreen::State::drawBanner() {
	ImGui::PushStyleColor(ImGuiCol_ChildBg, vec(messageIsError ? IM_COL32(72, 36, 42, 255) : IM_COL32(36, 60, 46, 255)));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 7));
	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
	if (ImGui::BeginChild("message", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding)) {
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float h = ImGui::GetTextLineHeight();
		ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5, p.y + h / 2), 5,
		                                             messageIsError ? colour::kBad : colour::kGood, 20);
		ImGui::Indent(20);
		ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 34);
		ImGui::TextWrapped("%s", message.c_str());
		ImGui::PopTextWrapPos();
		if (!detail.empty())
			ImGui::SetItemTooltip("%s", detail.c_str());
		ImGui::Unindent(20);
		ImGui::SameLine(ImGui::GetContentRegionMax().x - 24);
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 3);
		ImGui::PushStyleColor(ImGuiCol_Button, vec(IM_COL32(0, 0, 0, 0)));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, vec(IM_COL32(255, 255, 255, 24)));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, vec(IM_COL32(255, 255, 255, 36)));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 0));
		ImGui::PushFont(nullptr, 20.0f);
		if (ImGui::Button("\xC3\x97")) // ×
			message.clear();
		ImGui::PopFont();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor(3);
		ImGui::SetItemTooltip("Dismiss");
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();
	ImGui::Dummy(ImVec2(0, 0));
}

void LibraryScreen::State::drawTiles(bool busy) {
	const int n = int(tiles.size());
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	if (width < 600) {
		// Narrow windows: a list, the cover beside the title.
		const float gap = 12, coverW = std::floor(std::min(kListH * 4 / 3, width * 0.4f));
		lastCoverH = -1;
		for (int i = 0; i < n; i++) {
			TileBox box;
			box.p0 = ImVec2(origin.x, origin.y + float(i) * (kListH + gap));
			box.p1 = ImVec2(box.p0.x + width, box.p0.y + kListH);
			box.c1 = ImVec2(box.p0.x + coverW, box.p1.y);
			box.corners = ImDrawFlags_RoundCornersLeft;
			box.body = ImVec2(box.c1.x + 14, box.p0.y + 9);
			box.innerW = width - coverW - 28;
			box.buttonH = 36;
			drawTile(tiles[size_t(i)], i, box, busy);
		}
		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy(ImVec2(width, float(n) * (kListH + gap)));
		return;
	}
	const float gap = 22;
	const int cols = std::clamp(int((width + gap) / (kMinTile + gap)), 1, std::max(n, 1));
	float tileW = std::min(kMaxTile, (width - gap * float(cols - 1)) / float(cols));
	if (cols == n && coverFit > 0) // one row: what follows fits in the window too
		tileW = std::min(tileW, std::max(kMinTile, coverFit / 0.75f));
	tileW = std::floor(tileW);
	const float coverH = std::floor(tileW * 0.75f); // 320x200 shown as 4:3, as on the CRT
	lastCoverH = cols == n ? coverH : -1;
	const int rows = (n + cols - 1) / cols;
	for (int i = 0; i < n; i++) {
		const int row = i / cols, col = i % cols;
		const int inRow = std::min(cols, n - row * cols);
		const float rowW = float(inRow) * tileW + float(inRow - 1) * gap;
		TileBox box;
		box.p0 = ImVec2(origin.x + std::floor((width - rowW) / 2) + float(col) * (tileW + gap),
		                origin.y + float(row) * (coverH + kBodyH + gap));
		box.p1 = ImVec2(box.p0.x + tileW, box.p0.y + coverH + kBodyH);
		box.c1 = ImVec2(box.p1.x, box.p0.y + coverH);
		box.corners = ImDrawFlags_RoundCornersTop;
		box.body = ImVec2(box.p0.x + kPad, box.c1.y + 11);
		box.innerW = tileW - 2 * kPad;
		box.buttonH = 40;
		drawTile(tiles[size_t(i)], i, box, busy);
	}
	ImGui::SetCursorScreenPos(origin);
	ImGui::Dummy(ImVec2(width, float(rows) * (coverH + kBodyH) + float(rows - 1) * gap + 8));
}

void LibraryScreen::State::drawTile(Tile &t, int index, const TileBox &box, bool busy) {
	const bool installed = !t.folder.empty();
	const bool usable = allowed(t.game);
	const bool installing = import && import->game == t.game;
	const TileLook &look = lookOf(*t.game);
	const char *title = t.game->title;
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImGui::PushID(index);
	const ImVec2 p0 = box.p0, p1 = box.p1, c1 = box.c1;
	const float coverW = c1.x - p0.x, coverH = c1.y - p0.y;

	ImGui::SetCursorScreenPos(p0);
	const bool coverClicked = ImGui::InvisibleButton("cover", ImVec2(coverW, coverH)) && usable && !busy;
	const bool coverHovered = ImGui::IsItemHovered() && usable && !busy;
	const bool cardHovered = usable && !busy && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(p0, p1);
	if (coverHovered) {
		if (installed && kWeb)
			ImGui::SetTooltip("Play %s", title);
		else if (installed)
			ImGui::SetTooltip("Play %s\n%s", title, t.folder.c_str());
		else
			ImGui::SetTooltip("Install %s from its GOG installer", title);
	}

	// The card and its cover: the game's own title picture, or a tinted
	// card with its name and year.
	shadow(dl, p0, p1, kRound);
	dl->AddRectFilled(p0, p1, cardHovered ? colour::kCardHover : colour::kCard, kRound);
	if (installed && t.texture) {
		// (cropped at the sides when the cover is narrower than 4:3)
		const float crop = std::max(0.0f, 1 - (coverW / coverH) / (4.0f / 3)) / 2;
		dl->AddImageRounded(ImTextureRef(ImTextureID(t.texture)), p0, c1, ImVec2(crop, 0), ImVec2(1 - crop, 1),
		                    usable ? IM_COL32_WHITE : IM_COL32(110, 110, 110, 255), kRound, box.corners);
	} else {
		gradientRect(dl, p0, c1, usable ? look.top : IM_COL32(70, 66, 76, 255),
		             usable ? look.bottom : IM_COL32(40, 37, 44, 255), kRound, box.corners);
		const float numSize = coverH * 1.15f;
		const ImVec2 ns = textSize(fonts.bold, numSize, look.numeral);
		dl->PushClipRect(p0, c1, true);
		dl->AddText(fonts.bold, numSize, ImVec2(c1.x - ns.x - coverW * 0.03f, c1.y - ns.y * 0.84f),
		            IM_COL32(255, 255, 255, 24), look.numeral);
		dl->PopClipRect();
		float size = std::min(30.0f, std::floor(coverH * 0.17f));
		while (size > 14 && textSize(fonts.bold, size, title).x > coverW - 24)
			size -= 1;
		char year[16];
		std::snprintf(year, sizeof(year), "%d", look.year);
		const float yearSize = std::min(17.0f, size * 0.7f);
		const ImVec2 ts = textSize(fonts.bold, size, title), ys = textSize(fonts.regular, yearSize, year);
		const float y = p0.y + std::floor((coverH - ts.y - ys.y) / 2);
		dl->AddText(fonts.bold, size, ImVec2(p0.x + std::floor((coverW - ts.x) / 2), y), IM_COL32(255, 255, 255, 240),
		            title);
		dl->AddText(fonts.regular, yearSize, ImVec2(p0.x + std::floor((coverW - ys.x) / 2), y + ts.y),
		            IM_COL32(255, 255, 255, 160), year);
	}
	if (coverHovered) {
		// A round play (or install) badge over the cover.
		dl->AddRectFilled(p0, c1, IM_COL32(0, 0, 0, 60), kRound, box.corners);
		const ImVec2 c(std::floor((p0.x + c1.x) / 2), std::floor((p0.y + c1.y) / 2));
		const float r = std::min(34.0f, coverH * 0.2f);
		dl->AddCircleFilled(ImVec2(c.x, c.y + 3), r, IM_COL32(0, 0, 0, 70), 48);
		dl->AddCircleFilled(c, r, installed ? colour::kGreen : colour::kAmber, 48);
		if (installed)
			dl->AddTriangleFilled(ImVec2(c.x - r * 0.3f, c.y - r * 0.42f), ImVec2(c.x - r * 0.3f, c.y + r * 0.42f),
			                      ImVec2(c.x + r * 0.46f, c.y), IM_COL32_WHITE);
		else {
			// a down arrow
			const ImU32 ink = colour::kOnAmber;
			dl->AddRectFilled(ImVec2(c.x - r * 0.1f, c.y - r * 0.45f), ImVec2(c.x + r * 0.1f, c.y + r * 0.05f), ink);
			dl->AddTriangleFilled(ImVec2(c.x - r * 0.34f, c.y), ImVec2(c.x + r * 0.34f, c.y), ImVec2(c.x, c.y + r * 0.38f),
			                      ink);
			dl->AddRectFilled(ImVec2(c.x - r * 0.4f, c.y + r * 0.48f), ImVec2(c.x + r * 0.4f, c.y + r * 0.6f), ink);
		}
	}
	dl->AddRect(p0, p1, cardHovered ? withAlpha(colour::kAmber, 210) : colour::kBorder, kRound, 0,
	            cardHovered ? 2.0f : 1.0f);

	// The title, the year and the status.
	const float x = box.body.x, innerW = box.innerW;
	float y = box.body.y;
	dl->PushClipRect(ImVec2(x, y), ImVec2(x + innerW, p1.y), true);
	dl->AddText(fonts.bold, 21, ImVec2(x, y), usable ? colour::kText : colour::kFaint, title);
	y += 27;
	char year[16];
	std::snprintf(year, sizeof(year), "%d", look.year);
	const float small = 15.5f;
	dl->AddText(fonts.regular, small, ImVec2(x, y), colour::kMuted, year);
	float sx = x + textSize(fonts.regular, small, year).x + 10;
	const char *status = installing ? "Installing..." : installed ? "Ready to play" : usable ? "Not installed" : "Not available";
	const ImU32 dot = installing ? colour::kAmber : installed ? colour::kGood : colour::kFaint;
	const float lh = textSize(fonts.regular, small, "Ag").y;
	dl->AddCircleFilled(ImVec2(sx + 4, y + lh / 2 + 1), 4, dot, 16);
	sx += 14;
	dl->AddText(fonts.regular, small, ImVec2(sx, y), installed || installing ? withAlpha(dot, 255) : colour::kMuted, status);
	dl->PopClipRect();
	y += 25;

	// The one obvious button: Play, or Install...; the install's progress.
	ImGui::SetCursorScreenPos(ImVec2(x, y));
	bool playClicked = false, installClicked = false;
	const float buttonH = box.buttonH;
	if (installing) {
		ImGui::SetCursorScreenPos(ImVec2(x, y + (buttonH - 22) / 2));
		progressBar(fonts.bold, import->fraction, innerW, 10);
	} else if (installed) {
		ImGui::BeginDisabled(!usable || busy);
		playClicked = primaryButton("Play", ImVec2(innerW, buttonH), fonts.bold, colour::kGreen, colour::kGreenHover,
		                            colour::kGreenActive, IM_COL32_WHITE);
		ImGui::EndDisabled();
	} else {
		ImGui::BeginDisabled(!usable || busy);
		installClicked = primaryButton("Install...", ImVec2(innerW, buttonH), fonts.bold, colour::kAmber,
		                               colour::kAmberHover, colour::kAmberActive, colour::kOnAmber);
		ImGui::EndDisabled();
		if (usable && !busy)
			ImGui::SetItemTooltip("Choose the game's GOG installer (setup_...exe)");
	}
	y += buttonH + 6;

	// Small, quiet actions.
	ImGui::SetCursorScreenPos(ImVec2(x - 6, y));
	if (installed) {
		ImGui::BeginDisabled(busy);
		bool first = true;
		if (!t.manual.empty()) {
			if (quietButton("Manual")) {
#ifdef __EMSCRIPTEN__
				web::openFile(t.manual, "application/pdf");
#else
				SDL_OpenURL(fileUrl(t.manual).c_str());
#endif
			}
			ImGui::SetItemTooltip(kWeb ? "The game's manual (PDF), in a new tab" : "The game's manual (PDF)");
			first = false;
		}
#ifdef __EMSCRIPTEN__
		if (!first)
			ImGui::SameLine(0, 2);
		if (quietButton("Remove"))
			ImGui::OpenPopup("Remove game");
		ImGui::SetItemTooltip("Remove the game's files from this browser (saves and settings stay)");
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
		if (ImGui::BeginPopupModal("Remove game", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
			ImGui::PushFont(fonts.bold, 20.0f);
			ImGui::Text("Remove %s?", title);
			ImGui::PopFont();
			ImGui::TextDisabled("Its files are removed from this browser. Your saves and settings stay;\n"
			                    "install it again to play.");
			ImGui::Spacing();
			bool remove = false;
			if (primaryButton("Remove", ImVec2(120, 38), fonts.bold, IM_COL32(196, 82, 70, 255),
			                  IM_COL32(214, 100, 88, 255), IM_COL32(176, 70, 60, 255), IM_COL32_WHITE)) {
				remove = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep it", ImVec2(120, 38)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
			if (remove)
				removeGame(t);
		}
		ImGui::PopStyleVar();
#else
		if (!first)
			ImGui::SameLine(0, 2);
		ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kFaint));
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
		ImGui::PushFont(nullptr, 14.5f);
		ImGui::AlignTextToFramePadding();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6);
		ImGui::TextUnformatted(t.imported ? "Installed by this app" : "From your folder");
		ImGui::PopFont();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("%s", t.folder.c_str());
#endif
		ImGui::EndDisabled();
	}
	ImGui::PopID();

	if ((coverClicked || playClicked) && installed)
		play(t.folder);
	else if ((coverClicked || installClicked) && !installed)
		installFor(t.game);
}

// Asks for the game's installer (the system's or the browser's file dialog).
void LibraryScreen::State::installFor(const GameInfo *game) {
#ifdef __EMSCRIPTEN__
	pickInstaller(game);
#else
	if (kHaveDialogs) {
		const std::string setup = browseInstaller(window, game);
		if (!setup.empty()) {
			setPath(setup);
			startImport(setup, game);
		}
	} else
		say(std::string("Drop the GOG installer of ") + game->title +
		        " (setup_...exe) onto this window, or type its path under \"More options\".",
		    false);
#endif
}

// How to get a game that isn't installed: three numbered steps.
void LibraryScreen::State::drawSteps() {
	bool all = true;
	for (const Tile &t : tiles)
		all &= t.folder.empty();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 16));
	if (ImGui::BeginChild("steps", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding)) {
		const char *heading = all ? "Get your games" : "Get the missing games";
		const char *intro = "You play your own copies from GOG.com.";
		ImGui::PushFont(fonts.bold, 21.0f);
		ImGui::TextUnformatted(heading);
		const float headingW = ImGui::CalcTextSize(heading).x;
		ImGui::PopFont();
		ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kMuted));
		if (headingW + ImGui::CalcTextSize(intro).x + 40 < ImGui::GetContentRegionAvail().x) {
			ImGui::SameLine(0, 14);
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
		}
		ImGui::TextWrapped("%s", intro);
		ImGui::PopStyleColor();
		ImGui::Dummy(ImVec2(0, 2));
		const bool wide = width >= 720;
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(wide ? 10.0f : 2.0f, wide ? 2.0f : 6.0f));
		if (ImGui::BeginTable("steps", wide ? 3 : 1, ImGuiTableFlags_SizingStretchSame)) {
			// A number, then (in a group beside it) what to do and how.
			auto step = [&](int n, const char *what, const char *how) {
				ImGui::TableNextColumn();
				const ImVec2 p = ImGui::GetCursorScreenPos();
				stepNumber(fonts.bold, n);
				ImGui::SetCursorScreenPos(ImVec2(p.x + 40, p.y + 2));
				// (not aligned to the frame of the button in the first cell)
				ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0;
				ImGui::BeginGroup();
				ImGui::PushFont(fonts.bold, 17.0f);
				ImGui::TextWrapped("%s", what);
				ImGui::PopFont();
				if (how) {
					ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kMuted));
					ImGui::PushFont(nullptr, 15.5f);
					ImGui::TextWrapped("%s", how);
					ImGui::PopFont();
					ImGui::PopStyleColor();
				}
			};
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 4));
			step(1, "Open your GOG library", nullptr);
			ImGui::Dummy(ImVec2(0, 2));
			if (primaryButton("Open GOG.com", ImVec2(0, 32), fonts.bold, colour::kAmber, colour::kAmberHover,
			                  colour::kAmberActive, colour::kOnAmber))
				openUrl(kGogLibrary);
			ImGui::SetItemTooltip("%s, in your web browser", kGogLibrary);
			ImGui::EndGroup();
			step(2, "Download the installer",
			     "In the game's \"offline backup game installers\", get the setup_...exe file.");
			ImGui::EndGroup();
			step(3, kWeb ? "Drop it onto this page" : "Drop it onto this window",
			     "Or click Install... on the game and choose the file.");
			ImGui::EndGroup();
			ImGui::PopStyleVar();
			ImGui::EndTable();
		}
		ImGui::PopStyleVar();
		if (kWeb) {
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kFaint));
			ImGui::PushFont(nullptr, 14.5f);
			ImGui::TextWrapped("The installer is read right here: the games, your settings and saves stay in this "
			                   "browser. Nothing is uploaded.");
			ImGui::PopFont();
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
}

// An install's progress (or, in a browser, the file dialog and the reading
// of the chosen files).
void LibraryScreen::State::drawProgress() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 18));
	if (ImGui::BeginChild("progress", ImVec2(width, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding)) {
		const float w = ImGui::GetContentRegionAvail().x;
		bool cancel = false;
		auto heading = [&](const std::string &what, const char *how) {
			ImGui::PushFont(fonts.bold, 21.0f);
			ImGui::TextUnformatted(what.c_str());
			ImGui::PopFont();
			ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kMuted));
			ImGui::TextWrapped("%s", how);
			ImGui::PopStyleColor();
		};
		if (import) {
			heading(std::string("Installing ") + import->game->title,
			        "Unpacking the game from its installer. This takes a few seconds.");
			progressBar(fonts.bold, import->fraction, w);
			{
				std::lock_guard<std::mutex> lock(import->mutex);
				if (!import->current.empty())
					ImGui::SetItemTooltip("%s\n%s", import->setup.filename().string().c_str(), import->current.c_str());
			}
			ImGui::Spacing();
			cancel = ImGui::Button("Cancel", ImVec2(110, 0));
			if (cancel)
				import->cancel = true;
		}
#ifdef __EMSCRIPTEN__
		else if (web::pickState() == web::PickState::Reading) {
			heading(picking == Picking::Installer ? "Reading the installer" : "Copying the game's files",
			        "Your browser is reading the file. Nothing leaves this computer.");
			progressBar(fonts.bold, web::pickProgress(), w);
			ImGui::SetItemTooltip("%s", web::pickCurrent().c_str());
		} else {
			heading("Choose the file", picking == Picking::Installer
			                               ? "Pick the game's GOG installer (setup_...exe) in your browser's window."
			                               : "Pick the game's folder in your browser's window.");
			ImGui::Spacing();
			if (ImGui::Button("Cancel", ImVec2(110, 0))) { // (browsers without the dialog's cancel event)
				std::string ignored;
				web::takePicked(ignored);
				picking = Picking::None;
			}
		}
#endif
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
}

// "More options", closed at first: folders of the player's own, an
// installer for any game, a path field, the recent folders, the details
// of the latest message.
void LibraryScreen::State::drawMore() {
	const ImGuiStyle &style = ImGui::GetStyle();
	const float headerH = ImGui::GetFrameHeight() + 8;
	// Pushed to the bottom of the window when there is room; the tiles of
	// the next frame are made as large as the window allows.
	// (Not while it is open: its contents scroll.)
	const float room = ImGui::GetContentRegionAvail().y - headerH - style.ItemSpacing.y;
	if (!moreOpen)
		coverFit = lastCoverH > 0 ? lastCoverH + room - 4 : -1;
	ImGui::Dummy(ImVec2(0, std::max(4.0f, moreOpen ? 4.0f : room)));

	ImGui::PushStyleColor(ImGuiCol_Header, vec(IM_COL32(0, 0, 0, 0)));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, vec(IM_COL32(255, 255, 255, 14)));
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, vec(IM_COL32(255, 255, 255, 22)));
	ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kMuted));
	ImGui::PushFont(nullptr, 15.5f);
	ImGui::SetNextItemWidth(width);
	const bool open = ImGui::CollapsingHeader("More options");
	ImGui::PopFont();
	ImGui::PopStyleColor(4);
	const bool opened = open && !moreOpen;
	moreOpen = open;
	if (!open)
		return;

#ifdef __EMSCRIPTEN__
	const bool busy = import != nullptr || picking != Picking::None;
#else
	const bool busy = import != nullptr;
#endif
	ImGui::Indent(12);
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 24);
	ImGui::BeginDisabled(busy);
	ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kMuted));
	ImGui::TextWrapped(kWeb ? "Already have a game's files (for example the folder GOG's own installer makes)? Add the "
	                          "folder: it is copied into this browser's storage."
	                        : "Already have a game on this computer (for example installed with GOG's own installer)? "
	                          "Add its folder.");
	ImGui::PopStyleColor();
#ifdef __EMSCRIPTEN__
	if (ImGui::Button("Add a game folder..."))
		pickFolder();
	ImGui::SameLine();
	if (ImGui::Button("Install from an installer..."))
		pickInstaller(nullptr);
	ImGui::SetItemTooltip("A GOG installer (setup_...exe) of any of the three games");
#else
	if (kHaveDialogs) {
		if (ImGui::Button("Add a game folder...")) {
			const std::string folder = browseFolder(window);
			if (!folder.empty()) {
				setPath(folder);
				addFolder(folder);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Install from an installer...")) {
			const std::string setup = browseInstaller(window, nullptr);
			if (!setup.empty()) {
				setPath(setup);
				startImport(setup, nullptr);
			}
		}
		ImGui::SetItemTooltip("A GOG installer (setup_...exe) of any of the three games");
	}
	ImGui::Spacing();
	ImGui::TextDisabled("Or type the path of a game folder or an installer:");
	ImGui::SetNextItemWidth(std::max(120.0f, width - 24 - ImGui::CalcTextSize("Open").x - style.FramePadding.x * 2 -
	                                             style.ItemSpacing.x));
	ImGui::PushID(pathId);
	bool go = ImGui::InputTextWithHint("##path", "/path/to/the/game or setup_...exe", path, sizeof(path),
	                                   ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::PopID();
	ImGui::SameLine();
	go |= ImGui::Button("Open");
	if (go)
		openPath(path);
	if (!recent.empty()) {
		ImGui::Spacing();
		ImGui::TextDisabled("Recent folders (double-click to play):");
		if (ImGui::BeginTable("recent", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp,
		                      ImVec2(width - 24, 0))) {
			ImGui::TableSetupColumn("game", ImGuiTableColumnFlags_WidthFixed, 150);
			ImGui::TableSetupColumn("folder", ImGuiTableColumnFlags_WidthStretch);
			for (size_t i = 0; i < recent.size(); i++) {
				const Recent &r = recent[i];
				const bool ok = r.info && allowed(r.info);
				ImGui::PushID(int(i));
				ImGui::TableNextColumn();
				if (!ok)
					ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kFaint));
				if (ImGui::Selectable(r.game.c_str(), false,
				                      ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick) &&
				    ok && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
					play(r.folder);
				if (ok)
					ImGui::SetItemTooltip("Double-click to play");
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(r.folder.c_str());
				if (!ok)
					ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}
#endif
	ImGui::EndDisabled();
	if (!detail.empty()) {
		ImGui::Spacing();
		ImGui::TextDisabled("Details of the last message:");
		ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kFaint));
		ImGui::TextWrapped("%s", detail.c_str());
		ImGui::PopStyleColor();
	}
#ifndef __EMSCRIPTEN__
	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, vec(colour::kFaint));
	ImGui::TextWrapped("You can also drop a game folder or an installer onto this window. Escape quits.");
	ImGui::PopStyleColor();
#endif
	ImGui::PopTextWrapPos();
	ImGui::Unindent(12);
	if (opened)
		ImGui::SetScrollHereY(1.0f); // what was opened is in view
}

void LibraryScreen::State::shutdown() {
	if (import) {
		import->cancel = true;
		import->join();
		if (import->running)
			import->installer.cancel();
		import.reset();
	}
	for (Tile &t : tiles)
		if (t.texture)
			gl::DeleteTextures(1, &t.texture);
	tiles.clear();
	if (imguiReady) {
		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		imguiReady = false;
	}
	if (context)
		SDL_GL_DeleteContext(context);
	if (window)
		SDL_DestroyWindow(window);
	context = nullptr;
	window = nullptr;
}

LibraryScreen::LibraryScreen(LibraryContext &context, const LibraryDebug &debug)
	: _s(std::make_unique<State>(context, debug)) {}

LibraryScreen::~LibraryScreen() {
	_s->shutdown();
}

bool LibraryScreen::open() {
	return _s->openWindow();
}

bool LibraryScreen::frame() {
	return _s->frame();
}

std::string LibraryScreen::close() {
	_s->shutdown();
	return _s->chosen;
}

std::string runLibrary(LibraryContext &ctx, const LibraryDebug &debug) {
	LibraryScreen library(ctx, debug);
	if (!library.open())
		return {};
	while (library.frame()) {
	}
	return library.close();
}

} // namespace gob
