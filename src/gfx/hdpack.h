// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HD packs: upscaled replacements for the game's pictures (docs/hdpacks.md).
//
// A pack is a folder (normally <pref dir>/HDPacks/<name>/) laid out like
// gobexport's output:
//
//   <TOT>/<resid>.png        the upscaled picture, any PNG format; its size
//                            must be the original's times 1, 2, 3, 4 or 8
//                            (1: a recoloured picture)
//   <TOT>/<resid>.mask.png   optional: an upscaled transparency mask (white
//                            = opaque), same size as the picture
//
// Other files (gobexport's .json files, README.txt, index.json, pack.txt)
// are ignored. The pack keeps every picture in full colour (RGBA8) and
// rebuilds its alpha from the original 1x picture: colour 0, which the game
// draws transparently, becomes alpha 0, enlarged with nearest neighbour (or
// edge-smoothed, or from the pack's own mask when there is one). Pictures
// the game draws without transparency (backgrounds) still get that alpha;
// a renderer just ignores it for opaque blits.
//
// Lookups decode lazily and keep the decoded images in an LRU cache with a
// memory cap. The class is self-contained (no SDL, no GL) and thread-safe.

#pragma once

#include "data/datafiles.h"
#include "formats/tot.h"
#include "gfx/image.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gob {

struct HdKey {
	std::string tot; // upper case, e.g. "GOB00"
	int resId = 0;

	bool operator<(const HdKey &o) const { return tot != o.tot ? tot < o.tot : resId < o.resId; }
	bool operator==(const HdKey &o) const { return tot == o.tot && resId == o.resId; }
	std::string str() const { return tot + "/" + std::to_string(resId); }
};

// A decoded HD picture.
struct HdImage {
	HdKey key;        // the picture asked for
	HdKey source;     // the pack file used (another, identical picture's for a fallback)
	int width = 0, height = 0;         // = original size * scale
	int origWidth = 0, origHeight = 0; // the 1x picture
	int scale = 1;
	bool maskFromPack = false; // alpha from the pack's <resid>.mask.png
	std::vector<uint8_t> rgba; // width * height * 4, top row first

	size_t bytes() const { return rgba.size(); }
};

struct HdIssue {
	enum Severity { Info, Warning, Error };
	Severity severity = Warning;
	std::string file; // relative to the pack folder ("" for the pack as a whole)
	std::string message;
};

enum class HdAlpha {
	Nearest, // the original mask enlarged with nearest neighbour (exact game silhouettes)
	Smooth,  // the original mask enlarged with the edge smoothing of gfx/smooth.h
};

struct HdPackOptions {
	HdAlpha alpha = HdAlpha::Nearest;
	bool usePackMasks = true;      // use <resid>.mask.png when present
	uint8_t maskThreshold = 128;   // a pack mask is opaque from this value up
	bool identicalFallback = true; // a picture missing from the pack uses the
	                               // HD version of an identical one (same pixels)
	size_t memoryCap = size_t(256) << 20; // decoded bytes kept in the cache
};

class HdPack {
public:
	// The original 1x picture `resId` of script file `tot`, or nothing when
	// the game has no such picture.
	using OriginalProvider = std::function<std::optional<Image>(const std::string &tot, int resId)>;

	using Alpha = HdAlpha;
	using Options = HdPackOptions;

	static constexpr int kScales[] = {1, 2, 3, 4, 8};

	HdPack() = default;
	~HdPack();
	HdPack(const HdPack &) = delete;
	HdPack &operator=(const HdPack &) = delete;

	// Scans `dir` (no decoding) and checks every picture's size against its
	// original (reads PNG headers only). Returns false when the folder can't
	// be read; problems with single files go to issues().
	bool open(const std::filesystem::path &dir, OriginalProvider originals, const Options &options = Options());
	void close();
	bool isOpen() const;

	std::string name() const;              // the folder name
	std::filesystem::path dir() const;

	// Problems found by open() and by later decodes (corrupt files).
	std::vector<HdIssue> issues() const;
	int errorCount() const;

