// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/interpreter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <stdexcept>

namespace gob {

namespace {

// Yield to the host after this many opcodes without a natural wait, so a
// script busy-looping on a timer variable can't freeze the frame loop.
constexpr uint32_t kOpsPerFrameBudget = 20000;

std::string upper(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
	return s;
}

// Strips a ".TOT" (or any) extension and normalizes case.
std::string totBaseName(const std::string &name) {
	std::string base = upper(name);
	if (auto dot = base.rfind('.'); dot != std::string::npos)
		base.resize(dot);
	return base;
}

std::string hex(uint32_t v) {
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%04X", v);
	return buf;
}

// Normalizes text for comparisons in text-input puzzles: lower case, accents
// folded to their base letter, spaces trimmed and collapsed.
std::string cleanupString(const std::string &in) {
	auto fold = [](unsigned char c) -> char {
		// Common accented letters in the DOS code page used by the game.
		switch (c) {
		case 0x80: case 0x87: return 'c';
		case 0x81: case 0x96: case 0x97: case 0xA3: case 0x9A: return 'u';
		case 0x82: case 0x88: case 0x89: case 0x8A: case 0x90: return 'e';
		case 0x83: case 0x84: case 0x85: case 0x86: case 0xA0: case 0x8E: case 0x8F: return 'a';
		case 0x8B: case 0x8C: case 0x8D: case 0xA1: return 'i';
		case 0x93: case 0x94: case 0x95: case 0xA2: case 0x99: return 'o';
		case 0xA4: case 0xA5: return 'n';
		default: break;
		}
		if (c < 0x20 || c >= 0x80)
			return ' ';
		return char(std::tolower(c));
	};

	std::string out;
	for (unsigned char c : in) {
		char f = fold(c);
		if (f == ' ' && (out.empty() || out.back() == ' '))
			continue;
		out.push_back(f);
	}
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

} // namespace

Interpreter::Interpreter(DataFiles &files, Host &host, const GameInfo &game)
	: _files(files), _host(host), _game(game), _rules(gameRules(game.id)), _ver(game.engineVersion),
	  _expr(_nullScript, _nullVars) {
	_envs.push_back(std::make_unique<Env>());
	_expr.setRandom([](int32_t n) { return n > 0 ? int32_t(std::rand() % n) : 0; });
	_hostLast = _host.ticks();
	_draw.scriptName = [this] { return currentTot(); };
	if (_ver == 1) {
		_scenery.maxDrawOrder = 100;
		_draw.v1Cursor = true;
		_gobV1 = std::make_unique<GoblinsV1>(*this);
	}
}

bool Interpreter::inCutscene() const {
	if (_inPlayMult || _inPlayImd)
		return true;
	return _rules.isCutsceneTot(currentTot());
}

double Interpreter::effectiveSpeed() const {
	if (cutscenesAtNormalSpeed && inCutscene())
		return 1.0;
	return std::clamp(speed, 0.1, 16.0);
}

// Advances the game clock by the real time since the last call, scaled by
// the current speed. Called often (at least once per waiting frame), so a
// speed change only affects time from about that frame on.
uint32_t Interpreter::gameTicks() {
	uint32_t now = _host.ticks();
	_gameMs += double(now - _hostLast) * effectiveSpeed();
	_hostLast = now;
	return uint32_t(int64_t(_gameMs));
}

// At speed s the scheduler runs s frames per host frame on average, so
// waits counted in frames (input polls, fade steps, the frame each CD
// position poll takes) speed up along with the timed ones.
void Interpreter::runFrame() {
	if (_ffSkip) {
		fastForwardFrame();
		return;
	}
	_frameCredit += effectiveSpeed();
	int n = std::max(1, int(_frameCredit));
	_frameCredit = std::max(0.0, _frameCredit - n);
	for (int i = 0; i < n && _sched.waiting(); i++) {
		if (i && effectiveSpeed() <= 1.0) { // a cutscene started
			_frameCredit = 0;
			break;
		}
		_sched.resume();
	}
}

// A skip request lasts until a playMult takes it or the cutscene is left.
bool Interpreter::skipPending() {
	if (_skipCutscene && !inCutscene())
		_skipCutscene = false;
	return _skipCutscene;
}

const std::string &Interpreter::currentTot() const {
	return _envs[_cur]->name;
}

void Interpreter::trace(const std::string &line) {
	_host.trace("[" + (env().name.empty() ? std::string("-") : env().name) + "] " + line);
}

void Interpreter::bindEnv() {
	Env &e = env();
	if (e.tot && _vars) {
		_expr.bind(e.script, *_vars);
		_expr.setAnimDataSize(e.tot->header().animDataSize);
	} else {
		_expr.bind(_nullScript, _nullVars);
	}
}

void Interpreter::stub(uint32_t opStart) {
	auto [text, end] = env().dis->decodeInstruction(opStart);
	trace("stub " + text);
	script().seek(end);
}

void Interpreter::spriteOp(int op) {
	_draw.spriteOperation(op, [this](uint16_t id) -> std::optional<Resource> {
		return env().tot ? env().tot->resource(id) : std::nullopt;
	});
}

Task<void> Interpreter::fadeToPalette(const VgaPalette &target) {
	while (!_quit && !_draw.fadeStep(target, 2)) {
		_frameBudget = 0;
		co_await _sched.nextFrame();
	}
	_draw.setPalette();
}

// Palette commands (func 0x13). Sizes are the VGA / 256-colour variants.
Task<void> Interpreter::palLoad() {
	Script &s = script();
	uint8_t cmd = s.readByte();
	switch (cmd & 0x7F) {
	case 48: case 52: s.skip(48); co_return; // EGA / 16-colour data
	case 49: s.skip(18); co_return;
	case 50: s.skip(16); co_return;
	case 51: s.skip(2); co_return;
	default: break;
	}

	_draw.applyPal = !(cmd & 0x80);
	switch (cmd & 0x7F) {
	case 53: { // load full palette from a resource
		if (auto res = env().tot->resource(uint16_t(s.readInt16())))
			std::copy_n(res->data.begin(), std::min<size_t>(768, res->data.size()), _draw.vgaPalette.begin());
		break;
	}
	case 54: // black
		_draw.vgaPalette.fill(0);
		break;
	case 55: // push
		_draw.paletteStack.push_back(_draw.vgaPalette);
		s.skip(2);
		_draw.applyPal = false;
		co_return;
	case 56: { // pop a range
		int first = s.readByte();
		int last = s.readByte();
		if (!_draw.paletteStack.empty()) {
			const VgaPalette &top = _draw.paletteStack.back();
			for (int i = first * 3; i < (last + 1) * 3 && i < 768; i++)
				_draw.vgaPalette[i] = top[i];
			_draw.paletteStack.pop_back();
		}
		_draw.setPalette();
		_draw.applyPal = false;
		break;
	}
	case 61: { // load a range from a resource
		int first = s.readByte();
		int last = s.readByte();
		if (auto res = env().tot->resource(uint16_t(s.readInt16())))
			for (int i = first * 3; i < (last + 1) * 3 && i < 768 && size_t(i) < res->data.size(); i++)
				_draw.vgaPalette[i] = res->data[i];
		if (_draw.applyPal) {
			_draw.applyPal = false;
			_draw.setPalette();
			co_return;
		}
		break;
	}
	default:
		break;
	}

	// Without the "apply later" bit... the original fades to the new palette now.
	if (!_draw.applyPal)
		co_await fadeToPalette(_draw.vgaPalette);
}

void Interpreter::loadCursorResource(int16_t resId, int slot) {
	Surface *strip = _draw.cursorStrip();
	if (!strip || slot < 0 || slot * _draw.cursorWidth >= strip->width())
		return;
	auto res = env().tot->resource(uint16_t(resId));
	if (!res)
		return;
	int x = slot * _draw.cursorWidth;
	strip->fillRect(x, 0, x + _draw.cursorWidth - 1, _draw.cursorHeight - 1, 0);
	if (auto pic = _draw.decodePicture(*res, uint16_t(resId), true, PictureUse::Cursor))
		strip->blit(*pic, 0, 0, pic->width() - 1, pic->height() - 1, x, 0);
	_draw.cursorAnimLow[size_t(slot)] = 0;
	if (size_t(slot) < _cursorResources.size())
		_cursorResources[size_t(slot)] = resId;
}

bool Interpreter::loadFont(int slot, const std::string &file) {
	if (slot < 0 || slot >= Draw::kFontCount)
		return false;
	_draw.fonts[slot].reset(); // a failed load leaves the slot empty, as in the original
	if (auto data = _files.read(file))
		_draw.fonts[slot] = Font::load(*data);
	return _draw.fonts[slot].has_value();
}

// Draws a localized text item: a clear rectangle, optional boxes, then a
// command stream of positioned, coloured text runs (docs/graphics.md 1.2).
void Interpreter::printTotText(int16_t id) {
	if (!env().tot)
		return;
	auto item = env().tot->text(uint16_t(id & 0xFFF));
	if (!item || item->size() < 11)
		return;
	const std::vector<uint8_t> &d = *item;
	size_t pos = 0;
	auto u8 = [&]() -> uint8_t { return pos < d.size() ? d[pos++] : 1; };
	auto i16 = [&]() -> int16_t { int16_t v = int16_t(u8() | (u8() << 8)); return v; };

	// Bit 15 of `left` marks dialogue lines in the US data; draw them anyway.
	int left = uint16_t(i16()) & 0x7FFF;
	int top = i16();
	int right = i16();
	int bottom = i16();
	int back = u8();

	_draw.destSpriteX = int16_t(left);
	_draw.destSpriteY = int16_t(top);
	_draw.spriteRight = int16_t(right);
	_draw.spriteBottom = int16_t(bottom);
	_draw.destSurface = draw::kBackSurface;
	_draw.backColor = int16_t(back);
	_draw.transparency = 1;
	spriteOp(draw::ClearRect);

	_draw.backColor = 0;
	int16_t savedFlags = _draw.renderFlags;
	_draw.renderFlags &= ~draw::NoInvalidate;

	// Box records: 4 relative coordinates and a command byte, ended by -1.
	while (pos + 2 <= d.size() && !(d[pos] == 0xFF && d[pos + 1] == 0xFF)) {
		int x1 = i16(), y1 = i16(), x2 = i16(), y2 = i16();
		uint8_t cmd = u8();
		_draw.destSpriteX = int16_t(left + x1);
		_draw.destSpriteY = int16_t(top + y1);
		_draw.spriteRight = int16_t(left + x2);
		_draw.spriteBottom = int16_t(top + y2);
		switch (cmd >> 4) {
		case 0: _draw.frontColor = cmd & 0xF; spriteOp(draw::DrawLine); break;
		case 1: _draw.frontColor = cmd & 0xF; spriteOp(draw::DrawBar); break;
		case 2: _draw.backColor = cmd & 0xF; spriteOp(draw::FillRectAbs); break;
		default: break;
		}
	}
	pos += 2;

	int fontIdx = 0, color = 0, penX = 0, penY = 0;
	std::string run;
	_draw.backColor = 0;
	_draw.transparency = 1;

	auto flush = [&]() {
		if (run.empty())
			return;
		const Font *f = _draw.font(fontIdx);
		if (f) {
			_draw.destSpriteX = int16_t(penX);
			_draw.destSpriteY = int16_t(penY);
			_draw.fontIndex = uint16_t(fontIdx);
			_draw.frontColor = int16_t(color);
			_draw.textToPrint = run;
			spriteOp(draw::PrintText);
			for (unsigned char c : run)
				penX += f->charWidth(c);
		}
		run.clear();
	};

	while (pos < d.size()) {
		uint8_t b = u8();
		if ((b >= 1 && b <= 7) || b == 10)
			flush();
		switch (b) {
		case 1:
			pos = d.size();
			break;
		case 2:
		case 5: {
			int dx = i16(), dy = i16();
			penX = left + dx;
			penY = top + dy;
			break;
		}
		case 3: {
			uint8_t v = u8();
			fontIdx = (v >> 4) & 7;
			color = v & 0xF;
			break;
		}
		case 4:
			color = u8();
			break;
		case 6: { // button/hotspot setup: unused by Gobliins 2, skip operands
			uint8_t flags = u8();
			if (flags & 0x80) pos += 2;
			if (flags & 0x40) pos += 8;
			break;
		}
		case 7: case 8: case 9:
			break;
		case 10: { // glyph-code reference: unused by Gobliins 2
			uint8_t n = u8();
			pos += size_t(n) * 2;
			break;
		}
		case 0xBA: // variable insertion: unused by Gobliins 2
			break;
		default:
			run.push_back(char(b));
			break;
		}
	}

	_draw.renderFlags = savedFlags;
}

// copySprite (func 0x32): source and destination surfaces (immediate or
// expressions), the source rectangle, the destination and the transparency.
void Interpreter::copySprite() {
	Script &s = script();
	_draw.sourceSurface = s.peekByte(1) == 0 ? s.readInt16() : evalVal();
	_draw.destSurface = s.peekByte(1) == 0 ? s.readInt16() : evalVal();
	_draw.spriteLeft = evalVal();
	_draw.spriteTop = evalVal();
	_draw.spriteRight = evalVal();
	_draw.spriteBottom = evalVal();
	_draw.destSpriteX = evalVal();
	_draw.destSpriteY = evalVal();
	_draw.transparency = s.readInt16();
	spriteOp(draw::BlitSurf);
}

// Inline text: five expressions (x, y, back, front, font) and a string
// ending in byte 200, where '.' marks an inserted variable.
void Interpreter::printText() {
	Script &s = script();
	_draw.destSpriteX = evalVal();
	_draw.destSpriteY = evalVal();
	_draw.backColor = evalVal();
	_draw.frontColor = evalVal();
	_draw.fontIndex = uint16_t(evalVal());
	_draw.destSurface = draw::kBackSurface;
	_draw.transparency = 0;
	if (_ver == 1 ? _draw.backColor >= 16 : _draw.backColor == 16) {
		_draw.backColor = 0;
		_draw.transparency = 1;
	}

	do {
		std::string buf;
		while (buf.size() < 59 && s.peekByte() != '.' && s.peekByte() != 200)
			buf.push_back(char(s.readByte()));

		if (s.peekByte() != 200) {
			s.skip(1);
			uint8_t type = s.peekByte();
			VarRef ref = readVar();
			if (_ver == 1 && type != tok::LoadVarInt32 && type != tok::ArrayInt32 && type != tok::LoadVarStr &&
			    type != tok::ArrayStr)
				type = 0; // version 1 prints only 32-bit and string variables
			switch (type) {
			case tok::LoadVarInt8:
			case tok::ArrayInt8:
				buf += std::to_string(int8_t(_vars->read8(ref.offset)));
				break;
			case tok::LoadVarInt16:
			case tok::LoadVarInt32AsInt16:
			case tok::ArrayInt16:
				buf += std::to_string(int16_t(_vars->read16(ref.offset)));
				break;
			case tok::LoadVarInt32:
			case tok::ArrayInt32:
				buf += std::to_string(int32_t(_vars->read32(ref.offset)));
				break;
			case tok::LoadVarStr:
			case tok::ArrayStr:
				buf += _vars->readString(ref.offset);
				break;
			default:
				break;
			}
			s.skip(1);
		}

		if (_ver == 1 || (_draw.destSpriteX < _draw.back().width() && _draw.destSpriteY < _draw.back().height())) {
			_draw.textToPrint = buf;
			spriteOp(draw::PrintText);
		}
	} while (s.peekByte() != 200);
	s.skip(1);
}

// ---------------------------------------------------------------------------
// Top level

Task<void> Interpreter::run(std::string startTot) {
	_startTime = gameTicks();
	env().name = totBaseName(startTot);

	// CD audio image (GOG release); without it the game runs silently.
	if (auto mp3 = _files.read("Track1.mp3")) {
		bool ok = _mixer.loadCdImage(std::move(*mp3));
		trace(ok ? "CD audio: Track1.mp3 loaded" : "CD audio: Track1.mp3 unreadable");
	}

	// Default fonts, as the original loads them at startup.
	for (size_t i = 0; i < _game.defaultFonts.size(); i++)
		loadFont(int(i), _game.defaultFonts[i]);
	// The CD track table, when the game's scripts don't load one.
	if (_game.startupLic) {
		auto data = _files.read(_game.startupLic);
		bool ok = data && _cd.loadLic(*data);
		trace(std::string("startup LIC \"") + _game.startupLic + "\"" + (ok ? "" : " (failed)"));
	}
	co_await playTot(0);
	trace("game ended");
}

void Interpreter::initControlVars(bool full) {
	if (full)
		_animPalDir.fill(0);
	_nestLevel = 0;
	_breakFromLevel = -1;
	_break = false;
	_terminate = 0;
	_captureCounter = 0;
}

void Interpreter::renewTimeInVars() {
	std::time_t now = std::time(nullptr);
	std::tm t = *std::localtime(&now);
	writeVar(5, uint32_t(1900 + t.tm_year));
	writeVar(6, uint32_t(t.tm_mon + 1));
	writeVar(7, uint32_t((t.tm_wday + 6) % 7 + 1)); // Monday = 1
	writeVar(8, uint32_t(t.tm_mday));
	writeVar(9, uint32_t(t.tm_hour));
	writeVar(10, uint32_t(t.tm_min));
	writeVar(11, uint32_t(t.tm_sec));
}

void Interpreter::storeKey(int16_t key) {
	writeVar(12, gameTicks() - _startTime);
	storeMouse();
	writeVar(1, blasterPlaying() ? 1 : 0);
	switch (uint16_t(key)) {
	case 0x4800: key = 0x0B; break; // up
	case 0x5000: key = 0x0A; break; // down
	case 0x4D00: key = 0x09; break; // right
	case 0x4B00: key = 0x08; break; // left
	case 0x011B: key = 0x1B; break; // escape
	case 0x0E08: key = 0x19; break; // backspace
	case 0x5300: key = 0x1A; break; // delete
	default:
		if ((key & 0xFF) != 0)
			key &= 0xFF;
		break;
	}
	writeVar(0, uint32_t(uint16_t(key)));
	if (key != 0)
		flushKeys();
}

Task<void> Interpreter::playTot(int function) {
	int savedNest = _nestLevel;
	int savedBreakFrom = _breakFromLevel;
	int savedCaptures = _captureCounter;
	script().push();

	if (function <= 0) {
		while (!_quit) {
			initControlVars(function != -1);
			_totToLoad.clear();

			Env &e = env();
			if (e.name.empty())
				break;

			_draw.cursorHotspotXVar = -1; // the previous TOT's hotspot table is gone
			animateCursor(4);              // wait cursor while loading
			if (function != -1) {
				if (_ver == 1 && _cd.current()) { // Gobliiins stops the music at every TOT load
					_cd.stop();
					_mixer.stopCd();
					_ffCdChanged = true;
				}
				resetAnimationState();
			}
			e.tot = std::make_unique<TotFile>();
			if (!e.tot->load(_files, e.name, language)) {
				trace("cannot load " + e.name + ".TOT");
				e.tot.reset();
				_terminate = 2;
				break;
			}
			e.dis = std::make_unique<Disassembler>(*e.tot);
			e.script.attach(&e.tot->bytes());

			if (!_vars)
				_vars = std::make_shared<Variables>((e.tot->header().variablesCount & 0xFFFF) * 4);
			bindEnv();

			trace("loaded " + e.name + ".TOT, start " + hex(e.tot->header().functions[0]));
			if ((_rules.levels == LevelSystem::Gob2 || _rules.levels == LevelSystem::Gob3) && e.name == "MENU")
				_freshVars.clear(); // a new game: the next room start is its first
			if (_rules.levels == LevelSystem::Gob1)
				noteTotStartV1(e.name);
			e.script.seek(e.tot->header().functions[0]);

			renewTimeInVars();
			writeVar(13, _game.useMouse);
			writeVar(14, _game.soundFlags);
			writeVar(15, _ver == 3 ? uint32_t(_fakeVideoMode) : _game.videoMode);
			writeVar(16, _game.scriptLanguage(language));

			co_await callSub(2);

			if (!_totToLoad.empty())
				_terminate = 0;

			_draw.blitInvalidated();
			for (int i = 0; i < _captureCounter; i++)
				capturePop(false);
			_captureCounter = 0;
			if (function != -1) {
				freeGoblins();
				freeAllSounds();
			}

			e.script.attach(nullptr);
			e.dis.reset();
			e.tot.reset();
			bindEnv();

			if (_totToLoad.empty())
				break;
			e.name = _totToLoad;
		}
	} else {
		initControlVars(false);
		const auto &fns = env().tot->header().functions;
		script().seek(function > 13 ? function : fns[function + 1]);
		co_await callSub(2);
		if (_terminate != 0)
			_terminate = 2;
	}

	_nestLevel = savedNest;
	_breakFromLevel = savedBreakFrom;
	_captureCounter = savedCaptures;
	script().pop(true);
}

Task<void> Interpreter::callSub(int retFlag) {
	while (!_quit && !script().finished() && script().pos() != 0) {
		uint8_t block = script().peekByte();
		if (block == 1)
			co_await funcBlock(retFlag);
		else if (block == 2)
			co_await evaluateHotspots();
		else
			throw std::runtime_error("unknown block type " + std::to_string(block) + " at " + hex(script().pos()) +
			                         " in " + env().name);
	}
	if (!script().finished() && script().pos() == 0)
		_terminate = 1;
}

Task<void> Interpreter::funcBlock(int retFlag) {
	Params p;
	p.retFlag = retFlag;

	if (script().finished())
		co_return;

	_break = false;
	script().skip(1);
	p.cmdCount = script().readByte();
	script().skip(2);

	if (p.cmdCount == 0) {
		script().setFinished(true);
		co_return;
	}

	p.counter = 0;
	do {
		if (_terminate)
			break;

		p.opStart = script().pos();
		if (traceOps && env().dis)
			trace("op " + hex(p.opStart) + " " + env().dis->decodeInstruction(p.opStart).first);
		uint8_t b = script().readByte();
		uint8_t hi = b >> 4;
		uint8_t index = hi >= 12 ? uint8_t((16 - hi) * 16 + (b & 0xF)) : hi;

		p.counter++;
		p.doReturn = false;
		co_await execFunc(index, p);

		if (p.doReturn)
			co_return;
		if (_quit)
			break;

		if (_break) {
			if (p.retFlag != 2)
				break;
			if (_breakFromLevel == -1)
				_break = false;
			break;
		}

		if (++_frameBudget >= kOpsPerFrameBudget) {
			_frameBudget = 0;
			co_await _sched.nextFrame();
		}
	} while (p.counter != p.cmdCount);

	script().setFinished(true);
}

Task<void> Interpreter::sleep(uint32_t ms) {
	uint32_t until = gameTicks() + ms;
	do {
		_frameBudget = 0;
		co_await _sched.nextFrame();
	} while (!_quit && int32_t(gameTicks() - until) < 0);
}

uint32_t Interpreter::checkSwitchTable() {
	if (_ver == 1)
		return checkSwitchTableV1();
	uint8_t type = script().peekByte();
	VarRef ref = readVar();

	int32_t value;
	switch (type) {
	case tok::LoadVarInt8:
	case tok::ArrayInt8:
		value = int8_t(_vars->read8(ref.offset));
		break;
	case tok::LoadVarInt32:
	case tok::ArrayInt32:
		value = int32_t(_vars->read32(ref.offset));
		break;
	default:
		value = int16_t(_vars->read16(ref.offset));
		break;
	}

	uint32_t offset = 0;
	bool found = false;
	int8_t len = script().readInt8();
	while (len != -5) {
		for (int i = 0; i < len; i++) {
			uint8_t t = script().peekByte();
			switch (t) {
			case tok::LoadImmInt32:
				script().skip(1);
				found |= !found && value == int32_t(script().peekUint32());
				script().skip(5);
				break;
			case tok::LoadImmInt16:
				script().skip(1);
				found |= !found && value == script().peekInt16();
				script().skip(3);
				break;
			case tok::LoadImmInt8:
				script().skip(1);
				found |= !found && value == int8_t(script().peekByte());
				script().skip(2);
				break;
			default:
				if (!found) {
					_expr.parse();
					found = value == _expr.resultInt();
				} else {
					_expr.skip();
				}
				break;
			}
		}
		if (found && offset == 0)
			offset = script().pos();
		script().skipBlock();
		len = script().readInt8();
	}

	if ((script().peekByte() >> 4) != 4)
		return offset;

	script().skip(1);
	if (offset == 0)
		offset = script().pos();
	script().skipBlock();
	return offset;
}

// ---------------------------------------------------------------------------
// Opcodes

Task<void> Interpreter::execFunc(uint8_t index, Params &p) {
	if (_ver != 2 && co_await execFuncVersion(index, p))
		co_return;
	Script &s = script();
	bool tail = p.counter == p.cmdCount && p.retFlag == 2;

	switch (index) {
	case 0x00:
	case 0x01: { // callSub
		uint16_t offset = s.readUint16();
		if (offset < 128)
			co_return;
		if (skipCopyProtection && _rules.isCopyProtectionCall(env().name, offset)) {
			trace("skipping copy protection screen");
			co_return;
		}
		s.call(offset);
		if (tail) {
			s.pop(false);
			p.doReturn = true;
			co_return;
		}
		co_await callSub(2);
		script().pop(true);
		co_return;
	}

	case 0x05: { // switch
		uint32_t offset = checkSwitchTable();
		s.call(offset);
		if (offset == 0)
			s.setFinished(true);
		if (tail) {
			s.pop(false);
			p.doReturn = true;
			co_return;
		}
		co_await funcBlock(0);
		script().pop(true);
		co_return;
	}

	case 0x06: { // repeat ... until
		_nestLevel++;
		uint32_t blockPos = s.pos();
		bool flag;
		do {
			script().seek(blockPos);
			uint32_t size = script().peekUint16(2) + 2;
			co_await funcBlock(1);
			script().seek(blockPos + size + 1);
			flag = _expr.evalBool();
		} while (!flag && !_break && !_terminate && !_quit);
		_nestLevel--;
		if (_breakFromLevel > -1) {
			_break = false;
			_breakFromLevel = -1;
		}
		co_return;
	}

	case 0x07: { // while
		_nestLevel++;
		bool flag;
		do {
			uint32_t startPos = script().pos();
			flag = _expr.evalBool();
			if (_terminate)
				co_return;
			uint32_t blockPos = script().pos();
			uint32_t size = script().peekUint16(2) + 2;
			if (flag) {
				co_await funcBlock(1);
				script().seek(startPos);
			} else {
				script().skip(int32_t(size));
			}
			if (_break || _terminate || _quit) {
				script().seek(blockPos + size);
				break;
			}
		} while (flag);
		_nestLevel--;
		if (_breakFromLevel > -1) {
			_break = false;
			_breakFromLevel = -1;
		}
		co_return;
	}

	case 0x08: { // if / else
		bool cond = _expr.evalBool();
		if (cond) {
			if (tail) {
				p.doReturn = true;
				co_return;
			}
			script().push();
			co_await funcBlock(0);
			script().pop(true);
			script().skipBlock();
			if ((script().readByte() >> 4) != 12)
				co_return;
			script().skipBlock();
		} else {
			script().skipBlock();
			if ((script().readByte() >> 4) != 12)
				co_return;
			if (tail) {
				p.doReturn = true;
				co_return;
			}
			script().push();
			co_await funcBlock(0);
			script().pop(true);
			script().skipBlock();
		}
		co_return;
	}

	case 0x09: { // assign
		uint8_t destType = s.peekByte();
		VarRef dest = readVar();
		int count = 1;
		if (s.peekByte() == tok::Stop) {
			s.skip(1);
			count = s.readByte();
		}
		for (int i = 0; i < count; i++) {
			ExprType t = _expr.parse();
			int32_t v = _expr.resultInt();
			switch (destType) {
			case tok::LoadVarInt8:
			case tok::ArrayInt8:
				_vars->write8(dest.offset + i, uint8_t(v));
				break;
			case tok::LoadVarInt16:
			case tok::ArrayInt16:
				_vars->write16(dest.offset + i * 2, uint16_t(v));
				break;
			case tok::LoadVarInt32:
			case tok::ArrayInt32:
				_vars->write32(dest.offset + i * 4, uint32_t(v));
				break;
			case tok::LoadVarInt32AsInt16:
				_vars->write16(dest.offset + i * 4, uint16_t(v));
				break;
			case tok::LoadVarStr:
			case tok::ArrayStr:
				if (t == ExprType::Int)
					_vars->write8(dest.offset, uint8_t(v));
				else
					_vars->writeString(dest.offset, _expr.resultStr());
				break;
			default:
				break;
			}
		}
		co_return;
	}

	case 0x12: { // loadTot
		std::string name;
		if (s.peekByte() & 0x80) {
			s.skip(1);
			name = evalString();
		} else {
			uint8_t len = s.readByte();
			for (int i = 0; i < len; i++)
				name.push_back(char(s.readByte()));
		}
		_totToLoad = totBaseName(name);
		_terminate = 1;
		trace("loadTot " + _totToLoad);
		co_return;
	}

	case 0x14: { // keyFunc
		animPalette();
		_draw.blitInvalidated();
		int16_t cmd = s.readInt16();
		// Goblins 3: a script polling the keys in a tight loop (FINAL waits
		// for a click so) waits a frame between polls, as Inter::
		// handleBusyWait slows such loops down, instead of running up the
		// op budget every frame.
		if (_ver >= 3 && cmd == 2) {
			const uint32_t now = gameTicks();
			if (now - _lastKeyPoll <= 20) {
				_frameBudget = 0;
				co_await _sched.nextFrame();
			}
			_lastKeyPoll = gameTicks();
		}
		switch (cmd) {
		case -1:
			break;
		case 0: { // wait for a key or click; hover callbacks still run
			_draw.showCursor &= ~2;
			_frameBudget = 0;
			co_await _sched.nextFrame();
			uint16_t id, index;
			int key = co_await checkHotspots(0, 0, &id, &index);
			storeKey(int16_t(key));
			flushKeys();
			break;
		}
		case 1: { // poll input; a button press is reported once
			forceMouseUp(true);
			int key = checkKeys();
			storeKey(int16_t(key));
			_frameBudget = 0;
			co_await _sched.nextFrame();
			break;
		}
		case 2:
			writeVar(0, uint32_t(_host.pollKey()));
			break;
		default:
			co_await sleep(uint32_t(cmd));
			break;
		}
		co_return;
	}

	case 0x1E: // draw operations
		co_await execDraw(p.opStart);
		co_return;

	case 0x1F: // setcmdCount
		p.cmdCount = s.readByte();
		p.counter = 0;
		co_return;

	case 0x20: // return
		if (p.retFlag != 2)
			_break = true;
		s.setFinished(true);
		p.doReturn = true;
		co_return;

	case 0x21:
		renewTimeInVars();
		co_return;

	case 0x30: // break out of the innermost loop
		if (p.retFlag == 1) {
			_break = true;
			s.setFinished(true);
			p.doReturn = true;
			co_return;
		}
		if (_nestLevel == 0)
			co_return;
		_breakFromLevel = _nestLevel;
		_break = true;
		s.setFinished(true);
		p.doReturn = true;
		co_return;

	case 0x0A: // loadSpriteToPos
		_draw.spriteLeft = s.readInt16();
		_draw.destSpriteX = evalVal();
		_draw.destSpriteY = evalVal();
		_draw.transparency = s.peekByte() & 1;
		_draw.destSurface = int16_t((s.peekByte() >> 1) - 1);
		if (_draw.destSurface < 0)
			_draw.destSurface = 101;
		s.skip(2);
		spriteOp(draw::LoadSprite);
		co_return;

	case 0x13:
		co_await palLoad();
		co_return;

	case 0x02: // printTotText
		printTotText(s.readInt16());
		co_return;

	case 0x11: // printText
		printText();
		co_return;

	case 0x4B: { // loadFont
		std::string file = evalString();
		int16_t slot = s.readInt16();
		animateCursor(4);
		bool ok = loadFont(slot, file);
		trace("loadFont \"" + file + "\" -> slot " + std::to_string(slot) + (ok ? "" : " (failed)"));
		co_return;
	}

	case 0x4C: { // freeFont
		int16_t slot = s.readInt16();
		if (slot >= 0 && slot < Draw::kFontCount)
			_draw.fonts[slot].reset();
		co_return;
	}

	case 0x24: // putPixel
		_draw.destSurface = s.readInt16();
		_draw.destSpriteX = evalVal();
		_draw.destSpriteY = evalVal();
		_draw.frontColor = evalVal();
		spriteOp(draw::PutPixel);
		co_return;

	case 0x26: { // createSprite
		int16_t index, width, height;
		if (s.peekByte(1) == 0) {
			index = s.readInt16();
			width = s.readInt16();
			height = s.readInt16();
		} else {
			index = evalVal();
			width = evalVal();
			height = evalVal();
		}
		s.readInt16(); // flags: surface type, irrelevant here
		_draw.createSprite(index, width, height);
		co_return;
	}

	case 0x27: // freeSprite
		_draw.freeSprite(s.readInt16());
		co_return;

	case 0x31: // loadSpriteContent
		_draw.spriteLeft = s.readInt16();
		_draw.destSurface = s.readInt16();
		_draw.transparency = s.readInt16();
		_draw.destSpriteX = 0;
		_draw.destSpriteY = 0;
		spriteOp(draw::LoadSprite);
		co_return;

	case 0x32: // copySprite
		copySprite();
		co_return;

	case 0x33: { // fillRect
		_draw.destSurface = s.readInt16();
		_draw.destSpriteX = evalVal();
		_draw.destSpriteY = evalVal();
		_draw.spriteRight = evalVal();
		_draw.spriteBottom = evalVal();
		_draw.backColor = evalVal();
		if (!_draw.sprite(_draw.destSurface))
			co_return;
		if (_draw.spriteRight < 0) {
			_draw.destSpriteX += _draw.spriteRight - 1;
			_draw.spriteRight = -_draw.spriteRight + 2;
		}
		if (_draw.spriteBottom < 0) {
			_draw.destSpriteY += _draw.spriteBottom - 1;
			_draw.spriteBottom = -_draw.spriteBottom + 2;
		}
		spriteOp(draw::FillRect);
		co_return;
	}

	case 0x34: // drawLine
		_draw.destSurface = s.readInt16();
		_draw.destSpriteX = evalVal();
		_draw.destSpriteY = evalVal();
		_draw.spriteRight = evalVal();
		_draw.spriteBottom = evalVal();
		_draw.frontColor = evalVal();
		spriteOp(draw::DrawLine);
		co_return;

	case 0x36: // invalidate (draws a circle)
		_draw.destSurface = s.readInt16();
		_draw.destSpriteX = evalVal();
		_draw.destSpriteY = evalVal();
		_draw.spriteRight = evalVal();
		_draw.frontColor = evalVal();
		spriteOp(draw::Invalidate);
		co_return;

	case 0x37: // setBackDelta
		_draw.backDeltaX = evalVal();
		_draw.backDeltaY = evalVal();
		co_return;

	case 0x25: { // goblinFunc
		int16_t cmd = s.readInt16();
		int16_t paramCount = s.readInt16();
		if (_ver != 2 && co_await goblinFuncVersion(cmd, paramCount))
			co_return;
		if (cmd == 100) {
			int16_t a[6];
			for (int16_t &v : a)
				v = s.readInt16();
			handleGoblins(a[0], a[1], a[2], a[3], a[4], a[5]);
		} else if (cmd == 0 || cmd == 1 || cmd == 2 || cmd == 10) {
			s.skip(2); // Amiga music commands: unused here
		} else if (cmd != 101 && cmd != 500 && cmd != 501) {
			s.skip(std::max<int16_t>(paramCount, 0) * 2);
		}
		co_return;
	}

	case 0x22: // speakerOn
		speakerOn(evalVal());
		co_return;

	case 0x23: // speakerOff
		speakerOff();
		co_return;

	case 0x38: { // playSound (slot, repCount, frequency)
		int slot = evalVal(), rep = evalVal(), freq = evalVal();
		if (slot >= 0 && slot < kSoundSlots && _sounds[size_t(slot)].pcm) {
			blasterStop(0);
			blasterPlay(slot, rep - 1, freq, 0);
		}
		co_return;
	}

	case 0x39: { // stopSound: negative = AdLib (unused by the CD version)
		int v = evalVal();
		if (v >= 0)
			blasterStop(v);
		co_return;
	}

	case 0x3B: // freeSoundSlot
		freeSoundSlot(evalVal());
		co_return;

	case 0x35: { // strToLong
		VarRef str = readVar();
		int32_t v = std::atoi(_vars->readString(str.offset).substr(0, 19).c_str());
		VarRef dest = readVar();
		writeVarOffset(dest.offset, uint32_t(v));
		co_return;
	}

	case 0x3E: { // getFreeMem
		VarRef a = readVar();
		VarRef b = readVar();
		writeVarOffset(a.offset, 1000000);
		writeVarOffset(b.offset, 1000000);
		writeVar(16, env().tot->header().variablesCount * 4);
		co_return;
	}

	case 0x3F: { // checkData
		std::string file = evalString();
		VarRef dest = readVar();
		int32_t size = -1;
		if (SaveKind kind = saveKind(file); kind != SaveKind::None)
			size = saveFileSize(kind);
		else if (auto data = _files.read(file))
			size = int32_t(data->size());
		trace("checkData \"" + file + "\" -> " + std::to_string(size));
		writeVarOffset(dest.offset, size == -1 ? uint32_t(-1) : 50);
		writeVar(16, uint32_t(size));
		co_return;
	}

	case 0x41: { // cleanupStr
		VarRef str = readVar();
		_vars->writeString(str.offset, cleanupString(_vars->readString(str.offset)));
		co_return;
	}

	case 0x42: { // insertStr
		VarRef str = readVar();
		_expr.parse();
		std::string ins = _expr.resultStr();
		int16_t pos = evalVal();
		std::string cur = _vars->readString(str.offset);
		size_t at = std::min<size_t>(size_t(std::max<int16_t>(pos, 0)), cur.size());
		_vars->writeString(str.offset, cur.insert(at, ins));
		co_return;
	}

	case 0x43: { // cutStr
		VarRef str = readVar();
		int16_t pos = evalVal();
		int16_t len = evalVal();
		std::string cur = _vars->readString(str.offset);
		if (pos >= 0 && size_t(pos) < cur.size())
			_vars->writeString(str.offset, cur.erase(size_t(pos), size_t(std::max<int16_t>(len, 0))));
		co_return;
	}

	case 0x44: { // strstr
		VarRef str = readVar();
		_expr.parse();
		std::string needle = _expr.resultStr();
		VarRef dest = readVar();
		size_t at = _vars->readString(str.offset).find(needle);
		writeVarOffset(dest.offset, at == std::string::npos ? uint32_t(-1) : uint32_t(at));
		co_return;
	}

	case 0x45: { // istrlen
		VarRef str = readVar();
		uint32_t len = uint32_t(_vars->readString(str.offset).size());
		VarRef dest = readVar();
		writeVarOffset(dest.offset, len);
		co_return;
	}

	case 0x4D: // readData
		readData();
		co_return;

	case 0x4E: // writeData
		writeData();
		co_return;

	case 0x47: // setFrameRate
		_frameRate = std::max<int>(1, evalVal());
		co_return;

	case 0x03: { // loadCursor
		int16_t resId = s.readInt16();
		int8_t slot = s.readInt8();
		loadCursorResource(resId, slot);
		co_return;
	}

	case 0x15: { // capturePush
		int16_t l = evalVal(), t = evalVal(), w = evalVal(), h = evalVal();
		if (w > 0 && h > 0) {
			capturePush(l, t, w, h);
			_captureCounter++;
		}
		co_return;
	}

	case 0x16: // capturePop
		if (_captureCounter > 0) {
			_captureCounter--;
			capturePop(true);
		}
		co_return;

	case 0x46: { // setMousePos
		_interMouseX = evalVal();
		_interMouseY = evalVal();
		_host.warpMouse(_interMouseX, _interMouseY);
		co_return;
	}

	case 0x48: { // animatePalette: the per-frame "present and pace" opcode
		_draw.blitInvalidated();
		co_await waitEndFrame();
		animPalette();
		storeKey(int16_t(checkKeys()));
		co_return;
	}

	case 0x49: // animateCursor
		animateCursor(1);
		co_return;

	case 0x4A: // blitCursor
		_draw.blitCursor();
		co_return;

	case 0x4F: { // manageDataFile
		std::string file = evalString();
		if (!file.empty()) {
			bool ok = _files.openArchive(file);
			trace("open archive \"" + file + "\"" + (ok ? "" : " (not found)"));
		} else {
			trace("close archive (ignored)");
		}
		co_return;
	}

	default:
		// Not implemented yet (graphics, sound, goblins, ...): log and skip.
		stub(p.opStart);
		co_return;
	}
}

Task<void> Interpreter::execDraw(uint32_t opStart) {
	Script &s = script();
	uint8_t op = s.readByte();
	if (_ver != 2 && co_await execDrawVersion(op, opStart))
		co_return;

	switch (op) {
	case 0x40: { // totSub
		uint8_t length = s.readByte();
		std::string name;
		if (length & 0x80)
			name = evalString();
		else
			for (int i = 0; i < (length & 0x7F); i++)
				name.push_back(char(s.readByte()));
		if (totBaseName(name) != "EDIT")
			forceMouseUp(false);
		uint8_t flags = s.readByte();
		co_await totSub(flags, name);
		co_return;
	}

	case 0x41: { // switchTotSub
		int16_t index = s.readInt16();
		int16_t function = s.readInt16();
		co_await switchTotSub(index, function);
		co_return;
	}

	case 0x42: { // pushVars
		uint8_t count = s.readByte();
		for (int i = 0; i < count; i++) {
			uint8_t t = s.peekByte();
			if (t == tok::LoadVarStr || t == tok::ArrayStr) {
				VarRef ref = readVar();
				s.skip(1);
				uint32_t size = env().tot->header().animDataSize * 4u;
				VarStackEntry e;
				for (uint32_t b = 0; b < size; b++)
					e.data.push_back(_vars->read8(ref.offset + b));
				_varStack.push_back(std::move(e));
			} else {
				ExprType t2 = _expr.parse();
				int32_t v = t2 == ExprType::Int ? _expr.resultInt() : 0;
				VarStackEntry e;
				for (int b = 0; b < 4; b++)
					e.data.push_back(uint8_t(uint32_t(v) >> (8 * b)));
				_varStack.push_back(std::move(e));
			}
		}
		co_return;
	}

	case 0x43: { // popVars
		uint8_t count = s.readByte();
		for (int i = 0; i < count; i++) {
			VarRef ref = readVar();
			if (_varStack.empty()) {
				trace("popVars: stack underflow");
				continue;
			}
			const VarStackEntry &e = _varStack.back();
			for (size_t b = 0; b < e.data.size(); b++)
				_vars->write8(ref.offset + uint32_t(b), e.data[b]);
			_varStack.pop_back();
		}
		co_return;
	}

	case 0x00: // loadMult
		loadMult();
		co_return;

	case 0x01: { // playMult
		int16_t w = s.readInt16();
		co_await playMult(w);
		co_return;
	}

	case 0x02: // freeMultKeys
		freeMultKeys(s.readUint16());
		co_return;

	case 0x10: // loadAnim
		sceneryLoadAnim(false);
		co_return;

	case 0x11: // freeAnim
		_scenery.freeAnim(int(evalInt()));
		co_return;

	case 0x13: // multSub
		multSub();
		co_return;

	case 0x14: // initMult
		initMult();
		co_return;

	case 0x15: // freeMult
		freeMult();
		co_return;

	case 0x16: // animate
		animate();
		co_return;

	case 0x17: // loadMultObject
		loadMultObject();
		co_return;

	case 0x19: { // getObjAnimSize
		int obj = int(evalInt());
		VarRef l = readVar(), t = readVar(), r = readVar(), b = readVar();
		if (obj < 0 || obj >= int(_objects.size())) {
			_scenery.toRedrawLeft = _scenery.toRedrawTop = _scenery.toRedrawRight = _scenery.toRedrawBottom = 0;
		} else if (animField(obj, kIsStatic) == 0) {
			_scenery.updateAnim(animField(obj, kLayer), animField(obj, kFrame), animField(obj, kAnimation), 0,
			                    objPosX(obj), objPosY(obj), false);
		}
		_scenery.toRedrawLeft = std::max<int16_t>(_scenery.toRedrawLeft, 0);
		_scenery.toRedrawTop = std::max<int16_t>(_scenery.toRedrawTop, 0);
		writeVarOffset(l.offset, uint32_t(int32_t(_scenery.toRedrawLeft)));
		writeVarOffset(t.offset, uint32_t(int32_t(_scenery.toRedrawTop)));
		writeVarOffset(r.offset, uint32_t(int32_t(_scenery.toRedrawRight)));
		writeVarOffset(b.offset, uint32_t(int32_t(_scenery.toRedrawBottom)));
		co_return;
	}

	case 0x50: // loadMapObjects
		loadMapObjects();
		co_return;

	case 0x51: // freeGoblins
		freeGoblins();
		co_return;

	case 0x52: { // moveGoblin
		int x = evalVal(), y = evalVal(), obj = evalVal();
		goblinMove(x, y, obj);
		co_return;
	}

	case 0x53: { // writeGoblinPos
		VarRef vx = readVar(), vy = readVar();
		int obj = evalVal();
		if (obj >= 0 && obj < int(_objects.size())) {
			writeVarOffset(vx.offset, _objects[size_t(obj)].goblinX);
			writeVarOffset(vy.offset, _objects[size_t(obj)].goblinY);
		}
		co_return;
	}

	case 0x54: { // stopGoblin
		int obj = evalVal();
		if (obj >= 0 && obj < int(_objects.size()))
			setAnimField(obj, kPathExistence, 4);
		co_return;
	}

	case 0x55: { // setGoblinState
		int obj = evalVal(), state = evalVal(), type = evalVal();
		setGoblinState(obj, state, type);
		co_return;
	}

	case 0x56: { // placeGoblin
		int obj = evalVal(), x = evalVal(), y = evalVal(), state = evalVal();
		placeGoblin(obj, x, y, state);
		co_return;
	}

	case 0x1D: // loadCurLayer: static scenery is unused by Gobliins 2
		evalVal();
		evalVal();
		co_return;

	case 0x07: { // initCursor
		VarRef hx = readVar();
		VarRef hy = readVar();
		int w = std::max<int>(16, s.readInt16());
		int h = std::max<int>(16, s.readInt16());
		int count = s.readInt16();
		if (count > 0x80)
			count -= 0x80;
		count = std::max(2, count);
		_draw.cursorHotspotXVar = int32_t(hx.offset / 4);
		_draw.cursorHotspotYVar = int32_t(hy.offset / 4);
		Surface *strip = _draw.cursorStrip();
		if (w != _draw.cursorWidth || h != _draw.cursorHeight || !strip || strip->width() != w * count) {
			_draw.cursorWidth = w;
			_draw.cursorHeight = h;
			_draw.createSprite(draw::kCursorSurface, w * count, h);
			_cursorResources.fill(-1);
			_draw.cursorAnimLow.fill(-1);
			_draw.cursorAnimHigh.fill(0);
			_draw.cursorAnimDelays.fill(0);
			_draw.cursorAnimLow[1] = 0;
		}
		co_return;
	}

	case 0x08: { // initCursorAnim
		int idx = evalVal();
		int8_t low = int8_t(s.readInt16()), high = int8_t(s.readInt16()), delay = int8_t(s.readInt16());
		if (idx >= 0 && idx < 40) {
			_draw.cursorAnimLow[size_t(idx)] = low;
			_draw.cursorAnimHigh[size_t(idx)] = high;
			_draw.cursorAnimDelays[size_t(idx)] = delay;
		}
		_draw.showCursor = 3;
		co_return;
	}

	case 0x09: { // clearCursorAnim
		int idx = evalVal();
		if (idx >= 0 && idx < 40) {
			_draw.cursorAnimLow[size_t(idx)] = -1;
			_draw.cursorAnimHigh[size_t(idx)] = 0;
			_draw.cursorAnimDelays[size_t(idx)] = 0;
		}
		_draw.showCursor = 0;
		co_return;
	}

	case 0x0A: { // setRenderFlags
		int16_t v = evalVal();
		if (v & 0x8000)
			_draw.renderFlags |= v & 0x3FFF;
		else if (v & 0x4000)
			_draw.renderFlags &= v & 0x3FFF;
		else
			_draw.renderFlags = v;
		co_return;
	}

	case 0x80: { // initScreen
		s.readInt16(); // video mode / offset: always VGA 320x200 here
		evalVal();
		evalVal();
		_draw.initScreen();
		co_return;
	}

	case 0x20: { // playCDTrack
		if (!(_draw.renderFlags & draw::NoBlitInvalidated))
			_draw.blitInvalidated();
		startCdTrack(evalString());
		co_return;
	}

	case 0x21: // waitCDTrackEnd
		while (!_quit && _cd.position({}, cdTicks()) >= 0) {
			_frameBudget = 0;
			co_await _sched.nextFrame();
		}
		co_return;

	case 0x22: // stopCD
		_cd.stop();
		_mixer.stopCd();
		_ffCdChanged = true;
		trace("stopCD");
		co_return;

	case 0x23: { // readLIC
		std::string file = evalString() + ".LIC";
		auto data = _files.read(file);
		bool ok = data && _cd.loadLic(*data);
		trace("readLIC \"" + file + "\"" + (ok ? "" : " (failed)"));
		co_return;
	}

	case 0x24: // freeLIC
		_cd.freeLic();
		co_return;

	case 0x25: { // getCDTrackPos
		// The original waits a tick here, which keeps polling loops from spinning.
		_frameBudget = 0;
		co_await _sched.nextFrame();
		VarRef pos = readVar();
		VarRef name = readVar();
		int32_t cdPos = _cd.position(_vars->readString(name.offset), cdTicks());
		if (cdPos != -1 && _cd.current() && skipPending())
			cdPos = int32_t(_cd.current()->end - _cd.current()->start); // skipping: the track is over
		writeVarOffset(pos.offset, uint32_t(cdPos));
		_vars->writeString(name.offset, _cd.currentName());
		co_return;
	}

	default:
		stub(opStart);
		co_return;
	}
}

// Plays CD track `t` from `offset` CD frames in: the stretch of the CD image,
// or the music pack's replacement (which always starts at its beginning).
void Interpreter::startCdTrack(const std::string &name) {
	const std::string track = _rules.cdTrackName(name); // a script's misspelt name (Goblins 3)
	bool ok = _cd.start(track, cdTicks());
	std::string note = ok ? "" : " (unknown track)";
	if (ok && _ffSkip) {
		_ffCdChanged = true; // the music starts when the skip ends (resyncCdAudio)
		note = " (skipping: silent)";
	} else if (ok) {
		startCdMusic(*_cd.current(), 0, note);
	}
	trace("playCDTrack \"" + track + "\"" + note);
}

void Interpreter::startCdMusic(const CdAudio::Track &t, int32_t offset, std::string &note) {
	// The GOG audio image keeps the CD's 150-sector lead-in, so file time is
	// simply sector / 75 (verified against the silent gaps between tracks).
	double start = double(t.start + uint32_t(offset)) / 75.0;
	double length = double(int32_t(t.end - t.start + 1) - offset) / 75.0;
	if (length <= 0)
		return;
	std::string error;
	const bool fullLength = musicPack && musicPack->fullLength;
	if (fullLength && t.name == _cdReplacing && _mixer.cdPlaying()) {
		// A room restarting its loop track (it does when the CD track
		// is over): a replacement played to its end goes on instead.
		note = " (replacement continues)";
	} else if (auto replacement = musicPack ? musicPack->open(t.name, &error) : nullptr) {
		_mixer.playCdSource(std::move(replacement), fullLength ? -1.0 : length);
		_cdReplacing = t.name;
		note = " (replaced by " + musicPack->find(t.name)->file + ")";
	} else {
		_mixer.playCd(start, length);
		_cdReplacing.clear();
		if (!error.empty())
			note = " (replacement unreadable: " + error + ")";
	}
}

// Runs another TOT in a fresh environment, then returns to the caller.
Task<void> Interpreter::totSub(uint8_t flags, std::string name) {
	if (flags == 16 || flags == 17)
		co_return; // prefetch hints

	if (_terminate != 0)
		co_return;

	trace("totSub " + name + " flags " + std::to_string(flags));

	auto e = std::make_unique<Env>();
	e->name = totBaseName(name);
	int callerEnv = _cur;
	_envs.push_back(std::move(e));
	_cur = int(_envs.size()) - 1;

	std::shared_ptr<Variables> savedVars;
	if (flags & 5) {
		savedVars = _vars;
		_vars.reset();
	}

	if (!(flags & 0x20))
		hotspotsPush(0, true);

	noteRoomStart(_envs[_cur]->name, callerEnv);
	co_await playTot((flags & 0x06) ? -1 : 0);

	if (_terminate != 2)
		_terminate = 0;

	if (!(flags & 0x20)) {
		hotspotsClear();
		hotspotsPop();
	}

	if (flags & 5)
		_vars = savedVars;

	_envs.pop_back();
	_cur = callerEnv;
	bindEnv();
	trace("returned from totSub " + name);
	finishLevelJump(totBaseName(name));
}

// Runs a function inside an environment further down the TOT stack.
Task<void> Interpreter::switchTotSub(int16_t index, int16_t function) {
	int count = int(_envs.size()) - 1;
	if (count - index < 1)
		co_return;

	int target = _cur - index - (index >= 0 ? 1 : 0);
	if (target < 0 || target >= int(_envs.size()) || !_envs[target]->tot)
		co_return;

	// Script bugs worked around (engine/rules.cpp).
	if (_rules.ignoresSwitchTotSub(_envs[target]->name, index, function))
		co_return;

	if (_terminate != 0)
		co_return;

	trace("switchTotSub to " + _envs[target]->name + " function " + std::to_string(function));
	int saved = _cur;
	_cur = target;
	bindEnv();

	hotspotsPush(0, true);
	co_await playTot(function);

	if (_terminate != 2)
		_terminate = 0;

	hotspotsClear();
	hotspotsPop();

	_cur = saved;
	bindEnv();
}

} // namespace gob
