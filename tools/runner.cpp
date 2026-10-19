// gob2native - headless script runner
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Runs the game's scripts without a window, on a simulated 60 fps clock, and
// prints a trace of what the engine does. Used to verify the interpreter.
//
// Usage: gobrun <gamedir> [--game gob1|gob2|gob3] [--start TOT] [--frames N] [--quiet] [--press-every SECONDS]
//
// The game is detected from the folder (printed to stderr); --game picks it.
// --press-every simulates the player pressing Enter at that interval, to get
// past "press a key" screens. --snap-every saves the visible screen as BMP
// files into DIR at that interval (and at the end of the run). --click T X Y
// simulates a left click at screen position (X, Y) at simulated time T
// seconds (repeatable), --rclick T X Y a right click; --move T X Y only
// moves the mouse there.
// --save-dir DIR enables save games in DIR. --type T TEXT types TEXT
// (one character per poll) starting at time T. --press-stop T ends the
// periodic key presses at time T. --dump-vars FILE writes the variable
// space at the end of the run. --language NAME (french, german, british,
// spanish, italian, american) selects the game language. --speed X runs
// the game clock X times as fast (cutscenes stay at 1x unless
// --fast-cutscenes is given). --seed N seeds the engine's random numbers
// (default 1; the same sequence on every platform). --key T CODE presses the key with the
// BIOS-style CODE (e.g. 0x011B Escape) at time T; --skip-at T requests a
// cutscene skip at time T. --watch OFFSET (repeatable) traces every change
// of the variable byte at OFFSET. --level-at T ROOM jumps to room ROOM
// (GOB08, or just 8) at time T, as the overlay's Levels tab does;
// --quicksave-at T and --quickload-at T quick save / quick load.
// --set-var T SLOT VALUE sets the 32-bit variable v[SLOT] at time T (0:
// before the start script gets past its first frame); --set-str T SLOT TEXT
// writes the string variable s[SLOT] (Goblins 3: s[580] is the room MENU
// starts next).
// --watch-skip logs when a skip becomes possible or impossible (what the
// overlay's Skip button shows); --print-objects and --print-hotspots print
// the mult objects (and Gobliiins' goblins and objects) and the hotspot
// table at the end of the run. --state-on-notify PREFIX writes the
// variables and the objects (PREFIX-N.vars, PREFIX-N.objects) at the moment
// a quick save or quick load completes; --state-on-trace TEXT PREFIX does it
// whenever a trace line contains TEXT ("saved slot", "loaded save slot").
// --list-levels prints the levels the
// overlay's Levels tab lists (Gobliiins: number, script, access code) and
// exits. For Gobliiins, --level-at takes the level's number in play order
// (1-22) or its script (AVT005).
//
// Audio: --audio-dump FILE.wav writes the mixer's output (44.1 kHz stereo
// 16-bit) in lock-step with the simulated clock, so sample n is simulated
// time n / 44100 s. --effects original|enhanced picks how effect samples
// and PC-speaker tones are rendered (default enhanced). --volumes M C E S
// sets the master, CD music, effects and PC-speaker volumes (0..1),
// --speech-volume V the speech volume (Goblins 3's videos), --no-speech turns
// the speech off (the option of the overlay's Audio tab).
// --music-dir DIR plays replacement music files from DIR (named after the
// CD tracks); --music-full-length lets them run past the track's length.
//
// Player scripts: --script FILE plays the commands of FILE one after the
// other (one per line, # starts a comment), each when the one before it is
// done, so a walkthrough doesn't depend on exact times:
//   wait S          wait S simulated seconds
//   idle [S]        wait until the room has given the player control back
//                   (Interpreter::roomIdle; the goblins stand still and,
//                   in Goblins 3, no walk to a hotspot or a trigger cell is
//                   pending; in Gobliiins, no puzzle step runs and the
//                   current goblin neither walks nor acts) for S seconds
//                   (default 0.5)
//   room NAME       wait until room NAME runs (and is idle)
//   tot NAME        wait until the script file NAME runs
//   click X Y       left click at screen position X,Y (rclick: right click)
//   move X Y        move the pointer to screen position X,Y
//   at X Y          left click at room position X,Y (rat: right click),
//                   scrolling the room first with the pointer at an edge
//   zone N          left click on the room's hotspot N (the id's low 15
//                   bits; Goblins 3's zones), at the point nearest to its
//                   middle that no hotspot before it covers, scrolling as at
//   item NAME       Goblins 3: open the inventory and take the item NAME
//                   (NAME#N: the Nth item of that name)
//   drop            Goblins 3: put away the item Blount holds
//   walk X Y        Gobliiins: the current goblin walks to screen position
//                   X,Y (act, pick: acts / picks up there; use: the picker
//                   uses his item there), the cursor set with right clicks
//                   first and X,Y being where its hot spot lands; then idle
//   exit            Gobliiins: once the exit sign shows, click it (unless
//                   the level ends by itself), trace the access code shown,
//                   click it away and wait for the next level (or the next
//                   script, INTER aside; after the last level, the ending)
//   goblin N        Gobliiins: select goblin N (0 picker, 1 fighter, 2
//                   wizard) with the portrait
//   untilzone N     wait until hotspot N is on the screen
//   untilvar SLOT V wait until v[SLOT] == V (SLOT LO HI: until LO <= v[SLOT] <= HI)
//   ifvar SLOT V    run the next step only if v[SLOT] == V (SLOT LO HI: if
//                   LO <= v[SLOT] <= HI); skip it otherwise (a level's
//                   random set-up, Gobliiins' AVT001)
//   act N           zone N, then idle; once more when a trigger cell (Goblins
//                   3's v[1196]) cancelled the walk to it before its action
//                   (v[60]) ran
//   actb N OFF V    act N until the variable byte at OFF is V (at most 6 times)
//   key CODE        press the key with the BIOS-style CODE
//   quicksave, quickload, level ROOM, skip   as the overlay does
//   snap NAME       write the screen to NAME.bmp in the --snap-every DIR
//   print SLOT..    trace the variables v[SLOT]..
//   printb OFFSET.. trace the variable bytes at OFFSET..
//   set SLOT VALUE  set v[SLOT] (debugging)
//   vars FILE       write the variable space to FILE
//   text            trace the text on the screen
//   objects         trace the goblins' positions, cells and states
//   hotspots        trace the hotspots on the screen (id, rectangle)
//   inventory       Goblins 3: trace the items owned
//   items           Gobliiins: trace the items on the map (id, the cells'
//                   bounding box and count, the object it stands for, and
//                   per row y the cells as a bit mask, y:mask)
//   end             end the run (a script whose game ends while only waits,
//                   keys and `end` are left counts as finished)
// A step that waits more than 300 simulated seconds fails the run (exit
// code 1). --log-text traces the text on the screen whenever it changes.
//
// HD packs (docs/hdpacks.md): --hd-pack DIR runs the HD compositor with the
// pack in DIR, as gobnative shows it; --hd-dump-at T PREFIX (repeatable)
// writes PREFIX_native.png and PREFIX_hd.png at time T and prints how far
// the HD frame, reduced to 320x200 by averaging, is from the 320x200 frame
// (0 with a pack made by gobexport --scale-export).
//
// Repainting guide (tools/roomguide.h): --guide-at T DIR waits from time T
// for the room to be idle (the player in control for half a second),
// writes guide-original.png, objects-layer.png and objects.json into DIR
// (guide-pack.png too with --guide-pack PACKDIR) and ends the run;
// --guide-scale N (default: the pack's scale, else 4), --guide-hotspots
// outlines the hotspots as well.

#include "data/datafiles.h"
#include "engine/game.h"
#include "engine/goblins_v1.h"
#include "engine/hdrender.h"
#include "engine/interpreter.h"
#include "gfx/bmp.h"
#include "gfx/hdpack.h"
#include "gfx/png.h"
#include "roomguide.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace gob;

namespace {

class HeadlessHost : public Host {
public:
	bool quiet = false;
	uint32_t now = 0;
	int lines = 0;
	uint32_t pressEveryMs = 0;
	uint32_t lastPress = 0;
	uint32_t pressStopMs = 0xFFFFFFFF;

	std::string saveDir;
	std::string saveDirectory() override { return saveDir; }

	struct Click { uint32_t at; int x, y; bool press = true; int button = 1; };
	std::vector<Click> clicks;
	int mx = 152, my = 92;

	int mouseX() override { updateMouse(); return mx; }
	int mouseY() override { updateMouse(); return my; }
	uint32_t lastClickPoll = 0;
	int takeMouseClicks() override {
		int b = 0;
		for (const Click &c : clicks)
			if (c.press && c.at > lastClickPoll && c.at <= now)
				b |= c.button;
		lastClickPoll = now;
		return b;
	}
	int mouseButtons() override {
		for (const Click &c : clicks)
			if (c.press && now >= c.at && now < c.at + 150)
				return c.button;
		return 0;
	}
	int forceX = -1, forceY = -1; // the guide's hover probes
	void updateMouse() {
		for (const Click &c : clicks)
			if (now >= c.at) {
				mx = c.x;
				my = c.y;
			}
		if (forceX >= 0) {
			mx = forceX;
			my = forceY;
		}
	}

