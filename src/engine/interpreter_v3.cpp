// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The version 3 layer (Goblins Quest 3): the opcodes whose behaviour differs
// from version 2, and the version 2 opcodes that Gobliins 2 never uses, so
// that they don't touch its code path (docs/port-plan.md 4.2,
// docs/opcodes.md 11). The hooks return true for an opcode they handled;
// anything else runs the shared implementation.

#include "engine/interpreter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace gob {

// ---------------------------------------------------------------------------
// Texts

// Draw_v2::printTotText, complete: a rectangle cleared to a back colour,
// boxes, then a stream of letters and commands: 2/5 move the pen, 3 font and
// colour, 4 colour, 6 a clickable area (its text centred in a box), 7 ends
// it, 8/9 underline on/off, 10 a glyph reference and 0xBA a variable
// (described by the 23-byte records after the stream's end byte). Bit 15 of
// the first word flags a subtitle, hidden when subtitles are off. Goblins 3's
// texts use only commands 2, 3 and 5 (checked over every text file).
void Interpreter::printTotTextV3(int16_t id) {
	if (!env().tot)
		return;
	auto item = env().tot->text(uint16_t(id & 0xFFF));
	if (!item || item->size() < 9)
		return;
	const std::vector<uint8_t> &d = *item;
	auto at = [&](size_t o) -> uint8_t { return o < d.size() ? d[o] : 1; };
	auto u16 = [&](size_t o) -> int16_t { return int16_t(at(o) | (at(o + 1) << 8)); };

	const bool isSubtitle = (at(1) & 0x80) != 0;
	if (isSubtitle && !subtitles)
		return;

	const int destX = uint16_t(u16(0)) & 0x7FFF;
	const int destY = u16(2);
	const int spriteRight = u16(4), spriteBottom = u16(6);
	size_t ptr = 8;

	if (_draw.renderFlags & draw::CapturePush) {
		capturePush(destX, destY, spriteRight - destX + 1, spriteBottom - destY + 1);
		_captureCounter++;
	}

	_draw.destSpriteX = int16_t(destX);
	_draw.destSpriteY = int16_t(destY);
	_draw.spriteRight = int16_t(spriteRight);
	_draw.spriteBottom = int16_t(spriteBottom);
	_draw.destSurface = draw::kBackSurface;
	_draw.backColor = at(ptr++);
	_draw.transparency = 1;
	spriteOp(draw::ClearRect);

	_draw.backColor = 0;
	const int16_t savedFlags = _draw.renderFlags;
	_draw.renderFlags &= ~draw::NoInvalidate;

	while (ptr + 1 < d.size() && u16(ptr) != -1) {
		_draw.destSpriteX = int16_t(u16(ptr) + destX);
		_draw.destSpriteY = int16_t(u16(ptr + 2) + destY);
		_draw.spriteRight = int16_t(u16(ptr + 4) + destX);
		_draw.spriteBottom = int16_t(u16(ptr + 6) + destY);
		ptr += 8;
		uint8_t cmd = at(ptr++);
		switch (cmd >> 4) {
		case 0: _draw.frontColor = cmd & 0xF; spriteOp(draw::DrawLine); break;
		case 1: _draw.frontColor = cmd & 0xF; spriteOp(draw::DrawBar); break;
		case 2: _draw.backColor = cmd & 0xF; spriteOp(draw::FillRectAbs); break;
		default: break;
		}
	}
	ptr += 2;

	// The variable records follow the stream's end byte.
	size_t vars = ptr;
	while (vars < d.size() && at(vars) != 1) {
		switch (at(vars)) {
		case 2: case 5: vars += 5; break;
		case 3: case 4: vars += 2; break;
		case 6:
			vars++;
			switch (at(vars) & 0xC0) {
			case 0x40: vars += 9; break;
			case 0x80: vars += 3; break;
			case 0xC0: vars += 11; break;
			default: vars++; break;
			}
			break;
		case 10: vars += size_t(at(vars + 1)) * 2 + 2; break;
		default: vars++; break;
		}
	}
	vars++;

	int fontIndex = 0, frontColor = 0;
	int strPos = 0, strPos2 = -1;
	int offX = 0, offY = 0;
	int colId = 0, colCmd = 0;
	bool maskChar = false;
	std::array<bool, 80> mask{};
	std::array<char, 80> str;
	str.fill(' ');
	_draw.backColor = 0;
	_draw.transparency = 1;

	auto fontOf = [&](int i) { return _draw.font(i); };
	auto put = [&](char c) {
		if (strPos < 79) {
			str[size_t(strPos)] = c;
			mask[size_t(strPos)] = maskChar;
			strPos++;
		}
	};

	while (true) {
		uint8_t cmd = at(ptr);
		if (((cmd >= 1 && cmd <= 7) || cmd == 10) && strPos != 0) {
			const Font *f = fontOf(fontIndex);
			str[size_t(std::max(strPos, strPos2))] = 0;
			const int strPosBak = strPos;
			const std::string text(str.data());
			if (f && (colCmd & 0x0F)) {
				int width = 0;
				for (unsigned char c : text)
					width += f->monospaced() ? f->width() : f->charWidth(c);
				int rectLeft = offX - 2, rectTop = offY - 2;
				int rectRight = offX + width + 1, rectBottom = offY + f->height() + 1;
				if (colId != -1)
					hotspotAdd({uint16_t(colId + 0xD000), uint16_t(rectLeft), uint16_t(rectTop), uint16_t(rectRight),
					            uint16_t(rectBottom), 2, 0, 0, 0, 0});
				// printTextCentered, inside the box. A script "center"
				// function (TOT function 13) would draw the box first; no
				// Goblins 3 text uses boxes.
				if (env().tot->header().functions[13] != 0)
					trace("printTotText: centre function not implemented");
				_draw.transparency = 1;
				_draw.destSpriteX = int16_t(rectLeft + 2 + (rectRight - rectLeft - 4 + 1 - width) / 2);
				_draw.destSpriteY = int16_t(rectTop + 2);
				_draw.fontIndex = uint16_t(fontIndex);
				_draw.frontColor = int16_t(frontColor);
				_draw.textToPrint = text;
				spriteOp(draw::PrintText);
			} else if (f) {
				_draw.destSpriteX = int16_t(offX);
				_draw.destSpriteY = int16_t(offY);
				_draw.fontIndex = uint16_t(isSubtitle ? _subtitleFont : fontIndex);
				_draw.frontColor = int16_t(isSubtitle ? _subtitleColor : frontColor);
				_draw.textToPrint = text;
				spriteOp(draw::PrintText);

				// Underlined (masked) letters.
				for (size_t i = 0; i < text.size(); i++) {
					if (!mask[i])
						continue;
					_draw.destSpriteX = int16_t(int(i) * f->width() + offX);
					_draw.spriteRight = int16_t(_draw.destSpriteX + f->width() - 1);
					_draw.spriteBottom = int16_t(offY + f->height());
					_draw.destSpriteY = _draw.spriteBottom;
					spriteOp(draw::DrawLine);
				}
			}
			if (const Font *pf = fontOf(isSubtitle ? _subtitleFont : fontIndex))
				for (int i = 0; i < strPosBak; i++)
					offX += pf->charWidth(uint8_t(str[size_t(i)]));
			strPos = 0;
			strPos2 = -1;
			mask.fill(false);
			str.fill(' ');
		}

		if (cmd == 1 || ptr >= d.size())
			break;

		switch (cmd) {
		case 2:
		case 5:
			offX = destX + u16(ptr + 1);
			offY = destY + u16(ptr + 3);
			ptr += 5;
			break;
		case 3:
			fontIndex = (at(ptr + 1) >> 4) & 7;
			frontColor = at(ptr + 1) & 0xF;
			if (isSubtitle) {
				_subtitleFont = int16_t(fontIndex);
				_subtitleColor = int16_t(frontColor);
			}
			ptr += 2;
			break;
		case 4:
			frontColor = at(ptr + 1);
			if (isSubtitle)
				_subtitleColor = int16_t(frontColor);
			ptr += 2;
			break;
		case 6: {
			ptr++;
			colCmd = at(ptr++);
			colId = -1;
			if (colCmd & 0x80) {
				colId = u16(ptr);
				ptr += 2;
			}
			if (colCmd & 0x40) {
				int l = destX + u16(ptr), r = destX + u16(ptr + 2);
				int t = destY + u16(ptr + 4), b = destY + u16(ptr + 6);
				hotspotAdd({uint16_t(colId + 0xD000), uint16_t(l), uint16_t(t), uint16_t(r), uint16_t(b), 2, 0, 0, 0, 0});
				ptr += 8;
			}
			break;
		}
		case 7:
			ptr++;
			colCmd = 0;
			break;
		case 8:
			ptr++;
			maskChar = true;
			break;
		case 9:
			ptr++;
			maskChar = false;
			break;
		case 10: { // a glyph reference: 0xFF and the text table offset
			str[0] = char(0xFF);
			uint16_t off = uint16_t(env().tot->textOffset(uint16_t(id & 0xFFF)) + ptr);
			str[1] = char(off & 0xFF);
			str[2] = char(off >> 8);
			str[3] = 0;
			for (int i = at(ptr + 1); i > 0; i--) {
				if (strPos < 79)
					mask[size_t(strPos++)] = maskChar;
			}
			ptr += 2 + size_t(at(ptr + 1)) * 2;
			break;
		}
		case 0xBA: { // a variable
			uint8_t format = at(vars + 17);
			uint32_t varOff = uint32_t(uint16_t(u16(vars + 18))) * 4;
			std::string buf;
			if ((format & 0x7F) == 1) {
				buf = _vars->readString(varOff).substr(0, 19);
			} else {
				buf = std::to_string(int32_t(_vars->read32(varOff)));
				if (format & 0x7F) { // fixed point: `format` decimals
					size_t decimals = std::min<size_t>(format & 0x7F, 18);
					size_t at0 = buf[0] == '-' ? 1 : 0;
					while (buf.size() - 1 < decimals)
						buf.insert(at0, "0");
					uint32_t lang = _game.scriptLanguage(language);
					buf.insert(buf.size() - decimals, lang == 2 ? "." : ",");
				}
			}
			for (char c : buf)
				put(c);
			if (format & 0x80) {
				strPos2 = strPos;
				strPos = strPos - int(buf.size()) + 1;
			} else if (at(ptr + 1) != ' ') {
				if (at(ptr + 1) == 2 && u16(ptr + 4) == _draw.destSpriteY) {
					ptr += 5;
					put(' ');
				}
			} else {
				put(' ');
				while (at(ptr + 1) == ' ')
					ptr++;
				if (at(ptr + 1) == 2 && u16(ptr + 4) == _draw.destSpriteY)
					ptr += 5;
			}
			vars += 23;
			ptr++;
			break;
		}
		case 32:
			if (strPos < 79)
				mask[size_t(strPos++)] = maskChar;
			ptr++;
			break;
		default:
			put(char(cmd));
			ptr++;
			break;
		}
	}

	_draw.renderFlags = savedFlags;
	if (!(_draw.renderFlags & draw::Collisions))
		return;
	trace("printTotText: collision check not implemented");
	if (_captureCounter > 0) {
		_captureCounter--;
		capturePop(true);
	}
}