	// The scale most pictures use (0 when the pack has no usable picture);
	// pictures at other scales are still usable (HdImage::scale says theirs).
	int scale() const;
	bool mixedScales() const;
	// The largest scale any picture uses (the HD compositor runs at it).
	int maxScale() const;

	// Usable pictures (right size, original exists).
	std::vector<HdKey> keys() const;
	size_t size() const;
	bool contains(const HdKey &key) const;

	// The HD picture for (tot, resId), decoded on first use; nullptr when
	// the pack has none (or it failed to decode). The returned image stays
	// valid while held even if the cache drops it. Decoding runs outside
	// the lock: other lookups go on meanwhile, and a lookup of a picture
	// being decoded waits for that decode.
	std::shared_ptr<const HdImage> find(const std::string &tot, int resId);

	// Decodes every picture of script file `tot` on background threads
	// (into the cache, within the memory cap), e.g. when a room starts
	// loading its pictures.
	void prefetch(const std::string &tot);
	static constexpr int kPrefetchThreads = 4;

	void setOptions(const Options &options); // drops the cache
	Options options() const;

	struct Stats {
		size_t cachedImages = 0, cachedBytes = 0;
		size_t decodes = 0, hits = 0, misses = 0, evictions = 0, fallbacks = 0;
	};
	Stats stats() const;
	void clearCache();

	// Sub-folders of `root` (the HDPacks folder), sorted.
	static std::vector<std::string> list(const std::filesystem::path &root);

	// Builds the RGBA picture from a decoded HD image (RGBA8, `w` x `h`) and
	// its original: checks the scale and replaces alpha. `mask` is an
	// optional decoded pack mask (RGBA8, same size). Exposed for tests.
	static bool compose(const Image &original, const uint8_t *rgba, int w, int h, const uint8_t *mask,
	                    const Options &options, HdImage &out, std::string *error);

	// Integer scale of a `w` x `h` picture over a `ow` x `oh` original: 1, 2,
	// 3, 4 or 8, or 0 if it is none of them (or the aspect differs).
	static int scaleOf(int w, int h, int ow, int oh);

private:
	struct Entry {
		std::filesystem::path image, mask; // mask empty when absent
		int width = 0, height = 0, scale = 0;
		int origWidth = 0, origHeight = 0;
		uint32_t origHash = 0;
		bool failed = false;
	};
	struct Cached {
		std::shared_ptr<const HdImage> image;
		std::list<HdKey>::iterator lru;
	};

	void evict();
	void prefetchWorker();
	void addIssue(HdIssue::Severity s, const std::string &file, const std::string &message);

	mutable std::mutex _mutex;
	std::filesystem::path _dir;
	OriginalProvider _originals;
	Options _options;
	std::map<HdKey, Entry> _entries;
	std::multimap<uint32_t, HdKey> _byHash; // original pixel hash -> pack picture
	std::vector<HdIssue> _issues;
	int _scale = 0, _maxScale = 0;
	bool _mixed = false;
	bool _open = false;

	std::map<HdKey, Cached> _cache;
	std::list<HdKey> _lru; // most recent first
	std::map<HdKey, std::optional<HdKey>> _fallbackOf; // resolved lookups of pictures not in the pack
	Stats _stats;

	uint64_t _generation = 0; // bumped by open/close/setOptions: decodes started before are dropped
	std::map<HdKey, std::shared_future<std::shared_ptr<const HdImage>>> _inflight;
	std::deque<HdKey> _queue; // prefetch
	std::vector<std::thread> _workers;
	std::condition_variable _queueCv;
	bool _stopWorkers = false;
};

// Original pictures from the game files: decodes resources of the TOT/EXT
// files on request, keeping each loaded TotFile. Usable as an
// HdPack::OriginalProvider. Thread-safe.
class GameImages {
public:
	explicit GameImages(std::filesystem::path gameDir);
	std::optional<Image> operator()(const std::string &tot, int resId);

private:
	std::mutex _mutex;
	DataFiles _files;
	std::map<std::string, std::unique_ptr<TotFile>> _tots; // null: failed to load
};

} // namespace gob
