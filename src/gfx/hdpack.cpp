// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/hdpack.h"

#include "gfx/png.h"
#include "gfx/smooth.h"

#include <algorithm>
#include <cctype>

namespace gob {

namespace fs = std::filesystem;

namespace {

std::string upper(std::string s) {
	for (char &c : s)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

bool endsWith(const std::string &s, const std::string &suffix) {
	return s.size() >= suffix.size() &&
	       std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
	                  [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
}

// Resource ids are 0..65535 written in decimal.
bool parseId(const std::string &s, int &id) {
	if (s.empty() || s.size() > 5)
		return false;
	int v = 0;
	for (char c : s) {
		if (c < '0' || c > '9')
			return false;
		v = v * 10 + (c - '0');
	}
	if (v > 65535)
		return false;
	id = v;
	return true;
}

uint32_t pixelHash(const Image &img) {
	// FNV-1a over size and pixels.
	uint32_t h = 2166136261u;
	auto mix = [&](uint8_t b) { h = (h ^ b) * 16777619u; };
	for (int v : {img.width, img.height})
		for (int i = 0; i < 4; i++)
			mix(uint8_t(v >> (i * 8)));
	for (uint8_t p : img.pixels)
		mix(p);
	return h;
}

std::string relPath(const fs::path &p, const fs::path &base) {
	std::error_code ec;
	fs::path r = fs::relative(p, base, ec);
	return (ec ? p : r).generic_string();
}

} // namespace

int HdPack::scaleOf(int w, int h, int ow, int oh) {
	if (ow <= 0 || oh <= 0 || w % ow || h % oh)
		return 0;
	int s = w / ow;
	if (h / oh != s)
		return 0;
	for (int k : kScales)
		if (k == s)
			return s;
	return 0;
}

bool HdPack::compose(const Image &original, const uint8_t *rgba, int w, int h, const uint8_t *mask,
                     const Options &options, HdImage &out, std::string *error) {
	const int s = scaleOf(w, h, original.width, original.height);
	if (!s) {
		if (error)
			*error = "size " + std::to_string(w) + "x" + std::to_string(h) + " is not 1, 2, 3, 4 or 8 times " +
			         std::to_string(original.width) + "x" + std::to_string(original.height);
		return false;
	}
	out.width = w;
	out.height = h;
	out.origWidth = original.width;
	out.origHeight = original.height;
	out.scale = s;
	out.rgba.assign(rgba, rgba + size_t(w) * size_t(h) * 4);
	out.maskFromPack = mask != nullptr;

	if (mask) {
		for (size_t i = 0, n = size_t(w) * size_t(h); i < n; i++) {
			// White = opaque; use the brightest channel, times the mask's own
			// alpha (a mask saved with transparency).
			const uint8_t *m = mask + i * 4;
			int v = std::max({m[0], m[1], m[2]}) * m[3] / 255;
			out.rgba[i * 4 + 3] = v >= options.maskThreshold ? 255 : 0;
		}
		return true;
	}

	std::vector<uint8_t> opaque(original.pixels.size());
	for (size_t i = 0; i < opaque.size(); i++)
		opaque[i] = original.pixels[i] ? 255 : 0;
	const std::vector<uint8_t> alpha = options.alpha == Alpha::Smooth
	                                       ? smoothUpscaleMask(opaque.data(), original.width, original.height, s)
	                                       : nearestUpscale(opaque.data(), original.width, original.height, s);
	for (size_t i = 0; i < alpha.size(); i++)
		out.rgba[i * 4 + 3] = alpha[i];
	return true;
}

void HdPack::addIssue(HdIssue::Severity s, const std::string &file, const std::string &message) {
	_issues.push_back({s, file, message});
}

bool HdPack::open(const fs::path &dir, OriginalProvider originals, const Options &options) {
	std::lock_guard<std::mutex> lock(_mutex);
	_generation++;
	_inflight.clear();
	_queue.clear();
	_dir = dir;
	_originals = std::move(originals);
	_options = options;
	_entries.clear();
	_byHash.clear();
	_issues.clear();
	_cache.clear();
	_lru.clear();
	_fallbackOf.clear();
	_stats = Stats();
	_scale = _maxScale = 0;
	_mixed = false;
	_open = false;

	std::error_code ec;
	if (!fs::is_directory(dir, ec)) {
		addIssue(HdIssue::Error, "", "the pack folder doesn't exist");
		return false;
	}

	// Pass 1: pictures; pass 2: masks (attached to their pictures).
	std::vector<fs::path> masks;
	std::map<int, int> scaleCount;
	for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
	     !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		std::error_code fileEc;
		if (!it->is_regular_file(fileEc))
			continue;
		const fs::path p = it->path();
		const std::string rel = relPath(p, dir);
		const std::string file = p.filename().string();
		if (file.empty() || file[0] == '.')
			continue; // .DS_Store and the like
		const fs::path relp = fs::relative(p, dir, fileEc);
		const int depth = int(std::distance(relp.begin(), relp.end()));
		if (!endsWith(file, ".png")) {
			if (!(endsWith(file, ".json") || endsWith(file, ".txt") || endsWith(file, ".md") || endsWith(file, ".ini")))
				addIssue(HdIssue::Info, rel, "ignored (not a PNG file)");
			continue;
		}
		if (depth != 2) {
			addIssue(HdIssue::Warning, rel, "ignored: pictures go in <TOT>/<id>.png");
			continue;
		}
		if (endsWith(file, ".mask.png")) {
			masks.push_back(p);
			continue;
		}
		HdKey key;
		key.tot = upper(p.parent_path().filename().string());
		if (!parseId(p.stem().string(), key.resId)) {
			addIssue(HdIssue::Warning, rel, "ignored: the file name is not a resource number");
			continue;
		}
		PngInfo info;
		std::string err;
		if (!readPngInfo(p, info, &err)) {
			addIssue(HdIssue::Error, rel, "can't read: " + err);
			continue;
		}
		std::optional<Image> orig = _originals ? _originals(key.tot, key.resId) : std::nullopt;
		if (!orig) {
			addIssue(HdIssue::Error, rel, "the game has no picture " + key.str());
			continue;
		}
		const int s = scaleOf(info.width, info.height, orig->width, orig->height);
		if (!s) {
			addIssue(HdIssue::Error, rel,
			         "wrong size " + std::to_string(info.width) + "x" + std::to_string(info.height) +
			             ": must be 1, 2, 3, 4 or 8 times the original " + std::to_string(orig->width) + "x" +
			             std::to_string(orig->height));
			continue;
		}
		Entry e;
		e.image = p;
		e.width = info.width;
		e.height = info.height;
		e.scale = s;
		e.origWidth = orig->width;
		e.origHeight = orig->height;
		e.origHash = pixelHash(*orig);
		_entries[key] = e;
		_byHash.insert({e.origHash, key});
		scaleCount[s]++;
	}
	if (ec)
		addIssue(HdIssue::Error, "", "can't read the folder: " + ec.message());

	for (const fs::path &p : masks) {
		const std::string rel = relPath(p, dir);
		std::string stem = p.filename().string();
		stem = stem.substr(0, stem.size() - std::string(".mask.png").size());
		HdKey key;
		key.tot = upper(p.parent_path().filename().string());
		if (!parseId(stem, key.resId)) {
			addIssue(HdIssue::Warning, rel, "ignored: the file name is not a resource number");
			continue;
		}
		auto it = _entries.find(key);
		if (it == _entries.end()) {
			addIssue(HdIssue::Info, rel, "ignored: a mask without its picture");
			continue;
		}
		PngInfo info;
		std::string err;
		if (!readPngInfo(p, info, &err)) {
			addIssue(HdIssue::Warning, rel, "mask ignored, can't read: " + err);
			continue;
		}
		if (info.width == it->second.origWidth && info.height == it->second.origHeight)
			continue; // the exported 1x mask: the same as the rebuilt one
		if (info.width != it->second.width || info.height != it->second.height) {
			addIssue(HdIssue::Warning, rel,
			         "mask ignored: " + std::to_string(info.width) + "x" + std::to_string(info.height) +
			             " differs from its picture's " + std::to_string(it->second.width) + "x" +
			             std::to_string(it->second.height));
			continue;
		}
		it->second.mask = p;
	}

	int best = 0, bestCount = 0;
	for (auto [s, n] : scaleCount)
		if (n > bestCount) {
			best = s;
			bestCount = n;
		}
	_scale = best;
	_maxScale = scaleCount.empty() ? 0 : scaleCount.rbegin()->first;
	_mixed = scaleCount.size() > 1;
	if (_mixed) {
		std::string list;
		for (auto [s, n] : scaleCount)
			list += (list.empty() ? "" : ", ") + std::to_string(n) + " at " + std::to_string(s) + "x";
		addIssue(HdIssue::Info, "", "mixed scales (" + list + "): shown at " + std::to_string(_maxScale) +
		                                "x, the others enlarged");
	}
	if (_entries.empty())
		addIssue(HdIssue::Warning, "", "no usable pictures");
	_open = true;
	return true;
}

void HdPack::close() {
	std::lock_guard<std::mutex> lock(_mutex);
	_open = false;
	_generation++;
	_inflight.clear();
	_queue.clear();
	_entries.clear();
	_byHash.clear();
	_issues.clear();
	_cache.clear();
	_lru.clear();
	_fallbackOf.clear();
	_originals = nullptr;
	_scale = _maxScale = 0;
	_mixed = false;
}

bool HdPack::isOpen() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _open;
}

std::string HdPack::name() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _dir.filename().string();
}