// getTotTextItemPart (func 0x1A): text item, string variable, part. Copies
// one part of a text item into the string: part n of the clickable areas
// (command 6 with id -n-1), or the nth line of text; -1 only names the
// variable as the hotspot text (for text-to-speech; nothing here).
void Interpreter::getTotTextItemPart() {
	Script &s = script();
	const int16_t itemId = s.readInt16();
	const uint32_t startVar = readVar().offset;
	const int16_t part = evalVal();
	if (part == -1)
		return;

	uint32_t var = startVar;
	_vars->write8(var, 0);
	if (!env().tot)
		return;
	auto item = env().tot->text(uint16_t(itemId));
	if (!item)
		return;
	const std::vector<uint8_t> &d = *item;
	auto at = [&](size_t o) -> uint8_t { return o < d.size() ? d[o] : 1; };
	auto u16 = [&](size_t o) -> int16_t { return int16_t(at(o) | (at(o + 1) << 8)); };

	size_t p = 0;
	while (p + 1 < d.size() && u16(p) != -1) // the background rectangles
		p += 9;
	p += 2;

	int curPart = 0;
	int16_t offX = 0, offY = 0, collId = 0;
	while (p < d.size() && at(p) != 1) {
		switch (at(p)) {
		case 2:
		case 5:
			offX = u16(p + 1);
			offY = u16(p + 3);
			p += 5;
			break;
		case 3:
		case 4:
			p += 2;
			break;
		case 6: {
			p++;
			uint8_t collCmd = at(p++);
			if (collCmd & 0x80) {
				collId = u16(p);
				p += 2;
			}
			if (collCmd & 0x40)
				p += 8;
			if ((collCmd & 0x8F) && -collId - 1 == part) {
				int n = 0;
				while (p < d.size()) {
					uint8_t c = at(p);
					if (c < 1 || c > 7) {
						if (c >= 32) {
							_vars->write8(var++, c);
							n++;
						}
						p++;
						continue;
					}
					if (n != 0 || c == 1 || c == 6 || c == 7)
						break;
					p += (c == 2 || c == 5) ? 5 : (c == 3 || c == 4) ? 2 : 0;
				}
				_vars->write8(var, 0);
				return;
			}
			break;
		}
		case 7:
		case 8:
		case 9:
			p++;
			break;
		case 10:
			if (curPart == part) {
				_vars->write8(var++, 0xFF);
				_vars->write16(var, uint16_t(offX));
				_vars->write16(var + 2, uint16_t(offY));
				_vars->write16(var + 4, uint16_t(env().tot->textOffset(uint16_t(itemId)) + p));
				_vars->write8(var + 6, 0);
				return;
			}
			for (bool end = false; !end && p < d.size();) {
				switch (at(p)) {
				case 2:
				case 5:
					if (std::abs(offY - u16(p + 3)) > 1)
						end = true;
					else
						p += 5;
					break;
				case 3:
					p += 2;
					break;
				case 10:
					p += size_t(at(p + 1)) * 2 + 2;
					break;
				default:
					if (at(p) < 32)
						end = true;
					while (p < d.size() && at(p) >= 32)
						p++;
					break;
				}
			}
			if (part >= 0)
				curPart++;
			break;
		default: // a line of text, continued by moves on the same line
			while (p < d.size()) {
				while (p < d.size() && at(p) >= 32)
					_vars->write8(var++, at(p++));
				_vars->write8(var, 0);
				if ((at(p) != 2 && at(p) != 5) || std::abs(offY - u16(p + 3)) > 1) {
					if (curPart == part)
						return;
					var = startVar;
					_vars->write8(var, 0);
					while (p < d.size() && at(p) >= 32)
						p++;
					if (part >= 0)
						curPart++;
					break;
				}
				p += 5;
			}
			break;
		}
	}
}

