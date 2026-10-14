// gob2native - GOG installer import
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reads the Inno Setup installers GOG sells the games as (setup_*.exe) and
// extracts their files, without running them and without innoextract: the
// format notes are in docs/install.md (the format reference was innoextract
// by Daniel Scharrer; this is our own implementation).
//
// Layout of a single-file installer (setup data versions 5.3.9 to 6.3.x):
// - the Windows program, with an RCDATA resource 11111 that holds the
//   offset table (magic, offsets of the setup headers and of the file data,
//   CRC32);
// - at the header offset: a 64-byte version string ("Inno Setup Setup Data
//   (5.5.7) (u)") and two compressed blocks: the setup header with the
//   languages, messages, ..., directories, files, icons, ... entries, then
//   the data entries (where each file's bytes are);
// - at the data offset: "chunks", each "zlb\x1a" and an LZMA or LZMA2
//   stream holding one or more files back to back (solid compression).
//
// Plain C++ without threads or platform calls: the extractor works in
// steps of a given size (install/gameinstall.h, the app's progress bar).

#pragma once

#include "install/lzma.h"
#include "install/hash.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace gob {

enum class InnoCompression { Stored, Zlib, Bzip2, Lzma1, Lzma2 };

// A [Files] entry: where a file goes and under which conditions.
//
// GOG's "Galaxy" installers store each file as parts of up to 10 MB, each
// deflated (zlib) and then put into the installer under a temporary name;
// script calls in the entries (before_install('md5', 'name', parts) on the
// first part, after_install('md5', size, unpacked size) on each) say how to
// put them together. open() does that: the first part's entry gets the
// real name and all parts, the other parts' entries are marked `merged`.
struct InnoFile {
	std::string source;      // empty for files stored in the installer
	std::string destination; // as written by the setup script: "{app}\INTRO.STK"
	std::string components, tasks, languages, check, afterInstall, beforeInstall;
	uint32_t data = UINT32_MAX; // index into InnoSetup::data, or UINT32_MAX (an external file)
	uint64_t externalSize = 0;
	uint8_t type = 0; // 0: a normal file, 1: the uninstaller

	std::vector<uint32_t> parts;     // data entries of the file's parts (just `data` for normal files)
	std::vector<uint64_t> partSizes; // their sizes once inflated (deflated files)
	bool deflated = false;           // the parts are zlib streams (GOG Galaxy)
	bool merged = false;             // a later part of another entry's file: not a file of its own
	bool hasMd5 = false;             // md5 of the whole file (GOG Galaxy)
	std::array<uint8_t, 16> md5{};
	uint64_t size = 0;               // of the extracted file
};

// Where a file's bytes are: in which chunk, and where in it.
struct InnoData {
	uint32_t firstSlice = 0, lastSlice = 0;
	uint32_t chunkOffset = 0; // from the data offset
	uint64_t chunkSize = 0;   // compressed size of the chunk
	uint64_t fileOffset = 0;  // in the uncompressed chunk
	uint64_t fileSize = 0;
	std::array<uint8_t, 20> sha1{};
	int64_t fileTime = 0;     // Windows FILETIME
	bool compressed = false;  // the chunk is compressed (else stored)
	bool encrypted = false;
	bool callFilter = false;  // x86 CALL/JMP addresses were made relative before compression
};

class InnoSetup {
public:
	// Reads the offset table and the setup headers. False with error() set
	// when the file isn't an installer we can read.
	bool open(const std::filesystem::path &path);
	const std::string &error() const { return _error; }

	const std::filesystem::path &path() const { return _path; }
	const std::string &versionString() const { return _versionString; } // "5.5.7 (unicode)"
	uint32_t version() const { return _version; } // innoVersion(5, 5, 7)
	bool unicode() const { return _unicode; }
	const std::string &appName() const { return _appName; }
	const std::string &appVersion() const { return _appVersion; }
	const std::string &publisher() const { return _publisher; }
	InnoCompression compression() const { return _compression; }
	uint64_t dataOffset() const { return _dataOffset; }
	const std::vector<std::string> &languages() const { return _languages; }
	const std::vector<InnoFile> &files() const { return _files; }
	const std::vector<InnoData> &data() const { return _data; }