fs::path HdPack::dir() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _dir;
}

std::vector<HdIssue> HdPack::issues() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _issues;
}

int HdPack::errorCount() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return int(std::count_if(_issues.begin(), _issues.end(),
	                         [](const HdIssue &i) { return i.severity == HdIssue::Error; }));
}

int HdPack::scale() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _scale;
}

bool HdPack::mixedScales() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _mixed;
}

int HdPack::maxScale() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _maxScale;
}

std::vector<HdKey> HdPack::keys() const {
	std::lock_guard<std::mutex> lock(_mutex);
	std::vector<HdKey> out;
	for (const auto &[k, e] : _entries)
		if (!e.failed)
			out.push_back(k);
	return out;
}

size_t HdPack::size() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _entries.size();
}

bool HdPack::contains(const HdKey &key) const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _entries.count(key) != 0;
}

void HdPack::evict() {
	while (!_lru.empty() && _stats.cachedBytes > _options.memoryCap && _cache.size() > 1) {
		const HdKey victim = _lru.back();
		_lru.pop_back();
		auto it = _cache.find(victim);
		if (it != _cache.end()) {
			_stats.cachedBytes -= it->second.image->bytes();
			_cache.erase(it);
			_stats.evictions++;
		}
	}
	_stats.cachedImages = _cache.size();
}

