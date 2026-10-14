// gob2native - web build
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/web.h"

#ifdef __EMSCRIPTEN__

#include <cstdlib>
#include <sstream>

extern "C" {
int gob_web_is_localhost();
void gob_web_sync();
int gob_web_sync_busy();
void gob_web_autosync(int on);
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

} // namespace gob::web

#endif