// ---------------------------------------------------------------------------
// Hotspots and sounds (version 2 opcodes)

// addHotspot (func 0x18): id, then the coordinates (re-read from the script
// by recalculateHotspots), flags and key. Negative ids make a hotspot on a
// 4-pixel grid with a 3-pixel margin.
void Interpreter::addHotspotV3() {
	Script &s = script();
	int16_t id = evalVal();
	uint16_t funcPos = uint16_t(s.pos());
	int left = evalVal(), top = evalVal();
	int width = uint16_t(evalVal()), height = uint16_t(evalVal());
	uint16_t flags = uint16_t(evalVal());
	uint16_t key = s.readUint16();
	if (key == 0)
		key = uint16_t(std::abs(id) + 41960);
	if (left < 0) {
		width += left;
		left = 0;
	}
	if (top < 0) {
		height += top;
		top = 0;
	}
	Hotspot h;
	if (id < 0) {
		h.id = uint16_t(0xD000 - id);
		h.left = uint16_t(left & 0xFFFC);
		h.top = uint16_t(top & 0xFFFC);
		h.right = uint16_t(left + width + 3);
		h.bottom = uint16_t(top + height + 3);
	} else {
		h.id = uint16_t(0xE000 + id);
		h.left = uint16_t(left);
		h.top = uint16_t(top);
		h.right = uint16_t(left + width - 1);
		h.bottom = uint16_t(top + height - 1);
	}
	h.flags = flags;
	h.key = key;
	h.funcPos = funcPos;
	hotspotAdd(h);
}

