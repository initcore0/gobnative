// gob2native - native reimplementation of the Coktel Vision Gob engine
// Copyright (C) 2026  gob2native contributors
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. Format knowledge derived from ScummVM's gob engine (GPL).

#pragma once

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace gob {

// One entry in an STK archive's file table.
struct StkEntry {
	std::string name;     // upper-case 8.3 name, e.g. "GOB2.TOT"
	uint32_t size = 0;    // stored (possibly packed) size in bytes
	uint32_t offset = 0;  // absolute offset of the data in the archive
	uint8_t compression = 0; // 0 = stored, 1 = LZSS, 2 = chunked LZSS
};

// Read-only view of a Coktel STK archive.
//
// Layout: uint16 count, then `count` records of
//   char name[13]; uint32 size; uint32 offset; uint8 packed;
// all little-endian, followed by the file data.
class StkArchive {
public:
	bool open(const std::string &path);

	const std::string &path() const { return _path; }
	const std::vector<StkEntry> &entries() const { return _entries; }

	const StkEntry *find(const std::string &name) const;

	// Returns the unpacked contents of an entry.
	std::vector<uint8_t> read(const StkEntry &entry);
	std::optional<std::vector<uint8_t>> read(const std::string &name);

private:
	std::string _path;
	std::ifstream _file;
	std::vector<StkEntry> _entries;
};

// LZSS decompression used by STK archives and some standalone files.
// `src` points at the packed stream (after any size header).
std::vector<uint8_t> unpackLzss(const uint8_t *src, size_t srcSize, uint32_t outSize);

// Unpacks a compression-1 blob: uint32 unpacked size followed by LZSS data.
std::vector<uint8_t> unpack(const std::vector<uint8_t> &packed, uint8_t compression);

} // namespace gob