	uint32_t ticks() override { return now; }
	struct Typed { uint32_t at; std::string text; size_t pos = 0; };
	std::vector<Typed> typed;

	struct Key { uint32_t at; int code; bool sent = false; };
	std::vector<Key> keys;

	int pollKey() override {
		for (Key &k : keys)
			if (!k.sent && now >= k.at) {
				k.sent = true;
				trace("(simulated key " + std::to_string(k.code) + ")");
				return k.code;
			}
		for (Typed &t : typed)
			if (now >= t.at && t.pos < t.text.size())
				return static_cast<unsigned char>(t.text[t.pos++]);
		if (pressEveryMs && now < pressStopMs && now - lastPress >= pressEveryMs) {
			lastPress = now;
			trace("(simulated Enter key)");
			return 0x1C0D;
		}
		return 0;
	}
	std::function<void(const std::string &)> onNotify;
	void notify(const std::string &message) override {
		std::printf("%8.3fs  (notify) %s\n", now / 1000.0, message.c_str());
		if (onNotify)
			onNotify(message);
	}
	std::function<void(const std::string &)> onTrace;
	void trace(const std::string &line) override {
		if (onTrace)
			onTrace(line);
		lines++;
		if (!quiet || line.find("loaded") != std::string::npos || line.find("hotspot") != std::string::npos ||
		    line.find("level jump") != std::string::npos || line.find("quick load") != std::string::npos ||
		    line.find("saved slot") != std::string::npos)
			std::printf("%8.3fs  %s\n", now / 1000.0, line.c_str());
	}
};

// Streams 44.1 kHz stereo 16-bit PCM to a WAV file; the header's sizes
// are filled in when it is closed.
class WavWriter {
public:
	bool open(const std::string &path) {
		_f.open(path, std::ios::binary | std::ios::trunc);
		header(0);
		return bool(_f);
	}
	void write(const int16_t *frames, size_t n) {
		for (size_t i = 0; i < n * 2; i++) {
			char b[2] = {char(frames[i] & 0xFF), char((frames[i] >> 8) & 0xFF)};
			_f.write(b, 2);
		}
		_frames += n;
	}
	~WavWriter() { close(); }
	void close() {
		if (!_f.is_open())
			return;
		_f.seekp(0);
		header(uint32_t(_frames * 4));
		_f.close();
	}
	uint64_t frames() const { return _frames; }

private:
	void put(uint32_t v, int bytes) {
		for (int i = 0; i < bytes; i++)
			_f.put(char((v >> (8 * i)) & 0xFF));
	}
	void header(uint32_t dataBytes) {
		_f.write("RIFF", 4);
		put(36 + dataBytes, 4);
		_f.write("WAVEfmt ", 8);
		put(16, 4);
		put(1, 2); // PCM
		put(2, 2);
		put(Mixer::kRate, 4);
		put(Mixer::kRate * 4, 4);
		put(4, 2);
		put(16, 2);
		_f.write("data", 4);
		put(dataBytes, 4);
	}
	std::ofstream _f;
	uint64_t _frames = 0;
};

// All the text the screen shows, line by line (glyph ink back to
// characters, as the room guide reads the hover text).
std::string screenText(Interpreter &interp) {
	std::vector<Draw::TextCell> cells;
	interp.draw().visibleText(cells);
	std::sort(cells.begin(), cells.end(),
	          [](const auto &a, const auto &b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
	std::map<Font::Rows, char> byInk;
	for (int i = Draw::kFontCount - 1; i >= 0; i--)
		if (const Font *f = interp.draw().font(i))
			for (int c = 255; c >= 33; c--)
				if (const Font::Rows ink = f->glyphRows(uint8_t(c)); ink != Font::Rows{} && (c < 0x7F || !byInk.count(ink)))
					byInk[ink] = char(c);
	std::string s;
	int nextX = -1, row = -1000;
	for (const Draw::TextCell &c : cells) {
		if (std::abs(c.y - row) > 2) {
			if (!s.empty())
				s += " / ";
			nextX = -1;
		} else if (nextX >= 0 && c.x > nextX && c.w)
			s.append(size_t((c.x - nextX) / c.w), ' ');
		auto it = byInk.find(c.ink);
		s += it != byInk.end() ? it->second : '?';
		nextX = c.x + c.w;
		row = c.y;
	}
	return s;
}

// --script FILE: the player's steps, each started when the one before it
// is done.
class PlayerScript {
public:
	bool load(const std::string &path) {
		std::ifstream in(path);
		if (!in)
			return false;
		std::string line;
		int n = 0;
		while (std::getline(in, line)) {
			n++;
			if (size_t hash = line.find('#'); hash != std::string::npos)
				line.resize(hash);
			std::vector<std::string> words;
			size_t p = 0;
			while (true) {
				p = line.find_first_not_of(" \t\r", p);
				if (p == std::string::npos)
					break;
				size_t e = line.find_first_of(" \t\r", p);
				words.push_back(line.substr(p, e == std::string::npos ? std::string::npos : e - p));
				p = e;
			}
			if (!words.empty())
				_steps.push_back({words[0], {words.begin() + 1, words.end()}, n});
		}
		return true;
	}
	bool done() const { return _pos >= _steps.size(); }
	// Only waits, key presses and `end` are left: what is left of a script
	// that has watched the game to its end.
	bool onlyWaitsLeft() const {
		for (size_t i = _pos; i < _steps.size(); i++)
			if (_steps[i].cmd != "wait" && _steps[i].cmd != "key" && _steps[i].cmd != "end")
				return false;
		return true;
	}
	bool failed() const { return _failed; }
	bool ended() const { return _ended; }
	std::string snapDir;

	// Runs the steps that can run now (before the engine frame at host.now).
	void step(Interpreter &interp, HeadlessHost &host) {
		while (!done() && !_failed && !_ended) {
			const Step &st = _steps[_pos];
			if (!_started) {
				_started = true;
				_startedAt = host.now;
				_idleSince = UINT32_MAX;
				_clicked = false;
				_aimedAt = UINT32_MAX;
				_phase = 0;
				_slot = 0;
				_retries = 0;
				_cycles = 0;
				_matches = 0;
			}
			if (host.now - _startedAt > 300000) {
				fail(host, "timed out" + (st.cmd == "idle" || st.cmd == "room"
				                             ? std::string(interp.roomIdle() ? " (room idle, " : " (room busy, ") +
				                                   "goblins " + _still + ")"
				                             : std::string()));
				return;
			}
			if (!run(st, interp, host))
				return;
			_pos++;
			_started = false;
			if (_pos == _steps.size())
				std::printf("%8.3fs  (script done)\n", host.now / 1000.0);
		}
	}

private:
	struct Step { std::string cmd; std::vector<std::string> args; int line; };
	std::vector<Step> _steps;
	size_t _pos = 0;
	bool _started = false, _failed = false, _ended = false;
	uint32_t _startedAt = 0, _idleSince = UINT32_MAX, _until = 0;

	void fail(HeadlessHost &host, const std::string &why) {
		const Step &st = _steps[_pos];
		std::string cmd = st.cmd;
		for (const std::string &a : st.args)
			cmd += " " + a;
		std::printf("%8.3fs  -- script FAILED at line %d (%s): %s\n", host.now / 1000.0, st.line, cmd.c_str(),
		            why.c_str());
		_failed = true;
	}
	int arg(const Step &st, size_t i, int def = 0) const {
		return i < st.args.size() ? int(std::strtol(st.args[i].c_str(), nullptr, 0)) : def;
	}
	// True when the step is done.
	bool pause(HeadlessHost &host, uint32_t ms) {
		if (host.now == _startedAt)
			_until = host.now + ms;
		return host.now >= _until;
	}
	void press(HeadlessHost &host, int x, int y, int button) {
		host.clicks.push_back({host.now, x, y, true, button});
	}
	// A click at room position (x, y): scroll with the pointer at an edge
	// until the point is well inside the window shown, then click.
	bool clickRoom(Interpreter &interp, HeadlessHost &host, int x, int y, int button) {
		const Draw &d = interp.draw();
		const int maxX = std::max(0, d.surfaceWidth() - Draw::kScreenW);
		const int maxY = std::max(0, d.surfaceHeight() - Draw::kScreenH);
		const int sx = x - d.viewX, sy = y - d.viewY;
		const int my = std::clamp(sy, 2, 150);
		if (host.now - _startedAt > 20000) {
			fail(host, "can't scroll to " + std::to_string(x) + "," + std::to_string(y));
			return false;
		}
		const int jiggle = int(host.now / 16) & 1; // the left and top edges scroll on every move
		if (sx < 12 && d.viewX > 0) {
			host.clicks.push_back({host.now, 0, my + jiggle, false});
			return false;
		}
		if (sx > 307 && d.viewX < maxX) {
			host.clicks.push_back({host.now, 319, my, false});
			return false;
		}
		if (sy > 150 && d.viewY < maxY) {
			host.clicks.push_back({host.now, std::clamp(sx, 20, 300), 159, false});
			return false;
		}
		if (sy < 4 && d.viewY > 0) {
			host.clicks.push_back({host.now, std::clamp(sx, 20, 300) + jiggle, 0, false});
			return false;
		}
		// Point first, press a few frames later (as a player does; the
		// engine may test the button before it polls the pointer), once the
		// view has stopped scrolling.
		if (_aimedAt == UINT32_MAX || sx != _aimX || sy != _aimY) {
			host.clicks.push_back({host.now, sx, sy, false});
			_aimedAt = host.now;
			_aimX = sx;
			_aimY = sy;
			return false;
		}
		if (host.now < _aimedAt + 100)
			return false;
		press(host, sx, sy, button);
		if (std::getenv("GOBRUN_DEBUG_CLICK"))
			std::printf("%8.3fs  (click at room %d,%d: screen %d,%d, view %d)\n", host.now / 1000.0, x, y, sx, sy, d.viewX);
		return true;
	}

	bool run(const Step &st, Interpreter &interp, HeadlessHost &host) {
		const std::string &c = st.cmd;
		if (interp.game().id == GameId::Gob1 && (c == "walk" || c == "act" || c == "pick" || c == "use"))
			return gob1Action(st, interp, host);
		if (c == "wait")
			return pause(host, uint32_t(std::atof(st.args.empty() ? "1" : st.args[0].c_str()) * 1000));
		if (c == "idle" || c == "room") {
			const uint32_t need = c == "idle" ? uint32_t(std::atof(st.args.empty() ? "0.5" : st.args[0].c_str()) * 1000) : 500;
			return idleFor(interp, host, need) &&
			       (c == "idle" || (!st.args.empty() && interp.currentLevel() == st.args[0]));
		}
		if (c == "tot")
			return !st.args.empty() && interp.currentTot() == st.args[0];
		if (c == "click" || c == "rclick") {
			if (host.now == _startedAt)
				host.clicks.push_back({host.now, arg(st, 0), arg(st, 1), false});
			else if (host.now >= _startedAt + 50 && !_clicked) {
				press(host, arg(st, 0), arg(st, 1), c == "click" ? 1 : 2);
				_clicked = true;
			}
			return pause(host, 350);
		}
		if (c == "move") {
			if (host.now == _startedAt)
				host.clicks.push_back({host.now, arg(st, 0), arg(st, 1), false});
			return pause(host, 100);
		}
		if (c == "at" || c == "rat" || c == "zone") {
			if (_clicked)
				return host.now >= _until;
			int x = arg(st, 0), y = arg(st, 1);
			if (c == "zone" && !zonePoint(interp, host, arg(st, 0), x, y))
				return false;
			if (!clickRoom(interp, host, x, y, c == "rat" ? 2 : 1))
				return false;
			_clicked = true;
			_until = host.now + 300;
			return false;
		}
		if (c == "actb") { // act N until the variable byte OFFSET is V (a switch that cycles; up to 6 times)
			if (_phase == 0 && interp.variables().read8(uint32_t(arg(st, 1))) == uint8_t(arg(st, 2)))
				return true;
			Step one{"act", {st.args.empty() ? "0" : st.args[0]}, st.line};
			if (!run(one, interp, host))
				return false;
			if (++_cycles > 6) {
				fail(host, "the byte never reached the value");
				return false;
			}
			_phase = 0;
			_retries = 0;
			_aimedAt = UINT32_MAX;
			_startedAt = host.now;
			return false;
		}
		if (c == "act") { // zone N, then idle; again if a trigger cell stopped the walk
			if (_phase == 0) {
				int x = 0, y = 0;
				if (_retries > 0 && !zoneVisible(interp, arg(st, 0)))
					return true; // what the trigger did made the click moot
				if (!zonePoint(interp, host, arg(st, 0), x, y) || !clickRoom(interp, host, x, y, 1))
					return false;
				_phase = 1;
				_until = host.now + 300;
				_triggered = _acted = false;
				_action = 0;
				_actRoom = interp.currentTot();
				return false;
			}
			const int32_t pending = int32_t(interp.variables().slot(1025)), action = int32_t(interp.variables().slot(60));
			if (pending > 0 && pending < 1000)
				_action = pending; // the room's action the click asked for
			if (_action != 0 && action == _action)
				_acted = true;
			if (host.now < _until)
				return false;
			if (interp.variables().slot(1196) != 0)
				_triggered = true;
			if (interp.currentTot() != _actRoom && interp.currentTot() != "MENU")
				return true; // the click left the room
			if (!idleFor(interp, host, 500))
				return false;
			if (_triggered && !_acted && _retries++ < 2) {
				_phase = 0;
				_aimedAt = UINT32_MAX;
				_startedAt = host.now;
				return false;
			}
			return true;
		}
		if (c == "goblin")
			return selectGoblin(st, interp, host);
		if (c == "exit")
			return gob1Exit(interp, host);
		if (c == "item")
			return takeItem(st, interp, host);
		if (c == "drop")
			return dropItem(interp, host);
		if (c == "untilzone")
			return zoneVisible(interp, arg(st, 0));
		if (c == "ifvar") {
			const int32_t v = int32_t(interp.variables().slot(uint32_t(arg(st, 0))));
			const bool ok = st.args.size() > 2 ? v >= arg(st, 1) && v <= arg(st, 2) : v == arg(st, 1);
			if (!ok && _pos + 1 < _steps.size())
				_pos++;
			return true;
		}
		if (c == "untilvar") {
			const int32_t v = int32_t(interp.variables().slot(uint32_t(arg(st, 0))));
			return st.args.size() > 2 ? v >= arg(st, 1) && v <= arg(st, 2) : v == arg(st, 1);
		}
		if (c == "key") {
			host.keys.push_back({host.now, arg(st, 0)});
			return true;
		}
		if (c == "quicksave") {
			interp.requestQuickSave();
			return true;
		}
		if (c == "quickload") {
			interp.requestQuickLoad();
			return true;
		}
		if (c == "level") {
			if (st.args.empty() || !interp.requestLevel(st.args[0]))
				fail(host, "unknown room");
			return true;
		}
		if (c == "skip") {
			interp.skipCutscene();
			return true;
		}
		if (c == "snap") {
			if (!snapDir.empty() && !st.args.empty()) {
				const Surface &scr = interp.screen();
				writeBmp(snapDir + "/" + st.args[0] + ".bmp", scr.data(), scr.width(), scr.height(), interp.screenPalette());
			}
			return true;
		}
		if (c == "print") {
			std::string out;
			for (size_t i = 0; i < st.args.size(); i++)
				out += " v[" + st.args[i] + "]=" + std::to_string(int32_t(interp.variables().slot(uint32_t(arg(st, i)))));
			std::printf("%8.3fs  (script line %d)%s\n", host.now / 1000.0, st.line, out.c_str());
			return true;
		}
		if (c == "printb") {
			std::string out;
			for (size_t i = 0; i < st.args.size(); i++)
				out += " b@" + st.args[i] + "=" + std::to_string(interp.variables().read8(uint32_t(arg(st, i))));
			std::printf("%8.3fs  (script line %d)%s\n", host.now / 1000.0, st.line, out.c_str());
			return true;
		}
		if (c == "set") {
			interp.variables().setSlot(uint32_t(arg(st, 0)), uint32_t(arg(st, 1)));
			return true;
		}
		if (c == "text") {
			std::printf("%8.3fs  (text) %s\n", host.now / 1000.0, screenText(interp).c_str());
			return true;
		}
		if (c == "vars") {
			if (!st.args.empty()) {
				std::ofstream out(st.args[0], std::ios::binary);
				const Variables &v = interp.variables();
				out.write(reinterpret_cast<const char *>(v.data()), long(v.size()));
			}
			return true;
		}
		if (c == "inventory") { // Goblins 3: the items owned (as[7418]: 40-byte entries, byte 39 flags item n in entry n, whose name is entry n - 1)
			std::string out;
			const Variables &v = interp.variables();
			for (uint32_t i = 1; i < 60; i++) { // item i: owned flag in entry i, name in entry i - 1
				const uint32_t off = 7418 * 4 + i * 40, name0 = off - 40;
				if (v.read8(off + 39) != 1)
					continue;
				std::string name;
				for (uint32_t k = 0; k < 30 && v.read8(name0 + k); k++)
					name += char(v.read8(name0 + k));
				out += " " + std::to_string(i) + ":" + name;
			}
			std::printf("%8.3fs  (inventory)%s\n", host.now / 1000.0, out.c_str());
			return true;
		}
		if (c == "hotspots") {
			for (const Interpreter::HotspotInfo &h : interp.hotspots())
				if (h.type() == 1 && !h.disabled() && h.left < 1000 && h.right >= h.left)
					std::printf("%8.3fs  (hotspot %d) %d,%d-%d,%d\n", host.now / 1000.0, h.id & 0x7FFF, h.left, h.top,
					            h.right, h.bottom);
			return true;
		}
		if (c == "objects") {
			for (const Interpreter::ObjectInfo &o : interp.objects())
				if (o.isGoblin)
					std::printf("%8.3fs  (goblin %d) pos %d,%d cell %d,%d dest %d,%d state %d path %d busy %d\n",
					            host.now / 1000.0, o.index, o.x, o.y, o.cellX, o.cellY, o.gobDestX, o.gobDestY, o.state,
					            o.pathExistence, o.isBusy);
			return true;
		}
		if (c == "items") {
			if (const GoblinsV1 *g = interp.goblinsV1())
				for (const GoblinsV1::ItemSpot &s : g->itemSpots()) {
					int cells = 0;
					for (uint32_t r : s.rows)
						cells += std::popcount(r);
					std::string rows;
					for (size_t y = 0; y < s.rows.size(); y++)
						if (s.rows[y])
							rows += " " + std::to_string(y) + ":" + std::to_string(s.rows[y]);
					std::printf("%8.3fs  (item %d) %d,%d-%d,%d cells %d object %d rows%s\n", host.now / 1000.0, s.id,
					            s.left, s.top, s.right, s.bottom, cells, s.object, rows.c_str());
				}
			return true;
		}
		if (c == "end") {
			_ended = true;
			return true;
		}
		fail(host, "unknown command");
		return false;
	}
	bool _clicked = false, _triggered = false, _acted = false;
	int32_t _action = 0;
	std::string _actRoom;
	int _retries = 0;
	int _cycles = 0; // actb's clicks so far
	int _matches = 0; // item: slots with the name seen so far
	uint32_t _aimedAt = UINT32_MAX;
	int _aimX = 0, _aimY = 0;

	// The room has given the player control back for `need` ms: idle in its
	// main loop, the goblins standing still, and in Goblins 3 no walk to a
	// hotspot (v[1025], v[1026]) or to a trigger cell (v[1196], v[1227])
	// pending.
	bool idleFor(Interpreter &interp, HeadlessHost &host, uint32_t need) {
		bool ok = interp.roomIdle();
		std::string still;
		for (const Interpreter::ObjectInfo &o : interp.objects())
			if (o.isGoblin)
				still += std::to_string(o.x) + "," + std::to_string(o.y) + "," + std::to_string(o.cellX) + "," +
				         std::to_string(o.cellY) + ";";
		if (still != _still)
			ok = false;
		if (interp.game().id == GameId::Gob3)
			for (uint32_t slot : {1025u, 1026u, 1196u, 1227u})
				if (interp.variables().slot(slot) != 0)
					ok = false;
		_still = still;
		if (!ok) {
			_idleSince = UINT32_MAX;
			return false;
		}
		if (_idleSince == UINT32_MAX)
			_idleSince = host.now;
		return host.now - _idleSince >= need;
	}

	// Gobliiins: walk/act/pick X Y. Right clicks in the room cycle the
	// cursor (v[111]: 0 walk, 3 act or use the pocket item, 4 pick, for the
	// picker only) until it is the one wanted, then the left click lands so
	// that the cursor's hot spot (act +6,+7, pick +7,+12) is on X,Y; then
	// wait until the level is idle again. `use` is `act`, said of the
	// picker's item.
	bool gob1Action(const Step &st, Interpreter &interp, HeadlessHost &host) {
		const std::string &c = st.cmd;
		const int mode = c == "walk" ? 0 : c == "pick" ? 4 : 3;
		const int ox = mode == 3 ? 6 : mode == 4 ? 7 : 0, oy = mode == 3 ? 7 : mode == 4 ? 12 : 0;
		const int x = std::clamp(arg(st, 0) - ox, 0, 319), y = std::clamp(arg(st, 1) - oy, 0, 166);
		if (_phase == 0) { // wait until the level takes a click
			if (!idleFor(interp, host, 100))
				return false;
			_actRoom = interp.currentLevel();
			_phase = 1;
			_until = host.now;
		}
		if (_phase == 1) { // the cursor
			if (int32_t(interp.variables().slot(111)) == mode) {
				_phase = 2;
				_startedAt = host.now;
				_aimedAt = UINT32_MAX;
				return false;
			}
			if (host.now < _until)
				return false;
			if (_cycles++ > 4) {
				fail(host, "the cursor never became " + c);
				return false;
			}
			host.clicks.push_back({host.now, x, y, false});
			if (std::getenv("GOBRUN_DEBUG_CLICK"))
				std::printf("%8.3fs  (cursor %d, right click at %d,%d)\n", host.now / 1000.0, int32_t(interp.variables().slot(111)), x, y);
			_clicked = false;
			_until = host.now + 250;
			_phase = 10;
			return false;
		}
		if (_phase == 10) { // press the right button a little after pointing
			if (host.now >= _until - 200 && !_clicked) {
				press(host, x, y, 2);
				_clicked = true;
			}
			if (host.now >= _until)
				_phase = 1;
			return false;
		}
		if (_phase == 2) {
			if (!clickRoom(interp, host, x, y, 1))
				return false;
			_phase = 3;
			_until = host.now + 300;
			return false;
		}
		if (host.now < _until)
			return false;
		if (interp.variables().slot(16) != 0 || interp.currentLevel() != _actRoom)
			return true; // the level ends
		return idleFor(interp, host, 500);
	}

	// Gobliiins: exit. Once the exit sign shows (v[136]), click it (the
	// level ends: v[16] = 3), trace the access code the level shows for the
	// next one (s[531]), click it away and wait for the next level.
	bool gob1Exit(Interpreter &interp, HeadlessHost &host) {
		Variables &v = interp.variables();
		if (_phase == 0 && !interp.currentLevel().empty())
			_actRoom = interp.currentLevel();
		if (_phase == 0 && interp.currentTot() == "FINAL" && !_actRoom.empty()) { // the last level: the ending
			std::printf("%8.3fs  (%s solved, ending, energy %d)\n", host.now / 1000.0, _actRoom.c_str(),
			            int32_t(v.slot(296)));
			return true;
		}
		if (_phase == 0) {
			if (v.slot(16) == 3 && !interp.currentLevel().empty()) { // the level ended by itself (AVT007, AVT018)
				_actRoom = interp.currentLevel();
				_phase = 3;
				_until = host.now + 1000;
				return false;
			}
			if (v.slot(136) != 1 || !idleFor(interp, host, 100))
				return false;
			_actRoom = interp.currentLevel();
			_phase = 1;
			_aimedAt = UINT32_MAX;
			_startedAt = host.now;
		}
		const int x = int32_t(v.slot(287)) + int32_t(v.slot(289)) / 2, y = int32_t(v.slot(288)) + int32_t(v.slot(290)) / 2;
		if (_phase == 1) {
			if (!clickRoom(interp, host, std::clamp(x, 0, 319), std::clamp(y, 0, 166), 1))
				return false;
			_phase = 2;
			_until = host.now + 3000;
			return false;
		}
		if (_phase == 2) {
			if (v.slot(16) == 3) {
				_phase = 3;
				_until = host.now + 1000;
			} else if (host.now >= _until) {
				if (_retries++ >= 3) {
					fail(host, "the level didn't end");
					return false;
				}
				_phase = 1;
				_aimedAt = UINT32_MAX;
				_startedAt = host.now;
			}
			return false;
		}
		if (_phase == 3) {
			if (host.now < _until)
				return false;
			std::string code;
			for (uint32_t k = 0; k < 7; k++)
				if (char ch = char(v.read8(531 * 4 + k)); ch)
					code += ch;
			std::printf("%8.3fs  (%s solved, code %s, energy %d)\n", host.now / 1000.0, _actRoom.c_str(), code.c_str(),
			            int32_t(v.slot(296)));
			host.clicks.push_back({host.now, 160, 100, false});
			_phase = 4;
			_until = host.now + 50;
			return false;
		}
		if (_phase == 4) {
			if (host.now < _until)
				return false;
			press(host, 160, 100, 1);
			_phase = 5;
		}
		return interp.currentLevel() != _actRoom && interp.currentTot() != "INTER";
	}

	// Gobliiins: goblin N selects goblin N (0 the picker, 1 the fighter, 2
	// the wizard) with clicks on the portrait, which selects the next one.
	bool selectGoblin(const Step &st, Interpreter &interp, HeadlessHost &host) {
		// The engine's current goblin: v[106] follows it, but a level may
		// write v[106] itself (AVT022).
		const GoblinsV1 *g = interp.goblinsV1();
		const int current = g ? g->currentGoblin() : int32_t(interp.variables().slot(106));
		if (current == arg(st, 0) && idleFor(interp, host, 100))
			return true;
		if (_phase == 0) {
			if (!idleFor(interp, host, 100))
				return false;
			if (_cycles++ > 6) {
				fail(host, "the goblin can't be selected");
				return false;
			}
			host.clicks.push_back({host.now, 165, 182, false});
			_until = host.now + 50;
			_phase = 1;
			return false;
		}
		if (host.now < _until)
			return false;
		if (_phase == 1) {
			press(host, 165, 182, 1);
			_until = host.now + 300;
			_phase = 2;
			return false;
		}
		_phase = 0;
		_idleSince = UINT32_MAX;
		return false;
	}

	static bool zoneVisible(Interpreter &interp, int id) {
		for (const Interpreter::HotspotInfo &h : interp.hotspots())
			if ((h.id & 0x7FFF) == id && h.type() == 1 && !h.disabled() && h.left < 1000 && h.right >= h.left)
				return true;
		return false;
	}

	// Where to click hotspot `id`: the point nearest to its middle that no
	// hotspot before it in the table covers (the first one under the
	// pointer takes the click). False while it isn't there (the room may be
	// calling into MENU just now); fails the run after 10 seconds.
	bool zonePoint(Interpreter &interp, HeadlessHost &host, int id, int &x, int &y) {
		const auto spots = interp.hotspots();
		for (size_t i = 0; i < spots.size(); i++) {
			const Interpreter::HotspotInfo &h = spots[i];
			if ((h.id & 0x7FFF) != id || h.type() != 1 || h.disabled() || h.left >= 1000 || h.right < h.left)
				continue;
			const int cx = (h.left + h.right) / 2, cy = (h.top + h.bottom) / 2;
			auto uncovered = [&](int px, int py) {
				for (size_t j = 0; j < i; j++) {
					const Interpreter::HotspotInfo &o = spots[j];
					if (o.type() == 1 && !o.disabled() && px >= o.left && px <= o.right && py >= o.top && py <= o.bottom)
						return false;
				}
				return true;
			};
			// Keep the point aimed at while it is still on the zone (a
			// goblin's zone follows its animation frame by frame).
			const Draw &d = interp.draw();
			const int ax = _aimX + d.viewX, ay = _aimY + d.viewY;
			if (_aimedAt != UINT32_MAX && ax >= h.left && ax <= h.right && ay >= h.top && ay <= h.bottom &&
			    uncovered(ax, ay)) {
				x = ax;
				y = ay;
				return true;
			}
			long best = -1;
			for (int py = h.top; py <= h.bottom; py++)
				for (int px = h.left; px <= h.right; px++) {
					const long d = long(px - cx) * (px - cx) + long(py - cy) * (py - cy);
					if ((best < 0 || d < best) && uncovered(px, py)) {
						best = d;
						x = px;
						y = py;
					}
				}
			if (best >= 0)
				return true;
		}
		if (host.now - _startedAt > 10000)
			fail(host, "no hotspot for zone " + std::to_string(id));
		return false;
	}
	int _phase = 0;
	size_t _slot = 0;
	uint32_t _phaseAt = 0;

	// Goblins 3's inventory (MENU, opened with a right click): the slots
	// are hotspots 7-26; the bar names the item under the pointer.
	static std::vector<Interpreter::HotspotInfo> inventorySlots(Interpreter &interp) {
		std::vector<Interpreter::HotspotInfo> out;
		if (interp.currentTot() != "MENU")
			return out;
		for (const Interpreter::HotspotInfo &h : interp.hotspots()) {
			const int id = h.id & 0x7FFF;
			if (id >= 7 && id <= 26 && h.type() == 1 && !h.disabled() && h.left < 1000 && h.right >= h.left)
				out.push_back(h);
		}
		return out;
	}
	static std::string normalized(std::string t) {
		for (char &ch : t)
			ch = ch == '0' ? 'O' : char(std::toupper(static_cast<unsigned char>(ch)));
		return t;
	}
	// drop: Goblins 3, put away what Blount holds (the inventory opens with
	// a right click; another one closes it with empty hands).
	bool dropItem(Interpreter &interp, HeadlessHost &host) {
		const bool holding = interp.variables().slot(61) != uint32_t(-1);
		switch (_phase) {
		case 0:
			if (!holding && interp.currentTot() != "MENU")
				return true;
			if (!inventorySlots(interp).empty()) {
				_phase = 2;
				return false;
			}
			press(host, 160, 80, 2);
			_phase = 1;
			_phaseAt = host.now;
			return false;
		case 1: // put away at once the first time, else the inventory opens
			if (host.now < _phaseAt + 700)
				return false;
			if (inventorySlots(interp).empty()) {
				if (!holding && interp.currentTot() != "MENU")
					return true;
				if (host.now > _phaseAt + 3000)
					fail(host, "the inventory doesn't open");
				return false;
			}
			_phase = 2;
			_phaseAt = host.now;
			return false;
		case 2:
			if (host.now < _phaseAt + 200)
				return false;
			press(host, 160, 80, 2);
			_phase = 3;
			_phaseAt = host.now;
			return false;
		default:
			if (interp.currentTot() == "MENU" || holding) {
				if (host.now > _phaseAt + 3000)
					fail(host, "still holding an item");
				return false;
			}
			return host.now >= _phaseAt + 300;
		}
	}

	// item NAME: open the inventory and take the item the bar names NAME.
	bool takeItem(const Step &st, Interpreter &interp, HeadlessHost &host) {
		std::string name;
		for (const std::string &a : st.args)
			name += (name.empty() ? "" : " ") + a;
		int nth = 1; // NAME#N: the Nth item of that name
		if (const size_t hash = name.rfind('#'); hash != std::string::npos && hash + 1 < name.size()) {
			nth = std::max(1, std::atoi(name.c_str() + hash + 1));
			name.resize(hash);
		}
		name = normalized(name);
		const auto slots = inventorySlots(interp);
		switch (_phase) {
		case 0: // open it (a right click first puts away what Blount holds)
			if (!slots.empty()) {
				_phase = 2;
				return false;
			}
			press(host, 160, 80, 2);
			_phase = 1;
			_phaseAt = host.now;
			return false;
		case 1:
			if (host.now < _phaseAt + 600)
				return false;
			if (slots.empty()) {
				if (host.now > _phaseAt + 3000) {
					fail(host, "the inventory doesn't open");
					return false;
				}
				if (host.now >= _phaseAt + 600 && host.now < _phaseAt + 617)
					press(host, 160, 80, 2);
				return false;
			}
			_phase = 2;
			return false;
		case 2: // point at the next slot
			if (_slot >= slots.size()) {
				fail(host, "no " + name + " in the inventory");
				return false;
			}
			host.clicks.push_back({host.now, (slots[_slot].left + slots[_slot].right) / 2 - interp.draw().viewX,
			                       (slots[_slot].top + slots[_slot].bottom) / 2 - interp.draw().viewY, false});
			_phase = 3;
			_phaseAt = host.now;
			return false;
		case 3: { // read the name
			if (host.now < _phaseAt + 200)
				return false;
			if (std::getenv("GOBRUN_DEBUG_ITEM"))
				std::printf("%8.3fs  (item slot %zu: %s)\n", host.now / 1000.0, _slot, screenText(interp).c_str());
			// The name shows in the bottom bar, the last line (a subtitle above
			// may name the item too).
			std::string bar = screenText(interp);
			if (const size_t sep = bar.rfind(" / "); sep != std::string::npos)
				bar = bar.substr(sep + 3);
			if (normalized(bar).find(name) == std::string::npos || ++_matches < nth) {
				_slot++;
				_phase = 2;
				return false;
			}
			press(host, (slots[_slot].left + slots[_slot].right) / 2 - interp.draw().viewX,
			      (slots[_slot].top + slots[_slot].bottom) / 2 - interp.draw().viewY, 1);
			_phase = 4;
			_phaseAt = host.now;
			return false;
		}
		default:
			return host.now >= _phaseAt + 600;
		}
	}
	std::string _still;
};

// The mult objects, and Gobliiins' goblins, objects and pocket (--print-objects).
std::string printObjectState(Interpreter &interp) {
	std::string out;
	char line[512];
	for (const Interpreter::ObjectInfo &o : interp.objects()) {
		std::snprintf(line, sizeof(line), "obj %2d pos %4d,%4d anim %3d layer %3d frame %3d type %d order %d paused %d static %d%s\n",
		              o.index, o.x, o.y, o.animation, o.layer, o.frame, o.animType, o.order, o.isPaused, o.isStatic,
		              o.isGoblin ? (" cell " + std::to_string(o.cellX) + "," + std::to_string(o.cellY) + " state " +
		                            std::to_string(o.state) + " path " + std::to_string(o.pathExistence))
		                               .c_str()
		                         : "");
		out += line;
	}
	const GoblinsV1 *g1 = interp.goblinsV1();
	if (!g1 || !g1->loaded())
		return out;
	auto print = [&](const char *kind, int i, const GoblinsV1::Object &o) {
		std::snprintf(line, sizeof(line),
		              "%s %2d pos %4d,%4d anim %d state %2d frame %2d next %3d mult %3d look %d type %d order %d "
		              "doAnim %d visible %d pickable %d rect %d,%d-%d,%d\n",
		              kind, i, o.xPos, o.yPos, o.animation, o.state, o.curFrame, o.nextState, o.multState,
		              o.curLookDir, o.type, o.order, o.doAnim, o.visible, o.pickable, o.left, o.top, o.right, o.bottom);
		out += line;
	};
	for (int i = 0; i < GoblinsV1::kGoblins; i++)
		if (const GoblinsV1::Object *o = g1->goblin(i)) {
			print("gob", i, *o);
			if (i < 3) {
				std::snprintf(line, sizeof(line), "       cell %d,%d%s\n", g1->gobPosition(i).x, g1->gobPosition(i).y,
				              i == g1->currentGoblin() ? " (current)" : "");
				out += line;
			}
		}
	for (int i = 0; i < GoblinsV1::kObjects; i++)
		if (const GoblinsV1::Object *o = g1->object(i))
			print("item", i, *o);
	std::snprintf(line, sizeof(line), "pocket: object %d, item %d\n", g1->itemIndInPocket(), g1->itemIdInPocket());
	out += line;
	return out;
}

} // namespace

int main(int argc, char **argv) {
	const std::string gameKey = takeGameOption(argc, argv);
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <gamedir> [--game gob1|gob2|gob3] [--start TOT] [--frames N] [--quiet]\n",
		             argv[0]);
		return 2;
	}
	std::string gameMessage;
	const GameInfo *selected = selectGame(argv[1], gameKey, gameMessage);
	if (!selected) {
		std::fprintf(stderr, "%s\n", gameMessage.c_str());
		return 1;
	}
	std::fprintf(stderr, "game: %s\n", gameMessage.c_str());

