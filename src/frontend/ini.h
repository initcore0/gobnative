// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal INI file: [section] headers and key=value lines. Keys keep their
// file order and unknown keys survive a load/save round trip; comments
// (lines starting with ; or #) are dropped.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace gob {

class Ini {
public:
	bool load(const std::string &path);
	bool save(const std::string &path) const;

	// Value of section.key, or `def` when absent.
	std::string get(const std::string &section, const std::string &key, const std::string &def = {}) const;
	int getInt(const std::string &section, const std::string &key, int def) const;
	bool getBool(const std::string &section, const std::string &key, bool def) const;

	void set(const std::string &section, const std::string &key, const std::string &value);
	void setInt(const std::string &section, const std::string &key, int value);
	void setBool(const std::string &section, const std::string &key, bool value);
	void remove(const std::string &section, const std::string &key);

private:
	struct Section {
		std::string name;
		std::vector<std::pair<std::string, std::string>> entries;
	};
	std::vector<Section> _sections;

	Section &section(const std::string &name);
	const std::string *find(const std::string &section, const std::string &key) const;
};

} // namespace gob
