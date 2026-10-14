// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/scenery.h"

#include <algorithm>

namespace gob {

namespace {

int16_t le16s(const std::vector<uint8_t> &d, size_t o) {
	return o + 1 < d.size() ? int16_t(d[o] | (d[o + 1] << 8)) : 0;
}

} // namespace

Scenery::Scenery(Draw &draw) : _draw(draw) {
	_spriteResId.fill(-1);
}

int Scenery::findAnim(int resId) const {
	for (int i = 0; i < kAnimCount; i++)
		if (_anims[i].pictCount != 0 && _anims[i].resId == resId)
			return i;
	return -1;
}

int Scenery::lowestFreeSlot() const {
	for (int i = 0; i < kAnimCount; i++)
		if (_anims[i].pictCount == 0)
			return i;
	return -1;
}

const Scenery::Layer *Scenery::layer(int slot, int layer) const {
	if (slot < 0 || slot >= kAnimCount || _anims[slot].pictCount == 0)
		return nullptr;
	if (layer < 0 || layer >= int(_anims[slot].layers.size()))
		return nullptr;
	return &_anims[slot].layers[size_t(layer)];
}

bool Scenery::loadAnim(int slot, int resId, const std::vector<PictureDesc> &pics, const ResourceLoader &res) {
	if (slot < 0 || slot >= kAnimCount)
		return false;
	auto data = res(uint16_t(resId));
	if (!data)
		return false;

	freeAnim(slot);
	Anim &a = _anims[slot];
	a.pictCount = int(pics.size());
	a.resId = resId;

	// Layers: i16 count, u16 offsets, then each layer's 13-byte header and
	// 5-byte pieces.
	const std::vector<uint8_t> &d = data->data;
	int layersCount = le16s(d, 0);
	for (int i = 0; i < layersCount; i++) {
		size_t off = uint16_t(le16s(d, 2 + size_t(i) * 2));
		Layer l;
		l.unknown0 = le16s(d, off);
		l.posX = le16s(d, off + 2);
		l.posY = le16s(d, off + 4);
		l.animDeltaX = le16s(d, off + 6);
		l.animDeltaY = le16s(d, off + 8);
		l.transp = off + 10 < d.size() ? int8_t(d[off + 10]) : 0;
		l.framesCount = le16s(d, off + 11);

		size_t p = off + 13;
		for (int f = 0; f < std::max<int>(l.framesCount, 0); f++) {
			l.frameStart.push_back(l.pieces.size());
			while (p + 5 <= d.size()) {
				Piece pc{d[p], d[p + 1], int8_t(d[p + 2]), int8_t(d[p + 3]), int8_t(d[p + 4])};
				l.pieces.push_back(pc);
				p += 5;
				if (pc.notFinal != 1)
					break;
			}
		}
		a.layers.push_back(std::move(l));
	}

	for (size_t p = 0; p < pics.size() && p < size_t(kMaxPictures); p++) {
		a.pieces.push_back(loadPieces(pics[p].pieceTableId, res));
		a.pictToSprite[p] = acquireSprite(pics[p], res);
		a.pictResId[p] = pics[p].spriteResId;
	}
	return true;
}

// Piece table: records of i16 left, right, top, bottom (inclusive).
std::vector<Scenery::PieceRect> Scenery::loadPieces(int resId, const ResourceLoader &res) {
	std::vector<PieceRect> rects;
	if (auto t = res(uint16_t(resId)))
		for (size_t o = 0; o + 8 <= t->data.size(); o += 8)
			rects.push_back({le16s(t->data, o), le16s(t->data, o + 2), le16s(t->data, o + 4), le16s(t->data, o + 6)});
	return rects;
}

// Picture sprites are shared by resource id and allocated downward from
// sprite slot 19.
int Scenery::acquireSprite(const PictureDesc &pic, const ResourceLoader &res) {
	int sprite = -1;
	for (int s = 0; s < 20; s++)
		if (_spriteResId[s] == pic.spriteResId && _spriteRefs[s] > 0) {
			sprite = s;
			break;
		}
	if (sprite >= 0) {
		_spriteRefs[sprite]++;
		return sprite;
	}
	for (int s = 19; s >= 0; s--)
		if (!_draw.sprite(s)) {
			sprite = s;
			break;
		}
	if (sprite < 0)
		sprite = 0;
	_spriteRefs[sprite] = 1;
	_spriteResId[sprite] = pic.spriteResId;
	_draw.createSprite(sprite, pic.width, pic.height);
	if (auto sr = res(uint16_t(pic.spriteResId)))
		if (auto p = _draw.decodePicture(*sr, uint16_t(pic.spriteResId), true, PictureUse::Animation))
			_draw.sprite(sprite)->blit(*p, 0, 0, p->width() - 1, p->height() - 1, 0, 0);
	return sprite;
}

void Scenery::releaseSprite(int s) {
	if (s < 0 || s >= 20)
		return;
	if (--_spriteRefs[s] <= 0) {
		_spriteRefs[s] = 0;
		_draw.freeSprite(s);
		_spriteResId[s] = -1;
	}
}

void Scenery::freeAnim(int slot) {
	if (slot < 0 || slot >= kAnimCount || _anims[slot].pictCount == 0)
		return;
	Anim &a = _anims[slot];
	for (int p = 0; p < a.pictCount && p < kMaxPictures; p++)
		releaseSprite(a.pictToSprite[p]);
	a = Anim{};
}

int Scenery::findStatic(int resId, int &index) const {
	for (int i = 0; i < kAnimCount; i++) {
		if (_statics[i].pictCount != -1 && _statics[i].resId == resId)
			return i;
		if (_statics[i].pictCount == -1 && i < index)
			index = i;
	}
	return -1;
}

bool Scenery::loadStatic(int slot, int resId, const std::vector<int16_t> &backs, const std::vector<PictureDesc> &pics,
                         const ResourceLoader &res) {
	if (slot < 0 || slot >= kAnimCount)
		return false;
	auto data = res(uint16_t(resId));
	if (!data)
		return false;

	freeStatic(slot);
	Static &st = _statics[slot];
	st.pictCount = int(pics.size());
	st.resId = resId;

	// Layers: i16 count, u16 offsets, then per layer i16 back (replaced by
	// the script's list), i16 plane count and 8-byte planes.
	const std::vector<uint8_t> &d = data->data;
	int layersCount = le16s(d, 0);
	for (int i = 0; i < layersCount; i++) {
		size_t off = uint16_t(le16s(d, 2 + size_t(i) * 2));
		StaticLayer l;
		l.backResId = size_t(i) < backs.size() ? backs[size_t(i)] : -1;
		int planes = le16s(d, off + 2);
		for (int j = 0; j < planes; j++) {
			size_t p = off + 4 + size_t(j) * 8;
			if (p + 8 > d.size())
				break;
			l.planes.push_back({d[p], d[p + 1], d[p + 2], le16s(d, p + 3), le16s(d, p + 5), int8_t(d[p + 7])});
		}
		st.layers.push_back(std::move(l));
	}

	for (size_t p = 0; p < pics.size() && p < size_t(kMaxPictures); p++) {
		st.pieces.push_back(loadPieces(pics[p].pieceTableId, res));
		st.pictToSprite[p] = acquireSprite(pics[p], res);
	}
	return true;
}

void Scenery::freeStatic(int slot) {
	if (slot < 0 || slot >= kAnimCount || _statics[slot].pictCount == -1)
		return;
	Static &st = _statics[slot];
	for (int p = 0; p < st.pictCount && p < kMaxPictures; p++)
		releaseSprite(st.pictToSprite[p]);
	st = Static{};
}

void Scenery::renderStatic(int slot, int layer, const ResourceLoader &res) {
	if (slot < 0 || slot >= kAnimCount || layer < 0 || layer >= int(_statics[slot].layers.size()))
		return;
	const Static &st = _statics[slot];
	const StaticLayer &l = st.layers[size_t(layer)];

	if (l.backResId != -1) {
		_draw.spriteLeft = l.backResId;
		_draw.destSpriteX = 0;
		_draw.destSpriteY = 0;
		_draw.destSurface = draw::kBackSurface;
		_draw.transparency = 0;
		_draw.spriteOperation(draw::LoadSprite, res);
	}

	for (int order = 0; order < maxDrawOrder; order++) {
		for (const StaticPlane &pl : l.planes) {
			if (pl.drawOrder != order)
				continue;
			unsigned pict = unsigned(pl.pictIndex - 1);
			if (pict >= unsigned(st.pictCount) || pict >= st.pieces.size() || pl.pieceIndex >= st.pieces[pict].size())
				continue;
			const PieceRect &r = st.pieces[pict][pl.pieceIndex];
			_draw.destSpriteX = pl.destX;
			_draw.destSpriteY = pl.destY;
			_draw.sourceSurface = int16_t(st.pictToSprite[pict]);
			_draw.destSurface = draw::kBackSurface;
			_draw.spriteLeft = r.left;
			_draw.spriteTop = r.top;
			_draw.spriteRight = int16_t(r.right - r.left + 1);
			_draw.spriteBottom = int16_t(r.bottom - r.top + 1);
			_draw.transparency = pl.transp ? 3 : 0;
			_draw.spriteOperation(draw::BlitSurf, [](uint16_t) { return std::optional<Resource>(); });
		}
	}
}

void Scenery::updateStatic(int orderFrom, int slot, int layer) {
	if (slot < 0 || slot >= kAnimCount || layer < 0 || layer >= int(_statics[slot].layers.size()))
		return;
	const Static &st = _statics[slot];
	const StaticLayer &l = st.layers[size_t(layer)];

	for (int order = orderFrom; order < maxDrawOrder; order++) {
		for (const StaticPlane &pl : l.planes) {
			if (pl.drawOrder != order)
				continue;
			unsigned pict = unsigned(pl.pictIndex - 1);
			if (pict >= unsigned(st.pictCount) || pict >= st.pieces.size() || pl.pieceIndex >= st.pieces[pict].size())
				continue;
			const PieceRect &r = st.pieces[pict][pl.pieceIndex];
			int x = pl.destX, y = pl.destY;
			int left = r.left, top = r.top;
			if (x > toRedrawRight || y > toRedrawBottom)
				continue;
			if (x < toRedrawLeft) {
				left += toRedrawLeft - x;
				x = toRedrawLeft;
			}
			if (y < toRedrawTop) {
				top += toRedrawTop - y;
				y = toRedrawTop;
			}
			int w = r.right - left + 1, h = r.bottom - top + 1;
			if (w <= 0 || h <= 0)
				continue;
			if (x + w - 1 > toRedrawRight)
				w = toRedrawRight - x + 1;
			if (y + h - 1 > toRedrawBottom)
				h = toRedrawBottom - y + 1;
			_draw.destSpriteX = int16_t(x);
			_draw.destSpriteY = int16_t(y);
			_draw.spriteLeft = int16_t(left);
			_draw.spriteTop = int16_t(top);
			_draw.spriteRight = int16_t(w);
			_draw.spriteBottom = int16_t(h);
			_draw.sourceSurface = int16_t(st.pictToSprite[pict]);
			_draw.destSurface = draw::kBackSurface;
			_draw.transparency = pl.transp ? 3 : 0;
			_draw.spriteOperation(draw::BlitSurf, [](uint16_t) { return std::optional<Resource>(); });
		}
	}
}

void Scenery::updateStatic(int orderFrom) {
	if (curStatic == -1)
		return;
	if (curStatic < 10000) {
		updateStatic(orderFrom, curStatic & 0xFF, curStaticLayer & 0xFF);
		if (curStatic & 0xFF00)
			updateStatic(orderFrom, ((curStatic >> 8) & 0xFF) - 1, (curStaticLayer >> 8) & 0xFF);
	} else {
		for (int i = 0; i < curStatic - 10000; i++)
			updateStatic(orderFrom, i, 0);
	}
}

void Scenery::reset() {
	for (int i = 0; i < kAnimCount; i++) {
		freeAnim(i);
		freeStatic(i);
	}
	curStatic = curStaticLayer = -1;
	for (int s = 0; s < 20; s++)
		if (_spriteRefs[s] > 0)
			_draw.freeSprite(s);
	_spriteRefs.fill(0);
	_spriteResId.fill(-1);
}

void Scenery::updateAnim(int layerIndex, int frame, int slot, int flags, int dx, int dy, bool doDraw) {
	const Layer *l = layer(slot, layerIndex);
	if (!l || frame < 0 || frame >= l->framesCount || size_t(frame) >= l->frameStart.size())
		return; // outputs stay stale, as in the original

	const Anim &a = _anims[slot];

	int16_t winLeft = toRedrawLeft, winTop = toRedrawTop, winRight = toRedrawRight, winBottom = toRedrawBottom;
	if (flags & 4) {
		winLeft = int16_t(std::max<int>(winLeft, areaLeft));
		winTop = int16_t(std::max<int>(winTop, areaTop));
		winRight = int16_t(std::min<int>(winRight, areaLeft + areaWidth - 1));
		winBottom = int16_t(std::min<int>(winBottom, areaTop + areaHeight - 1));
		toRedrawLeft = winLeft;
		toRedrawTop = winTop;
		toRedrawRight = winRight;
		toRedrawBottom = winBottom;
	} else {
		toRedrawLeft = -12345;
	}

	int transparency = l->transp ? 3 : 0;
	bool first = true;

	for (size_t i = l->frameStart[size_t(frame)]; i < l->pieces.size(); i++) {
		const Piece &pc = l->pieces[i];

		// High bits of pictByte extend the 8-bit offsets by multiples of 128,
		// growing their magnitude away from zero.
		int hx = (pc.pictByte >> 6) & 3, hy = (pc.pictByte >> 4) & 3;
		int destX = pc.destX >= 0 ? pc.destX + hx * 128 : pc.destX - hx * 128;
		int destY = pc.destY >= 0 ? pc.destY + hy * 128 : pc.destY - hy * 128;
		destX += dx == 1000 ? l->posX : dx;
		destY += dy == 1000 ? l->posY : dy;

		unsigned pict = unsigned((pc.pictByte & 15) - 1);
		bool last = pc.notFinal != 1;
		if (pict >= unsigned(a.pictCount) || pict >= a.pieces.size() || pc.pieceIndex >= a.pieces[pict].size()) {
			if (last)
				break;
			continue;
		}
		PieceRect r = a.pieces[pict][pc.pieceIndex];
		int left = r.left, right = r.right, top = r.top, bottom = r.bottom;

		int clipL, clipT, clipR, clipB;
		bool clip = false;
		if (flags & 2) {
			clipL = areaLeft; clipT = areaTop;
			clipR = areaLeft + areaWidth - 1; clipB = areaTop + areaHeight - 1;
			clip = true;
		} else if (flags & 4) {
			clipL = winLeft; clipT = winTop; clipR = winRight; clipB = winBottom;
			clip = true;
		}
		if (clip) {
			if (destX < clipL) { left += clipL - destX; destX = clipL; }
			if (destX + (right - left) > clipR) right -= destX + (right - left) - clipR;
			if (destY < clipT) { top += clipT - destY; destY = clipT; }
			if (destY + (bottom - top) > clipB) bottom -= destY + (bottom - top) - clipB;
		}

		if (left <= right && top <= bottom) {
			if (doDraw) {
				_draw.sourceSurface = int16_t(a.pictToSprite[pict]);
				_draw.destSurface = draw::kBackSurface;
				_draw.spriteLeft = int16_t(left);
				_draw.spriteTop = int16_t(top);
				_draw.spriteRight = int16_t(right - left + 1);
				_draw.spriteBottom = int16_t(bottom - top + 1);
				_draw.destSpriteX = int16_t(destX);
				_draw.destSpriteY = int16_t(destY);
				_draw.transparency = int16_t(transparency);
				_draw.spriteOperation(draw::BlitSurf, [](uint16_t) { return std::optional<Resource>(); });
			}
			if (!(flags & 4)) {
				int16_t pl = int16_t(destX), pt = int16_t(destY);
				int16_t pr = int16_t(destX + right - left), pb = int16_t(destY + bottom - top);
				if (first) {
					toRedrawLeft = pieceLeft = pl;
					toRedrawTop = pieceTop = pt;
					toRedrawRight = pieceRight = pr;
					toRedrawBottom = pieceBottom = pb;
					first = false;
				} else {
					toRedrawLeft = std::min(toRedrawLeft, pl);
					toRedrawTop = std::min(toRedrawTop, pt);
					toRedrawRight = std::max(toRedrawRight, pr);
					toRedrawBottom = std::max(toRedrawBottom, pb);
				}
			}
		}
		if (last)
			break;
	}
}

} // namespace gob
