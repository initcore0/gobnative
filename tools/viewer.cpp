// gob2native - interactive resource viewer (SDL2)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: gobview <gamedir> [--game gob1|gob2|gob3]
//
// Keys:  Up/Down     previous/next TOT file
//        Left/Right  previous/next sprite in the current TOT
//        T           toggle transparency (index 0 shown as checkerboard)
//        1-4         zoom level
//        Esc         quit

#include "data/datafiles.h"
#include "engine/game.h"
#include "formats/tot.h"
#include "gfx/image.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace gob;

namespace {

struct Entry {
	uint16_t id;
	Image image;
};

struct TotView {
	std::string name;
	Palette palette;
	bool hasPalette = false;
	std::vector<Entry> sprites;
};

TotView loadTotView(DataFiles &files, const std::string &base, const Palette &fallback) {
	TotView view;
	view.name = base;
	view.palette = fallback;

	TotFile tot;
	if (!tot.load(files, base))
		return view;

	auto visit = [&](uint16_t id) {
		auto res = tot.resource(id);
		if (!res)
			return;
		if (res->data.size() == 768 && !view.hasPalette) {
			if (auto pal = decodeVgaPalette(res->data)) {
				view.palette = *pal;
				view.hasPalette = true;
			}
			return;
		}
		if (res->width > 0 && res->height > 0)
			if (auto img = decodeSprite(*res))
				view.sprites.push_back({id, std::move(*img)});
	};

	for (int i = 0; i < tot.totResourceCount(); i++)
		visit(uint16_t(i));
	for (int i = 0; i < tot.extResourceCount(); i++)
		visit(uint16_t(30000 + i));
	return view;
}

} // namespace

int main(int argc, char **argv) {
	// takeGameOption() moves the arguments within argv, and SDL2main on
	// Windows frees argv's strings after main returns: work on a copy.
	std::vector<char *> argvCopy(argv, argv + argc + 1);
	argv = argvCopy.data();
	const std::string gameKey = takeGameOption(argc, argv);
	if (argc < 2) {
		std::fprintf(stderr, "usage: %s <gamedir> [--game gob1|gob2|gob3]\n", argv[0]);
		return 2;
	}
	const GameInfo *game = selectToolGame(argv[1], gameKey);
	if (!game)
		return 2;

	DataFiles files(argv[1]);
	if (files.openAllArchives() == 0) {
		std::fprintf(stderr, "no STK archives found in %s\n", argv[1]);
		return 1;
	}

	std::vector<std::string> totNames;
	for (const std::string &name : files.list())
		if (name.size() > 4 && name.ends_with(".TOT"))
			totNames.push_back(name.substr(0, name.size() - 4));

	// Load everything up front; the whole game is a few MB.
	std::vector<TotView> views;
	Palette lastPalette = grayscalePalette();
	for (const std::string &base : totNames) {
		TotView v = loadTotView(files, base, lastPalette);
		if (v.hasPalette)
			lastPalette = v.palette;
		if (!v.sprites.empty())
			views.push_back(std::move(v));
	}
	if (views.empty()) {
		std::fprintf(stderr, "no sprites found\n");
		return 1;
	}

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

	const int kCanvasW = 320, kCanvasH = 200;
	int zoom = 3;
	SDL_Window *window = SDL_CreateWindow("gobview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	                                      kCanvasW * zoom, kCanvasH * zoom, SDL_WINDOW_ALLOW_HIGHDPI);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	SDL_RenderSetLogicalSize(renderer, kCanvasW, kCanvasH);
	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
	                                         kCanvasW, kCanvasH);

	size_t totIdx = 0, sprIdx = 0;
	bool transparent = false;
	bool dirty = true;
	std::vector<uint32_t> canvas(kCanvasW * kCanvasH);

	for (bool running = true; running;) {
		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT)
				running = false;
			if (ev.type != SDL_KEYDOWN)
				continue;

			switch (ev.key.keysym.sym) {
			case SDLK_ESCAPE: running = false; break;
			case SDLK_DOWN:  totIdx = (totIdx + 1) % views.size(); sprIdx = 0; break;
			case SDLK_UP:    totIdx = (totIdx + views.size() - 1) % views.size(); sprIdx = 0; break;
			case SDLK_RIGHT: sprIdx = (sprIdx + 1) % views[totIdx].sprites.size(); break;
			case SDLK_LEFT:  sprIdx = (sprIdx + views[totIdx].sprites.size() - 1) % views[totIdx].sprites.size(); break;
			case SDLK_t:     transparent = !transparent; break;
			case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
				zoom = ev.key.keysym.sym - SDLK_0;
				SDL_SetWindowSize(window, kCanvasW * zoom, kCanvasH * zoom);
				break;
			default: break;
			}
			dirty = true;
		}

		if (dirty) {
			const TotView &view = views[totIdx];
			const Entry &entry = view.sprites[sprIdx];
			const Image &img = entry.image;

			// Checkerboard backdrop so transparent pixels and sprite bounds are visible.
			for (int y = 0; y < kCanvasH; y++)
				for (int x = 0; x < kCanvasW; x++)
					canvas[y * kCanvasW + x] = ((x / 8 + y / 8) & 1) ? 0xFF2A2A2A : 0xFF1E1E1E;

			// Small sprites are centered; full-screen images start at the origin.
			int ox = std::max(0, (kCanvasW - img.width) / 2);
			int oy = std::max(0, (kCanvasH - img.height) / 2);
			for (int y = 0; y < img.height && oy + y < kCanvasH; y++)
				for (int x = 0; x < img.width && ox + x < kCanvasW; x++) {
					uint8_t idx = img.at(x, y);
					if (transparent && idx == 0)
						continue;
					const Rgb &c = view.palette[idx];
					canvas[(oy + y) * kCanvasW + ox + x] = 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b;
				}

			SDL_UpdateTexture(texture, nullptr, canvas.data(), kCanvasW * 4);

			char title[160];
			std::snprintf(title, sizeof(title), "gobview  %s  %s  [%zu/%zu]  res %u  %dx%d%s%s", game->title,
			              view.name.c_str(),
			              sprIdx + 1, view.sprites.size(), entry.id, img.width, img.height,
			              view.hasPalette ? "" : "  (borrowed palette)", transparent ? "  transp" : "");
			SDL_SetWindowTitle(window, title);
			dirty = false;
		}

		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, texture, nullptr, nullptr);
		SDL_RenderPresent(renderer);
	}

	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