// removeHotspot (func 0x19): -2 removes every hotspot of state 0xD (added
// with a negative id), -1 every one of state 0xE, others the one with id
// 0xE000 + id.
void Interpreter::removeHotspotV3() {
	int16_t id = evalVal();
	int state = id == -2 ? 0xD : id == -1 ? 0xE : -1;
	if (state < 0) {
		hotspotRemove(uint16_t(0xE000 + id));
		return;
	}
	for (Hotspot &h : _hotspots)
		if (!h.isEnd() && h.state() == state)
			h = Hotspot{};
}

// loadSound (func 0x3A): slot (negative: an AdLib song, which this engine
// doesn't play), resource id; id -1 is followed by a 9-byte file name
// (NAME.SND).
void Interpreter::loadSoundV3() {
	Script &s = script();
	int slot = evalVal();
	bool adlib = slot < 0;
	slot = std::abs(slot);
	int16_t id = s.readInt16();
	freeSoundSlot(slot);
	std::string file;
	if (id == -1) {
		for (int i = 0; i < 9; i++)
			if (char c = char(s.readByte()); c && file.size() == size_t(i))
				file.push_back(c);
	}
	if (adlib) {
		trace("loadSound: AdLib " + (file.empty() ? std::to_string(id) : file) + " ignored");
		return;
	}
	if (slot >= kSoundSlots)
		return;
	if (id != -1) {
		if (!loadSoundResource(slot, id))
			trace("loadSound: resource " + std::to_string(id) + " missing");
		return;
	}
	auto data = _files.read(file + ".SND");
	if (data && data->size() >= 6)
		loadSoundData(slot, *data, id);
	else
		trace("loadSound: " + file + ".SND missing");
}