	std::string start = selected->startTot;
	std::string snapDir;
	uint32_t snapEveryMs = 0;
	long maxFrames = 60 * 60; // one simulated minute
	HeadlessHost host;
	for (int i = 2; i < argc; i++) {
		if (!std::strcmp(argv[i], "--start") && i + 1 < argc)
			start = argv[++i];
		else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
			maxFrames = std::atol(argv[++i]);
		else if (!std::strcmp(argv[i], "--quiet"))
			host.quiet = true;
		else if (!std::strcmp(argv[i], "--snap-every") && i + 2 < argc) {
			snapEveryMs = uint32_t(std::atof(argv[++i]) * 1000);
			snapDir = argv[++i];
		} else if (!std::strcmp(argv[i], "--click") && i + 3 < argc) {
			uint32_t t = uint32_t(std::atof(argv[i + 1]) * 1000);
			host.clicks.push_back({t, std::atoi(argv[i + 2]), std::atoi(argv[i + 3])});
			i += 3;
		} else if (!std::strcmp(argv[i], "--rclick") && i + 3 < argc) {
			uint32_t t = uint32_t(std::atof(argv[i + 1]) * 1000);
			host.clicks.push_back({t, std::atoi(argv[i + 2]), std::atoi(argv[i + 3]), true, 2});
			i += 3;
		} else if (!std::strcmp(argv[i], "--type") && i + 2 < argc) {
			host.typed.push_back({uint32_t(std::atof(argv[i + 1]) * 1000), argv[i + 2]});
			i += 2;
		} else if (!std::strcmp(argv[i], "--press-stop") && i + 1 < argc) {
			host.pressStopMs = uint32_t(std::atof(argv[++i]) * 1000);
		} else if (!std::strcmp(argv[i], "--save-dir") && i + 1 < argc) {
			host.saveDir = argv[++i];
		} else if (!std::strcmp(argv[i], "--move") && i + 3 < argc) {
			uint32_t t = uint32_t(std::atof(argv[i + 1]) * 1000);
			host.clicks.push_back({t, std::atoi(argv[i + 2]), std::atoi(argv[i + 3]), false});
			i += 3;
		} else if (!std::strcmp(argv[i], "--press-every") && i + 1 < argc)
			host.pressEveryMs = uint32_t(std::atof(argv[++i]) * 1000);
	}

