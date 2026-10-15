// gob2native - web build
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/web.h"

#ifdef __EMSCRIPTEN__

#include <SDL.h>
#include <emscripten.h>

#include <cstdlib>
#include <sstream>

extern "C" {
int gob_web_is_localhost();
void gob_web_sync();
int gob_web_sync_busy();
void gob_web_autosync(int on);
void gob_web_load(const char *folders);
int gob_web_loading();
void gob_web_free_as_read(const char *path);
void gob_web_pick(const char *accept, int multiple, int folder, int topOnly, const char *destDir);
int gob_web_pick_state();
float gob_web_pick_progress();
char *gob_web_pick_current();
char *gob_web_pick_result();
void gob_web_download(const char *path, const char *name, const char *mime);
void gob_web_open_file(const char *path, const char *mime);
void gob_web_reload(const char *query);
void gob_web_status(const char *text);
char *gob_web_take_dropped();
void gob_web_open_url(const char *url);
void gob_web_keyboard_button(float x, float y, float w, float h);
int gob_web_is_touch();
void gob_web_playing(int playing);
void gob_web_fullscreen();
void gob_web_persist();
char *gob_web_storage_info();
int gob_web_can_pick_folders();

// A key typed in the page's hidden text field (the touch bar's Keys): SDL
// key code `sym`, shifted, and the character it types (0 for none).
EMSCRIPTEN_KEEPALIVE void gob_web_key(int sym, int shift, int ch) {
	SDL_Event ev{};
	ev.type = SDL_KEYDOWN;
	ev.key.state = SDL_PRESSED;
	ev.key.keysym.sym = sym;
	ev.key.keysym.scancode = SDL_GetScancodeFromKey(sym);
	ev.key.keysym.mod = shift ? KMOD_LSHIFT : KMOD_NONE;
	SDL_PushEvent(&ev);
	if (ch > 0) { // the text, as UTF-8
		SDL_Event text{};
		text.type = SDL_TEXTINPUT;
		char *t = text.text.text;
		if (ch < 0x80)
			t[0] = char(ch);
		else if (ch < 0x800) {
			t[0] = char(0xC0 | ch >> 6);
			t[1] = char(0x80 | (ch & 0x3F));
		} else if (ch < 0x10000) {
			t[0] = char(0xE0 | ch >> 12);
			t[1] = char(0x80 | (ch >> 6 & 0x3F));
			t[2] = char(0x80 | (ch & 0x3F));
		} else {
			t[0] = char(0xF0 | ch >> 18);
			t[1] = char(0x80 | (ch >> 12 & 0x3F));
			t[2] = char(0x80 | (ch >> 6 & 0x3F));
			t[3] = char(0x80 | (ch & 0x3F));
		}
		SDL_PushEvent(&text);
	}
	ev.type = SDL_KEYUP;
	ev.key.state = SDL_RELEASED;
	SDL_PushEvent(&ev);
}
}

namespace gob::web {

namespace {

std::string take(char *s) {
	std::string r = s ? s : "";
	std::free(s);
	return r;
}

} // namespace

bool isLocalhost() {
	return gob_web_is_localhost() != 0;
}

void sync() {
	gob_web_sync();
}

bool syncBusy() {
	return gob_web_sync_busy() != 0;
}

void setAutoSync(bool on) {
	gob_web_autosync(on ? 1 : 0);
}

void load(const std::vector<std::string> &folders) {
	std::string list;
	for (const std::string &f : folders)
		list += f + "\n";
	gob_web_load(list.c_str());
}

bool loading() {
	return gob_web_loading() != 0;
}

void freeAsRead(const std::string &path) {
	gob_web_free_as_read(path.c_str());
}

void pick(const PickOptions &o) {
	gob_web_pick(o.accept.c_str(), o.multiple, o.folder, o.topLevelOnly, o.destDir.c_str());
}

PickState pickState() {
	return PickState(gob_web_pick_state());
}

float pickProgress() {
	return gob_web_pick_progress();
}

std::string pickCurrent() {
	return take(gob_web_pick_current());
}

std::vector<std::string> takePicked(std::string &error) {
	const PickState state = pickState();
	const std::string result = take(gob_web_pick_result()); // resets the picker
	std::vector<std::string> files;
	error.clear();
	if (state == PickState::Failed) {
		error = result;
		return files;
	}
	std::istringstream in(result);
	for (std::string line; std::getline(in, line);)
		if (!line.empty())
			files.push_back(line);
	return files;
}

void download(const std::string &path, const std::string &name, const std::string &mime) {
	gob_web_download(path.c_str(), name.c_str(), mime.c_str());
}

void openFile(const std::string &path, const std::string &mime) {
	gob_web_open_file(path.c_str(), mime.c_str());
}

void reload(const std::string &query) {
	gob_web_reload(query.c_str());
}

void status(const std::string &text) {
	gob_web_status(text.c_str());
}

std::vector<std::string> takeDropped() {
	std::vector<std::string> out;
	std::istringstream in(take(gob_web_take_dropped()));
	for (std::string line; std::getline(in, line);)
		if (!line.empty())
			out.push_back(line);
	return out;
}

void openUrl(const std::string &url) {
	gob_web_open_url(url.c_str());
}

bool isTouch() {
	return gob_web_is_touch() != 0;
}

void enterFullscreen() {
	gob_web_fullscreen();
}

void requestPersistentStorage() {
	gob_web_persist();
}

std::string storageInfo() {
	return take(gob_web_storage_info());
}

bool canPickFolders() {
	return gob_web_can_pick_folders() != 0;
}

void setPlaying(bool playing) {
	gob_web_playing(playing ? 1 : 0);
}

void setKeyboardButton(float x, float y, float w, float h) {
	gob_web_keyboard_button(x, y, w, h);
}

} // namespace gob::web

#endif