// ---------------------------------------------------------------------------
// Palette cycling

// animPalInit (func 0x17): i16 n, val low, val high. n > 0 cycles colours
// low..high upwards on channel n - 1, n < 0 downwards on channel -n - 1, 0
// stops every channel (its two values are read and ignored).
void Interpreter::animPalInit() {
	int16_t n = script().readInt16();
	int low = evalVal(), high = evalVal();
	if (n == 0) {
		_animPalDir.fill(0);
		return;
	}
	int ch = n > 0 ? n - 1 : -n - 1;
	if (ch >= 8)
		return;
	_animPalLow[size_t(ch)] = int16_t(std::clamp(low, 0, 255));
	_animPalHigh[size_t(ch)] = int16_t(std::clamp(high, 0, 255));
	_animPalDir[size_t(ch)] = n > 0 ? 1 : -1;
}

// Inter_v2::animPalette: each active channel rotates its range by one entry
// (upwards: the highest colour moves to the lowest), then the palette is
// shown.
void Interpreter::animPalette() {
	bool any = false;
	for (size_t ch = 0; ch < 8; ch++) {
		if (_animPalDir[ch] == 0)
			continue;
		any = true;
		const int low = _animPalLow[ch], high = _animPalHigh[ch];
		if (low >= high)
			continue;
		uint8_t *p = _draw.vgaPalette.data();
		if (_animPalDir[ch] == -1)
			std::rotate(p + low * 3, p + low * 3 + 3, p + high * 3 + 3);
		else
			std::rotate(p + low * 3, p + high * 3, p + high * 3 + 3);
	}
	if (any)
		_draw.setPalette();
}

// ---------------------------------------------------------------------------
// Screen effects

// Video::sparseRetrace: shows the screen after every `max` + 1 calls, or when
// the last call was more than a second ago. Goblins 3 dissolves to a
// close-up with 800 small copies to the front surface; showing them in
// batches makes that visible as in the original.
Task<void> Interpreter::sparseRetrace(int max) {
	uint32_t now = gameTicks();
	if (_curSparse++ > max || now - _lastSparse > 1000) {
		_curSparse = 0;
		_frameBudget = 0;
		co_await _sched.nextFrame();
		now = gameTicks();
	}
	_lastSparse = now;
}

