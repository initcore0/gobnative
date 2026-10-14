// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File reads/writes from scripts (readData/writeData) and the save games
// they route to. Gobliins 2 addresses its save data through a few virtual
// file names:
//   cat.inf / cat.cat  slot index (15 x 40-byte names) at offset 0, then one
//                      full variable-space snapshot per slot
//   bloc.inf           the 600-byte notepad
//   save.inf           a temporary copy of a sprite (kept in memory)
// Saves use this project's own file format (see writeSlot below).

#include "engine/interpreter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace gob {

namespace {

constexpr char kSlotMagic[4] = {'G', '2', 'N', 'S'};
constexpr uint32_t kSlotVersion = 1;

std::string lower(std::string s) {
	for (char &c : s)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

void put32(std::ofstream &f, uint32_t v) {
	for (int i = 0; i < 4; i++)
		f.put(char((v >> (8 * i)) & 0xFF));
}

uint32_t get32(const std::vector<uint8_t> &d, size_t o) {
	return o + 3 < d.size() ? uint32_t(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (uint32_t(d[o + 3]) << 24)) : 0;
}

std::optional<std::vector<uint8_t>> readFile(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return std::nullopt;
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Slot file layout (little-endian):
//   "G2NS", u32 version, u32 variable bytes, 40-byte name, variable bytes.
struct SlotFile {
	std::array<uint8_t, 40> name{};
	std::vector<uint8_t> vars;
};

std::optional<SlotFile> readSlot(const std::string &path) {
	auto d = readFile(path);
	if (!d || d->size() < 52 || std::memcmp(d->data(), kSlotMagic, 4) != 0 || get32(*d, 4) != kSlotVersion)
		return std::nullopt;
	uint32_t varSize = get32(*d, 8);
	if (d->size() < 52 + size_t(varSize))
		return std::nullopt;
	SlotFile s;
	std::copy_n(d->begin() + 12, 40, s.name.begin());
	s.vars.assign(d->begin() + 52, d->begin() + 52 + long(varSize));
	return s;
}

} // namespace

std::string Interpreter::slotPath(int slot) const {
	std::string dir = _host.saveDirectory();
	if (dir.empty())
		return {};
	char name[32];
	std::snprintf(name, sizeof(name), "%s-slot%02d.sav", _game.key, slot); // gob2-slot00.sav
	return (fs::path(dir) / name).string();
}

std::string Interpreter::notesPath() const {
	std::string dir = _host.saveDirectory();
	return dir.empty() ? std::string() : (fs::path(dir) / (std::string(_game.key) + "-notes.sav")).string();
}

Interpreter::SaveKind Interpreter::saveKind(const std::string &file) const {
	if (_rules.saves == SaveFormat::Gob3)
		return saveKindV3(file);
	if (_rules.saves != SaveFormat::Gob2)
		return SaveKind::None;
	std::string f = lower(file);
	if (f == "cat.inf" || f == "cat.cat")
		return SaveKind::Game;
	if (f == "save.inf")
		return SaveKind::TempSprite;
	if (f == "bloc.inf")
		return SaveKind::Notes;
	return SaveKind::None;
}

int32_t Interpreter::saveFileSize(SaveKind kind) {
	if (_rules.saves == SaveFormat::Gob3)
		return saveFileSizeV3(kind);
	uint32_t varSize = uint32_t(_vars->size());
	switch (kind) {
	case SaveKind::Game: {
		int maxSlot = 0;
		for (int i = kSaveSlots - 1; i >= 0; i--)
			if (!slotPath(i).empty() && fs::exists(slotPath(i))) {
				maxSlot = i + 1;
				break;
			}
		return maxSlot ? int32_t(uint32_t(maxSlot) * varSize + kSaveIndexSize) : -1;
	}
	case SaveKind::Notes:
		return !notesPath().empty() && fs::exists(notesPath()) ? kNotesSize : -1;
	case SaveKind::TempSprite:
		return _tempSprite ? int32_t(_tempSprite->pixels.size()) : -1;
	default:
		return -1;
	}
}

bool Interpreter::saveLoad(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset) {
	if (_rules.saves == SaveFormat::Gob3)
		return saveLoadV3(kind, dataVar, size, offset);
	uint32_t varSize = uint32_t(_vars->size());

	if (kind == SaveKind::Game) {
		if (size == 0) {
			dataVar = 0;
			size = int32_t(varSize);
		}
		if (offset == 0) {
			// The slot index: names of existing saves, zeros for empty slots.
			if (size != kSaveIndexSize)
				return false;
			for (int i = 0; i < kSaveSlots; i++) {
				std::array<uint8_t, 40> name{};
				if (auto slot = readSlot(slotPath(i)))
					name = slot->name;
				for (int b = 0; b < kSlotNameLength; b++)
					_vars->write8(dataVar + uint32_t(i * kSlotNameLength + b), name[size_t(b)]);
			}
			return true;
		}
		int32_t rel = offset - kSaveIndexSize;
		int slot = varSize ? int(rel / int32_t(varSize)) : -1;
		if (slot < 0 || slot >= kSaveSlots || rel % int32_t(varSize) != 0 || dataVar != 0 || uint32_t(size) != varSize)
			return false;
		auto s = readSlot(slotPath(slot));
		bool ok = s && s->vars.size() == varSize;
		if (ok) {
			std::copy(s->vars.begin(), s->vars.end(), _vars->data());
			trace("loaded save slot " + std::to_string(slot));
		}
		if (_loadArmed && slot == kQuickSaveSlot) {
			_loadArmed = false;
			_host.notify(ok ? "Loaded" : "Quick load failed");
		}
		return ok;
	}

	if (kind == SaveKind::Notes) {
		if (int32_t(dataVar) < 0 || size < 0 || offset < 0)
			return false;
		auto d = readFile(notesPath());
		if (!d)
			return false;
		for (int32_t i = 0; i < size && size_t(offset + i) < d->size(); i++)
			_vars->write8(dataVar + uint32_t(i), (*d)[size_t(offset + i)]);
		return true;
	}

	if (kind == SaveKind::TempSprite) {
		// The size argument encodes a sprite index: -(index + 1), minus 1000
		// (or 3000) when the palette travels with it.
		if (size == 0)
			return true;
		if (!_tempSprite || size >= 0)
			return false;
		bool withPal = size < -1000;
		int32_t v = size;
		if (v < -3000) v += 3000;
		if (v < -1000) v += 1000;
		int index = -v - 1;
		auto &dst = _draw.sprite(index);
		if (index < 0 || index >= draw::kSpriteCount || !dst)
			return false;
		Surface tmp(_tempSprite->width, _tempSprite->height);
		std::copy(_tempSprite->pixels.begin(), _tempSprite->pixels.end(), tmp.data());
		dst->blit(tmp);
		if (withPal && _tempSprite->hasPalette) {
			_draw.vgaPalette = _tempSprite->palette;
			_draw.setPalette();
		}
		if (index == draw::kBackSurface)
			_draw.forceBlit();
		return true;
	}
	return false;
}

// Writes the whole variable space as save slot `slot` named `name`
// (kSlotNameLength bytes).
bool Interpreter::writeSlotFile(int slot, const uint8_t *name) {
	std::string path = slotPath(slot);
	if (path.empty())
		return false;
	std::error_code ec;
	fs::create_directories(fs::path(path).parent_path(), ec);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f)
		return false;
	uint32_t varSize = uint32_t(_vars->size());
	f.write(kSlotMagic, 4);
	put32(f, kSlotVersion);
	put32(f, varSize);
	f.write(reinterpret_cast<const char *>(name), kSlotNameLength);
	f.write(reinterpret_cast<const char *>(_vars->data()), long(varSize));
	trace("saved slot " + std::to_string(slot) + " to " + path);
	return bool(f);
}

// Slot kQuickSaveSlot holds a readable save named QUICKSAVE (a save made in
// the game's menu under another name is never quick loaded).
bool Interpreter::hasQuickSave() const {
	if (_rules.levels == LevelSystem::Gob1)
		return hasQuickSaveV1();
	if (_rules.levels == LevelSystem::Gob3)
		return hasQuickSaveV3();
	if (!_rules.hasQuickSave())
		return false;
	auto s = readSlot(slotPath(kQuickSaveSlot));
	return s && _vars && s->vars.size() == _vars->size() && !quickSaveSlotTaken();
}

bool Interpreter::quickSaveSlotTaken() const {
	if (_rules.levels == LevelSystem::Gob3)
		return quickSaveSlotTakenV3();
	if (!_rules.hasQuickSave() || _rules.levels == LevelSystem::Gob1) // Gobliiins' quick save has its own file
		return false;
	const std::string path = slotPath(kQuickSaveSlot);
	std::error_code ec;
	if (path.empty() || !fs::exists(path, ec))
		return false;
	auto s = readSlot(path);
	if (!s)
		return true;
	// The name as the menu stores it: text, then zero (or space) padding.
	size_t len = sizeof(kQuickSaveName) - 1;
	if (std::memcmp(s->name.data(), kQuickSaveName, len) != 0)
		return true;
	for (size_t i = len; i < s->name.size(); i++)
		if (s->name[i] != 0 && s->name[i] != ' ')
			return true;
	return false;
}

bool Interpreter::saveStore(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset) {
	if (_rules.saves == SaveFormat::Gob3)
		return saveStoreV3(kind, dataVar, size, offset);
	uint32_t varSize = uint32_t(_vars->size());

	if (kind == SaveKind::Game) {
		if (size == 0) {
			dataVar = 0;
			size = int32_t(varSize);
		}
		if (offset == 0) {
			if (size != kSaveIndexSize)
				return false;
			for (int i = 0; i < kSaveIndexSize; i++)
				_saveIndex[size_t(i)] = _vars->read8(dataVar + uint32_t(i));
			_hasSaveIndex = true;
			return true;
		}
		int32_t rel = offset - kSaveIndexSize;
		int slot = varSize ? int(rel / int32_t(varSize)) : -1;
		if (slot < 0 || slot >= kSaveSlots || rel % int32_t(varSize) != 0 || dataVar != 0 || uint32_t(size) != varSize ||
		    !_hasSaveIndex)
			return false;
		_hasSaveIndex = false;
		return writeSlotFile(slot, &_saveIndex[size_t(slot * kSlotNameLength)]);
	}

	if (kind == SaveKind::Notes) {
		if (int32_t(dataVar) < 0 || size < 0 || offset < 0 || notesPath().empty())
			return false;
		std::vector<uint8_t> d = readFile(notesPath()).value_or(std::vector<uint8_t>());
		d.resize(std::max<size_t>(d.size(), kNotesSize), 0);
		for (int32_t i = 0; i < size && size_t(offset + i) < d.size(); i++)
			d[size_t(offset + i)] = _vars->read8(dataVar + uint32_t(i));
		std::error_code ec;
		fs::create_directories(fs::path(notesPath()).parent_path(), ec);
		std::ofstream f(notesPath(), std::ios::binary | std::ios::trunc);
		f.write(reinterpret_cast<const char *>(d.data()), long(d.size()));
		return bool(f);
	}

	if (kind == SaveKind::TempSprite) {
		if (size == 0)
			return true;
		if (size >= 0)
			return false;
		bool withPal = size < -1000;
		int32_t v = size;
		if (v < -3000) v += 3000;
		if (v < -1000) v += 1000;
		int index = -v - 1;
		auto &src = _draw.sprite(index);
		if (index < 0 || index >= draw::kSpriteCount || !src)
			return false;
		TempSprite t;
		t.width = src->width();
		t.height = src->height();
		t.pixels.assign(src->data(), src->data() + size_t(t.width) * size_t(t.height));
		t.hasPalette = withPal;
		if (withPal)
			t.palette = _draw.vgaPalette;
		_tempSprite = std::move(t);
		return true;
	}
	return false;
}

// func 0x4D: file name, destination variable, size, offset.
void Interpreter::readData() {
	std::string file = evalString();
	VarRef dest = readVar();
	int32_t size = evalVal();
	int32_t offset = evalInt();

	SaveKind kind = saveKind(file);
	if (kind == SaveKind::Ignore)
		return;
	if (kind != SaveKind::None) {
		writeVar(1, 1);
		if (saveLoad(kind, dest.offset, size, offset))
			writeVar(1, 0);
		return;
	}

	if (size < 0)
		return; // raw sprite reads: unused by Gobliins 2
	uint32_t dataVar = dest.offset;
	if (size == 0) {
		dataVar = 0;
		size = int32_t(_vars->size());
	}
	if (file.empty()) {
		writeVar(1, uint32_t(size));
		return;
	}

	writeVar(1, 1);
	auto data = _files.read(file);
	if (!data) {
		trace("readData: \"" + file + "\" not found");
		return;
	}
	animateCursor(4);

	size_t pos = offset < 0 ? size_t(std::max<int64_t>(0, int64_t(data->size()) + offset + 1)) : size_t(offset);
	if (dataVar / 4 == 59 && size == 4) {
		uint32_t v = 0;
		for (int i = 0; i < 4 && pos + size_t(i) < data->size(); i++)
			v |= uint32_t((*data)[pos + size_t(i)]) << (8 * i);
		writeVar(59, v);
		writeVar(1, 0);
		return;
	}
	int32_t got = 0;
	for (; got < size && pos + size_t(got) < data->size(); got++)
		_vars->write8(dataVar + uint32_t(got), (*data)[pos + size_t(got)]);
	if (got == size)
		writeVar(1, 0);
}

// func 0x4E: only the save-game files are writable.
void Interpreter::writeData() {
	std::string file = evalString();
	VarRef src = readVar();
	int32_t size = evalVal();
	int32_t offset = evalInt();

	writeVar(1, 1);
	SaveKind kind = saveKind(file);
	if (kind == SaveKind::Ignore)
		return;
	if (kind == SaveKind::None) {
		trace("writeData: ignoring write to \"" + file + "\"");
		return;
	}
	if (saveStore(kind, src.offset, size, offset))
		writeVar(1, 0);
	else
		trace("writeData: \"" + file + "\" failed (size " + std::to_string(size) + ", offset " + std::to_string(offset) + ")");
}

} // namespace gob
