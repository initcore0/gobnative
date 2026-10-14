// gob2native - the app's icon on its windows
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/appicon.h"

#include "gfx/png.h"

#include <SDL.h>

#include <cstddef>

namespace gob {

namespace embedded {
extern const unsigned char windowIcon[];
extern const std::size_t windowIconSize;
} // namespace embedded

void setWindowIcon(SDL_Window *window) {
#ifdef __EMSCRIPTEN__
	(void)window;
#else
	if (!window)
		return;
	PngImage icon;
	if (!decodePng(embedded::windowIcon, embedded::windowIconSize, icon))
		return;
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(icon.rgba.data(), icon.info.width, icon.info.height, 32,
	                                                    icon.info.width * 4, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		SDL_SetWindowIcon(window, s);
		SDL_FreeSurface(s);
	}
#endif
}

} // namespace gob
