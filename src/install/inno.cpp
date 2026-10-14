// gob2native - GOG installer import
// SPDX-License-Identifier: GPL-3.0-or-later

#include "install/inno.h"

#include "gfx/png.h" // crc32

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace fs = std::filesystem;

namespace gob {

namespace {

constexpr uint32_t V(unsigned a, unsigned b, unsigned c) {
	return InnoSetup::innoVersion(a, b, c);
}

uint16_t le16(const uint8_t *p) {
	return uint16_t(p[0] | p[1] << 8);
}
uint32_t le32(const uint8_t *p) {
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool readAt(std::ifstream &in, uint64_t pos, uint8_t *dst, size_t size) {
	in.clear();
	in.seekg(std::streamoff(pos));
	in.read(reinterpret_cast<char *>(dst), std::streamsize(size));
	return bool(in);
}

std::string utf8(uint32_t c) {
	std::string s;
	if (c < 0x80)
		s += char(c);
	else if (c < 0x800) {
		s += char(0xC0 | (c >> 6));
		s += char(0x80 | (c & 0x3F));
	} else if (c < 0x10000) {
		s += char(0xE0 | (c >> 12));
		s += char(0x80 | ((c >> 6) & 0x3F));
		s += char(0x80 | (c & 0x3F));
	} else {
		s += char(0xF0 | (c >> 18));
		s += char(0x80 | ((c >> 12) & 0x3F));
		s += char(0x80 | ((c >> 6) & 0x3F));
		s += char(0x80 | (c & 0x3F));
	}
	return s;
}

std::string fromUtf16(const std::string &raw) {
	std::string out;
	const auto *p = reinterpret_cast<const uint8_t *>(raw.data());
	const size_t n = raw.size() / 2;
	for (size_t i = 0; i < n; i++) {
		uint32_t c = le16(p + i * 2);
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
			const uint32_t d = le16(p + i * 2 + 2);
			if (d >= 0xDC00 && d < 0xE000) {
				c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
				i++;
			}
		}
		out += utf8(c);
	}
	return out;
}

// Windows-1252 is close enough to Latin-1 for file names.
std::string fromAnsi(const std::string &raw) {
	std::string out;
	for (unsigned char c : raw)
		out += utf8(c);
	return out;
}

// A setup header block: CRC32, the stored size, a "compressed" flag, then
// the data in pieces of 4096 bytes, each after its own CRC32.
class BlockSource : public ByteSource {
public:
	BlockSource(std::ifstream &in, uint64_t size) : _in(in), _left(size) {}
	size_t read(uint8_t *dst, size_t size) override {
		size_t done = 0;
		while (done < size) {
			if (_pos == _len && !next())
				break;
			const size_t n = std::min(size - done, _len - _pos);
			std::memcpy(dst + done, _buf + _pos, n);
			_pos += n;
			done += n;
		}
		return done;
	}
	std::string error;

private:
	bool next() {
		if (_left == 0 || !error.empty())
			return false;
		if (_left <= 4) {
			error = "a setup header block ends too early";
			return false;
		}
		uint8_t crc[4];
		_len = size_t(std::min<uint64_t>(_left - 4, sizeof(_buf)));
		_in.read(reinterpret_cast<char *>(crc), 4);
		_in.read(reinterpret_cast<char *>(_buf), std::streamsize(_len));
		if (!_in) {
			error = "the installer ends too early (truncated download?)";
			return false;
		}
		if (crc32(_buf, _len) != le32(crc)) {
			error = "a setup header block is damaged (CRC mismatch)";
			return false;
		}
		_left -= 4 + _len;
		_pos = 0;
		return true;
	}