	// The path a file entry is extracted to, relative and with '/': "{app}\a\b"
	// becomes "a/b" (or "app/a/b" as innoextract names it, with
	// `keepApp`), other folders keep their name ("{tmp}\x" -> "tmp/x").
	// Empty for names that leave the folder.
	static std::string relativePath(const std::string &destination, bool keepApp = false);

	static constexpr uint32_t innoVersion(unsigned a, unsigned b, unsigned c) { return (a << 24) | (b << 16) | (c << 8); }

private:
	bool fail(const std::string &message);
	bool readOffsetTable(std::ifstream &in, uint32_t &headerOffset);
	bool readHeaders(std::ifstream &in, uint32_t headerOffset);
	void assembleGalaxyFiles();

	std::filesystem::path _path;
	std::string _error;
	std::string _versionString;
	uint32_t _version = 0;
	bool _unicode = false;
	std::string _appName, _appVersion, _publisher;
	InnoCompression _compression = InnoCompression::Stored;
	uint64_t _dataOffset = 0;
	std::vector<std::string> _languages;
	std::vector<InnoFile> _files;
	std::vector<InnoData> _data;
};

// The uncompressed bytes of one chunk, read in order.
class InnoChunkReader : public ByteSource {
public:
	// Opens the chunk that holds `entry`. `maxOutput` (the end of the last
	// file wanted from it) limits the decoder's memory.
	bool open(const InnoSetup &setup, const InnoData &entry, uint64_t maxOutput);
	size_t read(uint8_t *dst, size_t size) override;
	bool skip(uint64_t size);
	uint64_t position() const { return _position; }
	const std::string &error() const { return _error; }

private:
	// The chunk's compressed bytes.
	struct Raw : ByteSource {
		std::ifstream file;
		uint64_t left = 0;
		bool failed = false;
		size_t read(uint8_t *dst, size_t size) override;
	};
	Raw _raw;
	std::unique_ptr<LzmaDecoder> _lzma;
	bool _stored = false;
	uint64_t _position = 0;
	std::string _error;
};

// Extracts data entries to files, a few megabytes per step() call.
class InnoExtractor {
public:
	struct Item {
		uint32_t file = 0;                          // index into setup.files()
		std::vector<std::filesystem::path> outputs; // files to write (none: check only)
		std::string name;                           // for messages
	};

	// `items` may come in any order; each is read once from its chunk,
	// chunks in the order they are stored.
	bool begin(const InnoSetup &setup, std::vector<Item> items);
	// About `budget` bytes of work (uncompressed bytes decoded); false
	// when everything is done or something failed.
	bool step(uint64_t budget);

	bool done() const { return _done; }
	bool failed() const { return !_error.empty(); }
	const std::string &error() const { return _error; }
	uint64_t total() const { return _total; }       // bytes to decode in all
	uint64_t progress() const { return _progress; } // bytes decoded so far
	std::string current() const;                    // the file being extracted
	size_t filesDone() const { return _filesDone; }

private:
	bool fail(const std::string &message);
	const InnoFile &file() const { return _setup->files()[_items[_next].file]; }
	bool startItem();
	bool startPart();
	bool finishPart();
	bool finishItem();
	void write(const uint8_t *data, size_t size);

	const InnoSetup *_setup = nullptr;
	std::vector<Item> _items;
	size_t _next = 0;        // next item
	bool _inItem = false, _inPart = false;
	size_t _part = 0;        // of the current item
	uint64_t _partLeft = 0;  // bytes of the part still to read
	std::vector<std::unique_ptr<std::ofstream>> _out;
	Sha1 _sha1;              // of the part, as stored
	Md5 _md5;                // of the file (GOG Galaxy files)
	std::vector<uint8_t> _deflated, _inflated;
	// The x86 call filter's state.
	uint32_t _filterOffset = 0;
	int _filterHave = -1; // address bytes collected, -1 when not in an address
	uint8_t _filterBuf[4] = {};
	std::vector<uint8_t> _buf, _filtered;

	InnoChunkReader _chunk;
	bool _chunkOpen = false;
	uint32_t _chunkSlice = 0, _chunkOffset = 0;

	uint64_t _total = 0, _progress = 0;
	size_t _filesDone = 0;
	bool _done = false;
	std::string _error;
};

} // namespace gob
