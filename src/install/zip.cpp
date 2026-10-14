// gob2native - zip archives
// SPDX-License-Identifier: GPL-3.0-or-later

#include "install/zip.h"

#include "gfx/png.h"

#include <algorithm>

namespace gob {

namespace fs = std::filesystem;

namespace {

uint16_t le16(const uint8_t *p) {
	return uint16_t(p[0] | (p[1] << 8));
}

uint32_t le32(const uint8_t *p) {
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool readAt(std::ifstream &in, uint64_t pos, uint8_t *dst, size_t size) {
	in.clear();
	in.seekg(std::streamoff(pos));
	in.read(reinterpret_cast<char *>(dst), std::streamsize(size));
	return size_t(in.gcount()) == size;
}

// A safe relative path: no absolute paths, drive letters or "..".
bool safeName(const std::string &name) {
	if (name.empty() || name[0] == '/' || name.find(':') != std::string::npos)
		return false;
	size_t start = 0;
	while (start <= name.size()) {
		size_t end = name.find('/', start);
		if (end == std::string::npos)
			end = name.size();
		const std::string part = name.substr(start, end - start);
		if (part == "..")
			return false;
		start = end + 1;
	}
	return true;
}

} // namespace

bool ZipExtractor::fail(const std::string &message) {
	_error = message;
	return false;
}

bool ZipExtractor::open(const fs::path &zip) {
	_entries.clear();
	_top.clear();
	_error.clear();
	_in.open(zip, std::ios::binary);
	if (!_in)
		return fail("cannot open the file");
	_in.seekg(0, std::ios::end);
	_fileSize = uint64_t(_in.tellg());
	// The end of central directory record is in the last 64 KB + 22 bytes.
	const uint64_t tail = std::min<uint64_t>(_fileSize, 65557);
	std::vector<uint8_t> buf(tail);
	if (tail < 22 || !readAt(_in, _fileSize - tail, buf.data(), tail))
		return fail("not a zip file");
	size_t eocd = std::string::npos;
	for (size_t i = tail - 22 + 1; i-- > 0;)
		if (le32(&buf[i]) == 0x06054b50) {
			eocd = i;
			break;
		}
	if (eocd == std::string::npos)
		return fail("not a zip file");
	const uint8_t *e = &buf[eocd];
	const uint16_t count = le16(e + 10);
	const uint32_t cdSize = le32(e + 12), cdOffset = le32(e + 16);
	if (count == 0xFFFF || cdOffset == 0xFFFFFFFF)
		return fail("zip64 archives are not supported");
	if (uint64_t(cdOffset) + cdSize > _fileSize)
		return fail("the zip file is damaged (central directory)");
	std::vector<uint8_t> cd(cdSize);
	if (!readAt(_in, cdOffset, cd.data(), cdSize))
		return fail("cannot read the zip file");
	size_t p = 0;
	for (unsigned i = 0; i < count; i++) {
		if (p + 46 > cd.size() || le32(&cd[p]) != 0x02014b50)
			return fail("the zip file is damaged (directory entry)");
		const uint8_t *h = &cd[p];
		const uint16_t flags = le16(h + 8), nameLen = le16(h + 28), extraLen = le16(h + 30),
		               commentLen = le16(h + 32);
		if (p + 46 + nameLen > cd.size())
			return fail("the zip file is damaged (name)");
		Entry en;
		en.method = le16(h + 10);
		en.crc = le32(h + 16);
		en.compressedSize = le32(h + 20);
		en.size = le32(h + 24);
		en.offset = le32(h + 42);
		std::string name(reinterpret_cast<const char *>(h + 46), nameLen);
		std::replace(name.begin(), name.end(), '\\', '/');
		p += 46 + size_t(nameLen) + extraLen + commentLen;
		if (name.empty() || name.back() == '/' || name.rfind("__MACOSX/", 0) == 0 ||
		    fs::path(name).filename().string().rfind("._", 0) == 0 || fs::path(name).filename() == ".DS_Store")
			continue; // folders and macOS's extras
		if (flags & 1)
			return fail("encrypted zip files are not supported");
		if (en.method != 0 && en.method != 8)
			return fail(name + ": compression method " + std::to_string(en.method) + " is not supported");
		if (en.size == 0xFFFFFFFF || en.compressedSize == 0xFFFFFFFF)
			return fail("zip64 archives are not supported");
		if (!safeName(name))
			return fail("unsafe file name in the zip file: " + name);
		en.name = name;
		_entries.push_back(en);
	}
	if (_entries.empty())
		return fail("the zip file holds no files");
	// One top folder for everything: left out.
	const size_t slash = _entries.front().name.find('/');
	if (slash != std::string::npos) {
		const std::string top = _entries.front().name.substr(0, slash + 1);
		if (std::all_of(_entries.begin(), _entries.end(), [&](const Entry &x) { return x.name.rfind(top, 0) == 0; })) {
			_top = top.substr(0, slash);
			for (Entry &x : _entries)
				x.name = x.name.substr(top.size());
		}
	}
	return true;
}

bool ZipExtractor::begin(const fs::path &dest) {
	_dest = dest;
	_next = 0;
	_done = false;
	_bytesDone = 0;
	_bytesTotal = 0;
	for (const Entry &e : _entries)
		_bytesTotal += e.compressedSize;
	std::error_code ec;
	fs::create_directories(dest, ec);
	if (ec)
		return fail("cannot make " + dest.string() + ": " + ec.message());
	return true;
}

bool ZipExtractor::extract(const Entry &e) {
	uint8_t local[30];
	if (!readAt(_in, e.offset, local, sizeof(local)) || le32(local) != 0x04034b50)
		return fail(e.name + ": the zip file is damaged (local header)");
	const uint64_t dataAt = e.offset + 30 + le16(local + 26) + le16(local + 28);
	if (dataAt + e.compressedSize > _fileSize)
		return fail(e.name + ": the zip file ends too early");
	std::vector<uint8_t> packed(e.compressedSize), data;
	if (!readAt(_in, dataAt, packed.data(), packed.size()))
		return fail(e.name + ": cannot read the zip file");
	if (e.method == 0)
		data = std::move(packed);
	else if (!inflateRaw(packed.data(), packed.size(), data, e.size))
		return fail(e.name + ": the compressed data is damaged");
	if (data.size() != e.size || crc32(data.data(), data.size()) != e.crc)
		return fail(e.name + ": the checksum does not match");
	const fs::path out = _dest / fs::path(e.name);
	std::error_code ec;
	fs::create_directories(out.parent_path(), ec);
	std::ofstream f(out, std::ios::binary | std::ios::trunc);
	f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
	if (!f)
		return fail("cannot write " + out.string());
	return true;
}

bool ZipExtractor::step(uint64_t budget) {
	if (_done || !_error.empty())
		return false;
	uint64_t spent = 0;
	while (_next < _entries.size() && spent < budget) {
		const Entry &e = _entries[_next];
		if (!extract(e))
			return false;
		spent += e.compressedSize + 1;
		_bytesDone += e.compressedSize;
		_next++;
	}
	if (_next == _entries.size())
		_done = true;
	return !_done;
}

double ZipExtractor::fraction() const {
	return _bytesTotal ? double(_bytesDone) / double(_bytesTotal) : (_done ? 1.0 : 0.0);
}

} // namespace gob
