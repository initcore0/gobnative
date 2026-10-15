// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/touch.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gob {

namespace {

bool far(float x, float y, float x0, float y0) {
	return std::hypot(x - x0, y - y0) > TouchMouse::kSlop;
}

} // namespace

void TouchMouse::down(int64_t finger, float x, float y, uint32_t ms) {
	if (std::any_of(_down.begin(), _down.end(), [finger](const Finger &f) { return f.id == finger; }))
		return;
	_down.push_back({finger, x, y});
	if (_down.size() == 1) { // a new gesture
		_first = finger;
		_x0 = x;
		_y0 = y;
		_t0 = ms;
		_active = true;
		_moved = _second = _done = false;
		_out.push_back({TouchAction::Point, x, y});
	} else if (_active && !_moved && !_done)
		_second = true;
}

void TouchMouse::motion(int64_t finger, float x, float y, uint32_t ms) {
	(void)ms;
	auto f = std::find_if(_down.begin(), _down.end(), [finger](const Finger &f) { return f.id == finger; });
	if (f == _down.end() || !_active)
		return;
	if (!far(x, y, f->x0, f->y0))
		return;
	if (finger != _first || _second) { // not a two-finger tap after all
		_done = true;
		return;
	}
	_moved = true;
	_out.push_back({TouchAction::Point, x, y});
}

void TouchMouse::up(int64_t finger, float x, float y, uint32_t ms) {
	(void)ms;
	auto f = std::find_if(_down.begin(), _down.end(), [finger](const Finger &f) { return f.id == finger; });
	if (f == _down.end())
		return;
	if (finger == _first && !_second && far(x, y, f->x0, f->y0))
		_moved = true; // a quick swipe with no motion reported in between
	_down.erase(f);
	if (!_down.empty() || !_active)
		return;
	_active = false;
	if (!_done && !_moved) {
		int button = _second || rightNext ? 2 : 1;
		if (!_second)
			rightNext = false;
		_out.push_back({TouchAction::Click, _x0, _y0, button});
	}
	_out.push_back({TouchAction::Lift, x, y});
}

void TouchMouse::update(uint32_t ms) {
	if (_active && !_done && !_moved && !_second && ms - _t0 >= kLongPressMs) {
		_done = true;
		_out.push_back({TouchAction::Click, _x0, _y0, 2});
	}
}

void TouchMouse::cancel() {
	_down.clear();
	_active = false;
}

std::vector<TouchAction> TouchMouse::take() {
	return std::exchange(_out, {});
}

std::vector<TouchBarButton> layoutTouchBar(float windowW, float windowH, float gx, float gy, float gw, float gh,
                                           const TouchBarState &state) {
	std::vector<TouchBarButton> bar = {
		{TouchButton::Menu, "Menu", 0, 0, 0, 0},
		{TouchButton::Skip, "Skip", 0, 0, 0, 0, false, state.canSkip},
		{TouchButton::Right, "Right", 0, 0, 0, 0, state.rightArmed},
		{TouchButton::Save, "Save", 0, 0, 0, 0},
		{TouchButton::Load, "Load", 0, 0, 0, 0},
	};
	if (state.keyboard)
		bar.push_back({TouchButton::Keyboard, "Keys", 0, 0, 0, 0});
	const float n = float(bar.size()), margin = 8.0f, gap = 8.0f;
	float size = 52.0f;
	const float length = n * size + (n - 1) * gap;
	const float right = windowW - (gx + gw), below = windowH - (gy + gh);
	float x, y, dx = 0, dy = 0;
	if (right >= size + 2 * margin && windowH >= length + 2 * margin) { // beside the picture
		x = gx + gw + (right - size) / 2;
		y = (windowH - length) / 2;
		dy = size + gap;
	} else if (below >= size + 2 * margin && windowW >= length + 2 * margin) { // under it
		x = (windowW - length) / 2;
		y = gy + gh + (below - size) / 2;
		dx = size + gap;
	} else { // over its right edge, smaller if the window is short
		size = std::clamp((windowH - 2 * margin - (n - 1) * gap) / n, 28.0f, size);
		x = windowW - margin - size;
		y = std::max(margin, (windowH - (n * size + (n - 1) * gap)) / 2);
		dy = size + gap;
	}
	for (TouchBarButton &b : bar) {
		b.x = x;
		b.y = y;
		b.w = b.h = size;
		x += dx;
		y += dy;
	}
	return bar;
}

const TouchBarButton *touchBarHit(const std::vector<TouchBarButton> &bar, float x, float y) {
	for (const TouchBarButton &b : bar)
		if (b.contains(x, y))
			return &b;
	return nullptr;
}

} // namespace gob