	std::ifstream &_in;
	uint64_t _left;
	uint8_t _buf[4096];
	size_t _pos = 0, _len = 0;
};

class BlockStream : public ByteSource {
public:
	BlockStream(std::ifstream &in, uint64_t size, bool lzma) : _crc(in, size), _compressed(lzma) {}
	bool init() {
		if (!_compressed)
			return true;
		uint8_t props[5];
		if (_crc.read(props, 5) != 5)
			return false;
		return _lzma.initLzma1(&_crc, props[0], le32(props + 1));
	}
	size_t read(uint8_t *dst, size_t size) override { return _compressed ? _lzma.decode(dst, size) : _crc.read(dst, size); }
	std::string error() const { return !_crc.error.empty() ? _crc.error : _lzma.error(); }

private:
	BlockSource _crc;
	LzmaDecoder _lzma;
	bool _compressed;
};

// Reads the fields of the headers, with Inno's "stored" encodings.
class FieldReader {
public:
	FieldReader(ByteSource &src, bool unicode) : _src(src), _unicode(unicode) {}
	bool ok() const { return _ok; }
	void bytes(uint8_t *dst, size_t n) {
		if (_ok && _src.read(dst, n) != n) {
			_ok = false;
			std::memset(dst, 0, n);
		}
	}
	void skip(size_t n) {
		uint8_t tmp[256];
		while (n && _ok) {
			const size_t k = std::min(n, sizeof(tmp));
			bytes(tmp, k);
			n -= k;
		}
	}
	uint8_t u8() {
		uint8_t b = 0;
		bytes(&b, 1);
		return b;
	}
	uint16_t u16() {
		uint8_t b[2] = {};
		bytes(b, 2);
		return le16(b);
	}
	uint32_t u32() {
		uint8_t b[4] = {};
		bytes(b, 4);
		return le32(b);
	}
	uint64_t u64() {
		const uint64_t lo = u32();
		return lo | uint64_t(u32()) << 32;
	}
	std::string raw() {
		const uint32_t n = u32();
		if (!_ok || n > (64u << 20)) {
			_ok = false;
			return {};
		}
		std::string s(n, '\0');
		bytes(reinterpret_cast<uint8_t *>(s.data()), n);
		return s;
	}
	// A string field: UTF-16 in Unicode installers, else Windows-1252.
	std::string str() {
		const std::string r = raw();
		return _unicode ? fromUtf16(r) : fromAnsi(r);
	}
	std::string ansi() { return fromAnsi(raw()); }
	void skipStrings(int n) {
		while (n-- > 0 && _ok)
			raw();
	}
	// A set of `bits` flags: one byte per 8, and 3 bytes are padded to 4.
	uint64_t flags(unsigned bits) {
		const unsigned n = (bits + 7) / 8;
		uint64_t v = 0;
		for (unsigned i = 0; i < n; i++) {
			const uint64_t b = u8();
			if (i < 8)
				v |= b << (8 * i);
		}
		if (n == 3)
			u8();
		return v;
	}
	// Minimum and "only below" Windows versions: each (build, minor, major)
	// for Windows and NT, and the NT service pack.
	void winVersions() { skip(20); }

private:
	ByteSource &_src;
	bool _unicode;
	bool _ok = true;
};

// The conditions every item has: components, tasks, languages, check,
// after/before install.
void conditions(FieldReader &r, InnoFile *file) {
	if (file) {
		file->components = r.str();
		file->tasks = r.str();
		file->languages = r.str();
		file->check = r.str();
		file->afterInstall = r.str();
		file->beforeInstall = r.str();
	} else
		r.skipStrings(6);
}

} // namespace

bool InnoSetup::fail(const std::string &message) {
	if (_error.empty())
		_error = message;
	return false;
}

bool InnoSetup::open(const fs::path &path) {
	_path = path;
	_error.clear();
	_files.clear();
	_data.clear();
	_languages.clear();
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return fail("cannot open " + path.string());
	uint32_t headerOffset = 0;
	if (!readOffsetTable(in, headerOffset))
		return false;
	return readHeaders(in, headerOffset);
}

// The offset table: in the program's resources (RCDATA #11111) since Inno
// Setup 5.1.5, before that at a pointer at 0x30.
bool InnoSetup::readOffsetTable(std::ifstream &in, uint32_t &headerOffset) {
	uint8_t mz[64];
	if (!readAt(in, 0, mz, sizeof(mz)) || mz[0] != 'M' || mz[1] != 'Z')
		return fail("not a Windows program (setup_*.exe)");
	const uint32_t pe = le32(mz + 0x3C);
	uint8_t coff[24];
	if (!readAt(in, pe, coff, sizeof(coff)) || std::memcmp(coff, "PE\0\0", 4) != 0)
		return fail("not a Windows program (no PE header)");
	const unsigned sections = le16(coff + 6), optSize = le16(coff + 20);
	std::vector<uint8_t> opt(optSize);
	if (optSize < 96 || !readAt(in, pe + 24, opt.data(), optSize))
		return fail("not a Windows program (bad PE header)");
	const bool pe64 = le16(opt.data()) == 0x20B;
	const size_t dirs = pe64 ? 112 : 96; // the data directories; #2 is the resources
	if (optSize < dirs + 3 * 8 || le32(&opt[dirs - 4]) < 3)
		return fail("no resources in the program");
	const uint32_t resRva = le32(&opt[dirs + 16]);
	struct Section { uint32_t va, size, raw; };
	std::vector<Section> table;
	std::vector<uint8_t> sec(size_t(sections) * 40);
	if (!readAt(in, pe + 24 + optSize, sec.data(), sec.size()))
		return fail("bad section table");
	for (unsigned i = 0; i < sections; i++)
		table.push_back({le32(&sec[i * 40 + 12]), std::max(le32(&sec[i * 40 + 8]), le32(&sec[i * 40 + 16])),
		                 le32(&sec[i * 40 + 20])});
	auto fileOffset = [&](uint32_t rva) -> uint64_t {
		for (const Section &s : table)
			if (rva >= s.va && rva < s.va + s.size)
				return uint64_t(rva) - s.va + s.raw;
		return 0;
	};
	const uint64_t resBase = fileOffset(resRva);
	if (!resBase)
		return fail("no resources in the program");
	// Resource directories: type -> name -> language.
	auto findEntry = [&](uint64_t dir, uint32_t id, bool any) -> uint32_t {
		uint8_t h[16];
		if (!readAt(in, dir, h, sizeof(h)))
			return 0;
		const unsigned named = le16(h + 12), ids = le16(h + 14);
		std::vector<uint8_t> e(size_t(named + ids) * 8);
		if (!readAt(in, dir + 16, e.data(), e.size()))
			return 0;
		for (unsigned i = any ? 0 : named; i < named + ids; i++)
			if (any || le32(&e[i * 8]) == id)
				return le32(&e[i * 8 + 4]);
		return 0;
	};
	const uint32_t typeDir = findEntry(resBase, 10, false); // RT_RCDATA
	const uint32_t nameDir = (typeDir & 0x80000000) ? findEntry(resBase + (typeDir & 0x7FFFFFFF), 11111, false) : 0;
	const uint32_t leaf = (nameDir & 0x80000000) ? findEntry(resBase + (nameDir & 0x7FFFFFFF), 0, true) : 0;
	if (!leaf || (leaf & 0x80000000))
		return fail("not an Inno Setup installer (no setup loader resource)");
	uint8_t entry[8];
	if (!readAt(in, resBase + leaf, entry, sizeof(entry)))
		return fail("bad resource entry");
	const uint64_t tablePos = fileOffset(le32(entry));
	uint8_t t[64];
	if (!tablePos || !readAt(in, tablePos, t, 44))
		return fail("bad setup loader resource");
	static const uint8_t magic[12] = {'r', 'D', 'l', 'P', 't', 'S', 0xCD, 0xE6, 0xD7, '{', 0x0B, '*'};
	static const uint8_t magic2[12] = {'n', 'S', '5', 'W', '7', 'd', 'T', 0x83, 0xAA, 0x1B, 0x0F, 'j'};
	if (std::memcmp(t, magic, 12) != 0 && std::memcmp(t, magic2, 12) != 0)
		return fail("unsupported Inno Setup loader (older than 5.1.5?)");
	// magic, revision, (unused), exe offset, exe size, exe CRC, header
	// offset, data offset, then the CRC32 of all that.
	if (crc32(t, 40) != le32(t + 40))
		return fail("the setup loader table is damaged (CRC mismatch)");
	headerOffset = le32(t + 32);
	_dataOffset = le32(t + 36);
	if (_dataOffset == 0)
		return fail("the files are in separate setup-*.bin parts, which aren't supported yet");
	return true;
}

bool InnoSetup::readHeaders(std::ifstream &in, uint32_t headerOffset) {
	char vs[64];
	if (!readAt(in, headerOffset, reinterpret_cast<uint8_t *>(vs), sizeof(vs)))
		return fail("the installer ends too early (incomplete download?)");
	const std::string version(vs, strnlen(vs, sizeof(vs)));
	unsigned a = 0, b = 0, c = 0;
	const size_t open = version.find("Inno Setup Setup Data (");
	if (open == std::string::npos || std::sscanf(version.c_str() + open + 23, "%u.%u.%u", &a, &b, &c) != 3)
		return fail("unknown setup data version \"" + version + "\"");
	_version = V(a, b, c);
	_unicode = version.find("(u)") != std::string::npos || version.find("(U)") != std::string::npos ||
	           _version >= V(6, 3, 0);
	_versionString = std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c) +
	                 (_unicode ? " (unicode)" : "");
	if (_version < V(5, 3, 9) || _version >= V(6, 4, 0))
		return fail("setup data version " + _versionString +
		            " isn't supported (5.3.9 to 6.3 are; GOG's installers are 5.5.7)");
	// "5.5.7" may also be 5.6.0's layout (the version wasn't always raised).
	std::vector<uint32_t> tries = {_version};
	if (_version == V(5, 5, 7))
		tries.push_back(V(5, 6, 0));
	std::string firstError;
	for (uint32_t v : tries) {
		_error.clear();
		_files.clear();
		_data.clear();
		_languages.clear();
		const uint32_t listed = _version;
		_version = v;
		in.clear();
		in.seekg(std::streamoff(headerOffset + 64));
		auto readBlock = [&](auto &&parse) -> bool {
			uint8_t h[9];
			in.read(reinterpret_cast<char *>(h), 9);
			if (!in)
				return fail("the installer ends too early (incomplete download?)");
			if (crc32(h + 4, 5) != le32(h))
				return fail("a setup header block is damaged (CRC mismatch)");
			const uint64_t start = uint64_t(in.tellg());
			const uint32_t stored = le32(h + 4);
			// On the heap: the decoder's buffers are too big for small stacks (web builds).
			auto block = std::make_unique<BlockStream>(in, stored, h[8] != 0);
			if (!block->init())
				return fail("cannot read a setup header block: " + block->error());
			FieldReader r(*block, _unicode);
			if (!parse(r) || !r.ok())
				return fail(!block->error().empty() ? block->error()
				            : !r.ok()               ? "the setup headers end too early"
				                                    : "bad setup headers");
			in.clear();
			in.seekg(std::streamoff(start + stored));
			return true;
		};
		uint32_t dataCount = 0;
		bool ok = readBlock([&](FieldReader &r) {
			// The setup header.
			_appName = r.str();
			r.skipStrings(3); // versioned name, id, copyright
			_publisher = r.str();
			r.skipStrings(4); // publisher URL, phone, support/updates URLs
			_appVersion = r.str();
			r.skipStrings(14); // default dir/group, base file name, uninstall files dir/name/icon, mutex,
			                   // user name/organisation, serial, readme, contact, comments, modify path
			if (v >= V(5, 3, 8))
				r.skipStrings(1); // create uninstall registry key
			if (v >= V(5, 3, 10))
				r.skipStrings(1); // uninstallable
			if (v >= V(5, 5, 0))
				r.skipStrings(1); // close applications filter
			if (v >= V(5, 5, 6))
				r.skipStrings(1); // setup mutex
			if (v >= V(5, 6, 1))
				r.skipStrings(2); // changes environment, associations
			if (v >= V(6, 3, 0))
				r.skipStrings(2); // architecture expressions
			r.skipStrings(3);     // license, info before, info after
			if (v < V(5, 3, 10))
				r.skipStrings(1); // uninstaller signature
			r.skipStrings(1);     // compiled code
			if (!_unicode)
				r.skip(32); // lead bytes
			const uint32_t languages = r.u32(), messages = r.u32(), permissions = r.u32(), types = r.u32(),
			               components = r.u32(), tasks = r.u32(), directories = r.u32(), files = r.u32();
			dataCount = r.u32();
			r.skip(7 * 4); // icons, ini, registry, delete, uninstall delete, run, uninstall run entries
			r.winVersions();
			r.skip(8); // back colours
			if (v < V(5, 5, 7))
				r.skip(4); // image back colour
			if (v >= V(6, 0, 0))
				r.skip(9); // wizard style and size
			if (v >= V(5, 5, 7))
				r.skip(1); // image alpha format
			r.skip(20 + 8); // password SHA-1 and salt
			r.skip(8 + 4);  // extra disk space, slices per disk
			r.skip(1 + 1 + 1); // uninstall log mode, dir exists warning, privileges
			if (v >= V(5, 7, 0))
				r.skip(1); // privilege overrides
			r.skip(2); // language dialog, language detection
			const uint8_t compression = r.u8();
			if (v < V(6, 3, 0))
				r.skip(2); // architectures allowed / 64-bit mode
			if (v < V(5, 3, 10))
				r.skip(8); // signed uninstaller size and checksum
			r.skip(2); // disable dir page, program group page
			r.skip(v >= V(5, 5, 0) ? 8 : 4); // uninstall display size
			// The option flags, which depend on the version.
			unsigned bits = 0, encryptionBit = 0;
			auto flag = [&](bool present) { bits += present; };
			flag(true);              // disable startup prompt
			flag(v < V(5, 3, 10));   // uninstallable
			flag(true);              // create app dir
			flag(true);              // allow no icons
			flag(true);              // always restart
			flag(true);              // always use personal group
			bits += 4;               // window visible, caption, resizable, maximized
			flag(true);              // dir doesn't exist warning
			flag(true);              // password
			flag(true);              // allow root directory
			flag(true);              // disable finished page
			flag(v < V(5, 6, 1));    // changes associations
			bits += 5;               // previous app dir, back colour horizontal, previous group,
			                         // update uninstall log app name, previous setup type
			bits += 6;               // ready memo, components list, flat, sizes, previous tasks, ready page
			bits += 2;               // dir and group on ready page
			flag(true);              // allow UNC path
			bits += 2;               // user info page, previous user info
			bits += 2;               // uninstall restart computer, restart if needed by run
			flag(true);              // tasks tree lines
			flag(true);              // allow cancel during install
			flag(true);              // wizard image stretch
			bits += 2;               // append default dir/group name
			encryptionBit = bits;
			flag(true);              // encryption used
			flag(v < V(5, 6, 1));    // changes environment
			flag(!_unicode);         // show undisplayable languages
			bits += 2;               // setup logging, signed uninstaller
			flag(true);              // use previous language
			flag(true);              // disable welcome page
			if (v >= V(5, 5, 0))
				bits += 3; // close applications, restart applications, allow network drive
			if (v >= V(5, 5, 7))
				bits += 1; // force close applications
			if (v >= V(6, 0, 0))
				bits += 3;
			if (v >= V(6, 3, 0))
				bits += 1;
			const uint64_t options = r.flags(bits);
			if (options & (uint64_t(1) << encryptionBit))
				return fail("the installer is encrypted (password protected), which isn't supported");
			switch (compression) {
			case 0: _compression = InnoCompression::Stored; break;
			case 1: _compression = InnoCompression::Zlib; break;
			case 2: _compression = InnoCompression::Bzip2; break;
			case 3: _compression = InnoCompression::Lzma1; break;
			case 4: _compression = InnoCompression::Lzma2; break;
			default: return fail("unknown compression method");
			}

			for (uint32_t i = 0; i < languages && r.ok(); i++) {
				r.raw(); // name
				_languages.push_back(fromUtf16(r.raw()));
				r.skipStrings(8); // fonts, data, license, info before/after
				r.skip(4);        // language id
				if (!_unicode)
					r.skip(4); // code page
				else if (v < V(5, 3, 0))
					r.skip(4);
				r.skip(16 + 1); // font sizes, right to left
			}
			for (uint32_t i = 0; i < messages && r.ok(); i++) {
				r.skipStrings(2);
				r.skip(4);
			}
			for (uint32_t i = 0; i < permissions && r.ok(); i++)
				r.skipStrings(1);
			for (uint32_t i = 0; i < types && r.ok(); i++) {
				r.skipStrings(4);
				r.winVersions();
				r.skip(1 + 1 + 8); // flags, setup type, size
			}
			for (uint32_t i = 0; i < components && r.ok(); i++) {
				r.skipStrings(5);
				r.skip(8 + 4 + 1); // extra disk space, level, used
				r.winVersions();
				r.skip(1 + 8); // flags, size
			}
			for (uint32_t i = 0; i < tasks && r.ok(); i++) {
				r.skipStrings(6);
				r.skip(4 + 1); // level, used
				r.winVersions();
				r.skip(1); // flags
			}
			for (uint32_t i = 0; i < directories && r.ok(); i++) {
				r.skipStrings(1);
				conditions(r, nullptr);
				r.skip(4); // attributes
				r.winVersions();
				r.skip(2 + 1); // permission, flags
			}
			for (uint32_t i = 0; i < files && r.ok(); i++) {
				InnoFile f;
				f.source = r.str();
				f.destination = r.str();
				r.skipStrings(2); // font name, strong assembly name
				conditions(r, &f);
				r.winVersions();
				f.data = r.u32();
				r.skip(4); // attributes
				f.externalSize = r.u64();
				r.skip(2); // permission
				r.flags(32);
				f.type = r.u8();
				_files.push_back(std::move(f));
			}
			// The rest of the block (icons, ini and registry entries, ...,
			// the wizard images) isn't needed.
			return r.ok();
		});
		ok = ok && readBlock([&](FieldReader &r) {
			for (uint32_t i = 0; i < dataCount && r.ok(); i++) {
				InnoData d;
				d.firstSlice = r.u32();
				d.lastSlice = r.u32();
				d.chunkOffset = r.u32();
				d.fileOffset = r.u64();
				d.fileSize = r.u64();
				d.chunkSize = r.u64();
				r.bytes(d.sha1.data(), 20);
				d.fileTime = int64_t(r.u64());
				r.skip(8); // file version
				const unsigned bits = v >= V(5, 5, 7) && v < V(6, 3, 0) ? 11 : 9;
				const uint64_t f = r.flags(bits);
				if (v >= V(6, 3, 0))
					r.skip(1); // sign mode
				d.callFilter = f & (1 << 4);
				d.encrypted = f & (1 << 6);
				d.compressed = f & (1 << 7);
				_data.push_back(d);
			}
			return r.ok();
		});
		// Sanity checks: a wrong layout guess shows here.
		for (const InnoFile &f : _files)
			if (ok && f.data != UINT32_MAX && f.data >= _data.size())
				ok = fail("bad file entry (data index out of range)");
		if (ok && _files.empty())
			ok = fail("the installer has no files");
		_version = ok ? v : listed;
		if (ok) {
			for (InnoFile &f : _files)
				if (f.data != UINT32_MAX) {
					f.parts = {f.data};
					f.size = _data[f.data].fileSize;
				}
			assembleGalaxyFiles();
			return true;
		}
		if (firstError.empty())
			firstError = _error;
	}
	_error = firstError;
	return false;
}