	DataFiles files(argv[1]);
	if (!files.openArchive(selected->startArchive)) {
		std::fprintf(stderr, "%s not found in %s\n", selected->startArchive, argv[1]);
		return 1;
	}

	Interpreter interp(files, host, *selected);
	interp.skipFrameBudgetMs = 0; // deterministic fast-forwards
	std::vector<uint32_t> skips;
	bool watchSkip = false, printObjects = false, printHotspots = false;
	enum class Action { Level, QuickSave, QuickLoad, SetVar, SetStr };
	struct Timed { uint32_t at; Action action; std::string arg; bool done = false; };
	std::vector<Timed> actions;
	struct Watch { uint32_t offset; int value; };
	std::vector<Watch> watches;
	std::unique_ptr<WavWriter> audioDump;
	MusicPack musicPack;
	PlayerScript script;
	bool haveScript = false, logText = false;
	uint32_t guideAt = UINT32_MAX;
	guide::Options guideOpt;
	guideOpt.gameDir = argv[1];
	Mixer::Volumes volumes;
	for (int i = 2; i < argc; i++) {
		if (!std::strcmp(argv[i], "--trace-ops"))
			interp.traceOps = true;
		else if (!std::strcmp(argv[i], "--speed") && i + 1 < argc)
			interp.speed = std::atof(argv[++i]);
		else if (!std::strcmp(argv[i], "--fast-cutscenes"))
			interp.cutscenesAtNormalSpeed = false;
		else if (!std::strcmp(argv[i], "--seed") && i + 1 < argc)
			interp.setRandomSeed(uint32_t(std::strtoul(argv[++i], nullptr, 0)));
		else if (!std::strcmp(argv[i], "--key") && i + 2 < argc) {
			host.keys.push_back({uint32_t(std::atof(argv[i + 1]) * 1000), int(std::strtol(argv[i + 2], nullptr, 0))});
			i += 2;
		} else if (!std::strcmp(argv[i], "--level-at") && i + 2 < argc) {
			std::string room = argv[i + 2];
			if (!room.empty() && std::isdigit(static_cast<unsigned char>(room[0]))) {
				const int n = std::atoi(room.c_str());
				char buf[16];
				std::snprintf(buf, sizeof(buf), "GOB%02d", n);
				room = buf;
				if (selected->id == GameId::Gob1) { // the n-th level in play order
					const auto levels = interp.levels();
					room = n >= 1 && n <= int(levels.size()) ? levels[size_t(n - 1)].tot : "?";
				}
			}
			actions.push_back({uint32_t(std::atof(argv[i + 1]) * 1000), Action::Level, room});
			i += 2;
		} else if (!std::strcmp(argv[i], "--set-var") && i + 3 < argc) {
			actions.push_back({uint32_t(std::atof(argv[i + 1]) * 1000), Action::SetVar,
			                   std::string(argv[i + 2]) + " " + argv[i + 3]});
			i += 3;
		} else if (!std::strcmp(argv[i], "--set-str") && i + 3 < argc) {
			actions.push_back({uint32_t(std::atof(argv[i + 1]) * 1000), Action::SetStr,
			                   std::string(argv[i + 2]) + " " + argv[i + 3]});
			i += 3;
		} else if (!std::strcmp(argv[i], "--quicksave-at") && i + 1 < argc)
			actions.push_back({uint32_t(std::atof(argv[++i]) * 1000), Action::QuickSave, {}});
		else if (!std::strcmp(argv[i], "--quickload-at") && i + 1 < argc)
			actions.push_back({uint32_t(std::atof(argv[++i]) * 1000), Action::QuickLoad, {}});
		else if (!std::strcmp(argv[i], "--skip-at") && i + 1 < argc)
			skips.push_back(uint32_t(std::atof(argv[++i]) * 1000));
		else if (!std::strcmp(argv[i], "--watch-skip"))
			watchSkip = true;
		else if (!std::strcmp(argv[i], "--script") && i + 1 < argc) {
			if (!script.load(argv[++i])) {
				std::fprintf(stderr, "cannot read %s\n", argv[i]);
				return 2;
			}
			haveScript = true;
		} else if (!std::strcmp(argv[i], "--log-text"))
			logText = true;
		else if (!std::strcmp(argv[i], "--guide-at") && i + 2 < argc) {
			guideAt = uint32_t(std::atof(argv[i + 1]) * 1000);
			guideOpt.dir = argv[i + 2];
			i += 2;
		} else if (!std::strcmp(argv[i], "--guide-scale") && i + 1 < argc)
			guideOpt.scale = std::atoi(argv[++i]);
		else if (!std::strcmp(argv[i], "--guide-pack") && i + 1 < argc)
			guideOpt.packDir = argv[++i];
		else if (!std::strcmp(argv[i], "--guide-hotspots"))
			guideOpt.hotspots = true;
		else if (!std::strcmp(argv[i], "--print-objects"))
			printObjects = true;
		else if (!std::strcmp(argv[i], "--print-hotspots"))
			printHotspots = true;
		else if (!std::strcmp(argv[i], "--watch") && i + 1 < argc)
			watches.push_back({uint32_t(std::strtoul(argv[++i], nullptr, 0)), -1});
		else if (!std::strcmp(argv[i], "--audio-dump") && i + 1 < argc) {
			audioDump = std::make_unique<WavWriter>();
			if (!audioDump->open(argv[++i])) {
				std::fprintf(stderr, "cannot write %s\n", argv[i]);
				return 2;
			}
		} else if (!std::strcmp(argv[i], "--effects") && i + 1 < argc) {
			std::string q = argv[++i];
			if (q != "original" && q != "enhanced") {
				std::fprintf(stderr, "--effects: original or enhanced\n");
				return 2;
			}
			interp.mixer().setEffectsQuality(q == "original" ? Mixer::EffectsQuality::Original
			                                                 : Mixer::EffectsQuality::Enhanced);
		} else if (!std::strcmp(argv[i], "--volumes") && i + 4 < argc) {
			volumes.master = float(std::atof(argv[i + 1]));
			volumes.music = float(std::atof(argv[i + 2]));
			volumes.effects = float(std::atof(argv[i + 3]));
			volumes.speaker = float(std::atof(argv[i + 4]));
			i += 4;
		} else if (!std::strcmp(argv[i], "--speech-volume") && i + 1 < argc) {
			volumes.speech = float(std::atof(argv[++i]));
		} else if (!std::strcmp(argv[i], "--no-speech")) {
			interp.speech = false;
		} else if (!std::strcmp(argv[i], "--music-dir") && i + 1 < argc) {
			musicPack.scan(argv[++i]);
			interp.musicPack = &musicPack;
			for (const MusicPack::Entry &e : musicPack.entries())
				std::printf("music: %s -> %s (%s)\n", e.track.c_str(), e.file.c_str(),
				            e.info.error.empty() ? e.info.description.c_str() : e.info.error.c_str());
		} else if (!std::strcmp(argv[i], "--music-full-length"))
			musicPack.fullLength = true;
		else if (!std::strcmp(argv[i], "--language") && i + 1 < argc) {
			auto lang = parseLanguage(argv[++i]);
			if (!lang) {
				std::fprintf(stderr, "unknown language %s\n", argv[i]);
				return 2;
			}
			interp.language = *lang;
		}
	}
	interp.mixer().setVolumes(volumes);
	for (int i = 2; i < argc; i++)
		if (!std::strcmp(argv[i], "--list-levels")) { // what the Levels tab lists
			files.openAllArchives(); // the names may be in archives the scripts open later (Goblins 3)
			for (const LevelInfo &l : interp.levels())
				if (l.number && l.world)
					std::printf("level %d %s world %d %s%s\n", l.number, l.tot.c_str(), l.world, l.name.c_str(),
					            l.reached ? " (reached)" : "");
				else if (l.number)
					std::printf("level %d %s %s\n", l.number, l.tot.c_str(), l.code.empty() ? "-" : l.code.c_str());
				else
					std::printf("level %s world %d %s\n", l.tot.c_str(), l.world, l.name.c_str());
			return 0;
		}
	// --state-on-notify PREFIX: on "Saved" and "Loaded", the variables and the
	// objects as they are at that moment (PREFIX-N.vars, PREFIX-N.objects).
	for (int i = 2; i + 1 < argc; i++)
		if (!std::strcmp(argv[i], "--state-on-notify")) {
			const std::string prefix = argv[i + 1];
			host.onNotify = [&interp, prefix, n = 0](const std::string &message) mutable {
				if (message != "Saved" && message != "Loaded")
					return;
				const std::string base = prefix + "-" + std::to_string(++n);
				std::ofstream vars(base + ".vars", std::ios::binary);
				const Variables &v = interp.variables();
				vars.write(reinterpret_cast<const char *>(v.data()), long(v.size()));
				std::ofstream objects(base + ".objects");
				objects << printObjectState(interp);
			};
		}
	// --state-on-trace TEXT PREFIX: the same, whenever a trace line contains
	// TEXT ("saved slot", "loaded save slot": saves from the game's menu).
	for (int i = 2; i + 2 < argc; i++)
		if (!std::strcmp(argv[i], "--state-on-trace")) {
			const std::string text = argv[i + 1], prefix = argv[i + 2];
			host.onTrace = [&interp, text, prefix, n = 0](const std::string &line) mutable {
				if (line.find(text) == std::string::npos)
					return;
				const std::string base = prefix + "-" + std::to_string(++n);
				std::ofstream vars(base + ".vars", std::ios::binary);
				const Variables &v = interp.variables();
				vars.write(reinterpret_cast<const char *>(v.data()), long(v.size()));
				std::ofstream objects(base + ".objects");
				objects << printObjectState(interp);
			};
		}
	script.snapDir = snapDir;
	if (haveScript || logText)
		interp.draw().recordText = true; // the text command and --log-text read the screen's text
	std::string lastText;
	if (guideAt != UINT32_MAX) {
		interp.draw().setTrackSources(true); // which picture pixel every screen pixel shows
		interp.draw().recordText = true;     // to read the names shown on hover
	}
	int guideIdle = 0;