std::shared_ptr<const HdImage> HdPack::find(const std::string &totName, int resId) {
	std::unique_lock<std::mutex> lock(_mutex);
	if (!_open)
		return nullptr;
	const HdKey key{upper(totName), resId};

	auto cached = _cache.find(key);
	if (cached != _cache.end()) {
		_lru.splice(_lru.begin(), _lru, cached->second.lru);
		_stats.hits++;
		return cached->second.image;
	}
	if (auto busy = _inflight.find(key); busy != _inflight.end()) {
		auto pending = busy->second;
		lock.unlock();
		return pending.get();
	}

	// The pack's own picture, or (once resolved) an identical one's.
	HdKey source = key;
	auto entry = _entries.find(key);
	if (entry == _entries.end()) {
		if (!_options.identicalFallback) {
			_stats.misses++;
			return nullptr;
		}
		auto resolved = _fallbackOf.find(key);
		if (resolved == _fallbackOf.end()) {
			std::optional<HdKey> found;
			if (std::optional<Image> orig = _originals ? _originals(key.tot, key.resId) : std::nullopt) {
				const uint32_t h = pixelHash(*orig);
				for (auto [it, end] = _byHash.equal_range(h); it != end; ++it) {
					const Entry &e = _entries[it->second];
					if (!e.failed && e.origWidth == orig->width && e.origHeight == orig->height) {
						// Same hash and size: compare the pixels to be sure.
						std::optional<Image> other = _originals(it->second.tot, it->second.resId);
						if (other && other->pixels == orig->pixels) {
							found = it->second;
							break;
						}
					}
				}
			}
			resolved = _fallbackOf.emplace(key, found).first;
		}
		if (!resolved->second) {
			_stats.misses++;
			return nullptr;
		}
		source = *resolved->second;
		entry = _entries.find(source);
		if (entry == _entries.end()) {
			_stats.misses++;
			return nullptr;
		}
		_stats.fallbacks++;
	}
	if (entry->second.failed) {
		_stats.misses++;
		return nullptr;
	}

	// Decode without the lock.
	const Entry e = entry->second;
	const Options options = _options;
	const OriginalProvider originals = _originals;
	const fs::path dir = _dir;
	const uint64_t generation = _generation;
	std::promise<std::shared_ptr<const HdImage>> promise;
	_inflight[key] = promise.get_future().share();
	lock.unlock();

	std::shared_ptr<HdImage> img;
	std::string err, maskWarning;
	bool failed = false;
	std::optional<Image> original = originals ? originals(source.tot, source.resId) : std::nullopt;
	if (original) {
		PngImage png, mask;
		if (!readPng(e.image, png, &err)) {
			failed = true;
			err = "can't decode: " + err;
		} else {
			bool haveMask = false;
			if (options.usePackMasks && !e.mask.empty()) {
				std::string merr;
				if (readPng(e.mask, mask, &merr) && mask.info.width == png.info.width &&
				    mask.info.height == png.info.height)
					haveMask = true;
				else
					maskWarning = "mask ignored: " + (merr.empty() ? "wrong size" : merr);
			}
			img = std::make_shared<HdImage>();
			if (!compose(*original, png.rgba.data(), png.info.width, png.info.height,
			             haveMask ? mask.rgba.data() : nullptr, options, *img, &err)) {
				failed = true;
				img.reset();
			} else {
				img->key = key;
				img->source = source;
			}
		}
	}

	lock.lock();
	if (generation == _generation) {
		_inflight.erase(key);
		if (!maskWarning.empty())
			addIssue(HdIssue::Warning, relPath(e.mask, dir), maskWarning);
		if (failed) {
			if (auto it = _entries.find(source); it != _entries.end())
				it->second.failed = true;
			addIssue(HdIssue::Error, relPath(e.image, dir), err);
		}
		if (img) {
			_stats.decodes++;
			_lru.push_front(key);
			_cache[key] = Cached{img, _lru.begin()};
			_stats.cachedBytes += img->bytes();
			evict();
		} else {
			_stats.misses++;
		}
	}
	lock.unlock();
	promise.set_value(img);
	return img;
}