// Draw::wobble (goblin op 100): the back surface slides onto the screen with
// rows displaced by a decaying sine wave while the palette fades in.
Task<void> Interpreter::wobble() {
	static const std::array<int16_t, 360> table = [] {
		std::array<int16_t, 360> t{};
		for (int i = 0; i < 360; i++)
			t[size_t(i)] = int16_t(std::sin(i * std::numbers::pi / 180.0) * 0x4000);
		return t;
	}();
	Surface &back = _draw.back();
	Surface &front = _draw.front();
	const VgaPalette target = _draw.vgaPalette;
	std::vector<int> offsets(size_t(back.height()));
	int amplitude = 32;
	unsigned curFrame = 0, frameWobble = 0;
	while (amplitude > 0 && !_quit) {
		unsigned rowWobble = frameWobble;
		frameWobble = (frameWobble + 20) % 360;
		for (int &o : offsets) {
			o = amplitude + table[rowWobble] * amplitude / 0x4000;
			rowWobble = (rowWobble + 20) % 360;
		}
		if (curFrame++ & 16)
			amplitude--;
		for (int y = 0; y < back.height(); y++)
			front.blit(back, 0, y, back.width() - 1, y, offsets[size_t(y)], y);
		_draw.fadeStep(target, 2);
		_frameBudget = 0;
		co_await _sched.nextFrame();
	}
	front.blit(back);
	_draw.applyPal = false;
	_draw.clearInvalidated();
}

// ---------------------------------------------------------------------------
// Screens, the split bar and scrolling (o2_initScreen, o2_scroll,
// o2_setScrollOffset, Game::evaluateScroll; docs/graphics.md 9)

// initScreen (draw 0x80): i16 (high byte: the split's height, low byte: the
// video mode), val width, val height (-1: unchanged). Goblins 3's rooms use
// 0x2814, 472, 200: a 472x200 screen whose bottom 40 rows are the text bar;
// EMAP1012 uses 320x240.
void Interpreter::initScreenV3() {
	int16_t word = script().readInt16();
	int videoMode = word & 0xFF;
	int split = (word >> 8) & 0xFF;
	int width = evalVal();
	int height = evalVal();

	_fakeVideoMode = videoMode;
	if (videoMode == _videoMode && width == -1)
		return;
	int w = width > 0 ? width : _draw.surfaceWidth();
	int h = height > 0 ? height : _draw.surfaceHeight();

	_mouseMinX = _mouseMinY = 0;
	_mouseMaxX = Draw::kScreenW;
	_mouseMaxY = Draw::kScreenH - split - 1;
	_draw.clearPalette();
	_videoMode = videoMode;
	writeVar(15, uint32_t(_fakeVideoMode));
	_host.warpMouse(std::clamp(_interMouseX, 0, Draw::kScreenW - 1), std::clamp(_interMouseY, 0, Draw::kScreenH - 1));
	_draw.initScreen(w, h, split);
	_preventScroll = false;
	showScrollOffset();
	trace("initScreen " + std::to_string(w) + "x" + std::to_string(h) + (split ? ", split " + std::to_string(split) : ""));
}

// scroll (draw 0x81): startX, startY, endX, endY, stepX, stepY: scrolls step
// by step (the original shows each step and waits about 10 ms).
Task<void> Interpreter::scrollV3() {
	const int maxX = std::max(0, _draw.surfaceWidth() - Draw::kScreenW);
	const int maxY = std::max(0, _draw.surfaceHeight() - Draw::kScreenH);
	int startX = std::clamp<int>(evalVal(), 0, maxX), startY = std::clamp<int>(evalVal(), 0, maxY);
	int endX = std::clamp<int>(evalVal(), 0, maxX), endY = std::clamp<int>(evalVal(), 0, maxY);
	int stepX = evalVal(), stepY = evalVal();
	if ((stepX == 0 && startX != endX) || (stepY == 0 && startY != endY)) {
		trace("scroll: no step towards the end, ignored"); // would loop forever in the original
		co_return;
	}
	int curX = startX, curY = startY;
	double due = gameTicks();
	while (!_quit && (curX != endX || curY != endY)) {
		curX = stepX > 0 ? std::min(curX + stepX, endX) : std::max(curX + stepX, endX);
		curY = stepY > 0 ? std::min(curY + stepY, endY) : std::max(curY + stepY, endY);
		_draw.scrollX = curX;
		_draw.scrollY = curY;
		showScrollOffset();
		due += 10;
		while (!_quit && gameTicks() < due) {
			_frameBudget = 0;
			co_await _sched.nextFrame();
		}
	}
}