	// HD compositor (--hd-pack, --hd-dump-at).
	std::unique_ptr<GameImages> originals;
	HdPack hdPack;
	HdRenderer hdr;
	struct HdDump { uint32_t at; std::string prefix; };
	std::vector<HdDump> hdDumps;
	for (int i = 2; i < argc; i++) {
		if (!std::strcmp(argv[i], "--hd-pack") && i + 1 < argc) {
			originals = std::make_unique<GameImages>(argv[1]);
			if (!hdPack.open(argv[++i], [&](const std::string &tot, int id) { return (*originals)(tot, id); }) ||
			    !hdPack.size() || hdPack.maxScale() <= 0) {
				std::fprintf(stderr, "cannot use HD pack %s\n", argv[i]);
				return 2;
			}
			std::printf("hd pack: %zu pictures at %dx, %d errors\n", hdPack.size(), hdPack.scale(), hdPack.errorCount());
			interp.draw().setTrackSources(true);
			hdr.attach(interp.draw(), &hdPack, hdPack.maxScale(), [&interp] { return interp.currentTot(); });
		} else if (!std::strcmp(argv[i], "--hd-dump-at") && i + 2 < argc) {
			hdDumps.push_back({uint32_t(std::atof(argv[i + 1]) * 1000), argv[i + 2]});
			i += 2;
		}
	}
	auto hdDump = [&](const std::string &prefix) {
		const Surface &scr = interp.screen();
		const Palette &pal = interp.screenPalette();
		const int w = scr.width(), h = scr.height();
		std::vector<uint8_t> nat(size_t(w) * h * 3);
		for (int i = 0; i < w * h; i++) {
			const auto &c = pal[scr.data()[i]];
			nat[size_t(i) * 3] = c.r;
			nat[size_t(i) * 3 + 1] = c.g;
			nat[size_t(i) * 3 + 2] = c.b;
		}
		writePng(prefix + "_native.png", nat.data(), w, h, 3);
		if (!hdr.attached()) {
			std::printf("%8.3fs  hd dump %s: no HD pack\n", host.now / 1000.0, prefix.c_str());
			return;
		}
		hdr.invalidateAll();
		hdr.update();
		const int sc = hdr.scale(), fw = hdr.frameWidth(), fh = hdr.frameHeight();
		const uint8_t *hd = reinterpret_cast<const uint8_t *>(hdr.frame());
		std::vector<uint8_t> rgb(size_t(fw) * fh * 3);
		for (size_t i = 0; i < size_t(fw) * fh; i++)
			std::copy_n(hd + i * 4, 3, &rgb[i * 3]);
		writePng(prefix + "_hd.png", rgb.data(), fw, fh, 3);
		double sum = 0;
		size_t off = 0;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				for (int ch = 0; ch < 3; ch++) {
					int acc = 0;
					for (int dy = 0; dy < sc; dy++)
						for (int dx = 0; dx < sc; dx++)
							acc += rgb[(size_t(y * sc + dy) * fw + size_t(x * sc + dx)) * 3 + size_t(ch)];
					const double d = std::abs(double(acc) / (sc * sc) - nat[(size_t(y) * w + size_t(x)) * 3 + size_t(ch)]);
					sum += d;
					off += d > 48;
				}
		const HdRenderer::Coverage c = hdr.coverage();
		std::printf("%8.3fs  hd dump %s (%s): %dx, mean difference %.3f/255, %zu values off by more than 48; "
		            "%zu HD, %zu live, %zu repaired game pixels\n",
		            host.now / 1000.0, prefix.c_str(), interp.currentTot().c_str(), sc, sum / (w * h * 3.0), off, c.hd,
		            c.live, c.repaired);
	};

