// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Goblins 3's save files (SaveLoad_v3 in ScummVM; docs/levels-gob3.md 2):
//   cat.inf    500 bytes of global properties (kept in memory, as ScummVM
//              does: the game sets them up again at every start), the
//              30-slot index of 40-byte names at 500, then one variable
//              snapshot per slot from 1700
//   ima.inf    an 80-byte screenshot index (40 bytes the scripts keep, then
//              a flag per slot: a save exists), then one screenshot per
//              slot (a 120x160 sprite and the palette: 19968 bytes)
//   intro.$$$  a temporary copy of a sprite (as save.inf in Gobliins 2)
//   bloc.inf   the 2560-byte notes
//   prot, config: ignored
// Each slot is one file, gob3-slotNN.sav, holding the name, the variables
// and the screenshot (see SlotV3 below).

#include "engine/interpreter.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace gob {

namespace {

constexpr char kSlotMagicV3[4] = {'G', '3', 'N', 'S'};
constexpr uint32_t kSlotVersionV3 = 1;
constexpr int kSlotsV3 = 30;
constexpr int kPropsSizeV3 = 500;
constexpr int kIndexSizeV3 = kSlotsV3 * 40;
constexpr int kShotSizeV3 = 19968;  // 120 x 160 pixels and a palette, as the scripts count it
constexpr int kShotIndexSizeV3 = 80;
constexpr int kNotesSizeV3 = 2560;

uint32_t get32(const std::vector<uint8_t> &d, size_t o) {
	return o + 3 < d.size() ? uint32_t(d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (uint32_t(d[o + 3]) << 24)) : 0;
}

void put32(std::vector<uint8_t> &d, uint32_t v) {
	for (int i = 0; i < 4; i++)
		d.push_back(uint8_t(v >> (8 * i)));
}

std::optional<std::vector<uint8_t>> readFile(const std::string &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f)
		return std::nullopt;
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Slot file (little-endian): "G3NS", u32 version, u32 variable bytes (0:
// none yet), 40-byte name, the variables, u32 screenshot width and height
// (0: none), u8 palette flag, 768-byte palette, the screenshot's pixels.
struct SlotV3 {
	std::array<uint8_t, 40> name{};
	std::vector<uint8_t> vars;
	int shotW = 0, shotH = 0;
	bool shotPalette = false;
	VgaPalette palette{};
	std::vector<uint8_t> shot;
};

std::optional<SlotV3> readSlotV3(const std::string &path) {
	auto d = readFile(path);
	if (!d || d->size() < 52 || std::memcmp(d->data(), kSlotMagicV3, 4) != 0 || get32(*d, 4) != kSlotVersionV3)
		return std::nullopt;
	SlotV3 s;
	uint32_t varSize = get32(*d, 8);
	size_t p = 52;
	if (d->size() < p + varSize + 9 + 768)
		return std::nullopt;
	std::copy_n(d->begin() + 12, 40, s.name.begin());
	s.vars.assign(d->begin() + long(p), d->begin() + long(p + varSize));
	p += varSize;
	s.shotW = int(get32(*d, p));
	s.shotH = int(get32(*d, p + 4));
	s.shotPalette = (*d)[p + 8] != 0;
	std::copy_n(d->begin() + long(p + 9), 768, s.palette.begin());
	p += 9 + 768;
	size_t pixels = size_t(s.shotW) * size_t(s.shotH);
	if (s.shotW < 0 || s.shotH < 0 || pixels > 1000000 || d->size() < p + pixels)
		return std::nullopt;
	s.shot.assign(d->begin() + long(p), d->begin() + long(p + pixels));
	return s;
}

bool writeSlotV3(const std::string &path, const SlotV3 &s) {
	std::vector<uint8_t> d(kSlotMagicV3, kSlotMagicV3 + 4);
	put32(d, kSlotVersionV3);
	put32(d, uint32_t(s.vars.size()));
	d.insert(d.end(), s.name.begin(), s.name.end());
	d.insert(d.end(), s.vars.begin(), s.vars.end());
	put32(d, uint32_t(s.shotW));
	put32(d, uint32_t(s.shotH));
	d.push_back(s.shotPalette ? 1 : 0);
	d.insert(d.end(), s.palette.begin(), s.palette.end());
	d.insert(d.end(), s.shot.begin(), s.shot.end());
	std::error_code ec;
	fs::create_directories(fs::path(path).parent_path(), ec);
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char *>(d.data()), long(d.size()));
	return bool(f);
}

std::string lower(std::string s) {
	for (char &c : s)
		c = char(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// The sprite index a temp-sprite size names: -(index + 1), less 1000 (or
// 3000) when the palette goes with it.
int spriteIndex(int32_t size) {
	if (size < -3000)
		size += 3000;
	if (size < -1000)
		size += 1000;
	return -size - 1;
}

} // namespace

std::optional<std::array<uint8_t, 40>> Interpreter::slotNameV3(int slot) const {
	auto s = readSlotV3(slotPath(slot));
	if (!s || !_vars || s->vars.size() != _vars->size())
		return std::nullopt;
	return s->name;
}

bool Interpreter::slotHasFileV3(int slot) const {
	std::error_code ec;
	return !slotPath(slot).empty() && fs::exists(slotPath(slot), ec);
}

// A quick save: the variables under the name QUICKSAVE in the last slot,
// with a screenshot made as the menu's SAVE makes it (MENU 0x454F: every
// fourth column of the back surface's top 160 rows, then every fourth row
// moved up into the first 40).
bool Interpreter::writeQuickSaveV3() {
	SlotV3 s;
	std::memcpy(s.name.data(), kQuickSaveName, sizeof(kQuickSaveName) - 1);
	s.vars.assign(_vars->data(), _vars->data() + _vars->size());
	const Surface &back = _draw.back();
	s.shotW = 118;
	s.shotH = 160;
	s.shot.assign(size_t(s.shotW) * size_t(s.shotH), 0);
	for (int y = 0; y < s.shotH; y++)
		for (int x = 0; x < s.shotW; x++)
			s.shot[size_t(y * s.shotW + x)] = back.get(x * 4, y);
	for (int y = 1; y < 40; y++)
		for (int x = 0; x < s.shotW; x++)
			s.shot[size_t(y * s.shotW + x)] = s.shot[size_t(y * 4 * s.shotW + x)];
	s.shotPalette = true;
	s.palette = _draw.vgaPalette;
	bool ok = writeSlotV3(slotPath(kQuickSaveSlotV3), s);
	trace("saved slot " + std::to_string(kQuickSaveSlotV3) + " to " + slotPath(kQuickSaveSlotV3) + (ok ? "" : " (failed)"));
	return ok;
}

Interpreter::SaveKind Interpreter::saveKindV3(const std::string &file) const {
	std::string f = lower(file);
	if (f == "cat.inf")
		return SaveKind::Game;
	if (f == "ima.inf")
		return SaveKind::Screenshot;
	if (f == "intro.$$$")
		return SaveKind::TempSprite;
	if (f == "bloc.inf")
		return SaveKind::Notes;
	if (f == "prot" || f == "config")
		return SaveKind::Ignore;
	return SaveKind::None;
}

// The highest slot that has a file, plus one (0: none).
int Interpreter::slotCountV3() const {
	for (int i = kSlotsV3 - 1; i >= 0; i--) {
		std::error_code ec;
		if (!slotPath(i).empty() && fs::exists(slotPath(i), ec))
			return i + 1;
	}
	return 0;
}

int32_t Interpreter::saveFileSizeV3(SaveKind kind) {
	const uint32_t varSize = uint32_t(_vars->size());
	switch (kind) {
	case SaveKind::Game: {
		// As ScummVM: the first query sees no file, so the game sets up its
		// global properties afresh.
		if (!_catSizeAsked) {
			_catSizeAsked = true;
			return -1;
		}
		int n = slotCountV3();
		return n ? int32_t(uint32_t(n) * varSize + kPropsSizeV3 + kIndexSizeV3) : -1;
	}
	case SaveKind::Screenshot: {
		int n = slotCountV3();
		return n ? int32_t(n * kShotSizeV3 + kShotIndexSizeV3) : -1;
	}
	case SaveKind::Notes: {
		std::error_code ec;
		return !notesPath().empty() && fs::exists(notesPath(), ec) ? kNotesSizeV3 : -1;
	}
	case SaveKind::TempSprite:
		return _tempSprite ? int32_t(_tempSprite->pixels.size()) : -1;
	default:
		return -1;
	}
}

// Copies the temp sprite into sprite `index` (with its palette when the
// size asks for it), as TempSpriteHandler::load.
bool Interpreter::tempSpriteToSprite(int32_t size) {
	if (size == 0)
		return true;
	if (!_tempSprite || size >= 0)
		return false;
	int index = spriteIndex(size);
	auto &dst = _draw.sprite(index);
	if (index < 0 || index >= draw::kSpriteCount || !dst)
		return false;
	if (size_t(dst->width()) * size_t(dst->height()) != _tempSprite->pixels.size())
		return false;
	Surface tmp(dst->width(), dst->height());
	std::copy(_tempSprite->pixels.begin(), _tempSprite->pixels.end(), tmp.data());
	dst->blit(tmp);
	if (size < -1000 && _tempSprite->hasPalette) {
		_draw.vgaPalette = _tempSprite->palette;
		_draw.setPalette();
	}
	if (_draw.sprite(index) == _draw.sprite(draw::kBackSurface))
		_draw.forceBlit();
	return true;
}

// Copies sprite `index` (and the palette) into the temp sprite, as
// TempSpriteHandler::save.
bool Interpreter::spriteToTempSprite(int32_t size) {
	if (size == 0)
		return true;
	if (size >= 0)
		return false;
	int index = spriteIndex(size);
	auto &src = _draw.sprite(index);
	if (index < 0 || index >= draw::kSpriteCount || !src)
		return false;
	TempSprite t;
	t.width = src->width();
	t.height = src->height();
	t.pixels.assign(src->data(), src->data() + size_t(t.width) * size_t(t.height));
	t.hasPalette = size < -1000;
	if (t.hasPalette)
		t.palette = _draw.vgaPalette;
	_tempSprite = std::move(t);
	return true;
}

bool Interpreter::saveLoadV3(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset) {
	const uint32_t varSize = uint32_t(_vars->size());
	switch (kind) {
	case SaveKind::Game: {
		if (size == 0) {
			dataVar = 0;
			size = int32_t(varSize);
		}
		if (offset < kPropsSizeV3) { // global properties
			if (offset < 0 || size < 0 || offset + size > kPropsSizeV3)
				return false;
			for (int32_t i = 0; i < size; i++)
				_vars->write8(dataVar + uint32_t(i), _propsV3[size_t(offset + i)]);
			return true;
		}
		if (offset == kPropsSizeV3) { // the index: the names of the slots' saves
			if (size != kIndexSizeV3)
				return false;
			for (int i = 0; i < kSlotsV3; i++) {
				std::array<uint8_t, 40> name{};
				if (auto s = readSlotV3(slotPath(i)); s && !s->vars.empty())
					name = s->name;
				for (int b = 0; b < 40; b++)
					_vars->write8(dataVar + uint32_t(i * 40 + b), name[size_t(b)]);
			}
			return true;
		}
		int32_t rel = offset - kPropsSizeV3 - kIndexSizeV3;
		int slot = varSize ? int(rel / int32_t(varSize)) : -1;
		if (rel < 0 || slot >= kSlotsV3 || rel % int32_t(varSize) != 0 || dataVar != 0 || uint32_t(size) != varSize)
			return false;
		_hasSaveIndexV3 = false;
		auto s = readSlotV3(slotPath(slot));
		bool ok = s && s->vars.size() == varSize;
		if (ok) {
			std::copy(s->vars.begin(), s->vars.end(), _vars->data());
			trace("loaded save slot " + std::to_string(slot));
		}
		if (_loadArmed && slot == kQuickSaveSlotV3) {
			_loadArmed = false;
			_host.notify(ok ? "Loaded" : "Quick load failed");
		}
		return ok;
	}

	case SaveKind::Screenshot: {
		if (offset < kShotIndexSizeV3) {
			if (offset < 0 || size < 0 || offset + size > kShotIndexSizeV3)
				return false;
			for (int i = 0; i < kSlotsV3; i++) { // the slots that have a save
				std::error_code ec;
				_shotIndexV3[size_t(40 + i)] = !slotPath(i).empty() && fs::exists(slotPath(i), ec) ? 1 : 0;
			}
			std::fill(_shotIndexV3.begin() + 70, _shotIndexV3.end(), 0);
			for (int32_t i = 0; i < size; i++)
				_vars->write8(dataVar + uint32_t(i), _shotIndexV3[size_t(offset + i)]);
			return true;
		}
		int32_t rel = offset - kShotIndexSizeV3;
		int slot = rel / kShotSizeV3;
		if (slot >= kSlotsV3 || rel % kShotSizeV3 != 0)
			return false;
		auto s = readSlotV3(slotPath(slot));
		if (!s || s->shot.empty())
			return false;
		TempSprite t;
		t.width = s->shotW;
		t.height = s->shotH;
		t.pixels = s->shot;
		t.hasPalette = s->shotPalette;
		t.palette = s->palette;
		_tempSprite = std::move(t);
		return tempSpriteToSprite(size);
	}

	case SaveKind::TempSprite:
		return tempSpriteToSprite(size);

	case SaveKind::Notes: {
		if (int32_t(dataVar) < 0 || size < 0 || offset < 0)
			return false;
		auto d = readFile(notesPath());
		if (!d)
			return false;
		for (int32_t i = 0; i < size && size_t(offset + i) < d->size(); i++)
			_vars->write8(dataVar + uint32_t(i), (*d)[size_t(offset + i)]);
		return true;
	}

	default:
		return false;
	}
}

bool Interpreter::saveStoreV3(SaveKind kind, uint32_t dataVar, int32_t size, int32_t offset) {
	const uint32_t varSize = uint32_t(_vars->size());
	switch (kind) {
	case SaveKind::Game: {
		if (size == 0) {
			dataVar = 0;
			size = int32_t(varSize);
		}
		if (offset < kPropsSizeV3) {
			if (offset < 0 || size < 0 || offset + size > kPropsSizeV3)
				return false;
			for (int32_t i = 0; i < size; i++)
				_propsV3[size_t(offset + i)] = _vars->read8(dataVar + uint32_t(i));
			return true;
		}
		if (offset == kPropsSizeV3) {
			if (size != kIndexSizeV3)
				return false;
			for (int i = 0; i < kIndexSizeV3; i++)
				_saveIndexV3[size_t(i)] = _vars->read8(dataVar + uint32_t(i));
			_hasSaveIndexV3 = true;
			return true;
		}
		int32_t rel = offset - kPropsSizeV3 - kIndexSizeV3;
		int slot = varSize ? int(rel / int32_t(varSize)) : -1;
		if (rel < 0 || slot >= kSlotsV3 || rel % int32_t(varSize) != 0 || dataVar != 0 || uint32_t(size) != varSize ||
		    !_hasSaveIndexV3 || slotPath(slot).empty())
			return false;
		_hasSaveIndexV3 = false;
		SlotV3 s = readSlotV3(slotPath(slot)).value_or(SlotV3{}); // keeps the screenshot just saved
		std::copy_n(_saveIndexV3.begin() + slot * 40, 40, s.name.begin());
		s.vars.assign(_vars->data(), _vars->data() + varSize);
		bool ok = writeSlotV3(slotPath(slot), s);
		trace("saved slot " + std::to_string(slot) + " to " + slotPath(slot) + (ok ? "" : " (failed)"));
		return ok;
	}

	case SaveKind::Screenshot: {
		if (offset < kShotIndexSizeV3) {
			if (offset < 0 || size < 0 || offset + size > kShotIndexSizeV3)
				return false;
			for (int32_t i = 0; i < size; i++)
				_shotIndexV3[size_t(offset + i)] = _vars->read8(dataVar + uint32_t(i));
			return true;
		}
		if (!spriteToTempSprite(size))
			return false;
		int32_t rel = offset - kShotIndexSizeV3;
		int slot = rel / kShotSizeV3;
		if (rel < 0 || slot >= kSlotsV3 || rel % kShotSizeV3 != 0 || slotPath(slot).empty() || !_tempSprite)
			return false;
		SlotV3 s = readSlotV3(slotPath(slot)).value_or(SlotV3{});
		s.shotW = _tempSprite->width;
		s.shotH = _tempSprite->height;
		s.shot = _tempSprite->pixels;
		s.shotPalette = _tempSprite->hasPalette;
		s.palette = _tempSprite->palette;
		return writeSlotV3(slotPath(slot), s);
	}

	case SaveKind::TempSprite:
		return spriteToTempSprite(size);

	case SaveKind::Notes: {
		if (int32_t(dataVar) < 0 || size < 0 || offset < 0 || notesPath().empty())
			return false;
		std::vector<uint8_t> d = readFile(notesPath()).value_or(std::vector<uint8_t>());
		d.resize(std::max<size_t>(d.size(), kNotesSizeV3), 0);
		for (int32_t i = 0; i < size && size_t(offset + i) < d.size(); i++)
			d[size_t(offset + i)] = _vars->read8(dataVar + uint32_t(i));
		std::error_code ec;
		fs::create_directories(fs::path(notesPath()).parent_path(), ec);
		std::ofstream f(notesPath(), std::ios::binary | std::ios::trunc);
		f.write(reinterpret_cast<const char *>(d.data()), long(d.size()));
		return bool(f);
	}

	default:
		return false;
	}
}

} // namespace gob