namespace {

// The arguments of a script call such as "before_install('a', 'b', 3);",
// or nothing when `code` isn't a call of `name`.
std::vector<std::string> callArguments(const std::string &code, const std::string &name) {
	std::vector<std::string> args;
	size_t p = code.find_first_not_of(" \t\r\n");
	if (p == std::string::npos || code.compare(p, name.size(), name) != 0)
		return {};
	p = code.find_first_not_of(" \t\r\n", p + name.size());
	if (p == std::string::npos || code[p] != '(')
		return {};
	p++;
	for (;;) {
		p = code.find_first_not_of(" \t\r\n", p);
		if (p == std::string::npos)
			return {};
		std::string arg;
		if (code[p] == '\'') {
			for (p++;; p++) {
				if (p >= code.size())
					return {};
				if (code[p] == '\'') {
					if (p + 1 < code.size() && code[p + 1] == '\'') {
						arg += '\'';
						p++;
						continue;
					}
					p++;
					break;
				}
				arg += code[p];
			}
		} else if (code[p] != ')') {
			const size_t end = code.find_first_of(" \t\r\n,)", p);
			if (end == std::string::npos)
				return {};
			arg = code.substr(p, end - p);
			p = end;
		}
		p = code.find_first_not_of(" \t\r\n", p);
		if (p == std::string::npos)
			return {};
		if (code[p] == ')') {
			if (!arg.empty() || !args.empty())
				args.push_back(arg);
			return args;
		}
		if (code[p] != ',')
			return {};
		args.push_back(arg);
		p++;
	}
}

bool parseMd5(const std::string &hex, std::array<uint8_t, 16> &md5) {
	if (hex.size() != 32)
		return false;
	for (size_t i = 0; i < 16; i++) {
		unsigned v = 0;
		if (std::sscanf(hex.c_str() + i * 2, "%2x", &v) != 1)
			return false;
		md5[i] = uint8_t(v);
	}
	return true;
}

uint64_t number(const std::string &s) {
	return std::strtoull(s.c_str(), nullptr, 10);
}

} // namespace