	Task<void> game = interp.run(start);

	// Mixes audio up to simulated time `ms` into the dump.
	std::vector<int16_t> mixBuf;
	// Goblins 3: without a dump the mixer is still played through in step
	// with the clock, as a sound device would, so samples end (playMult and
	// the scripts wait for them; FINAL's credits would loop forever).
	const bool playAudio = !audioDump && selected->key == std::string("gob3");
	uint64_t played = 0;
	auto dumpAudio = [&](uint32_t ms) {
		if (!audioDump) {
			if (!playAudio)
				return;
			for (uint64_t target = uint64_t(ms) * Mixer::kRate / 1000; played < target;) {
				size_t n = size_t(std::min<uint64_t>(target - played, 4096));
				mixBuf.resize(n * 2);
				interp.mixer().mix(mixBuf.data(), int(n));
				played += n;
			}
			return;
		}
		uint64_t target = uint64_t(ms) * Mixer::kRate / 1000;
		while (audioDump->frames() < target) {
			size_t n = size_t(std::min<uint64_t>(target - audioDump->frames(), 4096));
			mixBuf.resize(n * 2);
			interp.mixer().mix(mixBuf.data(), int(n));
			audioDump->write(mixBuf.data(), n);
		}
	};

	long frames = 0;
	int snaps = 0;
	auto snap = [&]() {
		if (snapDir.empty())
			return;
		char name[64];
		std::snprintf(name, sizeof(name), "/frame_%03d_%07.2fs.bmp", snaps++, host.now / 1000.0);
		const Surface &scr = interp.screen();
		writeBmp(snapDir + name, scr.data(), scr.width(), scr.height(), interp.screenPalette());
	};
	uint32_t nextSnap = snapEveryMs;
	int couldSkip = -1;
	try {
		game.start();
		dumpAudio(host.now + 1000 / 60);
		while (!game.done() && frames < maxFrames) {
			if (interp.blockedOnInput()) {
				std::printf("-- stopped: script is waiting for player input\n");
				break;
			}
			host.now += 1000 / 60;
			frames++;
			for (uint32_t &at : skips)
				if (at != UINT32_MAX && host.now >= at) {
					at = UINT32_MAX;
					std::printf("%8.3fs  (skip cutscene requested)\n", host.now / 1000.0);
					interp.skipCutscene();
				}
			for (Timed &a : actions)
				if (!a.done && host.now >= a.at) {
					a.done = true;
					if (a.action == Action::Level) {
						std::printf("%8.3fs  (level jump to %s requested)\n", host.now / 1000.0, a.arg.c_str());
						if (!interp.requestLevel(a.arg))
							std::printf("-- unknown room %s\n", a.arg.c_str());
					} else if (a.action == Action::SetVar) {
						unsigned slot = 0;
						long value = 0;
						std::sscanf(a.arg.c_str(), "%u %ld", &slot, &value);
						std::printf("%8.3fs  (v[%u] = %ld)\n", host.now / 1000.0, slot, value);
						interp.variables().setSlot(slot, uint32_t(value));
					} else if (a.action == Action::SetStr) {
						size_t sp = a.arg.find(' ');
						unsigned slot = unsigned(std::strtoul(a.arg.c_str(), nullptr, 10));
						std::string text = a.arg.substr(sp + 1);
						std::printf("%8.3fs  (s[%u] = \"%s\")\n", host.now / 1000.0, slot, text.c_str());
						interp.variables().writeString(slot * 4, text);
					} else if (a.action == Action::QuickSave) {
						std::printf("%8.3fs  (quick save requested)\n", host.now / 1000.0);
						interp.requestQuickSave();
					} else {
						std::printf("%8.3fs  (quick load requested)\n", host.now / 1000.0);
						interp.requestQuickLoad();
					}
				}
			if (haveScript) {
				script.step(interp, host);
				if (script.failed() || script.ended())
					break;
			}
			interp.runFrame();
			dumpAudio(host.now + 1000 / 60); // sounds started this frame begin at its time
			if (logText && frames % 6 == 0) {
				std::string t = screenText(interp);
				if (t != lastText) {
					lastText = t;
					if (!t.empty())
						std::printf("%8.3fs  (text) %s\n", host.now / 1000.0, t.c_str());
				}
			}
			for (Watch &w : watches)
				if (w.offset < interp.variables().size() && interp.variables().data()[w.offset] != w.value) {
					w.value = interp.variables().data()[w.offset];
					std::printf("%8.3fs  watch [%u] = %d (%s)\n", host.now / 1000.0, w.offset, w.value,
					            interp.currentTot().c_str());
				}
			if (watchSkip && int(interp.canSkip()) != couldSkip) {
				couldSkip = interp.canSkip();
				std::printf("%8.3fs  (can skip: %s, in %s)\n", host.now / 1000.0, couldSkip ? "yes" : "no",
				            interp.currentTot().c_str());
			}
			if (hdr.attached())
				hdr.update(); // as gobnative does every frame
			for (HdDump &d : hdDumps)
				if (d.at != UINT32_MAX && host.now >= d.at) {
					d.at = UINT32_MAX;
					hdDump(d.prefix);
				}
			if (snapEveryMs && host.now >= nextSnap) {
				snap();
				nextSnap += snapEveryMs;
			}
			if (guideAt != UINT32_MAX && host.now >= guideAt) {
				const bool idle = !interp.currentLevel().empty() && !interp.canSkip() && !interp.skipping();
				guideIdle = idle ? guideIdle + 1 : 0;
				if (guideIdle >= 30) {
					guide::Capture cap = guide::capture(interp);
					snap();
					guide::probeNames(cap, [&](int x, int y) {
						host.forceX = x;
						host.forceY = y;
						for (int f = 0; f < 10; f++) {
							host.now += 1000 / 60;
							frames++;
							interp.runFrame();
						}
						return guide::hoverText(interp);
					});
					host.forceX = host.forceY = -1;
					if (!guide::write(cap, guideOpt))
						return 1;
					guideAt = UINT32_MAX;
					break;
				}
			}
		}
		game.rethrowIfFailed();
		snap();
	} catch (const std::exception &e) {
		std::printf("-- ERROR: %s\n", e.what());
		std::printf("-- after %ld frames (%.1fs simulated), %d trace lines, in %s\n", frames, host.now / 1000.0,
		            host.lines, interp.currentTot().c_str());
		return 1;
	}

