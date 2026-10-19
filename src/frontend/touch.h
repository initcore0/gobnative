// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Touchscreens (phones and tablets in the browser, touch laptops): the
// fingers become the game's mouse, and a bar of buttons stands in for the
// keys a touchscreen has none of. Plain logic in window points (no SDL, no
// ImGui), so tests can feed it fingers.
//
// Gestures (TouchMouse):
// - a finger down moves the pointer there (the game shows what is under it);
// - a tap (lifted soon, without moving) is a left click where it went down;
// - a long press (still, for kLongPressMs) is a right click there;
// - a tap with two fingers is a right click where the first one went down;
// - dragging moves the pointer and clicks nothing (to read the names of
//   things, or to hold it at the edge of a wide room to scroll).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gob {

struct TouchAction {
	enum Kind { Point, Click, Lift } kind;
	float x = 0, y = 0; // window points (Point, Click)
	int button = 0;     // Click: 1 left, 2 right
};

class TouchMouse {
public:
	static constexpr uint32_t kLongPressMs = 500; // a still finger becomes a right click
	static constexpr float kSlop = 12.0f;         // points a tap may move

	void down(int64_t finger, float x, float y, uint32_t ms);
	void motion(int64_t finger, float x, float y, uint32_t ms);
	void up(int64_t finger, float x, float y, uint32_t ms);
	// Once per frame: the long press.
	void update(uint32_t ms);
	// Forgets the fingers down (the overlay opened, the window lost focus).
	void cancel();
	// The actions since the last call, in order.
	std::vector<TouchAction> take();

	// The next tap is a right click (the touch bar's "Right" button).
	bool rightNext = false;

private:
	struct Finger {
		int64_t id;
		float x0, y0;
	};
	std::vector<Finger> _down;
	std::vector<TouchAction> _out;
	int64_t _first = 0;     // the gesture's first finger
	float _x0 = 0, _y0 = 0; // ... and where it went down
	uint32_t _t0 = 0;
	// A gesture is going on (a finger is down); the first finger moved; a
	// second one came down (a two-finger tap); the gesture has clicked or
	// is cancelled (lifting clicks no more).
	bool _active = false, _moved = false, _second = false, _done = false;
};

// The touch bar: a column of buttons beside the game picture (a row under
// it when the window is tall), over the picture's edge when there's no
// room. Always the same buttons in the same places (Skip is dimmed when
// there's nothing to skip; Keys only in the web build; Hint while hints are
// on and a level with hints is played).
enum class TouchButton { Menu, Skip, Hint, Right, Save, Load, Keyboard };

struct TouchBarButton {
	TouchButton id;
	const char *label;
	float x, y, w, h; // window points
	bool lit = false;    // on (Right armed)
	bool enabled = true; // Skip: something can be skipped
	bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct TouchBarState {
	bool canSkip = false, rightArmed = false, keyboard = false; // keyboard: the web build's text field
	bool hint = false; // the hint key does something (frontend/hints.cpp)
};

// `gx..` is the game picture's rectangle; all in window points.
std::vector<TouchBarButton> layoutTouchBar(float windowW, float windowH, float gx, float gy, float gw, float gh,
                                           const TouchBarState &state);
// The button at a point (nullptr when none is).
const TouchBarButton *touchBarHit(const std::vector<TouchBarButton> &bar, float x, float y);

} // namespace gob
