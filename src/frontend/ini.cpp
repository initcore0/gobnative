// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/ini.h"

#include <cstdlib>
#include <vector>
#include <fstream>

namespace gob {

namespace {

std::string trim(const std::string &s) {
	size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos)
		return {};
	size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

} // namespace

bool Ini::load(const std::string &path) {
	std::ifstream in(path);
	if (!in)
		return false;
	_sections.clear();
	std::string line, current;
	while (std::getline(in, line)) {
		line = trim(line);
		if (line.empty() || line[0] == ';' || line[0] == '#')
			continue;
		if (line.front() == '[' && line.back() == ']') {
			current = trim(line.substr(1, line.size() - 2));
			section(current);
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		set(current, trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
	}
	return true;
}

bool Ini::save(const std::string &path) const {
	std::ofstream out(path, std::ios::trunc);
	if (!out)
		return false;
	bool first = true;
	for (const Section &s : _sections) {
		if (s.entries.empty())
			continue;
		if (!s.name.empty())
			out << (first ? "" : "\n") << '[' << s.name << "]\n";
		for (const auto &[k, v] : s.entries)
			out << k << '=' << v << '\n';
		first = false;
	}
	return bool(out);
}

Ini::Section &Ini::section(const std::string &name) {
	for (Section &s : _sections)
		if (s.name == name)
			return s;
	// Keys before any [section] header go first.
	if (name.empty())
		return *_sections.insert(_sections.begin(), Section{name, {}});
	return _sections.emplace_back(Section{name, {}});
}

const std::string *Ini::find(const std::string &section, const std::string &key) const {
	for (const Section &s : _sections)
		if (s.name == section)
			for (const auto &[k, v] : s.entries)
				if (k == key)
					return &v;
	return nullptr;
}

std::string Ini::get(const std::string &section, const std::string &key, const std::string &def) const {
	const std::string *v = find(section, key);
	return v ? *v : def;
}

int Ini::getInt(const std::string &section, const std::string &key, int def) const {
	const std::string *v = find(section, key);
	if (!v || v->empty())
		return def;
	char *end = nullptr;
	long n = std::strtol(v->c_str(), &end, 10);
	return *end ? def : int(n);
}

bool Ini::getBool(const std::string &section, const std::string &key, bool def) const {
	const std::string *v = find(section, key);
	if (!v)
		return def;
	if (*v == "1" || *v == "true" || *v == "yes" || *v == "on")
		return true;
	if (*v == "0" || *v == "false" || *v == "no" || *v == "off")
		return false;
	return def;
}

void Ini::set(const std::string &section, const std::string &key, const std::string &value) {
	Section &s = this->section(section);
	for (auto &[k, v] : s.entries)
		if (k == key) {
			v = value;
			return;
		}
	s.entries.emplace_back(key, value);
}

void Ini::remove(const std::string &section, const std::string &key) {
	for (Section &s : _sections)
		if (s.name == section)
			std::erase_if(s.entries, [&](const auto &e) { return e.first == key; });
}

void Ini::setInt(const std::string &section, const std::string &key, int value) {
	set(section, key, std::to_string(value));
}

void Ini::setBool(const std::string &section, const std::string &key, bool value) {
	set(section, key, value ? "1" : "0");
}

} // namespace gob