	if (audioDump)
		audioDump->close();
	if (printHotspots)
		for (const Interpreter::HotspotInfo &h : interp.hotspots())
			std::printf("hotspot %3d id %5u type %d button %d rect %d,%d-%d,%d key 0x%04X%s\n", h.slot, h.id, h.type(),
			            h.button(), h.left, h.top, h.right, h.bottom, h.key, h.disabled() ? " disabled" : "");
	if (printObjects)
		std::fputs(printObjectState(interp).c_str(), stdout);
	for (int i = 2; i + 1 < argc; i++)
		if (!std::strcmp(argv[i], "--dump-vars")) {
			std::ofstream out(argv[i + 1], std::ios::binary);
			const Variables &v = interp.variables();
			out.write(reinterpret_cast<const char *>(v.data()), long(v.size()));
		}

	std::printf("-- %s after %ld frames (%.1fs simulated), %d trace lines, in %s\n",
	            game.done() ? "finished" : "paused", frames, host.now / 1000.0, host.lines,
	            interp.currentTot().c_str());
	const bool watchedToEnd = game.done() && script.onlyWaitsLeft(); // the game ended while the script waited
	if (haveScript && !script.done() && !script.failed() && !script.ended() && !watchedToEnd)
		std::printf("-- script not finished\n");
	return haveScript && (script.failed() || (!script.done() && !script.ended() && !watchedToEnd)) ? 1 : 0;
}