void InnoSetup::assembleGalaxyFiles() {
	InnoFile *start = nullptr;
	size_t remaining = 0;
	for (InnoFile &f : _files) {
		std::vector<std::string> first = callArguments(f.beforeInstall, "before_install");
		if (first.empty())
			first = callArguments(f.beforeInstall, "before_install_dependency");
		if (!first.empty()) {
			// md5 of the file, its name, the number of parts
			start = &f;
			if (first.size() >= 2 && !first[1].empty())
				f.destination = first[1];
			f.hasMd5 = parseMd5(first[0], f.md5);
			remaining = first.size() >= 3 ? std::max<uint64_t>(number(first[2]), 1) : 1;
			f.parts.clear();
			f.partSizes.clear();
			f.size = 0;
			f.deflated = true;
		}
		std::vector<std::string> part = callArguments(f.afterInstall, "after_install");
		if (part.empty())
			part = callArguments(f.afterInstall, "after_install_dependency");
		if (part.empty() || !start || remaining == 0 || f.data == UINT32_MAX || part.size() < 3) {
			if (start && remaining) {
				// Not what the parts said: leave these entries as they are.
				start->deflated = false;
				start->hasMd5 = false;
				start->parts = {start->data};
				start->size = start->data != UINT32_MAX ? _data[start->data].fileSize : 0;
			}
			start = nullptr;
			remaining = 0;
			continue;
		}
		// md5 of the part (the setup's SHA-1 covers it), stored size, size
		start->parts.push_back(f.data);
		start->partSizes.push_back(number(part[2]));
		start->size += number(part[2]);
		if (&f != start) {
			f.merged = true;
			if (start->languages.empty())
				start->languages = f.languages;
		}
		if (--remaining == 0)
			start = nullptr;
	}
	// GOG's language condition: check_if_install('en-US#fr-FR#', '32#64#', ...).
	for (InnoFile &f : _files) {
		const std::vector<std::string> check = callArguments(f.check, "check_if_install");
		if (!check.empty() && !check[0].empty() && f.languages.empty()) {
			std::string langs = check[0];
			while (!langs.empty() && langs.back() == '#')
				langs.pop_back();
			std::replace(langs.begin(), langs.end(), '#', ' ');
			f.languages = langs;
		}
	}
}

