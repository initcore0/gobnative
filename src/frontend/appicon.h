// gob2native - the app's icon on its windows
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The icon ("GOB" on an orange tile, assets/icon, drawn by
// scripts/make-icon.py) is compiled in; setWindowIcon gives it to a window
// (title bar, taskbar, Alt-Tab; on macOS the Dock when the app runs without
// its .app bundle). Does nothing in a browser (the page has a favicon).

#pragma once

struct SDL_Window;

namespace gob {

void setWindowIcon(SDL_Window *window);

} // namespace gob
