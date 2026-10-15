// gob2native - touchscreen tests: the fingers' gestures (taps, long
// presses, two-finger taps, drags) and the touch bar's layout
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/touch.h"

#include <cstdio>

using namespace gob;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

// The clicks among the actions (button, or 0 when there is none).
int clickOf(const std::vector<TouchAction> &actions, float *x = nullptr, float *y = nullptr) {
	for (const TouchAction &a : actions)
		if (a.kind == TouchAction::Click) {
			if (x)
				*x = a.x;
			if (y)
				*y = a.y;
			return a.button;
		}
	return 0;
}

int clicks(const std::vector<TouchAction> &actions) {
	int n = 0;
	for (const TouchAction &a : actions)
		n += a.kind == TouchAction::Click;
	return n;
}

void testTap() {
	TouchMouse t;
	t.down(1, 100, 50, 1000);
	std::vector<TouchAction> a = t.take();
	CHECK(a.size() == 1 && a[0].kind == TouchAction::Point && a[0].x == 100 && a[0].y == 50);
	t.update(1100);
	t.motion(1, 104, 52, 1120); // within the slop
	t.up(1, 104, 52, 1150);
	a = t.take();
	float x = 0, y = 0;
	CHECK(clickOf(a, &x, &y) == 1);
	CHECK(x == 100 && y == 50); // where it went down
	CHECK(!a.empty() && a.back().kind == TouchAction::Lift);
}

void testLongPress() {
	TouchMouse t;
	t.down(1, 10, 10, 0);
	t.update(TouchMouse::kLongPressMs - 1);
	CHECK(clicks(t.take()) == 0);
	t.update(TouchMouse::kLongPressMs);
	CHECK(clickOf(t.take()) == 2);
	t.update(TouchMouse::kLongPressMs + 500);
	t.up(1, 10, 10, TouchMouse::kLongPressMs + 600);
	CHECK(clicks(t.take()) == 0); // lifting doesn't click again
}

void testTwoFingers() {
	TouchMouse t;
	t.down(1, 30, 40, 0);
	t.down(2, 80, 40, 50);
	t.update(TouchMouse::kLongPressMs + 10); // no long press with two fingers
	CHECK(clicks(t.take()) == 0);
	t.up(2, 80, 40, 700);
	CHECK(clicks(t.take()) == 0); // not before both are up
	t.up(1, 30, 40, 720);
	float x = 0;
	std::vector<TouchAction> a = t.take();
	CHECK(clickOf(a, &x) == 2 && x == 30);

	// The second finger slides away: a pinch or such, no click.
	t.down(1, 30, 40, 1000);
	t.down(2, 80, 40, 1010);
	t.motion(2, 160, 40, 1050);
	t.up(2, 160, 40, 1100);
	t.up(1, 30, 40, 1110);
	CHECK(clicks(t.take()) == 0);
}

void testDrag() {
	TouchMouse t;
	t.down(1, 10, 10, 0);
	t.motion(1, 60, 10, 30);
	t.motion(1, 90, 20, 60);
	t.update(TouchMouse::kLongPressMs + 100); // a moving finger is no long press
	t.up(1, 90, 20, 900);
	std::vector<TouchAction> a = t.take();
	CHECK(clicks(a) == 0);
	CHECK(a.size() == 4 && a[2].kind == TouchAction::Point && a[2].x == 90);

	// A quick swipe with no motion reported in between: no click either.
	t.down(1, 10, 10, 1000);
	t.up(1, 200, 10, 1050);
	CHECK(clicks(t.take()) == 0);
}

void testRightNext() {
	TouchMouse t;
	t.rightNext = true;
	t.down(1, 5, 5, 0);
	t.up(1, 5, 5, 50);
	CHECK(clickOf(t.take()) == 2);
	CHECK(!t.rightNext); // for one tap only
	t.down(1, 5, 5, 100);
	t.up(1, 5, 5, 150);
	CHECK(clickOf(t.take()) == 1);
}

void testCancel() {
	TouchMouse t;
	t.down(1, 5, 5, 0);
	t.cancel();
	t.take();
	t.update(TouchMouse::kLongPressMs * 2);
	t.up(1, 5, 5, 50);
	CHECK(t.take().empty());
}

void testBar() {
	TouchBarState st;
	// A phone held sideways (844x390 points, the picture 4:3 in the middle):
	// a column in the space on the right.
	std::vector<TouchBarButton> bar = layoutTouchBar(844, 390, 162, 0, 520, 390, st);
	CHECK(bar.size() == 5);
	CHECK(bar[0].id == TouchButton::Menu && bar[0].x >= 682 && bar[0].x + bar[0].w <= 844);
	CHECK(bar[1].y > bar[0].y && bar[1].x == bar[0].x);
	CHECK(!bar[1].enabled); // nothing to skip
	CHECK(bar[4].y + bar[4].h <= 390);
	CHECK(touchBarHit(bar, bar[2].x + 1, bar[2].y + 1) == &bar[2]);
	CHECK(touchBarHit(bar, 300, 200) == nullptr);

	// Upright (390x844): a row under the picture.
	st.keyboard = st.canSkip = st.rightArmed = true;
	bar = layoutTouchBar(390, 844, 0, 276, 390, 292, st);
	CHECK(bar.size() == 6 && bar[5].id == TouchButton::Keyboard);
	CHECK(bar[0].y >= 568 && bar[1].y == bar[0].y && bar[1].x > bar[0].x);
	CHECK(bar[5].x + bar[5].w <= 390);
	CHECK(bar[1].enabled && bar[2].lit);

	// No room around the picture: over its right edge, small enough to fit.
	bar = layoutTouchBar(400, 300, 0, 0, 400, 300, st);
	CHECK(bar.back().y + bar.back().h <= 300);
	CHECK(bar[0].x + bar[0].w <= 400 && bar[0].w >= 28);
}

} // namespace

int main() {
	testTap();
	testLongPress();
	testTwoFingers();
	testDrag();
	testRightNext();
	testCancel();
	testBar();
	if (failures)
		std::printf("%d failure(s)\n", failures);
	else
		std::printf("touch: all tests passed\n");
	return failures ? 1 : 0;
}