// setScrollOffset (draw 0x82): x, y; x -1 toggles the edge scrolling off
// and on and writes the offset to VAR 2 and 3.
void Interpreter::setScrollOffsetV3() {
	int x = evalVal(), y = evalVal();
	if (x == -1) {
		_preventScroll = !_preventScroll;
		writeVar(2, uint32_t(_draw.scrollX));
		writeVar(3, uint32_t(_draw.scrollY));
	} else {
		int w = _draw.surfaceWidth(), h = _draw.surfaceHeight();
		if (w > Draw::kScreenW)
			w -= Draw::kScreenW;
		if (h > Draw::kScreenH)
			h -= Draw::kScreenH;
		_draw.scrollX = std::clamp(x, 0, w);
		_draw.scrollY = std::clamp(y, 0, h);
	}
	showScrollOffset();
}

// The mouse in version 3: kept inside the area above the split (the
// original moves the pointer back), seen by the scripts with the scroll
// offset added; a move asks for edge scrolling.
void Interpreter::pollMouseV3() {
	int hx = _host.mouseX(), hy = _host.mouseY();
	int x = std::clamp(hx, _mouseMinX, _mouseMaxX), y = std::clamp(hy, _mouseMinY, _mouseMaxY);
	if (x != hx || y != hy)
		_host.warpMouse(x, y);
	if (x != _lastMouseX || y != _lastMouseY) {
		_lastMouseX = x;
		_lastMouseY = y;
		_wantScroll = true;
		_wantScrollX = x;
		_wantScrollY = y;
		// WORKAROUND (ScummVM): EMAP1008 (the sofa) needs the scroll at once.
		if (currentTot() == "EMAP1008")
			evaluateScroll();
	}
	_interMouseX = x + _draw.viewX;
	_interMouseY = y + _draw.viewY;
}

// Game::evaluateScroll: with the pointer at an edge of the display, the
// screen scrolls by half a cursor (at least a pixel). At the right and
// bottom edges the pointer is put back a cursor's size from the edge, which
// the original sees as another move: the scroll goes on to the end. At the
// left and top edges the DOS mouse kept reporting moves while pushed against
// the edge; a pointer resting there on a modern system reports none, so
// (unlike ScummVM, which stops after one step) the scroll goes on while the
// pointer stays at the edge.
void Interpreter::evaluateScroll() {
	if (_preventScroll || !_wantScroll)
		return;
	if (_videoMode != 0x14 && _videoMode != 0x18)
		return;
	const int cw = _draw.cursorWidth, ch = _draw.cursorHeight;
	const int sw = _draw.surfaceWidth(), sh = _draw.surfaceHeight();
	const int barTop = Draw::kScreenH - _draw.splitHeight;

	bool atEdge = false;
	if (_wantScrollX == 0 && _draw.scrollX > 0) {
		_draw.scrollX -= std::max(std::min(cw, _draw.scrollX) / 2, 1);
		atEdge = true;
	} else if (_wantScrollY == 0 && _draw.scrollY > 0) {
		_draw.scrollY -= std::max(std::min(ch, _draw.scrollY) / 2, 1);
		atEdge = true;
	}

	bool warped = false;
	int wx = _wantScrollX, wy = _wantScrollY;
	if (_wantScrollX + cw >= Draw::kScreenW && _draw.scrollX + Draw::kScreenW < sw) {
		_draw.scrollX += std::max(std::min(cw, sw - (_draw.scrollX + Draw::kScreenW)) / 2, 1);
		wx = Draw::kScreenW - cw;
		warped = true;
	} else if (_wantScrollY + ch >= barTop && _draw.scrollY + Draw::kScreenH < sh) {
		_draw.scrollY += std::max(std::min(ch, sh - (_draw.scrollY + Draw::kScreenH)) / 2, 1);
		wy = barTop - ch;
		warped = true;
	}
	showScrollOffset();
	_wantScroll = atEdge; // still at the left or top edge: go on next time
	if (warped) {
		_host.warpMouse(std::clamp(wx, 0, Draw::kScreenW - 1), std::clamp(wy, 0, Draw::kScreenH - 1));
		_wantScroll = true; // the move the original's warp makes
		_wantScrollX = wx;
		_wantScrollY = wy;
	}
}

// ---------------------------------------------------------------------------
// Hooks

