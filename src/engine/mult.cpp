// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound-effect slots, scenery loading and the mult (multi-object animation)
// system: per-tick object animation, keyframed sequences and the full-screen
// animation player. Behaviour follows docs/animation.md and
// docs/goblins.md section 3.

#include "engine/interpreter.h"

#include <algorithm>
#include <limits>

namespace gob {

namespace {

constexpr int kAnimDataStride = 40; // animDataSize (10) * 4 in every Gobliins 2 TOT

int16_t le16s(const std::vector<uint8_t> &d, size_t o) {
	return o + 1 < d.size() ? int16_t(d[o] | (d[o + 1] << 8)) : 0;
}

bool rectsOverlap(int l1, int t1, int r1, int b1, int l2, int t2, int r2, int b2) {
	return !(r1 < l2 || r2 < l1 || b1 < t2 || b2 < t1);
}

} // namespace

// ---------------------------------------------------------------------------
// Sound samples

int Interpreter::loadSoundSearch() {
	Script &s = script();
	int16_t id = s.readInt16();

	int slot = -1;
	int mask = 0;
	for (int i = 0; i < kSoundSlots; i++)
		if (_sounds[i].pcm && _sounds[i].id == id) {
			slot = i;
			mask = 0x8000; // shared: the owner that loaded it frees it
			break;
		}
	if (slot < 0)
		for (int i = kSoundSlots - 1; i >= 0; i--)
			if (!_sounds[i].pcm) {
				slot = i;
				break;
			}
	if (slot < 0) {
		trace("no free sound slot");
		return 0;
	}

	freeSoundSlot(slot);
	if (id == -1) {
		s.skip(9); // file name: never used by Gobliins 2
		return 0;
	}

	if (!loadSoundResource(slot, id))
		return 0;
	return slot | mask;
}

// Decodes SND resource `id` of the current TOT into `slot`.
bool Interpreter::loadSoundResource(int slot, int16_t id) {
	auto res = env().tot->resource(uint16_t(id));
	if (!res || res->data.size() < 6)
		return false;

	loadSoundData(slot, res->data, id);
	return true;
}

// SND: flag byte, 24-bit big-endian length, big-endian u16 rate, then
// signed 8-bit PCM. `snd` holds at least 6 bytes.
void Interpreter::loadSoundData(int slot, const std::vector<uint8_t> &d, int id) {
	size_t len = (size_t(d[1]) << 16) | (size_t(d[2]) << 8) | d[3];
	len = std::min(len, d.size() - 6);
	Sample smp;
	smp.pcm = std::make_shared<std::vector<uint8_t>>(d.begin() + 6, d.begin() + 6 + long(len));
	smp.rate = std::max(4700, (d[4] << 8) | d[5]);
	smp.id = id;
	_sounds[size_t(slot)] = std::move(smp);
}

void Interpreter::freeSoundSlot(int slot) {
	if (slot < 0 || slot >= kSoundSlots)
		return;
	_sounds[size_t(slot)] = Sample{};
}

void Interpreter::freeAllSounds() {
	blasterStop(0);
	for (int i = 0; i < kSoundSlots; i++)
		freeSoundSlot(i);
}

// Single sound-effect voice: a new sample is ignored while one plays, so
// callers stop first. repCount r plays max(r, 1) times, -1 loops; freq <= 0
// uses the sample's own rate; fades are in tenths of a second.
void Interpreter::blasterPlay(int slot, int repCount, int freq, int fadeLength) {
	if (slot < 0 || slot >= kSoundSlots || !_sounds[size_t(slot)].pcm || blasterPlaying())
		return;
	const Sample &smp = _sounds[size_t(slot)];
	int rate = freq > 0 ? freq : smp.rate;
	int repeat = repCount == -1 ? 0 : std::max(repCount, 1);
	if (_ffSkip) { // silent while skipping, but as long as it would play
		_ffSample.slot = slot;
		_ffSample.repCount = repCount;
		_ffSample.freq = freq;
		_ffSample.end = repeat == 0 ? std::numeric_limits<double>::infinity()
		                            : _gameMs + 1000.0 * double(smp.pcm->size()) * repeat / std::max(rate, 1);
		return;
	}
	_mixer.playSample(0, smp.pcm, rate, repeat, fadeLength * 100);
	trace("sound " + std::to_string(smp.id) + ": " + std::to_string(smp.pcm->size()) + " samples at " +
	      std::to_string(rate) + " Hz, x" + std::to_string(repeat));
}

void Interpreter::blasterStop(int fadeLength) {
	_ffSample.end = 0;
	_mixer.stopSample(0, fadeLength * 100);
}

// ---------------------------------------------------------------------------
// Scenery

Scenery::ResourceLoader Interpreter::resourceLoader() {
	return [this](uint16_t id) -> std::optional<Resource> {
		return env().tot ? env().tot->resource(id) : std::nullopt;
	};
}

// Reads an animation descriptor from the script and loads it. In search
// mode (from loadMult) an already loaded animation is reused. Returns the
// slot, plus 100 if this call loaded it.
int Interpreter::sceneryLoadAnim(bool search) {
	Script &s = script();
	int index = int(evalInt());
	int16_t picsCount = s.readInt16();
	int16_t resId = s.readInt16();

	if (search) {
		int found = _scenery.findAnim(resId);
		if (found >= 0) {
			s.skip(8 * _scenery.anim(found).pictCount);
			return found;
		}
		int free = _scenery.lowestFreeSlot();
		index = free >= 0 ? free : 9;
	}

	auto loader = resourceLoader();
	if (!_scenery.resourceExists(resId, loader)) {
		trace("animation resource " + std::to_string(resId) + " missing");
		return 0;
	}

	std::vector<Scenery::PictureDesc> pics;
	for (int i = 0; i < picsCount; i++) {
		Scenery::PictureDesc p;
		p.pieceTableId = s.readInt16();
		p.width = s.readInt16();
		p.height = s.readInt16();
		p.spriteResId = s.readInt16();
		pics.push_back(p);
	}
	_scenery.loadAnim(index, resId, pics, loader);
	return index + 100;
}

// ---------------------------------------------------------------------------
// Objects

int Interpreter::animField(int obj, AnimField f) const {
	uint8_t b = objVars(obj).read8(_objects[size_t(obj)].animData + uint32_t(f));
	switch (f) {
	case kLayer: case kFrame: case kNewLayer: case kIntersected: case kNewCycle:
	case kRedrawLayer: case kRedrawFrame:
		return b;
	default:
		return int8_t(b);
	}
}

void Interpreter::setAnimField(int obj, AnimField f, int v) {
	objVars(obj).write8(_objects[size_t(obj)].animData + uint32_t(f), uint8_t(v));
}

void Interpreter::resetAnimationState() {
	_objects.clear();
	_objCount = 0;
	_draw.freeSprite(draw::kAnimSurface);
	for (auto &md : _multDatas)
		md.reset();
	_multData = nullptr;
	_m1 = MultV1State{};
	_scenery.reset();
}

void Interpreter::initMult() {
	Script &s = script();
	int left = s.readInt16(), top = s.readInt16(), width = s.readInt16(), height = s.readInt16();
	int count = s.readInt16();
	VarRef posX = readVar(), posY = readVar(), animData = readVar();

	_multLeft = left;
	_multTop = top;
	_multWidth = width;
	_multHeight = height;
	_scenery.areaLeft = left;
	_scenery.areaTop = top;
	_scenery.areaWidth = width;
	_scenery.areaHeight = height;

	if (!_objects.empty() && int(_objects.size()) != count)
		_objects.clear();
	_objCount = count;

	if (_objects.empty()) {
		_objects.resize(size_t(count));
		for (int i = 0; i < count; i++) {
			MultObject &o = _objects[size_t(i)];
			o.posXSlot = posX.offset / 4 + uint32_t(i);
			o.posYSlot = posY.offset / 4 + uint32_t(i);
			o.animData = animData.offset + uint32_t(i * kAnimDataStride);
			setAnimField(i, kIsStatic, 1);
		}
	}

	auto &anim = _draw.sprite(draw::kAnimSurface);
	if (anim && (anim->width() != width || anim->height() != height))
		_draw.freeSprite(draw::kAnimSurface);
	if (!_draw.sprite(draw::kAnimSurface))
		_draw.createSprite(draw::kAnimSurface, width, height);

	// The anim surface keeps a clean copy of the background, used to erase
	// objects before they are redrawn.
	_draw.sprite(draw::kAnimSurface)->blit(_draw.back(), left, top, left + width - 1, top + height - 1, 0, 0);
}

void Interpreter::freeMult() {
	_objects.clear();
	_objCount = 0;
	_draw.freeSprite(draw::kAnimSurface);
}

void Interpreter::loadMultObject() {
	Script &s = script();
	int obj = evalVal();
	int x = evalVal();
	int y = evalVal();
	if (obj < 0 || obj >= int(_objects.size())) {
		for (int i = 0; i < 11; i++) {
			if (s.peekByte() == tok::Stop)
				s.skip(1);
			else
				evalVal();
		}
		return;
	}
	setObjPos(obj, x, y);
	for (int i = 0; i < 11; i++) {
		if (s.peekByte() == tok::Stop)
			s.skip(1);
		else
			setAnimField(obj, AnimField(i), evalVal());
	}

	int type = animField(obj, kAnimType);
	if ((type == 100 || type == 101) && obj < _gobsCount)
		goblinPlaceObject(obj, type);
}

// Key layer numbers count across all of the mult's animations.
void Interpreter::resolveGlobalLayer(const MultData &md, int globalLayer, int &animation, int &layer) {
	layer = globalLayer;
	animation = md.animIndices.empty() ? 0 : md.animIndices[0];
	for (size_t k = 0; k < md.animIndices.size(); k++) {
		animation = md.animIndices[k];
		int n = _scenery.layersCount(animation);
		if (layer < n || k + 1 == md.animIndices.size())
			break;
		layer -= n;
	}
}

// Parses a mult resource while consuming the matching script operands
// (docs/opcodes.md 8.6 for the layout, docs/animation.md 2.5 for meaning).
void Interpreter::loadMult() {
	Script &s = script();
	uint16_t raw = s.readUint16();
	int index = (raw & 0x8000) ? s.readByte() : 0;
	int resId = raw & 0x7FFF;
	index &= 7;

	auto res = env().tot->resource(uint16_t(resId));
	if (!res) {
		trace("mult resource " + std::to_string(resId) + " missing");
		return;
	}
	const std::vector<uint8_t> &d = res->data;
	size_t rp = 0;
	auto r8 = [&]() { return rp < d.size() ? int8_t(d[rp++]) : int8_t(0); };
	auto r16 = [&]() { int16_t v = le16s(d, rp); rp += 2; return v; };

	auto md = std::make_unique<MultData>();
	for (int slot = 0; slot < 4; slot++)
		for (int t = 0; t < 4; t++)
			md->animObjs[size_t(slot)][size_t(t)] = slot == 0 ? t : -1;

	uint8_t staticCount = uint8_t(r8() + 1);
	uint8_t animCount = uint8_t(r8() + 1);
	bool newFormat = staticCount & 0x80;
	staticCount &= 0x7F;

	for (int i = 0; i < staticCount; i++) {
		// Static scenery: never used by Gobliins 2; consume the descriptor.
		evalInt();
		int16_t backs = s.readInt16();
		s.skip(std::max<int16_t>(backs, 0) * 2);
		int16_t pics = s.readInt16();
		s.skip(2 + std::max<int16_t>(pics, 0) * 8);
		rp += 14;
	}
	for (int i = 0; i < animCount; i++) {
		int r = sceneryLoadAnim(true);
		md->animIndices.push_back(r >= 100 ? r - 100 : r);
		md->animLoaded.push_back(r >= 100);
		rp += 14;
	}

	r16(); // frame rate: unused when initMult ran first, as it always does here
	int16_t n = r16();
	for (int i = 0; i < n; i++) {
		int16_t f = r16(), l = r16();
		md->staticKeys.push_back({f, l});
	}
	for (int t = 0; t < 4; t++) {
		n = r16();
		for (int i = 0; i < n; i++) {
			MultData::AnimKey k;
			k.frame = r16();
			k.layer = r16();
			k.posX = r16();
			k.posY = r16();
			k.order = r16();
			md->animKeys[size_t(t)].push_back(k);
		}
	}
	rp += 240; // fade palettes
	rp += size_t(std::max<int16_t>(r16(), 0)) * 7;  // palette fade keys (unused)
	rp += size_t(std::max<int16_t>(r16(), 0)) * 80; // palette keys (unused)
	n = r16();
	for (int i = 0; i < n; i++) {
		int16_t f = r16(), c = r16();
		rp += newFormat ? 0 : 24;
		md->textKeys.push_back({f, c});
	}

	n = r16();
	std::vector<std::pair<uint16_t, int>> loaded; // resId -> soundIndex
	for (int i = 0; i < n; i++) {
		MultData::SoundKey k;
		k.frame = r16();
		k.cmd = r16();
		k.freq = r16();
		k.fadeLength = r16();
		k.repCount = r16();
		rp += 2 + (newFormat ? 0 : 24);
		k.soundIndex = -1;
		if (k.cmd == 1 || k.cmd == 4) {
			uint16_t id = s.peekUint16();
			auto it = std::find_if(loaded.begin(), loaded.end(), [&](auto &p) { return p.first == id; });
			if (it != loaded.end()) {
				s.skip(2);
				k.soundIndex = it->second;
			} else {
				int slot = loadSoundSearch();
				md->sndSlots.push_back(slot);
				k.soundIndex = slot & 0x7FFF;
				loaded.push_back({id, k.soundIndex});
			}
		} else if (k.cmd == 3) {
			s.skip(4);
		}
		md->soundKeys.push_back(k);
	}

	if (newFormat) {
		int16_t count = s.readInt16();
		for (int i = 0; i < count; i++)
			md->spriteList.push_back(s.readInt16());
		// TOT version 1.2 (Gobliins 2): nothing further is read. Version 1.3
		// (Goblins 3) adds the IMD keys (Mult_v2::loadImds).
		if (env().tot->header().versionMinor >= 3) {
			rp += size_t(std::max<int16_t>(r16(), 0)) * 20;
			int16_t files = s.readInt16();
			for (int i = 0; i < files; i++) {
				std::string name;
				for (int c = 0; c < 14; c++)
					if (char ch = char(s.readByte()); ch && name.size() == size_t(c))
						name.push_back(ch);
				md->imdFiles.push_back(name);
			}
			if (files > 0) {
				rp += 2;
				for (int t = 0; t < 4; t++) {
					n = r16();
					for (int i = 0; i < n; i++) {
						MultData::ImdKey k;
						k.frame = r16();
						k.imdFile = r16();
						k.field4 = r16();
						k.field6 = r16();
						k.flags = uint16_t(r16());
						k.palFrame = r16();
						k.lastFrame = r16();
						k.palStart = r8();
						k.palEnd = r8();
						md->imdKeys[size_t(t)].push_back(k);
					}
				}
			}
		}
	}

	_multDatas[size_t(index)] = std::move(md);
	_multData = _multDatas[size_t(index)].get();
}

void Interpreter::freeMultKeys(int index) {
	if (index < 0 || index >= 8 || !_multDatas[size_t(index)])
		return;
	MultData *md = _multDatas[size_t(index)].get();
	for (size_t i = 0; i < md->animIndices.size(); i++)
		if (md->animLoaded[i])
			_scenery.freeAnim(md->animIndices[i]);
	for (int slot : md->sndSlots)
		if (!(slot & 0x8000))
			freeSoundSlot(slot);
	if (_multData == md)
		_multData = nullptr;
	_multDatas[size_t(index)].reset();
}

// Starts (or aborts) a keyframed sequence on one of four slots.
void Interpreter::multSub() {
	int w = evalVal();
	int multIndex = (w >> 12) & 7;
	MultData *md = _multDatas[size_t(multIndex)].get();
	int tracks[3];
	for (int &t : tracks)
		t = evalVal();
	int start = evalVal();
	if (!md)
		return;
	_lastFrameTime = gameTicks();

	int slot = (w & 0x200) ? 3 : (w & 0x100) ? 2 : (w & 0x80) ? 1 : 0;
	auto &objs = md->animObjs[size_t(slot)];
	objs[0] = w & 0x7F;
	for (int t = 0; t < 3; t++)
		objs[size_t(t) + 1] = tracks[t];

	md->animKeysFrames[size_t(slot)] = start;
	writeVar(uint32_t(18 + slot), uint32_t(start));

	auto validObj = [&](int o) { return o >= 0 && o < int(_objects.size()); };
	if (start == -1) {
		for (int o : objs)
			if (validObj(o))
				setAnimField(o, kAnimType, animField(o, kAnimTypeBak));
		return;
	}

	int stop = 32000;
	for (const auto &k : md->textKeys)
		if (k.frame > start && k.frame < stop)
			stop = k.frame;
	md->animKeysStop[size_t(slot)] = stop;

	for (int o : objs)
		if (validObj(o))
			setAnimField(o, kAnimTypeBak, animField(o, kAnimType));

	for (int t = 0; t < 4; t++) {
		const auto &keys = md->animKeys[size_t(t)];
		int idx = 0;
		for (size_t j = 0; j < keys.size(); j++)
			if (keys[j].frame >= start) {
				idx = int(j);
				break;
			}
		md->animKeysIndices[size_t(slot)][size_t(t)] = idx;
	}
	for (int t = 0; t < 4; t++) { // IMD keys (version 1.3)
		const auto &keys = md->imdKeys[size_t(t)];
		int idx = 0;
		for (size_t j = 0; j < keys.size(); j++)
			if (keys[j].frame >= start) {
				idx = int(j);
				break;
			}
		md->imdKeysIndices[size_t(slot)][size_t(t)] = idx;
	}
}

void Interpreter::doSoundAnim(MultData &md, int frame) {
	for (const auto &k : md.soundKeys) {
		if (k.frame != frame)
			continue;
		if (k.cmd == 1 || k.cmd == 4) {
			blasterStop(0);
			blasterPlay(k.soundIndex, k.repCount, k.freq, k.fadeLength);
		} else if (k.cmd == -1) {
			if (blasterPlaying())
				blasterStop(k.fadeLength);
		}
	}
}

// The anim keys of track `t` at this frame (Mult_v2::advanceObjects).
void Interpreter::advanceAnimKeys(MultData &md, int slot, int t, int o, int f) {
	if (o < 0 || o >= int(_objects.size()))
		return;
	const auto &keys = md.animKeys[size_t(t)];
	for (size_t j = size_t(md.animKeysIndices[size_t(slot)][size_t(t)]); j < keys.size(); j++) {
		const auto &k = keys[j];
		if (k.frame > f)
			break;
		if (k.frame < f)
			continue;
		if (k.layer > -1) {
			md.animKeysIndices[size_t(slot)][size_t(t)] = int(j);
			setObjPos(o, k.posX, k.posY);
			setAnimField(o, kFrame, 0);
			setAnimField(o, kAnimType, 1);
			setAnimField(o, kIsStatic, 0);
			setAnimField(o, kIsPaused, 0);
			setAnimField(o, kMaxTick, 0);
			_objects[size_t(o)].tick = 0;
			int animation, layer;
			resolveGlobalLayer(md, k.layer, animation, layer);
			setAnimField(o, kAnimation, animation);
			setAnimField(o, kLayer, layer);
			break;
		}
		setAnimField(o, kIsStatic, 1);
	}
}

// Mult_v2::advanceObjects' IMD part for track `t` (version 1.3): a key at
// this frame starts its video on that track (or stops it), and the track's
// video plays the frame that far into its loop. The original handles the
// tracks of every object but -1 (with no object behind them, too) and the
// anim keys after them. False when a key names no file: advanceObjects
// ends there.
bool Interpreter::advanceImdKeys(MultData &md, int slot, int t, int obj, int frame) {
	if (obj != -1) {
		const auto &keys = md.imdKeys[size_t(t)];
		for (size_t j = size_t(md.imdKeysIndices[size_t(slot)][size_t(t)]); j < keys.size(); j++) {
			if (keys[j].frame > frame)
				break;
			if (keys[j].frame < frame)
				continue;
			if (keys[j].imdFile != -1) {
				md.imdIndices.fill(-1);
				md.imdIndices[size_t(t)] = int(j); // forward only: Goblins 3 runs no sequence backwards
			} else {
				md.imdIndices[size_t(t)] = -1;
			}
		}
	}
	int idx = md.imdIndices[size_t(t)];
	if (idx == -1)
		return true;
	const MultData::ImdKey &key = md.imdKeys[size_t(t)][size_t(idx)];
	int fileN = -key.imdFile - 2;
	if (fileN < 0)
		return false;
	if (size_t(fileN) < md.imdFiles.size())
		playMultImd(md.imdFiles[size_t(fileN)], key, frame - key.frame);
	return true;
}

void Interpreter::advanceObjects(MultData &md, int slot) {
	int f = md.animKeysFrames[size_t(slot)];
	if (f == -1)
		return;

	for (int t = 0; t < 4; t++) {
		int o = md.animObjs[size_t(slot)][size_t(t)];
		advanceAnimKeys(md, slot, t, o, f);
		if (!md.imdFiles.empty() && !advanceImdKeys(md, slot, t, o, f))
			return; // a key with no file: the original leaves here
	}

	doSoundAnim(md, f);
	writeVar(22, uint32_t(f));

	if (f == md.animKeysStop[size_t(slot)]) {
		md.imdIndices.fill(-1);
		f = -1;
		for (int o : md.animObjs[size_t(slot)])
			if (o >= 0 && o < int(_objects.size()))
				setAnimField(o, kAnimType, animField(o, kAnimTypeBak));
	} else {
		f++;
	}
	md.animKeysFrames[size_t(slot)] = f;
	writeVar(uint32_t(18 + slot), uint32_t(f));
}

void Interpreter::newCycleAnim(int obj) {
	int animation = animField(obj, kAnimation);
	int layerIdx = animField(obj, kLayer);
	const Scenery::Layer *l = _scenery.layer(animation, layerIdx);
	if (animation >= 0 && !l)
		return;
	int type = animField(obj, kAnimType);

	if (type == 4) {
		setAnimField(obj, kFrame, 0);
		setAnimField(obj, kIsPaused, 1);
		return;
	}

	if (type != 8)
		setAnimField(obj, kFrame, animField(obj, kFrame) + 1);
	int frame = animField(obj, kFrame);
	if (l && frame < l->framesCount) {
		setAnimField(obj, kNewCycle, 0);
		return;
	}

	switch (type) {
	case 0:
		setAnimField(obj, kFrame, 0);
		break;
	case 1: {
		setAnimField(obj, kFrame, 0);
		int x = objPosX(obj), y = objPosY(obj);
		if (l) {
			if (x == 1000) x = l->posX;
			if (y == 1000) y = l->posY;
			setObjPos(obj, x + l->animDeltaX, y + l->animDeltaY);
		}
		break;
	}
	case 2:
		setAnimField(obj, kFrame, 0);
		setAnimField(obj, kAnimation, animField(obj, kNewAnimation));
		setAnimField(obj, kLayer, animField(obj, kNewLayer));
		break;
	case 3:
		setAnimField(obj, kAnimType, 4);
		setAnimField(obj, kFrame, 0);
		break;
	case 5:
		setAnimField(obj, kIsStatic, 1);
		setAnimField(obj, kFrame, 0);
		break;
	case 6:
	case 7:
		setAnimField(obj, kFrame, frame - 1);
		setAnimField(obj, kIsPaused, 1);
		break;
	default:
		break;
	}
	setAnimField(obj, kNewCycle, 1);
}

// One animation tick: run sequences, erase moved objects from the clean
// background, draw in priority order, then advance frames
// (docs/animation.md 2.8).
void Interpreter::animate() {
	if (_objects.empty())
		return;

	// A. Sequences.
	for (auto &md : _multDatas)
		if (md)
			for (int slot = 0; slot < 4; slot++)
				advanceObjects(*md, slot);

	// B. Render list.
	std::vector<int> list;
	for (int i = 0; i < int(_objects.size()); i++) {
		setAnimField(i, kIntersected, 200);
		int st = animField(i, kIsStatic);
		if (st != 2 && (st == 0 || _objects[size_t(i)].lastLeft != -1))
			list.push_back(i);
	}

	// C. Dirty rectangles.
	int minOrder = 100, maxOrder = 0;
	for (int i : list) {
		MultObject &o = _objects[size_t(i)];
		o.needRedraw = false;
		o.newLeft = 1000; o.newTop = 1000; o.newRight = 0; o.newBottom = 0;
		int st = animField(i, kIsStatic);
		auto measure = [&]() {
			_scenery.updateAnim(animField(i, kLayer), animField(i, kFrame), animField(i, kAnimation), 8, objPosX(i),
			                    objPosY(i), false);
		};
		if (st == 0) {
			bool advancing = !animField(i, kIsPaused) && animField(i, kMaxTick) == o.tick;
			if (advancing) {
				o.needRedraw = true;
				measure();
				if (o.lastLeft == -1) {
					o.newLeft = _scenery.toRedrawLeft; o.newTop = _scenery.toRedrawTop;
					o.newRight = _scenery.toRedrawRight; o.newBottom = _scenery.toRedrawBottom;
				} else {
					o.newLeft = std::min(o.lastLeft, _scenery.toRedrawLeft);
					o.newTop = std::min(o.lastTop, _scenery.toRedrawTop);
					o.newRight = std::max(o.lastRight, _scenery.toRedrawRight);
					o.newBottom = std::max(o.lastBottom, _scenery.toRedrawBottom);
					// Version 3: an object showing the same frame in the same
					// place is not redrawn.
					if (_ver >= 3 && o.newLeft == o.lastLeft && o.newTop == o.lastTop && o.newRight == o.lastRight &&
					    o.newBottom == o.lastBottom && animField(i, kRedrawLayer) == animField(i, kLayer) &&
					    animField(i, kRedrawFrame) == animField(i, kFrame) &&
					    animField(i, kRedrawAnimation) == animField(i, kAnimation))
						o.needRedraw = false;
				}
			} else if (o.lastLeft == -1) {
				o.needRedraw = true;
				measure();
				o.newLeft = _scenery.toRedrawLeft; o.newTop = _scenery.toRedrawTop;
				o.newRight = _scenery.toRedrawRight; o.newBottom = _scenery.toRedrawBottom;
			} else {
				o.newLeft = o.lastLeft; o.newTop = o.lastTop; o.newRight = o.lastRight; o.newBottom = o.lastBottom;
			}
		} else if (o.lastLeft != -1) {
			o.needRedraw = true;
			o.newLeft = o.lastLeft; o.newTop = o.lastTop; o.newRight = o.lastRight; o.newBottom = o.lastBottom;
		}
		setAnimField(i, kRedrawAnimation, animField(i, kAnimation));
		setAnimField(i, kRedrawLayer, animField(i, kLayer));
		setAnimField(i, kRedrawFrame, animField(i, kFrame));
		if (o.needRedraw || st == 0) {
			minOrder = std::min(minOrder, animField(i, kOrder));
			maxOrder = std::max(maxOrder, animField(i, kOrder));
		}
	}

	// D. Erase from the clean background.
	Surface *bg = _draw.sprite(draw::kAnimSurface).get();
	for (int i : list) {
		MultObject &o = _objects[size_t(i)];
		if (!o.needRedraw || o.lastLeft == -1)
			continue;
		o.lastLeft = -1;
		int l = std::max<int>(o.newLeft, _multLeft), t = std::max<int>(o.newTop, _multTop);
		int r = o.newRight, b = o.newBottom;
		if (bg && l <= r && t <= b) {
			_draw.back().blit(*bg, l - _multLeft, t - _multTop, r - _multLeft, b - _multTop, l, t);
			_draw.invalidateRect(l, t, r, b);
		}
	}

	// E. Draw order.
	std::vector<int> order;
	for (int ord = minOrder; ord <= maxOrder; ord++)
		for (int i : list)
			if (animField(i, kOrder) == ord && (_objects[size_t(i)].needRedraw || animField(i, kIsStatic) == 0))
				order.push_back(i);

	// F. Goblins with lower feet are drawn first among equal orders.
	if (_gobsCount >= 0) {
		for (size_t a = 0; a < order.size(); a++) {
			if (!_objects[size_t(order[a])].isGoblin)
				continue;
			for (size_t b = a + 1; b < order.size(); b++) {
				int oa = order[a], ob = order[b];
				if (animField(oa, kOrder) != animField(ob, kOrder))
					continue;
				int ba = _objects[size_t(oa)].newBottom, bb = _objects[size_t(ob)].newBottom;
				if (bb < ba || (bb == ba && animField(oa, kIsBusy) == 1))
					std::swap(order[a], order[b]);
			}
		}
	}

	// G. Draw.
	for (int i : order) {
		MultObject &o = _objects[size_t(i)];
		if (!o.needRedraw) {
			if (animField(i, kIsStatic) != 0)
				continue;
			// Repair parts of still objects erased in step D.
			for (int j : list) {
				MultObject &other = _objects[size_t(j)];
				if (!other.needRedraw)
					continue;
				if (!rectsOverlap(o.newLeft, o.newTop, o.newRight, o.newBottom, other.newLeft, other.newTop,
				                  other.newRight, other.newBottom))
					continue;
				_scenery.toRedrawLeft = other.newLeft;
				_scenery.toRedrawTop = other.newTop;
				_scenery.toRedrawRight = other.newRight;
				_scenery.toRedrawBottom = other.newBottom;
				_scenery.updateAnim(animField(i, kRedrawLayer), animField(i, kRedrawFrame),
				                    animField(i, kRedrawAnimation), 12, objPosX(i), objPosY(i), true);
			}
			continue;
		}
		if (animField(i, kIsStatic) != 0) {
			_scenery.toRedrawLeft = o.newLeft;
			_scenery.toRedrawTop = o.newTop;
			_scenery.toRedrawRight = o.newRight;
			_scenery.toRedrawBottom = o.newBottom;
			continue;
		}
		_scenery.updateAnim(animField(i, kLayer), animField(i, kFrame), animField(i, kAnimation), 10, objPosX(i),
		                    objPosY(i), true);
		if (_scenery.toRedrawLeft != -12345) {
			o.lastLeft = _scenery.toRedrawLeft;
			o.lastTop = _scenery.toRedrawTop;
			o.lastRight = _scenery.toRedrawRight;
			o.lastBottom = _scenery.toRedrawBottom;
		} else {
			o.lastLeft = -1;
		}
	}

	// H. Advance frames.
	for (int i : list) {
		if (animField(i, kIsStatic) != 0)
			continue;
		MultObject &o = _objects[size_t(i)];
		int type = animField(i, kAnimType);
		if (type == 7 && animField(i, kNewState) != -1) {
			setAnimField(i, kLayer, animField(i, kNewState));
			setAnimField(i, kFrame, 0);
			setAnimField(i, kNewState, -1);
			setAnimField(i, kIsPaused, 0);
		}
		if (animField(i, kIsPaused))
			continue;
		if (animField(i, kMaxTick) == o.tick) {
			o.tick = 0;
			if (type < 100 || _gobsCount < 0)
				newCycleAnim(i);
			else if (type == 100)
				goblinMoveAdvance(i);
			else if (type == 101)
				goblinAnimate(i);
		} else {
			o.tick++;
		}
	}

	// I. Intersections.
	for (int a : list)
		for (int b : list) {
			if (a == b || animField(a, kIsStatic) != 0 || animField(b, kIsStatic) != 0)
				continue;
			const MultObject &oa = _objects[size_t(a)], &ob = _objects[size_t(b)];
			if (oa.lastLeft == -1 || ob.lastLeft == -1)
				continue;
			if (rectsOverlap(oa.lastLeft, oa.lastTop, oa.lastRight, oa.lastBottom, ob.lastLeft, ob.lastTop,
			                 ob.lastRight, ob.lastBottom))
				setAnimField(b, kIntersected, a);
		}
}

// Full-screen animation player used by the intro and the ending: plays
// from VAR(57) until a text key hands control back to the script.
Task<void> Interpreter::playMult(int w) {
	MultData *md = _multDatas[size_t((w >> 1) & 7)].get();
	bool checkEscape = w & 1;
	if (!md)
		co_return;
	_multData = md;
	_inPlayMult = true;

	int frame = int32_t(_vars->slot(57));
	if (frame == -1)
		frame = 0;

	bool stop = false, stopNoClear = false;
	do {
		stop = true;
		if (_vars->slot(58) == 0) {
			// Static keys: only "draw sprite into the background" is used.
			if (!md->staticKeys.empty() && md->staticKeys.back().frame > frame)
				stop = false;
			for (const auto &k : md->staticKeys) {
				if (k.frame != frame || k.layer == -1)
					continue;
				if (k.layer <= -2) {
					size_t idx = size_t(-k.layer - 2);
					if (idx < md->spriteList.size()) {
						_draw.spriteLeft = md->spriteList[idx];
						_draw.destSurface = draw::kBackSurface;
						_draw.destSpriteX = 0;
						_draw.destSpriteY = 0;
						_draw.transparency = 0;
						spriteOp(draw::LoadSprite);
					}
				}
				if (Surface *bg = _draw.sprite(draw::kAnimSurface).get())
					bg->blit(_draw.back());
			}

			// Anim keys.
			for (int t = 0; t < 4; t++) {
				const auto &keys = md->animKeys[size_t(t)];
				if (!keys.empty() && keys.back().frame > frame)
					stop = false;
				int o = md->animObjs[0][size_t(t)];
				if (o < 0 || o >= int(_objects.size()))
					continue;
				for (const auto &k : keys) {
					if (k.frame != frame)
						continue;
					if (k.layer != -1) {
						setObjPos(o, k.posX, k.posY);
						setAnimField(o, kFrame, 0);
						setAnimField(o, kOrder, k.order);
						setAnimField(o, kAnimType, 1);
						setAnimField(o, kIsPaused, 0);
						setAnimField(o, kIsStatic, 0);
						setAnimField(o, kMaxTick, 0);
						_objects[size_t(o)].tick = 0;
						int animation, layer;
						resolveGlobalLayer(*md, k.layer, animation, layer);
						setAnimField(o, kAnimation, animation);
						setAnimField(o, kLayer, layer);
					} else {
						setAnimField(o, kIsStatic, 1);
					}
				}
			}
		}

		animate();
		_draw.blitInvalidated();

		if (_vars->slot(58) == 0)
			for (const auto &k : md->textKeys)
				if (k.frame == frame) {
					if (k.cmd == 1)
						stopNoClear = true;
					else
						stop = false;
				}

		doSoundAnim(*md, frame);
		if (blasterPlaying())
			stop = false;

		int key = checkKeys();
		if (checkEscape && (key == 0x011B || skipPending())) {
			stop = true;
			_skipCutscene = false;
		}
		co_await waitEndFrame();

		frame++;
	} while (!stop && !stopNoClear && !_quit);
	_inPlayMult = false;

	if (stopNoClear) {
		trace("playMult returns at frame " + std::to_string(frame - 1) + ", CD position " +
		      std::to_string(_cd.position({}, cdTicks())));
		writeVar(57, uint32_t(frame - 1));
	} else {
		blasterStop(10);
		writeVar(57, uint32_t(-1));
	}
}

} // namespace gob
