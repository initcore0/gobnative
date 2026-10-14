// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formats/tot.h"

#include <algorithm>

namespace gob {

namespace {

uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t *p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

constexpr uint32_t kNone = 0xFFFFFFFF;
constexpr int kTableHeader = 3; // int16 count + uint8 unknown
constexpr int kItemSize = 10;   // int32 offset + uint16 size + int16 w + int16 h

const char *languageSuffix(Language lang) {
	switch (lang) {
	case Language::French:   return "DAT";
	case Language::German:   return "ALL";
	case Language::Spanish:  return "ESP";
	case Language::Italian:  return "ITA";
	case Language::American: return "USA";
	case Language::Dutch:    return "NDL";
	case Language::Korean:   return "KOR";
	case Language::Hebrew:   return "ISR";
	default:                 return "ANG";
	}
}

} // namespace

const char *languageName(Language lang) {
	static const char *const kNames[] = {"french", "german", "british", "spanish", "italian",
	                                     "american", "dutch", "korean", "hebrew"};
	return kNames[int(lang)];
}

std::optional<Language> parseLanguage(const std::string &name) {
	for (int i = 0; i <= int(Language::Hebrew); i++)
		if (name == languageName(Language(i)))
			return Language(i);
	return std::nullopt;
}

bool TotFile::load(DataFiles &files, const std::string &baseName, Language lang) {
	_name = baseName;
	auto bytes = files.read(baseName + ".TOT");
	if (!bytes)
		return false;
	_bytes = std::move(*bytes);

	if (!parseHeader())
		return false;

	parseTotResources();
	if (auto ext = files.read(baseName + ".EXT")) {
		_ext = std::move(*ext);
		parseExtResources(_ext);
	}
	parseTexts(files, lang);
	return true;
}

bool TotFile::parseHeader() {
	if (_bytes.size() < 128 || _bytes[40] != '.')
		return false;

	const uint8_t *h = _bytes.data();
	_header.versionMajor = h[39] - '0';
	_header.versionMinor = h[41] - '0';
	_header.variablesCount = le32(h + 44);
	_header.textsOffset = le32(h + 48);
	_header.resourcesOffset = le32(h + 52);
	_header.animDataSize = le16(h + 56);
	_header.imFileNumber = h[59];
	_header.exFileNumber = h[60];
	_header.communHandling = h[61];
	for (int i = 0; i < 14; i++)
		_header.functions[i] = le16(h + 100 + i * 2);

	uint32_t end = uint32_t(_bytes.size());
	for (uint32_t off : {_header.textsOffset, _header.resourcesOffset})
		if (off != kNone && off != 0)
			end = std::min(end, off);
	_header.scriptEnd = end;
	return true;
}

void TotFile::parseTotResources() {
	uint32_t off = _header.resourcesOffset;
	if (off == kNone || off == 0 || off + kTableHeader > _bytes.size())
		return;

	int16_t count = int16_t(le16(&_bytes[off]));
	uint32_t tableEnd = off + kTableHeader + uint32_t(count) * kItemSize;
	if (count <= 0 || tableEnd > _bytes.size())
		return;

	_totDataOffset = tableEnd;
	for (int i = 0; i < count; i++) {
		const uint8_t *p = &_bytes[off + kTableHeader + i * kItemSize];
		_totItems.push_back({int32_t(le32(p)), le16(p + 4), int16_t(le16(p + 6)), int16_t(le16(p + 8))});
	}
}

void TotFile::parseExtResources(const std::vector<uint8_t> &ext) {
	if (ext.size() < kTableHeader)
		return;

	int16_t count = int16_t(le16(ext.data()));
	uint32_t dataStart = kTableHeader + uint32_t(std::max<int16_t>(count, 0)) * kItemSize;
	if (count <= 0 || dataStart > ext.size())
		return;

	for (int i = 0; i < count; i++) {
		const uint8_t *p = &ext[kTableHeader + i * kItemSize];
		ExtItem item;
		item.offset = le32(p) + dataStart;
		item.size = le16(p + 4);
		item.width = le16(p + 6);
		item.height = le16(p + 8);
		item.packed = (item.width & 0x8000) != 0;
		item.width &= 0x7FFF;
		_extItems.push_back(item);
	}
}

void TotFile::parseTexts(DataFiles &files, Language lang) {
	if (_header.textsOffset == kNone)
		return;

	if (_header.textsOffset == 0) {
		// Localized texts live in a sidecar file; fall back through the
		// languages the original tries.
		std::vector<Language> order = {lang, Language::American, Language::British, Language::French};
		for (Language l : order) {
			if (auto t = files.read(_name + "." + languageSuffix(l))) {
				_texts = std::move(*t);
				break;
			}
		}
	} else {
		uint32_t start = _header.textsOffset;
		uint32_t end = uint32_t(_bytes.size());
		if (_header.resourcesOffset != kNone && _header.resourcesOffset > start)
			end = _header.resourcesOffset;
		if (start < end)
			_texts.assign(_bytes.begin() + start, _bytes.begin() + end);
	}

	if (_texts.size() < 2)
		return;
	int count = le16(_texts.data()) & 0x3FFF;
	for (int i = 0; i < count && 2 + i * 4 + 4 <= int(_texts.size()); i++) {
		const uint8_t *p = &_texts[2 + i * 4];
		_textItems.push_back({le16(p), le16(p + 2)});
	}
}

std::optional<Resource> TotFile::resource(uint16_t id) const {
	if (id >= 30000) {
		uint16_t idx = id - 30000;
		if (idx >= _extItems.size())
			return std::nullopt;
		const ExtItem &item = _extItems[idx];

		// Large resources encode extra size bits in the width field.
		uint32_t size = item.size;
		if (item.width & 0x4000) size += 1 << 16;
		if (item.width & 0x2000) size += 2 << 16;
		if (item.width & 0x1000) size += 4 << 16;
		if (item.height == 0)    size += uint32_t(item.width) << 16;

		if (item.offset >= _ext.size())
			return std::nullopt;
		size = std::min<uint32_t>(size, uint32_t(_ext.size()) - item.offset);

		Resource r;
		r.data.assign(_ext.begin() + item.offset, _ext.begin() + item.offset + size);
		if (item.packed)
			r.data = unpack(r.data, 1);
		r.width = item.width & 0xFFF;
		r.height = item.height;
		return r;
	}

	if (id >= _totItems.size())
		return std::nullopt;
	const TotItem &item = _totItems[id];
	if (item.offset < 0 || item.size == 0)
		return std::nullopt; // IM-file resources: not used by Gobliins 2

	uint32_t off = _totDataOffset + uint32_t(item.offset);
	if (off + item.size > _bytes.size())
		return std::nullopt;

	Resource r;
	r.data.assign(_bytes.begin() + off, _bytes.begin() + off + item.size);
	r.width = item.width;
	r.height = item.height;
	return r;
}

std::optional<std::vector<uint8_t>> TotFile::text(uint16_t id) const {
	if (id >= _textItems.size())
		return std::nullopt;
	const TextItem &item = _textItems[id];
	if (item.offset == 0xFFFF || item.size == 0 || item.offset + item.size > _texts.size())
		return std::nullopt;
	return std::vector<uint8_t>(_texts.begin() + item.offset, _texts.begin() + item.offset + item.size);
}

} // namespace gob