void HdPack::prefetch(const std::string &totName) {
#ifdef __EMSCRIPTEN__
	(void)totName; // no threads in the web build: pictures are decoded when first drawn
	return;
#endif
	std::lock_guard<std::mutex> lock(_mutex);
	if (!_open)
		return;
	const std::string tot = upper(totName);
	for (const auto &[k, e] : _entries)
		if (k.tot == tot && !e.failed && !_cache.count(k) && !_inflight.count(k) &&
		    std::find(_queue.begin(), _queue.end(), k) == _queue.end())
			_queue.push_back(k);
	if (_queue.empty())
		return;
	while (int(_workers.size()) < kPrefetchThreads)
		_workers.emplace_back([this] { prefetchWorker(); });
	_queueCv.notify_all();
}

void HdPack::prefetchWorker() {
	std::unique_lock<std::mutex> lock(_mutex);
	while (true) {
		_queueCv.wait(lock, [this] { return _stopWorkers || !_queue.empty(); });
		if (_stopWorkers)
			return;
		const HdKey k = _queue.front();
		_queue.pop_front();
		lock.unlock();
		find(k.tot, k.resId);
		lock.lock();
	}
}

HdPack::~HdPack() {
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_stopWorkers = true;
		_queue.clear();
	}
	_queueCv.notify_all();
	for (std::thread &t : _workers)
		t.join();
}

void HdPack::setOptions(const Options &options) {
	std::lock_guard<std::mutex> lock(_mutex);
	_options = options;
	_generation++;
	_inflight.clear();
	_cache.clear();
	_lru.clear();
	_stats.cachedBytes = 0;
	_stats.cachedImages = 0;
}

HdPack::Options HdPack::options() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _options;
}

HdPack::Stats HdPack::stats() const {
	std::lock_guard<std::mutex> lock(_mutex);
	return _stats;
}

void HdPack::clearCache() {
	std::lock_guard<std::mutex> lock(_mutex);
	_cache.clear();
	_lru.clear();
	_stats.cachedBytes = 0;
	_stats.cachedImages = 0;
}

std::vector<std::string> HdPack::list(const fs::path &root) {
	std::vector<std::string> out;
	std::error_code ec;
	for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
		if (it->is_directory(ec) && !it->path().filename().string().empty() && it->path().filename().string()[0] != '.')
			out.push_back(it->path().filename().string());
	std::sort(out.begin(), out.end());
	return out;
}

GameImages::GameImages(fs::path gameDir) : _files(std::move(gameDir)) {
	_files.openAllArchives();
}

std::optional<Image> GameImages::operator()(const std::string &tot, int resId) {
	std::lock_guard<std::mutex> lock(_mutex);
	const std::string name = upper(tot);
	auto it = _tots.find(name);
	if (it == _tots.end()) {
		auto t = std::make_unique<TotFile>();
		if (!t->load(_files, name))
			t.reset();
		it = _tots.emplace(name, std::move(t)).first;
	}
	if (!it->second || resId < 0 || resId > 65535)
		return std::nullopt;
	auto res = it->second->resource(uint16_t(resId));
	if (!res || res->width <= 0 || res->height <= 0)
		return std::nullopt;
	return decodeSprite(*res);
}

} // namespace gob