std::string InnoSetup::relativePath(const std::string &destination, bool keepApp) {
	std::string expanded;
	for (size_t i = 0; i < destination.size(); i++) {
		const char ch = destination[i];
		if (ch == '{') {
			if (i + 1 < destination.size() && destination[i + 1] == '{') {
				expanded += '{';
				i++;
				continue;
			}
			const size_t close = destination.find('}', i);
			if (close == std::string::npos)
				return {};
			std::string name = destination.substr(i + 1, close - i - 1);
			if (name != "app" || keepApp)
				expanded += name;
			i = close;
		} else if (ch == '\\')
			expanded += '/';
		else if (uint8_t(ch) < 32 || std::strchr("<>:\"|?*", ch))
			expanded += '$';
		else
			expanded += ch;
	}
	// Drop empty and "." parts; ".." would leave the folder.
	std::string out;
	size_t pos = 0;
	while (pos <= expanded.size()) {
		size_t end = expanded.find('/', pos);
		if (end == std::string::npos)
			end = expanded.size();
		const std::string part = expanded.substr(pos, end - pos);
		if (part == "..")
			return {};
		if (!part.empty() && part != ".") {
			if (!out.empty())
				out += '/';
			out += part;
		}
		pos = end + 1;
	}
	return out;
}

size_t InnoChunkReader::Raw::read(uint8_t *dst, size_t size) {
	const size_t n = size_t(std::min<uint64_t>(size, left));
	if (n == 0)
		return 0;
	file.read(reinterpret_cast<char *>(dst), std::streamsize(n));
	const size_t got = size_t(file.gcount());
	left -= got;
	if (got < n)
		failed = true;
	return got;
}

