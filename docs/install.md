# Installing the games from their GOG installers

GOG sells Gobliiins, Gobliins 2 and Goblins Quest 3 as Windows installers
(`setup_gobliiins_1.02_(20270).exe` and so on). gobnative reads them as
data, without running them and without other tools: `gobinstall` on the
command line, and the app's game library (README, "Installing from GOG"). This page
describes the format as far as we read it and what is installed.

Code: `src/install/` (`lzma.*`, `hash.*`, `inno.*`, `gameinstall.*`),
`tools/gobinstall.cpp`; tests `tests/test_install.cpp`,
`tests/check_install.sh`.

The format reference was [innoextract](https://github.com/dscharrer/innoextract)
by Daniel Scharrer (zlib licence), read but not copied; the LZMA decoder
follows Igor Pavlov's LZMA specification from the LZMA SDK (public domain).
innoextract also served as the ground truth for the tests.

## 1. What is installed

| Game | Installed | Skipped |
|---|---|---|
| Gobliiins | `INTRO.STK`, `GOB.LIC`, `Track1.mp3`, `Manual.pdf` (49 MB) | |
| Gobliins 2 | `INTRO.STK`, `GOBNEW.LIC`, `Track1.mp3`, `Manual.pdf` (54 MB) | |
| Goblins Quest 3 | `INTRO.STK`, `TOT.STK`, `EXT.STK`, `IMD.ITK`, `USGOB3.ITK`, `MDOS.IMD`, `MUS_GOB3.LIC`, `Track1.mp3`, `Manual.pdf` (151 MB) | |
| all three | `FDD/` (the floppy version) only with `--floppy` | ScummVM (`scummvm/`), GOG's files (`goggame-*`, `support.txt`, `webcache.zip`, `__support/`, `__redist/`, `app/`, icons), the installer's own files (`{tmp}`, `{commonappdata}`), programs and DLLs |

The game is identified from the installer's file list before anything is
extracted (`identifyInstaller()`): `TOT.STK` + `EXT.STK` + one of
`USGOB3.ITK`/`MUS_GOB3.LIC`/`IMD.ITK` is Goblins Quest 3, `INTRO.STK` +
`GOBNEW.LIC` Gobliins 2, `INTRO.STK` + `GOB.LIC` Gobliiins. The files are
extracted into `<destination>.partial/`; when all checksums match, the
game detection (`detectGame()`, which opens the archives) must find the
same game there, and only then is the folder renamed to its destination.
On any error or a cancel the temporary folder is removed; nothing else is
deleted. When an installer has the same file for several languages, the
English one is taken (the three GOG installers have only English files;
Goblins Quest 3's installer lists four languages but no language files).

    build/gobinstall setup.exe [destdir]     # destdir: ./Gobliiins, ./Gobliins2 or ./Goblins3
    build/gobinstall setup.exe --list        # the installer's files and what would be installed
    build/gobinstall setup.exe dir --floppy  # with FDD/
    build/gobinstall setup.exe --check       # decompress and verify everything, write nothing
    build/gobinstall setup.exe dir --all     # every file, named as innoextract names them

Times on an M-series Mac: 1.1 s (Gobliiins), 1.2 s (Gobliins 2), 2.8 s
(Goblins Quest 3).

## 2. The installer

A single-file Inno Setup installer is a Windows program followed by the
setup data. All numbers are little-endian.

**Offset table.** Since Inno Setup 5.1.5 it is the program's resource
RCDATA #11111 (found through the PE header's resource directory: type 10,
name 11111, the first language). 44 bytes: the magic
`rDlPtS\xcd\xe6\xd7{\x0b*` (or `nS5W7dT\x83\xaa\x1b\x0fj`), u32 revision
(1), u32 unused, u32 offset/size/CRC of the setup program (unused here),
**u32 header offset**, **u32 data offset**, u32 CRC32 of the 40 bytes
before. A data offset of 0 means the files are in separate
`setup-1.bin`... parts (not supported; GOG's smaller games don't have
them).

**Setup headers** at the header offset (in GOG's installers near the end
of the file): a 64-byte version string, `Inno Setup Setup Data (5.5.7)
(u)` ("(u)" = Unicode: strings are UTF-16LE), then two **blocks**. A
block is u32 CRC32 (of the next 5 bytes), u32 stored size, u8 compressed
(LZMA since 4.1.6), then the stored bytes in pieces of up to 4096 bytes,
each after its own CRC32. Compressed blocks hold a 5-byte LZMA header
(the lc/lp/pb byte, u32 dictionary size) and a raw LZMA stream; the
decoder is pulled for as many bytes as the parser needs, so the stream's
end needs no marker.

Block 1: the setup header, then the language, message, permission, type,
component, task, directory, **file**, icon, ini, registry, delete, run
entries and the wizard images. We read up to the file entries and skip
the rest (the next block starts at the block's start + stored size).
Fields are strings (u32 byte length + bytes), integers, one-byte enums,
and flag sets (one byte per 8 flags; 3 bytes are padded to 4). Which
fields exist depends on the version; `InnoSetup::readHeaders()` has the
layout for 5.3.9 to 6.3 (the ranges innoextract documents), tested with
5.5.7. Things worth knowing:

- The setup header ends with the compression method (0 stored, 1 zlib,
  2 bzip2, 3 LZMA, 4 LZMA2; GOG uses LZMA2) and the option flags, whose
  "encryption used" bit we check (encrypted installers are refused).
- Every item has a Windows version range: 20 bytes (minimum and "only
  below", each (u16 build, u8 minor, u8 major) for Windows and NT, and
  u8 minor, u8 major of the NT service pack).
- Every item has six condition strings: components, tasks, languages,
  check, after install, before install. GOG keeps its own information in
  the last three (below).
- A file entry: source, destination (`{app}\INTRO.STK`), font name,
  assembly name, the conditions, the version range, **u32 data entry
  index** (the "location"), attributes, u64 external size, i16
  permission, 32 flags, u8 type.
- "5.5.7" is ambiguous (5.6.0 didn't always raise the number): when the
  5.5.7 layout doesn't parse, the 5.6.0 layout is tried.

Block 2: the **data entries**: u32 first and last slice, u32 chunk offset
(from the data offset), u64 offset in the uncompressed chunk, u64 size,
u64 compressed chunk size, SHA-1 (20 bytes, since 5.3.9) of the file as
stored, Windows FILETIME, u64 file version, flags (11 bits in 5.5.7:
bit 4 call instruction filter, bit 6 encrypted, bit 7 compressed).

**Chunks** at the data offset + chunk offset: `zlb\x1a`, then the
compressed size of bytes: for LZMA a 5-byte header and a raw LZMA stream,
for LZMA2 one dictionary byte (size `(2 | (b & 1)) << (b / 2 + 11)`) and
a raw LZMA2 stream. A chunk holds several files back to back (solid
compression), so a file in the middle needs everything before it
decoded; the extractor reads the wanted files in storage order, each
chunk once, and stops decoding a chunk after the last wanted file.

**x86 call filter** (files with flag bit 4; Inno Setup 5.3.9 and later):
after an `E8`/`E9` byte whose 4 address bytes don't cross a 64 KB block,
if the address's top byte is 00 or FF, the low 24 bits were made relative
(minus the position after the address, mod 2^24) and the top byte
inverted when bit 23 of the result is set. GOG's `uninstall.dll` uses it.

## 3. GOG Galaxy parts

GOG's installers store most files under temporary names (`{tmp}\30\76\
3076fd...`), each in parts of up to 10 MB that are zlib streams, and a
script inside the installer puts them together. The file entries say how:

- the first part's "before install" condition is
  `before_install('<md5 of the file>', '<real name>', <number of parts>)`
  (or `before_install_dependency(...)`), for example
  `before_install('...', 'Track1.mp3', 4)`;
- every part's "after install" condition is
  `after_install('<md5 of the part>', <stored size>, <size>)`;
- "check" is `check_if_install('en-US#', '32#64#', ...)`: the languages
  (used to prefer English files) and architectures.

`InnoSetup::assembleGalaxyFiles()` gives the first part's entry the real
name (relative to the game folder, `\` as separator), the list of parts
and the MD5, and marks the other parts' entries as merged. Extracting
checks each part's SHA-1 (as stored), inflates it (`zlibDecompress()`
from the PNG reader, with its Adler-32 check), and checks the whole
file's MD5. Files outside this scheme (`{app}\webcache.zip`, the support
DLL) are extracted as they are.

## 4. Tests

- `test_install` (in the unit test set): SHA-1 and MD5 vectors, LZMA and
  LZMA2 streams made with Python's `lzma` module (literals, matches,
  `lp` bits, a stored LZMA2 chunk, decoding 1 byte at a time), truncated
  and damaged streams, path mapping, files that aren't installers. With
  the folder of the installers (`test_install DIR` or `GOB_SETUP_DIR`)
  it also opens the three installers, identifies the games, checks the
  plans and extracts a small file in 1 KB steps.
- `tests/check_install.sh DIR`: for each installer found, every file
  extracted with `gobinstall --all` must be identical to innoextract's
  extraction (149, 147 and 160 files, all of them), and the game
  installed by gobinstall must equal innoextract's files. Skips without
  innoextract or the installers.

## 5. Limits

- Single-file installers only (no `setup-N.bin` slices), Inno Setup
  5.3.9 to 6.3, no encryption, LZMA/LZMA2 or stored chunks (no zlib or
  bzip2 chunks, which GOG's installers of this era don't use).
- A Galaxy part is inflated in one piece (up to 10 MB), which takes a few
  tens of milliseconds; the desktop app extracts on a worker thread, the
  web build in steps of about 20 ms per frame, with a short pause per
  part (README, "Web").
- Timestamps of the files aren't restored.
