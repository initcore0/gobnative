// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Version 1 (Gobliiins) scenery loading and the Mult_v1 animation player:
// mults made of statics (layered backgrounds) and up to four animated
// objects, with palette-cycling, palette-fade, text and sound keys, drawn by
// the version 1 dirty-rectangle renderer. Behaviour follows ScummVM's
// mult_v1.cpp, mult.cpp and scenery.cpp; see docs/animation.md section 4.

#include "engine/interpreter.h"

#include <algorithm>

namespace gob {

namespace {

int16_t le16v1(const std::vector<uint8_t> &d, size_t o) {
	return o + 1 < d.size() ? int16_t(d[o] | (d[o + 1] << 8)) : 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Scenery

// Scenery_v1::loadAnim: a playing CD track is first played to its end, then
// stopped.
Task<void> Interpreter::waitCdForLoadAnim() {
	if (!_cd.current())
		co_return;
	while (!_quit && !skipPending() && _cd.position({}, cdTicks()) != -1)
		co_await sleep(50);
	_cd.stop();
	_mixer.stopCd();
	_ffCdChanged = true;
}

Task<int> Interpreter::sceneryLoadAnimV1(bool search) {
	co_await waitCdForLoadAnim();
	co_return sceneryLoadAnim(search);
}

// Static descriptor: expr index, i16 back count and backs, i16 pictures,
// i16 resource, then per picture i16 piece table, width, height, sprite.
// Returns the slot + 100 when loaded, the slot when found, 0 on failure.
int Interpreter::sceneryLoadStatic(bool search) {
	Script &s = script();
	int index = int(evalInt());
	int16_t backCount = s.readInt16();
	std::vector<int16_t> backs;
	for (int i = 0; i < backCount; i++)
		backs.push_back(s.readInt16());
	int16_t picsCount = s.readInt16();
	int16_t resId = s.readInt16();

	if (search) {
		int found = _scenery.findStatic(resId, index);
		if (found >= 0) {
			s.skip(8 * std::max(_scenery.staticPictCount(found), 0));
			return found;
		}
	}

	auto loader = resourceLoader();
	if (!_scenery.resourceExists(resId, loader)) {
		trace("static resource " + std::to_string(resId) + " missing");
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
	_scenery.loadStatic(index, resId, backs, pics, loader);
	return index + 100;
}

// ---------------------------------------------------------------------------
// Loading

// Mult_v1::loadMult: a plain resource id; the statics and animations are
// loaded with search, then the keys follow (docs/opcodes.md 10.2).
Task<void> Interpreter::loadMultV1() {
	Script &s = script();
	int16_t resId = s.readInt16();

	freeMultKeysV1();
	auto md = std::make_unique<MultData>();
	for (int slot = 0; slot < 4; slot++)
		for (int t = 0; t < 4; t++)
			md->animObjs[size_t(slot)][size_t(t)] = slot == 0 ? t : -1;

	auto res = env().tot->resource(uint16_t(resId));
	if (!res) {
		trace("mult resource " + std::to_string(resId) + " missing");
		_multDatas[0] = std::move(md);
		co_return;
	}
	const std::vector<uint8_t> d = res->data;
	size_t rp = 0;
	auto r8 = [&]() { return rp < d.size() ? int8_t(d[rp++]) : int8_t(0); };
	auto r16 = [&]() { int16_t v = le16v1(d, rp); rp += 2; return v; };

	int staticCount = r8() + 1;
	int animCount = r8() + 1;
	for (int i = 0; i < staticCount; i++) {
		int r = sceneryLoadStatic(true);
		md->staticIndices.push_back(r >= 100 ? r - 100 : r);
		md->staticLoaded.push_back(r >= 100);
		rp += 14;
	}
	for (int i = 0; i < animCount; i++) {
		int r = co_await sceneryLoadAnimV1(true);
		md->animIndices.push_back(r >= 100 ? r - 100 : r);
		md->animLoaded.push_back(r >= 100);
		rp += 14;
	}

	md->frameRate = r16();
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
	for (auto &pal : md->fadePal)
		for (uint8_t &c : pal)
			c = uint8_t(r8());

	n = r16();
	for (int i = 0; i < n; i++) {
		MultData::PalFadeKey k;
		k.frame = r16();
		k.fade = r16();
		k.palIndex = r16();
		k.flag = r8();
		md->palFadeKeys.push_back(k);
	}
	n = r16();
	for (int i = 0; i < n; i++) {
		MultData::PalKey k;
		k.frame = r16();
		k.cmd = r16();
		for (int16_t &r : k.rates)
			r = r16();
		rp += 4; // unknown
		for (auto &row : k.subst)
			for (uint8_t &c : row)
				c = uint8_t(r8());
		md->palKeys.push_back(k);
	}
	n = r16();
	for (int i = 0; i < n; i++) {
		int16_t f = r16(), c = r16();
		rp += 24; // unknown, script
		md->textKeys.push_back({f, c});
	}

	n = r16();
	for (int i = 0; i < n; i++) {
		MultData::SoundKey k;
		k.frame = r16();
		k.cmd = r16();
		k.freq = r16();
		k.fadeLength = r16();
		k.repCount = r16();
		rp += 26;
		k.soundIndex = -1;
		switch (k.cmd) {
		case 1:
		case 4: {
			// One sound per resource, loaded into slots 19, 18, ...
			uint16_t id = s.peekUint16();
			auto same = std::find_if(md->soundKeys.begin(), md->soundKeys.end(), [&](const MultData::SoundKey &o) {
				return (o.cmd == 1 || o.cmd == 4) && o.resId == id;
			});
			k.resId = id;
			if (same != md->soundKeys.end()) {
				k.soundIndex = same->soundIndex;
				s.skip(2);
			} else {
				int slot = 19 - md->sndSlotsCount;
				s.skip(2);
				if (id == 0xFFFF) {
					s.skip(9);
				} else {
					freeSoundSlot(slot);
					loadSoundResource(slot, int16_t(id));
				}
				k.soundIndex = slot;
				md->sndSlotsCount++;
			}
			break;
		}
		case 3:
			s.skip(6);
			break;
		case 5:
			s.skip(std::max<int16_t>(k.freq, 0) * 2);
			break;
		default:
			break;
		}
		md->soundKeys.push_back(k);
	}

	trace("loadMult " + std::to_string(resId) + ": " + std::to_string(staticCount) + " statics, " +
	      std::to_string(animCount) + " anims, " + std::to_string(md->staticKeys.size()) + " static keys, " +
	      std::to_string(md->palFadeKeys.size()) + " fade keys, " + std::to_string(md->palKeys.size()) +
	      " palette keys, " + std::to_string(md->textKeys.size()) + " text keys, " +
	      std::to_string(md->soundKeys.size()) + " sound keys");
	_multDatas[0] = std::move(md);
}

void Interpreter::freeMultObjectsV1() {
	if (!_m1.animDataAllocated)
		return;
	_objects.clear();
	_objCount = 0;
	_draw.freeSprite(draw::kAnimSurface);
	_m1.animDataAllocated = false;
}

void Interpreter::freeMultKeysV1() {
	MultData *md = _multDatas[0].get();
	if (!md)
		return;
	for (size_t i = 0; i < md->staticIndices.size(); i++)
		if (md->staticLoaded[i])
			_scenery.freeStatic(md->staticIndices[i]);
	for (size_t i = 0; i < md->animIndices.size(); i++)
		if (md->animLoaded[i])
			_scenery.freeAnim(md->animIndices[i]);
	for (int i = 0; i < md->sndSlotsCount; i++)
		freeSoundSlot(19 - i);
	freeMultObjectsV1();
	if (_multData == md)
		_multData = nullptr;
	_multDatas[0].reset();
}

// Draw 0x17: object, x, y, then 11 anim-data bytes; a 16-bit 99 skips one
// field (and one byte).
void Interpreter::loadMultObjectV1() {
	Script &s = script();
	int obj = int(evalInt());
	int x = int(evalInt());
	int y = int(evalInt());
	bool valid = obj >= 0 && obj < int(_objects.size());
	if (valid)
		setObjPos(obj, x, y);
	for (int i = 0; i < 11; i++) {
		if (s.peekUint16() == tok::Stop) {
			s.skip(1);
			continue;
		}
		int v = int(evalInt());
		if (valid)
			setAnimField(obj, AnimField(i), v);
	}
}

// ---------------------------------------------------------------------------
// Playing

// Without an anim surface (no initMult), playMult makes its own four objects
// over a full-screen copy of the background.
void Interpreter::playMultInitV1(MultData &md) {
	_m1.doPalSubst = false;
	_m1.fading = false;
	if (_draw.sprite(draw::kAnimSurface)) {
		_m1.animDataAllocated = false;
		return;
	}

	_frameRate = std::max(1, md.frameRate);
	_multLeft = _multTop = 0;
	_multWidth = 320;
	_multHeight = 200;
	_scenery.areaLeft = _scenery.areaTop = 0;
	_scenery.areaWidth = 320;
	_scenery.areaHeight = 200;

	_m1.objectVars.resize(4 * 4 * 2 + 4 * 40);
	_objects.assign(4, MultObject{});
	_objCount = 4;
	for (int i = 0; i < 4; i++) {
		MultObject &o = _objects[size_t(i)];
		o.store = &_m1.objectVars;
		o.posXSlot = uint32_t(i);
		o.posYSlot = uint32_t(4 + i);
		o.animData = uint32_t(32 + i * 40);
		setAnimField(i, kIsStatic, 1);
	}
	_draw.createSprite(draw::kAnimSurface, 320, 200);
	_draw.sprite(draw::kAnimSurface)->blit(_draw.back(), 0, 0, 319, 199, 0, 0);
	_m1.animDataAllocated = true;
}

void Interpreter::drawStaticsV1(MultData &md, int frame, bool &stop) {
	if (md.staticKeys.empty())
		return;
	if (md.staticKeys.back().frame > frame)
		stop = false;

	for (const auto &k : md.staticKeys) {
		if (k.frame != frame || k.layer == -1)
			continue;
		// Key layers count across all of the mult's statics.
		int layer = k.layer;
		size_t idx = 0;
		while (idx + 1 < md.staticIndices.size() && layer >= _scenery.staticLayersCount(md.staticIndices[idx])) {
			layer -= _scenery.staticLayersCount(md.staticIndices[idx]);
			idx++;
		}
		if (md.staticIndices.empty())
			continue;
		_scenery.curStatic = int16_t(md.staticIndices[idx]);
		_scenery.curStaticLayer = int16_t(layer);
		_scenery.renderStatic(_scenery.curStatic, layer, resourceLoader());
		if (Surface *bg = _draw.sprite(draw::kAnimSurface).get())
			bg->blit(_draw.back(), 0, 0, 319, 199, 0, 0);
	}
}

void Interpreter::drawAnimsV1(MultData &md, int frame) {
	for (int t = 0; t < 4 && t < int(_objects.size()); t++) {
		for (const auto &k : md.animKeys[size_t(t)]) {
			if (k.frame != frame)
				continue;
			if (k.layer == -1) {
				setAnimField(t, kIsStatic, 1);
				continue;
			}
			setObjPos(t, k.posX, k.posY);
			setAnimField(t, kFrame, 0);
			setAnimField(t, kOrder, k.order);
			setAnimField(t, kAnimType, 1);
			setAnimField(t, kIsPaused, 0);
			setAnimField(t, kIsStatic, 0);
			setAnimField(t, kMaxTick, 0);
			_objects[size_t(t)].tick = 0;
			int animation, layer;
			resolveGlobalLayer(md, k.layer, animation, layer);
			setAnimField(t, kAnimation, animation);
			setAnimField(t, kLayer, layer);
		}
	}
}

// Palette keys: cmd -1 ends the cycling and restores the palette; anything
// else starts cycling up to four colour chains, each at its own rate.
void Interpreter::prepPalAnimV1(MultData &md, int frame, bool &stop) {
	size_t i = 0;
	while (i < md.palKeys.size() && md.palKeys[i].frame != frame)
		i++;
	if (i == md.palKeys.size())
		return;

	stop = false;
	if (md.palKeys[i].cmd == -1) {
		if (_m1.doPalSubst) {
			_draw.vgaPalette = _m1.realPalette;
			_draw.setPalette();
		}
		_m1.doPalSubst = false;
		return;
	}
	if (!_m1.doPalSubst)
		_m1.realPalette = _draw.vgaPalette;
	_m1.doPalSubst = true;
	_m1.palAnimKey = int(i);
	_m1.palAnimIndices.fill(0);
}

void Interpreter::doPalAnimV1(MultData &md, int frame) {
	if (!_m1.doPalSubst || size_t(_m1.palAnimKey) >= md.palKeys.size())
		return;
	const MultData::PalKey &key = md.palKeys[size_t(_m1.palAnimKey)];
	VgaPalette &pal = _draw.vgaPalette;
	auto colour = [&](int entry) -> uint8_t * { return &pal[size_t(std::clamp(entry, 0, 255)) * 3]; };

	for (int c = 0; c < 4; c++) {
		if (key.rates[size_t(c)] == 0 || frame % key.rates[size_t(c)] != 0)
			continue;
		int &idx = _m1.palAnimIndices[size_t(c)];
		uint8_t *saved = &_m1.palAnimColor[size_t(c) * 3];
		std::copy_n(colour(key.subst[0][size_t(c)] - 1), 3, saved);
		while (true) {
			int next = key.subst[size_t((idx + 1) % 16)][size_t(c)];
			if (next == 0)
				std::copy_n(saved, 3, colour(key.subst[size_t(idx)][size_t(c)] - 1));
			else
				std::copy_n(colour(next - 1), 3, colour(key.subst[size_t(idx)][size_t(c)] - 1));
			idx = (idx + 1) % 16;
			if (key.subst[size_t(idx)][size_t(c)] == 0) {
				idx = 0;
				std::copy_n(colour(key.subst[0][size_t(c)] - 1), 3, saved);
			}
			if (idx == 0)
				break;
		}
	}
	_draw.setPalette();
}

// Fade keys switch the first 16 colours to one of the mult's five
// palettes: at once (fade 0), with a blocking fade, or (flag bit 0) with a
// fade that runs on during the following frames.
Task<void> Interpreter::doFadeAnimV1(MultData &md, int frame, bool &stop) {
	for (const auto &k : md.palFadeKeys) {
		if (k.frame != frame)
			continue;
		stop = false;
		VgaPalette target = _draw.vgaPalette;
		if (k.palIndex >= 0 && k.palIndex < 5)
			std::copy_n(md.fadePal[size_t(k.palIndex)].begin(), 48, target.begin());
		int step = k.fade < 0 ? -k.fade : 2;
		_draw.vgaPalette = target;
		if (k.flag & 1) {
			_m1.fading = true;
			_m1.fadeStep = step;
			_m1.fadeTarget = target;
		} else if (k.fade == 0) {
			_draw.setPalette();
		} else {
			while (!_quit && !_draw.fadeStep(target, step)) {
				_frameBudget = 0;
				co_await _sched.nextFrame();
			}
			_draw.setPalette();
		}
	}
	if (_m1.fading) {
		_m1.fading = !_draw.fadeStep(_m1.fadeTarget, _m1.fadeStep);
		stop = false;
	}
}

// Mult::playMult with the version 1 parts: plays from VAR(57) (-1: from the
// start, after playMultInit) until a text key returns to the script.
Task<void> Interpreter::playMultV1(bool checkEscape) {
	MultData *md = _multDatas[0].get();
	if (!md)
		co_return;
	_multData = md;
	_inPlayMult = true;

	int frame = int16_t(_vars->slot(57));
	if (frame == -1) {
		playMultInitV1(*md);
		frame = 0;
	}

	bool stop = false, stopNoClear = false;
	do {
		stop = true;
		if (_vars->slot(58) == 0) {
			drawStaticsV1(*md, frame, stop);
			drawAnimsV1(*md, frame);
		}

		animateV1();
		_draw.blitInvalidated();

		if (_vars->slot(58) == 0)
			for (const auto &k : md->textKeys)
				if (k.frame == frame) {
					if (k.cmd == 1)
						stopNoClear = true;
					else if (k.cmd == 0 || k.cmd == 3)
						stop = false;
				}

		prepPalAnimV1(*md, frame, stop);
		doPalAnimV1(*md, frame);
		co_await doFadeAnimV1(*md, frame, stop);
		doSoundAnim(*md, frame);
		if (blasterPlaying())
			stop = false;

		int key = checkKeys();
		if (checkEscape && (key == 0x011B || skipPending())) {
			stop = true;
			_skipCutscene = _skipAllMults; // a level jump skips the rest of the intro too
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
		freeMultObjectsV1();
		if (blasterPlaying())
			blasterStop(10);
		writeVar(57, uint32_t(-1));
	}
}

// ---------------------------------------------------------------------------
// The version 1 renderer (Mult_v1::animate)

void Interpreter::newCycleAnimV1(int obj) {
	const Scenery::Layer *l = _scenery.layer(animField(obj, kAnimation), animField(obj, kLayer));
	if (!l)
		return;
	int frame = animField(obj, kFrame) + 1;
	setAnimField(obj, kFrame, frame);
	if (frame < l->framesCount) {
		setAnimField(obj, kNewCycle, 0);
		return;
	}

	switch (animField(obj, kAnimType)) {
	case 0:
		setAnimField(obj, kFrame, 0);
		break;
	case 1:
		setAnimField(obj, kFrame, 0);
		setObjPos(obj, objPosX(obj) + l->animDeltaX, objPosY(obj) + l->animDeltaY);
		break;
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
		setAnimField(obj, kFrame, frame - 1);
		setAnimField(obj, kIsPaused, 1);
		break;
	default:
		break;
	}
	setAnimField(obj, kNewCycle, 1);
}

void Interpreter::animateV1() {
	const int n = int(_objects.size());
	if (n == 0)
		return;

	struct Box { int16_t left, top, right, bottom; };
	const size_t count = size_t(n);
	std::vector<Box> dirty(count), cur(count);
	std::vector<int> needRedraw(size_t(n), 0);
	int minOrder = 100, maxOrder = 0;

	// Dirty areas.
	for (int i = 0; i < n; i++) {
		MultObject &o = _objects[size_t(i)];
		dirty[size_t(i)] = {1000, 1000, 1000, 1000};
		int order = animField(i, kOrder);
		if (!animField(i, kIsStatic) && !animField(i, kIsPaused) && o.tick == animField(i, kMaxTick)) {
			minOrder = std::min(minOrder, order);
			maxOrder = std::max(maxOrder, order);
			needRedraw[size_t(i)] = 1;
			_scenery.updateAnim(animField(i, kLayer), animField(i, kFrame), animField(i, kAnimation), 0, objPosX(i),
			                    objPosY(i), false);
			Box now{_scenery.toRedrawLeft, _scenery.toRedrawTop, _scenery.toRedrawRight, _scenery.toRedrawBottom};
			if (o.lastLeft != -1)
				dirty[size_t(i)] = {std::min(o.lastLeft, now.left), std::min(o.lastTop, now.top),
				                    std::max(o.lastRight, now.right), std::max(o.lastBottom, now.bottom)};
			else
				dirty[size_t(i)] = now;
			cur[size_t(i)] = now;
		} else if (o.lastLeft != -1) {
			minOrder = std::min(minOrder, order);
			maxOrder = std::max(maxOrder, order);
			if (animField(i, kIsStatic))
				needRedraw[0] = 1; // sic: the original marks object 0
			cur[size_t(i)] = dirty[size_t(i)] = {o.lastLeft, o.lastTop, o.lastRight, o.lastBottom};
		}
	}

	// Intersections.
	for (int i = 0; i < n; i++) {
		setAnimField(i, kIntersected, 200);
		if (animField(i, kIsStatic))
			continue;
		for (int j = 0; j < n; j++) {
			if (i == j || animField(j, kIsStatic))
				continue;
			const Box &a = cur[size_t(i)], &b = cur[size_t(j)];
			if (a.right < b.left || b.right < a.left || a.bottom < b.top || b.bottom < a.top)
				continue;
			setAnimField(i, kIntersected, j);
			break;
		}
	}

	// Restore the dirty areas from the clean background.
	for (int i = 0; i < n; i++) {
		MultObject &o = _objects[size_t(i)];
		if (!needRedraw[size_t(i)] || o.lastLeft == -1)
			continue;
		const Box &b = dirty[size_t(i)];
		_draw.sourceSurface = draw::kAnimSurface;
		_draw.destSurface = draw::kBackSurface;
		_draw.spriteLeft = int16_t(b.left - _multLeft);
		_draw.spriteTop = int16_t(b.top - _multTop);
		_draw.spriteRight = int16_t(b.right - b.left + 1);
		_draw.spriteBottom = int16_t(b.bottom - b.top + 1);
		_draw.destSpriteX = b.left;
		_draw.destSpriteY = b.top;
		_draw.transparency = 0;
		spriteOp(draw::BlitSurf);
		o.lastLeft = -1;
	}

	// Draw in order; static planes in front of each order go back on top.
	for (int order = minOrder; order <= maxOrder; order++) {
		for (int i = 0; i < n; i++) {
			if (animField(i, kOrder) != order)
				continue;
			MultObject &o = _objects[size_t(i)];
			if (needRedraw[size_t(i)]) {
				if (!animField(i, kIsStatic)) {
					_scenery.updateAnim(animField(i, kLayer), animField(i, kFrame), animField(i, kAnimation), 2,
					                    objPosX(i), objPosY(i), true);
					if (_scenery.toRedrawLeft != -12345) {
						o.lastLeft = _scenery.toRedrawLeft;
						o.lastTop = _scenery.toRedrawTop;
						o.lastRight = _scenery.toRedrawRight;
						o.lastBottom = _scenery.toRedrawBottom;
					} else {
						o.lastLeft = -1;
					}
				}
				_scenery.updateStatic(order + 1);
			} else if (!animField(i, kIsStatic)) {
				for (int j = 0; j < n; j++) {
					if (!needRedraw[size_t(j)])
						continue;
					const Box &a = dirty[size_t(i)], &b = dirty[size_t(j)];
					if (a.right < b.left || b.right < a.left || a.bottom < b.top || b.bottom < a.top)
						continue;
					_scenery.toRedrawLeft = b.left;
					_scenery.toRedrawRight = b.right;
					_scenery.toRedrawTop = b.top;
					_scenery.toRedrawBottom = b.bottom;
					_scenery.updateAnim(animField(i, kLayer), animField(i, kFrame), animField(i, kAnimation), 4,
					                    objPosX(i), objPosY(i), true);
					_scenery.updateStatic(order + 1);
				}
			}
		}
	}

	// Advance.
	for (int i = 0; i < n; i++) {
		if (animField(i, kIsStatic) || animField(i, kIsPaused))
			continue;
		MultObject &o = _objects[size_t(i)];
		if (o.tick == animField(i, kMaxTick)) {
			o.tick = 0;
			if (animField(i, kAnimType) == 4) {
				setAnimField(i, kIsPaused, 1);
				setAnimField(i, kFrame, 0);
			} else {
				newCycleAnimV1(i);
			}
		} else {
			o.tick++;
		}
	}
}

} // namespace gob