bool InnoChunkReader::open(const InnoSetup &setup, const InnoData &entry, uint64_t maxOutput) {
	_error.clear();
	_position = 0;
	_lzma.reset();
	_raw.file.close();
	_raw.file.clear();
	_raw.failed = false;
	if (entry.encrypted) {
		_error = "encrypted files aren't supported";
		return false;
	}
	if (entry.firstSlice != 0 || entry.lastSlice != 0) {
		_error = "files in other setup parts (slices) aren't supported";
		return false;
	}
	_raw.file.open(setup.path(), std::ios::binary);
	uint8_t magic[4];
	if (!_raw.file || !readAt(_raw.file, setup.dataOffset() + entry.chunkOffset, magic, 4) ||
	    std::memcmp(magic, "zlb\x1a", 4) != 0) {
		_error = "bad chunk in the installer (truncated download?)";
		return false;
	}
	_raw.left = entry.chunkSize;
	_stored = !entry.compressed || setup.compression() == InnoCompression::Stored;
	if (_stored)
		return true;
	_lzma = std::make_unique<LzmaDecoder>();
	uint8_t props[5];
	switch (setup.compression()) {
	case InnoCompression::Lzma1:
		if (_raw.read(props, 5) != 5 || !_lzma->initLzma1(&_raw, props[0], le32(props + 1), maxOutput))
			_error = "cannot start LZMA decoding: " + _lzma->error();
		break;
	case InnoCompression::Lzma2:
		if (_raw.read(props, 1) != 1 || !_lzma->initLzma2(&_raw, props[0], maxOutput))
			_error = "cannot start LZMA2 decoding: " + _lzma->error();
		break;
	default:
		_error = "zlib and bzip2 compressed installers aren't supported";
		break;
	}
	return _error.empty();
}

