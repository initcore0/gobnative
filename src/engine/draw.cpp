// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/draw.h"

#include <algorithm>

namespace gob {

using namespace draw;

Draw::Draw() {
	_front = std::make_shared<Surface>(kScreenW, kScreenH);
	_sprites[kFrontSurface] = _front;
	initScreen();
}

void Draw::initScreen(int width, int height, int split) {
	_back = std::make_shared<Surface>(width, height);
	attachLayer(*_back);
	_sprites[kBackSurface] = _back;
	if (_front->width() != width || _front->height() != height) {
		_front = std::make_shared<Surface>(width, height);
		attachLayer(*_front);
	}
	_sprites[kFrontSurface] = _front;
	scrollX = scrollY = viewX = viewY = 0;
	splitHeight = split;
	splitStart = height - split;
	_viewValid = false;
	if (!_sprites[kCursorSurface]) {
		_sprites[kCursorSurface] = std::make_shared<Surface>(32, 16);
		attachLayer(*_sprites[kCursorSurface]);
	}
	cursorAnimLow.fill(-1);
	_invalidated.clear();
	_noInvalidated = true;
	_textCells.clear();
}

std::shared_ptr<Surface> &Draw::sprite(int index) {
	static std::shared_ptr<Surface> none;
	index = surfaceIndex(index);
	if (index < 0 || index >= kSpriteCount) {
		none.reset();
		return none;
	}
	return _sprites[index];
}

void Draw::createSprite(int index, int width, int height) {
	index = surfaceIndex(index);
	if (index < 0 || index >= kSpriteCount || width <= 0 || height <= 0)
		return;
	_sprites[index] = std::make_shared<Surface>(width, height);
	attachLayer(*_sprites[index]);
}

void Draw::attachLayer(Surface &s) {
	s.trackSources(_trackSources);
	if (_hdScale <= 0) {
		s.detachHd();
		return;
	}
	if (!s.hd() || s.hd()->scale != _hdScale)
		s.attachHd(_hdScale);
	if (&s == _front.get())
		s.hd()->trackDirty = true;
}

std::vector<Surface *> Draw::surfaces() {
	std::vector<Surface *> out;
	auto visit = [&](const std::shared_ptr<Surface> &s) {
		if (s && std::find(out.begin(), out.end(), s.get()) == out.end())
			out.push_back(s.get());
	};
	visit(_front);
	visit(_back);
	for (const auto &s : _sprites)
		visit(s);
	visit(_view);
	return out;
}

void Draw::setHdScale(int scale) {
	_hdScale = std::max(0, scale);
	for (Surface *s : surfaces())
		attachLayer(*s);
	_viewValid = false;
}

// Copies the part of front rectangle (inclusive) that the display shows into
// the view: the scrolled window above the split, the split rows below it.
void Draw::composeView(int left, int top, int right, int bottom) {
	const int split = std::clamp(splitHeight, 0, kScreenH);
	struct Region { int srcX, srcY, w, h, dstY; };
	const Region regions[2] = {
		{viewX, viewY, std::min(_front->width() - viewX, kScreenW), std::min(_front->height() - split - viewY, kScreenH - split), 0},
		{0, splitStart, std::min(_front->width(), kScreenW), split, kScreenH - split},
	};
	for (const Region &r : regions) {
		int l = std::max(left, r.srcX), t = std::max(top, r.srcY);
		int rt = std::min(right, r.srcX + r.w - 1), b = std::min(bottom, r.srcY + r.h - 1);
		if (l <= rt && t <= b)
			_view->blit(*_front, l, t, rt, b, l - r.srcX, t - r.srcY + r.dstY);
	}
}

Surface &Draw::screenSurface() {
	if (!composed())
		return *_front;
	if (!_view) {
		_view = std::make_shared<Surface>(kScreenW, kScreenH);
		_viewValid = false;
	}
	if (_hdScale > 0 && (!_view->hd() || _view->hd()->scale != _hdScale)) {
		attachLayer(*_view);
		_viewValid = false;
	} else if (_hdScale <= 0 && _view->hd()) {
		_view->detachHd();
	}
	if (_view->hd())
		_view->hd()->trackDirty = true;
	if (_view->sources() == nullptr && _trackSources)
		_view->trackSources(true);

	HdLayer *fl = _front->hd();
	const bool full = !_viewValid || !fl || !fl->trackDirty || viewX != _viewFromX || viewY != _viewFromY ||
	                  splitHeight != _viewSplit || _viewFront != _front.get();
	if (full) {
		composeView(0, 0, _front->width() - 1, _front->height() - 1);
	} else {
		for (int ty = 0; ty < fl->tilesY; ty++)
			for (int tx = 0; tx < fl->tilesX; tx++)
				if (fl->dirty[size_t(ty) * size_t(fl->tilesX) + size_t(tx)]) {
					const int l = tx * HdLayer::kTileW, t = ty * HdLayer::kTileH;
					composeView(l, t, l + HdLayer::kTileW - 1, t + HdLayer::kTileH - 1);
				}
	}
	if (fl)
		std::fill(fl->dirty.begin(), fl->dirty.end(), 0); // the view took the changes
	_viewValid = true;
	_viewFromX = viewX;
	_viewFromY = viewY;
	_viewSplit = splitHeight;
	_viewFront = _front.get();
	return *_view;
}

void Draw::setTrackSources(bool on) {
	_trackSources = on;
	for (Surface *s : surfaces())
		s->trackSources(on);
	if (!on)
		_pictures.clear();
}

size_t Draw::rebuildHd(int scale, const std::function<const HdLayer *(int pic)> &picture,
                       const std::vector<uint8_t> *only) {
	if (only) { // some pictures again, in the layers there are
		size_t lost = 0;
		for (Surface *s : surfaces())
			lost += s->rebuildHd(_hdScale, picture, only);
		return lost;
	}
	_hdScale = std::max(0, scale);
	size_t lost = 0;
	for (Surface *s : surfaces()) {
		if (_hdScale <= 0) {
			s->detachHd();
			continue;
		}
		lost += s->rebuildHd(_hdScale, picture);
		if (s == _front.get() || s == _view.get())
			s->hd()->trackDirty = true; // and every tile is dirty
	}
	_viewValid = false;
	return lost;
}

int Draw::registerPicture(const Surface &pic, int resId, bool transparent, PictureUse use) {
	const std::string tot = scriptName ? scriptName() : std::string();
	const size_t size = size_t(pic.width()) * size_t(pic.height());
	for (size_t i = 0; i < _pictures.size(); i++) {
		const PictureSource &p = _pictures[i];
		if (p.resId == resId && p.transparent == transparent && p.use == use && p.width == pic.width() && p.height == pic.height() &&
		    p.tot == tot && std::equal(p.pixels.begin(), p.pixels.end(), pic.data()))
			return int(i) + 1;
	}
	if (_pictures.size() >= size_t(PixelSources::kMaxPictures))
		return 0;
	PictureSource p;
	p.tot = tot;
	p.resId = resId;
	p.transparent = transparent;
	p.use = use;
	p.width = pic.width();
	p.height = pic.height();
	p.pixels.assign(pic.data(), pic.data() + size);
	_pictures.push_back(std::move(p));
	return int(_pictures.size());
}

std::unique_ptr<Surface> Draw::decodePicture(const Resource &res, int resId, bool transparent, PictureUse use) {
	auto img = decodeSprite(res);
	if (!img)
		return nullptr;
	auto pic = std::make_unique<Surface>(img->width, img->height);
	std::copy(img->pixels.begin(), img->pixels.end(), pic->data());
	if (_trackSources && pic->width() <= PixelSources::kMaxSide && pic->height() <= PixelSources::kMaxSide) {
		pic->trackSources(true);
		pic->setPictureSources(registerPicture(*pic, resId, transparent, use));
	}
	if (_hdScale > 0 && pictureHook)
		pictureHook(*pic, resId, transparent, use);
	return pic;
}

void Draw::freeSprite(int index) {
	index = surfaceIndex(index);
	if (index < 0 || index >= kSpriteCount)
		return;
	_sprites[index].reset();
	if (index == kFrontSurface)
		_sprites[index] = _front;
	if (index == kBackSurface)
		_sprites[index] = _back;
}

// Keeps a short list of horizontal bands of the back surface that need
// copying to the front. Left/right are widened to 16-pixel boundaries and
// overlapping or vertically adjacent areas are merged, as in the original.
void Draw::invalidateRect(int left, int top, int right, int bottom) {
	const int w = _back->width(), h = _back->height();
	if (renderFlags & NoInvalidate) {
		_invalidated.assign(1, {0, 0, w - 1, h - 1});
		_noInvalidated = false;
		return;
	}

	if (left > right) std::swap(left, right);
	if (top > bottom) std::swap(top, bottom);
	if (left > w - 1 || right < 0 || top > h - 1 || bottom < 0)
		return;

	_noInvalidated = false;

	if (_invalidated.size() >= 30) {
		_invalidated.assign(1, {0, 0, w - 1, h - 1});
		return;
	}

	left = std::max(left, 0) & 0xFFF0;
	right = std::min(right, w - 1) | 0x000F;
	top = std::max(top, 0);
	bottom = std::min(bottom, h - 1);

	for (size_t i = 0; i < _invalidated.size(); i++) {
		Rect &r = _invalidated[i];
		if (r.top > top) {
			if (r.top > bottom) {
				_invalidated.insert(_invalidated.begin() + long(i), {left, top, right, bottom});
				return;
			}
			r.bottom = std::max(r.bottom, bottom);
			r.left = std::min(r.left, left);
			r.right = std::max(r.right, right);
			r.top = top;
			return;
		}
		if (r.bottom < top)
			continue;
		r.bottom = std::max(r.bottom, bottom);
		r.left = std::min(r.left, left);
		r.right = std::max(r.right, right);
		return;
	}
	_invalidated.push_back({left, top, right, bottom});
}

void Draw::dirtiedRect(int surface, int left, int top, int right, int bottom) {
	const auto &s = sprite(surface);
	if (s && s == _back)
		invalidateRect(left, top, right, bottom);
	// Drawing straight to the front surface is visible immediately.
}

void Draw::forceBlit(bool backwards) {
	if (_front == _back)
		return;
	if (backwards)
		_back->blit(*_front);
	else
		_front->blit(*_back);
}

void Draw::blitInvalidated() {
	if (cursorIndex == 4)
		blitCursor(); // hide the wait cursor

	if (_noInvalidated && !applyPal)
		return;

	if (_noInvalidated) {
		setPalette();
		applyPal = false;
		return;
	}

	if (cursorStrip())
		showCursor = uint8_t((showCursor & ~2) | ((showCursor & 1) << 1));

	if (applyPal) {
		clearPalette();
		forceBlit();
		setPalette();
		_invalidated.clear();
		_noInvalidated = true;
		applyPal = false;
		return;
	}

	for (const Rect &r : _invalidated)
		_front->blit(*_back, r.left, r.top, r.right, r.bottom, r.left, r.top);
	_invalidated.clear();
	_noInvalidated = true;
	applyPal = false;
}

Palette Draw::toRgb(const VgaPalette &vga) {
	Palette pal;
	for (int i = 0; i < 256; i++) {
		auto conv = [](uint8_t v) { v &= 0x3F; return uint8_t((v << 2) | (v >> 4)); };
		pal[i] = {conv(vga[i * 3]), conv(vga[i * 3 + 1]), conv(vga[i * 3 + 2])};
	}
	return pal;
}

namespace {
bool anyLit(const VgaPalette &p) {
	return std::any_of(p.begin(), p.end(), [](uint8_t v) { return (v & 0x3F) != 0; });
}
} // namespace

void Draw::setPalette() {
	if (anyLit(vgaPalette))
		litPalette = vgaPalette;
	_screenVga = vgaPalette;
	_screenPal = toRgb(_screenVga);
	paletteCleared = false;
}

void Draw::clearPalette() {
	if (paletteCleared)
		return;
	_screenVga.fill(0);
	_screenPal = toRgb(_screenVga);
	paletteCleared = true;
}

bool Draw::fadeStep(const VgaPalette &target, int step) {
	if (anyLit(target))
		litPalette = target;
	bool done = true;
	for (size_t i = 0; i < _screenVga.size(); i++) {
		int cur = _screenVga[i];
		int to = target[i] & 0x3F;
		if (cur < to)
			cur = std::min(cur + step, to);
		else if (cur > to)
			cur = std::max(cur - step, to);
		_screenVga[i] = uint8_t(cur);
		done &= cur == to;
	}
	_screenPal = toRgb(_screenVga);
	paletteCleared = false;
	return done;
}

void Draw::spriteOperation(int operation, const ResourceLoader &resources) {
	bool deltaVeto = (operation & 0x10) != 0;
	operation &= 0x0F;

	if (sourceSurface >= 100)
		sourceSurface -= 80;
	if (destSurface >= 100)
		destSurface -= 80;

	if ((renderFlags & UseDeltas) && !deltaVeto) {
		if (sourceSurface == kBackSurface && operation != LoadSprite) {
			spriteLeft += backDeltaX;
			spriteTop += backDeltaY;
		}
		if (destSurface == kBackSurface) {
			destSpriteX += backDeltaX;
			destSpriteY += backDeltaY;
			if (operation == DrawLine || (operation >= DrawBar && operation <= FillRectAbs)) {
				spriteRight += backDeltaX;
				spriteBottom += backDeltaY;
			}
		}
	}

	// Parameters are restored afterwards; operations work on copies.
	int16_t sLeft = spriteLeft, sTop = spriteTop, sRight = spriteRight, sBottom = spriteBottom;
	int16_t sDestX = destSpriteX, sDestY = destSpriteY, sDest = destSurface, sSrc = sourceSurface;

	// The original's coordinate adjustments (inclusive-edge +1, line to
	// rectangle conversion, corner swapping) only run when its "needAdjust"
	// mode is not 2. Gobliins 2 always runs in mode 2, so none apply.
	if (operation == DrawLetter)
		operation = BlitSurf;

	std::shared_ptr<Surface> src = sprite(sourceSurface);
	std::shared_ptr<Surface> dst = sprite(destSurface);

	if (dst) {
		switch (operation) {
		case BlitSurf:
			if (src) {
				dst->blit(*src, spriteLeft, spriteTop, spriteLeft + spriteRight - 1, spriteTop + spriteBottom - 1,
				          destSpriteX, destSpriteY, transparency == 0 ? -1 : 0, (transparency & 0x80) != 0);
				dirtiedRect(destSurface, destSpriteX, destSpriteY, destSpriteX + spriteRight - 1,
				            destSpriteY + spriteBottom - 1);
			}
			break;

		case PutPixel:
			dst->putPixel(destSpriteX, destSpriteY, uint8_t(frontColor));
			dirtiedRect(destSurface, destSpriteX, destSpriteY, destSpriteX, destSpriteY);
			break;

		case FillRect:
			// High-byte "shading" fills are rare; treat them as plain fills.
			dst->fillRect(destSpriteX, destSpriteY, destSpriteX + spriteRight - 1, destSpriteY + spriteBottom - 1,
			              uint8_t(backColor));
			dirtiedRect(destSurface, destSpriteX, destSpriteY, destSpriteX + spriteRight - 1,
			            destSpriteY + spriteBottom - 1);
			break;

		case DrawLine:
			dst->drawLine(destSpriteX, destSpriteY, spriteRight, spriteBottom, uint8_t(frontColor));
			dirtiedRect(destSurface, destSpriteX, destSpriteY, spriteRight, spriteBottom);
			break;

		case Invalidate:
			dst->drawCircle(destSpriteX, destSpriteY, spriteRight, uint8_t(frontColor));
			dirtiedRect(destSurface, destSpriteX - spriteRight, destSpriteY - spriteBottom, destSpriteX + spriteRight,
			            destSpriteY + spriteBottom);
			break;

		case LoadSprite:
			if (auto res = resources(uint16_t(spriteLeft))) {
				const bool screen = dst == _back || dst == _front;
				if (auto pic = decodePicture(*res, uint16_t(spriteLeft), transparency != 0,
				                             screen ? PictureUse::Screen : PictureUse::Sprite)) {
					const int w = pic->width(), h = pic->height();
					dst->blit(*pic, 0, 0, w - 1, h - 1, destSpriteX, destSpriteY, transparency ? 0 : -1);
					dirtiedRect(destSurface, destSpriteX, destSpriteY, destSpriteX + w - 1, destSpriteY + h - 1);
				}
			}
			break;

		case DrawBar: // rectangle outline, one pixel wide in this mode
			dst->drawLine(destSpriteX, spriteBottom, spriteRight, spriteBottom, uint8_t(frontColor));
			dst->drawLine(destSpriteX, destSpriteY, destSpriteX, spriteBottom, uint8_t(frontColor));
			dst->drawLine(spriteRight, destSpriteY, spriteRight, spriteBottom, uint8_t(frontColor));
			dst->drawLine(destSpriteX, destSpriteY, spriteRight, destSpriteY, uint8_t(frontColor));
			dirtiedRect(destSurface, destSpriteX, destSpriteY, spriteRight, spriteBottom);
			break;

		case ClearRect:
			if (backColor != 16 && backColor != 144)
				dst->fillRect(destSpriteX, destSpriteY, spriteRight, spriteBottom, uint8_t(backColor));
			dirtiedRect(destSurface, destSpriteX, destSpriteY, spriteRight, spriteBottom);
			break;

		case FillRectAbs:
			dst->fillRect(destSpriteX, destSpriteY, spriteRight, spriteBottom, uint8_t(backColor));
			dirtiedRect(destSurface, destSpriteX, destSpriteY, spriteRight, spriteBottom);
			break;

		case PrintText:
			if (const Font *f = font(fontIndex)) {
				int startX = destSpriteX;
				destSpriteX = int16_t(drawText(*dst, *f, textToPrint, destSpriteX, destSpriteY, uint8_t(frontColor),
				                               uint8_t(backColor), transparency != 0));
				dirtiedRect(destSurface, startX, destSpriteY, destSpriteX - 1, destSpriteY + f->height() - 1);
			}
			break;

		default:
			break;
		}
	}

	spriteLeft = sLeft;
	spriteTop = sTop;
	spriteRight = sRight;
	spriteBottom = sBottom;
	if (operation != PrintText)
		destSpriteX = sDestX;
	destSpriteY = sDestY;
	destSurface = sDest;
	sourceSurface = sSrc;

	if ((renderFlags & UseDeltas) && !deltaVeto) {
		if (sourceSurface == kBackSurface) {
			spriteLeft -= backDeltaX;
			spriteTop -= backDeltaY;
		}
		if (destSurface == kBackSurface) {
			destSpriteX -= backDeltaX;
			destSpriteY -= backDeltaY;
		}
	}
}

int Draw::drawText(Surface &dst, const Font &f, const std::string &s, int x, int y, uint8_t front, uint8_t back,
                   bool transparent) {
	const bool screen = &dst == _front.get() || &dst == _back.get();
	if (!recordText || !screen || !f.monospaced() || f.width() > kCellW || f.height() > kCellH)
		return f.drawString(dst, s, x, y, front, back, transparent);

	const int w = f.width(), h = f.height();
	for (unsigned char c : s) {
		// As Font::drawString: a cell is drawn only when it fits entirely.
		if (x >= 0 && y >= 0 && x + w <= dst.width() && y + h <= dst.height()) {
			std::array<uint8_t, kCellW * kCellH> before{};
			for (int j = 0; j < h; j++)
				for (int i = 0; i < w; i++)
					before[size_t(j * 8 + i)] = dst.get(x + i, y + j);
			f.drawChar(dst, c, x, y, front, back, transparent);
			TextCell cell;
			cell.x = int16_t(x);
			cell.y = int16_t(y);
			cell.w = uint8_t(w);
			cell.h = uint8_t(h);
			cell.front = front;
			cell.ink = f.glyphRows(c);
			for (int j = 0; j < h; j++)
				for (int i = 0; i < w; i++)
					cell.expect[size_t(j * 8 + i)] = dst.get(x + i, y + j);
			if (f.hasChar(c))
				recordCell(cell, before, transparent, back);
		}
		x += w;
	}
	return x;
}

void Draw::recordCell(TextCell cell, const std::array<uint8_t, kCellW * kCellH> &before, bool transparent,
                      uint8_t back) {
	// The background under the new glyph: the back colour for opaque text;
	// for transparent text what was there, except that text drawn over a
	// cell still showing earlier text at the same place inherits that
	// cell's background (so a reprinted label keeps a clean background).
	if (transparent) {
		cell.clean = before;
		for (const TextCell &old : _textCells)
			if (old.x == cell.x && old.y == cell.y && old.w == cell.w && old.h == cell.h) {
				bool shown = true;
				for (int j = 0; j < cell.h && shown; j++)
					for (int i = 0; i < cell.w && shown; i++)
						shown = before[size_t(j * 8 + i)] == old.expect[size_t(j * 8 + i)];
				if (shown)
					cell.clean = old.clean;
			}
	} else {
		cell.clean.fill(back);
	}
	// Pure: apart from this glyph's ink the cell is its clean background
	// (not, say, a different earlier glyph showing through).
	cell.pure = true;
	for (int j = 0; j < cell.h; j++)
		for (int i = 0; i < cell.w; i++) {
			const int b = j * 8 + i;
			if (!cell.inked(i, j) && cell.expect[size_t(b)] != cell.clean[size_t(b)])
				cell.pure = false;
		}

	// The new cell replaces any earlier one at the same place.
	_textCells.erase(std::remove_if(_textCells.begin(), _textCells.end(),
	                                [&](const TextCell &c) { return c.x == cell.x && c.y == cell.y; }),
	                 _textCells.end());
	if (cell.ink == Font::Rows{})
		return; // a space: nothing to draw sharply
	cell.serial = ++_textSerial;
	if (_textCells.size() >= kMaxTextCells)
		_textCells.erase(_textCells.begin(), _textCells.begin() + long(kMaxTextCells / 4));
	_textCells.push_back(cell);
}

void Draw::visibleText(std::vector<TextCell> &out) const {
	out.clear();
	const Surface &s = *_front;
	const int split = std::clamp(splitHeight, 0, kScreenH);
	for (const TextCell &c : _textCells) {
		if (!c.pure)
			continue;
		bool same = true;
		for (int j = 0; j < c.h && same; j++)
			for (int i = 0; i < c.w && same; i++)
				same = s.get(c.x + i, c.y + j) == c.expect[size_t(j * 8 + i)];
		if (!same)
			continue;
		if (!composed()) {
			out.push_back(c);
			continue;
		}
		// Where the display shows the cell, if it shows all of it.
		TextCell v = c;
		if (split > 0 && c.y >= splitStart && c.y + c.h <= splitStart + split && c.x + c.w <= kScreenW) {
			v.y = int16_t(c.y - splitStart + kScreenH - split);
		} else {
			v.x = int16_t(c.x - viewX);
			v.y = int16_t(c.y - viewY);
			if (v.x < 0 || v.y < 0 || v.x + v.w > kScreenW || v.y + v.h > kScreenH - split)
				continue;
		}
		out.push_back(v);
	}
}

} // namespace gob
