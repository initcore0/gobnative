// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gob {

// The script variable space: a flat little-endian byte array. Scripts address
// it by byte offset with 8/16/32-bit accesses and as NUL-terminated strings.
class Variables {
public:
	explicit Variables(size_t size = 0) : _data(size, 0) {}

	void resize(size_t size) { _data.assign(size, 0); }
	size_t size() const { return _data.size(); }

	uint8_t read8(uint32_t off) const { return off < _data.size() ? _data[off] : 0; }
	uint16_t read16(uint32_t off) const { return uint16_t(read8(off) | (read8(off + 1) << 8)); }
	uint32_t read32(uint32_t off) const { return read16(off) | (uint32_t(read16(off + 2)) << 16); }

	void write8(uint32_t off, uint8_t v) { if (off < _data.size()) _data[off] = v; }
	void write16(uint32_t off, uint16_t v) { write8(off, uint8_t(v)); write8(off + 1, uint8_t(v >> 8)); }
	void write32(uint32_t off, uint32_t v) { write16(off, uint16_t(v)); write16(off + 2, uint16_t(v >> 16)); }

	std::string readString(uint32_t off) const {
		std::string s;
		while (off < _data.size() && _data[off])
			s.push_back(char(_data[off++]));
		return s;
	}

	void writeString(uint32_t off, const std::string &s) {
		for (size_t i = 0; i < s.size(); i++)
			write8(off + uint32_t(i), uint8_t(s[i]));
		write8(off + uint32_t(s.size()), 0);
	}

	uint8_t *data() { return _data.data(); }
	const uint8_t *data() const { return _data.data(); }

	// Convenience accessors by 32-bit slot index, as scripts usually use them.
	uint32_t slot(uint32_t index) const { return read32(index * 4); }
	void setSlot(uint32_t index, uint32_t v) { write32(index * 4, v); }

private:
	std::vector<uint8_t> _data;
};

} // namespace gob