size_t InnoChunkReader::read(uint8_t *dst, size_t size) {
	if (!_error.empty())
		return 0;
	size_t n;
	if (_stored) {
		if (dst)
			n = _raw.read(dst, size);
		else {
			n = size_t(std::min<uint64_t>(size, _raw.left));
			_raw.file.seekg(std::streamoff(n), std::ios::cur);
			_raw.left -= n;
		}
	} else {
		n = _lzma->decode(dst, size);
		if (_lzma->failed())
			_error = _lzma->error();
	}
	if (_raw.failed && _error.empty())
		_error = "the installer ends too early (truncated download?)";
	_position += n;
	return n;
}

bool InnoChunkReader::skip(uint64_t size) {
	while (size && _error.empty()) {
		const size_t n = read(nullptr, size_t(std::min<uint64_t>(size, 1 << 20)));
		if (n == 0)
			break;
		size -= n;
	}
	if (size && _error.empty())
		_error = "a chunk ends too early";
	return _error.empty();
}

bool InnoExtractor::fail(const std::string &message) {
	if (_error.empty())
		_error = message;
	_out.clear();
	return false;
}

bool InnoExtractor::begin(const InnoSetup &setup, std::vector<Item> items) {
	_setup = &setup;
	_error.clear();
	_done = false;
	_next = 0;
	_inItem = _inPart = false;
	_chunkOpen = false;
	_progress = _total = 0;
	_filesDone = 0;
	for (const Item &item : items)
		if (item.file >= setup.files().size() || setup.files()[item.file].parts.empty())
			return fail("bad file entry");
	// In storage order: by chunk, then by place in the chunk (of the first part).
	auto first = [&](const Item &item) -> const InnoData & { return setup.data()[setup.files()[item.file].parts[0]]; };
	std::stable_sort(items.begin(), items.end(), [&](const Item &a, const Item &b) {
		const InnoData &x = first(a), &y = first(b);
		if (x.firstSlice != y.firstSlice)
			return x.firstSlice < y.firstSlice;
		if (x.chunkOffset != y.chunkOffset)
			return x.chunkOffset < y.chunkOffset;
		return x.fileOffset < y.fileOffset;
	});
	_items = std::move(items);
	// Work: every chunk is decoded from its start to the end of the last
	// part wanted from it.
	std::map<std::pair<uint32_t, uint32_t>, uint64_t> chunkEnds;
	for (const Item &item : _items)
		for (uint32_t part : setup.files()[item.file].parts) {
			const InnoData &d = setup.data()[part];
			uint64_t &end = chunkEnds[{d.firstSlice, d.chunkOffset}];
			end = std::max(end, d.fileOffset + d.fileSize);
		}
	for (const auto &[key, end] : chunkEnds)
		_total += end;
	_done = _items.empty();
	return true;
}

std::string InnoExtractor::current() const {
	return _next < _items.size() ? _items[_next].name : std::string();
}

void InnoExtractor::write(const uint8_t *data, size_t size) {
	if (file().hasMd5)
		_md5.update(data, size);
	for (auto &f : _out)
		f->write(reinterpret_cast<const char *>(data), std::streamsize(size));
}

bool InnoExtractor::startItem() {
	const Item &item = _items[_next];
	_out.clear();
	for (const fs::path &p : item.outputs) {
		std::error_code ec;
		if (p.has_parent_path())
			fs::create_directories(p.parent_path(), ec);
		auto f = std::make_unique<std::ofstream>(p, std::ios::binary | std::ios::trunc);
		if (!*f)
			return fail("cannot write " + p.string());
		_out.push_back(std::move(f));
	}
	_md5.reset();
	_part = 0;
	_inItem = true;
	return true;
}

// Gets the chunk reader to the start of the current part.
bool InnoExtractor::startPart() {
	const InnoData &d = _setup->data()[file().parts[_part]];
	// A new chunk, or a part before where the chunk has got to (two
	// entries sharing bytes): decode the chunk from its start.
	if (!_chunkOpen || d.firstSlice != _chunkSlice || d.chunkOffset != _chunkOffset ||
	    d.fileOffset < _chunk.position()) {
		uint64_t end = d.fileOffset + d.fileSize;
		for (size_t i = _next; i < _items.size(); i++)
			for (uint32_t part : _setup->files()[_items[i].file].parts) {
				const InnoData &e = _setup->data()[part];
				if (e.firstSlice == d.firstSlice && e.chunkOffset == d.chunkOffset)
					end = std::max(end, e.fileOffset + e.fileSize);
			}
		if (_chunkOpen && d.firstSlice == _chunkSlice && d.chunkOffset == _chunkOffset)
			_total += _chunk.position(); // decoded again
		if (!_chunk.open(*_setup, d, end))
			return fail(_items[_next].name + ": " + _chunk.error());
		_chunkOpen = true;
		_chunkSlice = d.firstSlice;
		_chunkOffset = d.chunkOffset;
	}
	if (_chunk.position() < d.fileOffset)
		return true; // step() skips to it
	_sha1.reset();
	_filterOffset = 0;
	_filterHave = -1;
	_deflated.clear();
	_partLeft = d.fileSize;
	_inPart = true;
	return true;
}

