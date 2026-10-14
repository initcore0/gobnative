// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "data/datafiles.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gob {

// Numbering matches the original's language variable (VAR 16).
enum class Language { French, German, British, Spanish, Italian, American, Dutch, Korean, Hebrew };

// Lower-case English name ("french", "american", ...) and its inverse.
const char *languageName(Language lang);
std::optional<Language> parseLanguage(const std::string &name);

// Fixed 128-byte TOT header fields.
struct TotHeader {
	int versionMajor = 0;
	int versionMinor = 0;
	uint32_t variablesCount = 0;
	uint32_t textsOffset = 0;     // 0 = external .ANG/.USA/..., 0xFFFFFFFF = none
	uint32_t resourcesOffset = 0; // 0 or 0xFFFFFFFF = none
	uint16_t animDataSize = 0;
	uint8_t imFileNumber = 0;
	uint8_t exFileNumber = 0;
	uint8_t communHandling = 0;
	std::array<uint16_t, 14> functions{}; // script entry points
	uint32_t scriptEnd = 0;                // end of bytecode in the file
};

struct Resource {
	std::vector<uint8_t> data;
	int width = 0;
	int height = 0;
};

// A TOT script file together with its embedded resources, the matching
// .EXT resource file and its (possibly localized) text table.
class TotFile {
public:
	bool load(DataFiles &files, const std::string &baseName, Language lang = Language::American);

	const std::string &name() const { return _name; }
	const TotHeader &header() const { return _header; }

	// Raw file bytes; bytecode lives in [128, header().scriptEnd).
	const std::vector<uint8_t> &bytes() const { return _bytes; }

	// ids < 30000 index the TOT table, ids >= 30000 the EXT table.
	std::optional<Resource> resource(uint16_t id) const;
	int totResourceCount() const { return int(_totItems.size()); }
	int extResourceCount() const { return int(_extItems.size()); }

	// Raw text item bytes (encoded draw commands + strings), by index.
	std::optional<std::vector<uint8_t>> text(uint16_t id) const;
	// Where item `id` starts in the text table (glyph references in texts are
	// offsets into it), or 0.
	uint16_t textOffset(uint16_t id) const { return id < _textItems.size() ? _textItems[id].offset : 0; }
	int textCount() const { return int(_textItems.size()); }

private:
	struct TotItem { int32_t offset; uint16_t size; int16_t width, height; };
	struct ExtItem { uint32_t offset; uint16_t size; uint16_t width, height; bool packed; };
	struct TextItem { uint16_t offset, size; };

	bool parseHeader();
	void parseTotResources();
	void parseExtResources(const std::vector<uint8_t> &ext);
	void parseTexts(DataFiles &files, Language lang);

	std::string _name;
	TotHeader _header;
	std::vector<uint8_t> _bytes;
	std::vector<uint8_t> _ext;
	std::vector<uint8_t> _texts;

	uint32_t _totDataOffset = 0;
	std::vector<TotItem> _totItems;
	std::vector<ExtItem> _extItems;
	std::vector<TextItem> _textItems;
};

} // namespace gob
