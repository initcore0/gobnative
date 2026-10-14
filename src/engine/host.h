// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

namespace gob {

// Platform services the engine needs. The SDL front end, the headless test
// runner and (later) a web build each provide one.
class Host {
public:
	virtual ~Host() = default;

	// Milliseconds since an arbitrary start point.
	virtual uint32_t ticks() = 0;

	// Input state, polled by the interpreter.
	virtual int mouseX() { return 152; }
	virtual int mouseY() { return 92; }
	virtual int mouseButtons() { return 0; }   // buttons held now (1 left, 2 right)
	virtual int takeMouseClicks() { return 0; } // buttons pressed since the last call
	virtual int pollKey() { return 0; } // 0 = no key pending; BIOS-style scan<<8 | ascii
	virtual void warpMouse(int x, int y) { (void)x; (void)y; }

	// Folder for save games; empty disables saving.
	virtual std::string saveDirectory() { return {}; }

	// A short message for the player ("Saved", "No quick save to load").
	virtual void notify(const std::string &message) { (void)message; }

	// Diagnostic trace of what the engine is doing.
	virtual void trace(const std::string &line) { (void)line; }
};

} // namespace gob