bool InnoExtractor::finishPart() {
	const InnoFile &f = file();
	const InnoData &d = _setup->data()[f.parts[_part]];
	if (_filterHave > 0) {
		// An address cut off by the end stays as it is.
		_sha1.update(_filterBuf, size_t(_filterHave));
		if (f.deflated)
			_deflated.insert(_deflated.end(), _filterBuf, _filterBuf + _filterHave);
		else
			write(_filterBuf, size_t(_filterHave));
	}
	if (_sha1.finish() != d.sha1)
		return fail(_items[_next].name + " is damaged (SHA-1 mismatch)");
	if (f.deflated) {
		const uint64_t size = f.partSizes[_part];
		if (!zlibDecompress(_deflated.data(), _deflated.size(), _inflated, size_t(size)) || _inflated.size() != size)
			return fail(_items[_next].name + " is damaged (bad zlib data)");
		write(_inflated.data(), _inflated.size());
	}
	_inPart = false;
	_part++;
	return true;
}

bool InnoExtractor::finishItem() {
	const Item &item = _items[_next];
	for (auto &f : _out) {
		f->close();
		if (!*f)
			return fail("cannot write " + item.name + " (disk full?)");
	}
	_out.clear();
	if (file().hasMd5 && _md5.finish() != file().md5)
		return fail(item.name + " is damaged (MD5 mismatch)");
	_inItem = false;
	_next++;
	_filesDone++;
	return true;
}

bool InnoExtractor::step(uint64_t budget) {
	if (_done || failed())
		return false;
	while (budget > 0 && !_done) {
		if (!_inItem) {
			if (_next >= _items.size()) {
				_done = true;
				break;
			}
			if (!startItem())
				return false;
			continue;
		}
		if (!_inPart) {
			if (_part == file().parts.size()) {
				if (!finishItem())
					return false;
				continue;
			}
			if (!startPart())
				return false;
			if (!_inPart) {
				// Decode (and drop) what comes before the part.
				const uint64_t gap = _setup->data()[file().parts[_part]].fileOffset - _chunk.position();
				const uint64_t n = std::min(gap, budget);
				if (!_chunk.skip(n))
					return fail(_items[_next].name + ": " + _chunk.error());
				_progress += n;
				budget -= n;
			}
			continue;
		}
		if (_partLeft == 0) {
			if (!finishPart())
				return false;
			continue;
		}
		const size_t n = size_t(std::min<uint64_t>({_partLeft, budget, 1 << 18}));
		_buf.resize(n);
		if (_chunk.read(_buf.data(), n) != n)
			return fail(_items[_next].name + ": " +
			            (_chunk.error().empty() ? "a chunk ends too early" : _chunk.error()));
		_partLeft -= n;
		_progress += n;
		budget -= n;
		const uint8_t *data = _buf.data();
		size_t size = n;
		if (_setup->data()[file().parts[_part]].callFilter) {
			// Undo the x86 call filter (Inno Setup 5.3.9 and later): E8/E9
			// opcodes are followed by an address made relative to the file
			// position, unless it would cross a 64 KB block.
			_filtered.clear();
			for (size_t i = 0; i < n; i++) {
				const uint8_t b = _buf[i];
				_filterOffset++;
				if (_filterHave < 0) {
					_filtered.push_back(b);
					if ((b == 0xE8 || b == 0xE9) && 0x10000 - ((_filterOffset - 1) % 0x10000) >= 5)
						_filterHave = 0;
					continue;
				}
				_filterBuf[_filterHave++] = b;
				if (_filterHave < 4)
					continue;
				if (_filterBuf[3] == 0x00 || _filterBuf[3] == 0xFF) {
					const uint32_t addr = _filterOffset & 0xFFFFFF;
					uint32_t rel = uint32_t(_filterBuf[0]) | uint32_t(_filterBuf[1]) << 8 | uint32_t(_filterBuf[2]) << 16;
					rel -= addr;
					_filterBuf[0] = uint8_t(rel);
					_filterBuf[1] = uint8_t(rel >> 8);
					_filterBuf[2] = uint8_t(rel >> 16);
					if (rel & 0x800000)
						_filterBuf[3] = uint8_t(~_filterBuf[3]);
				}
				_filtered.insert(_filtered.end(), _filterBuf, _filterBuf + 4);
				_filterHave = -1;
			}
			data = _filtered.data();
			size = _filtered.size();
		}
		_sha1.update(data, size);
		if (file().deflated)
			_deflated.insert(_deflated.end(), data, data + size);
		else
			write(data, size);
	}
	return !_done;
}

} // namespace gob