Task<bool> Interpreter::execFuncV3(uint8_t index, Params &p) {
	(void)p;
	Script &s = script();
	switch (index) {
	case 0x02: // printTotText
		printTotTextV3(s.readInt16());
		co_return true;

	case 0x17:
		animPalInit();
		co_return true;

	case 0x18:
		addHotspotV3();
		co_return true;

	case 0x19:
		removeHotspotV3();
		co_return true;

	case 0x1A:
		getTotTextItemPart();
		co_return true;

	case 0x32: // copySprite: copies to the screen are shown in batches
		copySprite();
		if (_draw.destSurface == draw::kFrontSurface || _draw.destSurface == draw::kFrontSurface + 80)
			co_await sparseRetrace(20);
		co_return true;

	case 0x3A:
		loadSoundV3();
		co_return true;

	case 0x46: // setMousePos: given on the screen surface, the pointer moves on the display
		_interMouseX = evalVal() - _draw.viewX;
		_interMouseY = evalVal() - _draw.viewY;
		_host.warpMouse(std::clamp(_interMouseX, 0, Draw::kScreenW - 1), std::clamp(_interMouseY, 0, Draw::kScreenH - 1));
		co_return true;

	default:
		co_return false;
	}
}

Task<bool> Interpreter::execDrawV3(uint8_t op, uint32_t opStart) {
	(void)opStart;
	switch (op) {
	case 0x1A: // loadStatic
		sceneryLoadStatic(false);
		co_return true;

	case 0x1B: // freeStatic
		_scenery.freeStatic(int(evalInt()));
		co_return true;

	case 0x1C: { // renderStatic: index, layer
		int index = evalVal(), layer = evalVal();
		_scenery.renderStatic(int16_t(index), int16_t(layer), resourceLoader());
		co_return true;
	}

	case 0x1D: { // loadCurLayer: the static and layer redrawn over objects
		int st = evalVal(), layer = evalVal();
		_scenery.curStatic = int16_t(st);
		_scenery.curStaticLayer = int16_t(layer);
		co_return true;
	}

	case 0x80: // initScreen
		initScreenV3();
		co_return true;

	case 0x81: // scroll
		co_await scrollV3();
		co_return true;

	case 0x82: // setScrollOffset
		setScrollOffsetV3();
		co_return true;

	case 0x83: // playImd
		co_await playImd();
		co_return true;

	case 0x84: // getImdInfo
		getImdInfo();
		co_return true;

	case 0x85: { // openItk: a video or speech archive (".ITK" when no extension)
		std::string file = evalString();
		if (file.find('.') == std::string::npos)
			file += ".ITK";
		std::string note, opened = file;
		bool ok = _files.openArchive(file, false);
		if (!ok) {
			if (const char *fallback = _rules.archiveFallback(file)) {
				ok = _files.openArchive(fallback, false);
				opened = fallback;
				note = ok ? std::string(" (missing: opened ") + fallback + " instead)" : " (not found)";
			} else {
				note = " (not found)";
			}
		}
		if (ok && _rules.isVoiceArchive(file))
			_voiceArchive = opened; // its videos' sound is the speech
		trace("openItk \"" + file + "\"" + note);
		co_return true;
	}

	case 0x86: { // closeItk: the last archive openItk opened
		bool ok = _files.closeArchive(false);
		trace(std::string("closeItk") + (ok ? "" : " (none open)"));
		co_return true;
	}

	case 0x87: // setImdFrontSurf, resetImdFrontSurf: no-ops
	case 0x88:
		co_return true;

	default:
		co_return false;
	}
}

// Goblins 3 has no goblin ops of its own but wobble: op 100 is wobble, not
// handleGoblins, and there is no protracker (500, 501). The scripts of the
// CD version call none of them.
Task<bool> Interpreter::goblinFuncV3(int16_t cmd, int16_t paramCount) {
	Script &s = script();
	switch (cmd) {
	case 0: // the Infogrames music commands (Amiga only)
	case 1:
	case 2:
	case 10:
		s.skip(2);
		break;
	case 100:
		trace("wobble");
		co_await wobble();
		break;
	case 101:
		break;
	default:
		s.skip(std::max<int16_t>(paramCount, 0) * 2);
		trace("stub goblin " + std::to_string(cmd) + " (" + std::to_string(paramCount) + " params)");
		break;
	}
	co_return true;
}

} // namespace gob
