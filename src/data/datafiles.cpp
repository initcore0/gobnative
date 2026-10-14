// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "data/datafiles.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

namespace gob {

namespace {

bool iequals(const std::string &a, const std::string &b) {
	return a.size() == b.size() &&
	       std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
		       return std::toupper(x) == std::toupper(y);
	       });
}

} // namespace

DataFiles::DataFiles(fs::path gameDir) : _gameDir(std::move(gameDir)) {}

std::optional<fs::path> DataFiles::findLoose(const std::string &name) const {
	// DOS names are case-insensitive; macOS volumes usually are too, but
	// don't rely on it.
	std::error_code ec;
	for (const auto &entry : fs::directory_iterator(_gameDir, ec))
		if (entry.is_regular_file() && iequals(entry.path().filename().string(), name))
			return entry.path();
	return std::nullopt;
}

bool DataFiles::openArchive(const std::string &name, bool base) {
	std::string fileName = name.find('.') == std::string::npos ? name + ".STK" : name;
	auto path = findLoose(fileName);
	if (!path)
		return false;

	auto archive = std::make_unique<StkArchive>();
	if (!archive->open(path->string()))
		return false;
	for (Slot &slot : _archives)
		if (!slot.archive) {
			slot = {std::move(archive), base};
			return true;
		}
	_archives.push_back({std::move(archive), base});
	return true;
}

bool DataFiles::closeArchive(bool base) {
	for (auto it = _archives.rbegin(); it != _archives.rend(); ++it)
		if (it->archive && it->base == base) {
			it->archive.reset();
			return true;
		}
	return false;
}

int DataFiles::openAllArchives() {
	std::vector<std::string> names;
	std::error_code ec;
	for (const auto &entry : fs::directory_iterator(_gameDir, ec)) {
		if (!entry.is_regular_file())
			continue;
		std::string name = entry.path().filename().string();
		std::string upper = name;
		for (char &c : upper)
			c = char(std::toupper(static_cast<unsigned char>(c)));
		if (upper.ends_with(".STK") || upper.ends_with(".ITK"))
			names.push_back(name);
	}
	std::sort(names.begin(), names.end(), [](const std::string &a, const std::string &b) {
		bool ia = iequals(a, "INTRO.STK"), ib = iequals(b, "INTRO.STK");
		return ia != ib ? ia : a < b;
	});
	int opened = 0;
	for (const std::string &name : names)
		opened += openArchive(name) ? 1 : 0;
	return opened;
}

bool DataFiles::exists(const std::string &name) const {
	for (auto it = _archives.rbegin(); it != _archives.rend(); ++it)
		if (it->archive && it->archive->find(name))
			return true;
	return findLoose(name).has_value();
}

std::string DataFiles::archiveOf(const std::string &name) const {
	for (auto it = _archives.rbegin(); it != _archives.rend(); ++it)
		if (it->archive && it->archive->find(name))
			return fs::path(it->archive->path()).filename().string();
	return {};
}

std::optional<std::vector<uint8_t>> DataFiles::read(const std::string &name) {
	for (auto it = _archives.rbegin(); it != _archives.rend(); ++it)
		if (const StkEntry *e = it->archive ? it->archive->find(name) : nullptr)
			return it->archive->read(*e);

	auto path = findLoose(name);
	if (!path)
		return std::nullopt;

	std::ifstream f(*path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

std::vector<std::string> DataFiles::list() const {
	std::set<std::string> names;
	for (const Slot &slot : _archives)
		if (slot.archive)
			for (const StkEntry &e : slot.archive->entries())
				names.insert(e.name);
	return {names.begin(), names.end()};
}

} // namespace gob
