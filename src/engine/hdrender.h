// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// HD compositor (docs/hdpacks.md, "The HD compositor"): shows an HD pack in
// the game. It gives every sprite of a Draw an HdLayer at the pack's scale
// (the Surface operations keep the layers in step with the 320x200 indexed
// drawing), hands decoded pictures their pack version (or the original
// enlarged), and makes the full-colour frame from the front surface's
// layer: live pixels through the screen palette, HD pixels through the
// palette's fades relative to the room palette. Only the tiles that changed
// are recoloured, and wherever the front surface differs from what its
// layer was told, the layer is repaired from the surface, so the picture is
// never wrong, at worst less sharp. No SDL or GL.

#pragma once

#include "engine/draw.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gob {

class HdPack;

// How pictures without HD art are enlarged (docs/hdpacks.md, "Pictures
// without HD art"): big pixels, Scale2x/3x on palette indices, xBR-like edge
// smoothing, or bilinear; the last two as mixes of palette indices
// (gfx/pixelfilter.h), so palettes and fades still apply.
enum class HdSpriteFilter : uint8_t { Original, Scale2x, SmoothEdges, Smooth };

struct HdRenderOptions {
	HdSpriteFilter filter = HdSpriteFilter::Original; // pictures without an HD version
	bool roomsOnly = false; // only pictures drawn into the screen surfaces (backgrounds, overlays) from the pack
	bool smoothSilhouettes = false; // the smoothing filters also smooth transparent outlines (gfx/smooth.h)
	bool tint = false;      // debug: tint live pixels blue and repaired ones red
};

class HdRenderer {
public:
	struct Rect {
		int x = 0, y = 0, w = 0, h = 0; // frame pixels
	};
	struct Stats {
		size_t hdPictures = 0, nativePictures = 0; // pictures decorated since attach
		size_t repairedPixels = 0;                 // surface pixels repaired, in total
		size_t lastRepaired = 0;                   // in the last update
		size_t rebuilds = 0;                       // layers rebuilt (pack, scale or option switches)
		double rebuildMs = 0;                      // the last rebuild's time
		size_t rebuildPictures = 0;                // pictures the last rebuild decorated
		size_t rebuildLost = 0;                    // pixels from pictures it had to show live
		double paletteRebuildMs = 0;               // the last smoothed pictures redone for a new room palette
		size_t resolvedPixels = 0;                 // frame pixels recoloured in the last update
		int mode = 0;                              // palette mapping of the last update (Mode)
	};
	enum Mode { Identity, FadeOut, FadeIn, Retint };

	// Starts compositing `draw` at `scale` with pictures from `pack` (may be
	// null: everything native, enlarged). `totName` gives the running script
	// file (pictures are looked up as (TOT, resource id)). Also used to
	// switch packs or scales at any moment: when `draw` tracks pixel sources
	// (Draw::setTrackSources), every layer is rebuilt (rebuild()), so what is
	// on screen shows the new pack at once.
	void attach(Draw &draw, HdPack *pack, int scale, std::function<std::string()> totName);
	// Rebuilds every layer from the pixel sources with the current pack,
	// scale and options (the pictures on screen decorated again, on several
	// threads); without sources, the layers restart from the indexed pixels.
	void rebuild();
	void detach();
	bool attached() const { return _draw != nullptr; }
	int scale() const { return _scale; }
	Draw *draw() const { return _draw; }

	void setOptions(const HdRenderOptions &o);
	const HdRenderOptions &options() const { return _options; }

	// Brings the frame up to date with the front surface and the screen
	// palette. Returns false when not attached.
	bool update();

	// The frame: (320 * scale) x (200 * scale) RGBA8, top row first.
	uint32_t *frame() { return _frame.data(); }
	int frameWidth() const { return _fw; }
	int frameHeight() const { return _fh; }

	// The front end drew over the frame (cursor, text) in this rectangle of
	// game pixels: it is uploaded with this frame and recoloured next time.
	void overlay(int left, int top, int right, int bottom);
	// Everything is recoloured and uploaded at the next update.
	void invalidateAll() { _full = true; }
	// Frame rectangles changed by the last update and the overlays since.
	std::vector<Rect> changedRects() const;

	// Colour of a layer pixel with the current palette mapping (for overlays).
	uint32_t resolve(uint32_t px, uint8_t idx) const;
	uint32_t liveColour(uint8_t idx) const { return _live[idx]; }
	// A mix of two palette indices (HdLayer::Mix) in the screen palette.
	uint32_t mixColour(uint32_t p) const {
		const uint32_t a = _live[p & 0xFF], b = _live[(p >> 8) & 0xFF], w = (p >> 16) & 0xFF;
		uint32_t out = 0xFF000000u;
		for (int sh = 0; sh < 24; sh += 8) {
			const uint32_t ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
			out |= ((ca * (255 - w) + cb * w + 127) / 255) << sh;
		}
		return out;
	}

	const Stats &stats() const { return _stats; }
	// What the front surface shows, in game pixels (each judged by its HD
	// block's first pixel): from HD pictures, live (original pixels), repaired.
	struct Coverage {
		size_t hd = 0, live = 0, repaired = 0;
	};
	Coverage coverage() const;

	// Decorates a decoded picture (Draw::pictureHook). Public for tests.
	void decorate(Surface &pic, int resId, bool transparent, PictureUse use = PictureUse::Screen);

private:
	std::unique_ptr<HdLayer> makeLayer(const Surface &pic, const std::string &tot, int resId, bool transparent,
	                                   PictureUse use, bool &fromPack) const;
	void enlarge(const Surface &pic, bool transparent, HdLayer &layer) const;
	void rebuildPictures(bool paletteDependentOnly);
	void setReference(const VgaPalette &ref);
	// Smoothed edges depend on the reference palette's colours.
	bool paletteDependent() const { return _options.filter == HdSpriteFilter::SmoothEdges && _scale > 1; }
	std::vector<uint8_t> _paletteDependent; // by picture id: decorated with the palette-dependent filter
	std::array<uint32_t, 256> _refRgb{};    // the reference palette, 8-bit RGB
	bool _haveRefRgb = false;
	// Pictures without HD art enlarged by a smoothing filter, by content
	// (a picture loaded again costs a copy).
	mutable std::mutex _filteredLock;
	mutable std::unordered_map<uint64_t, std::shared_ptr<const HdLayer>> _filtered;
	mutable size_t _filteredBytes = 0;
	void computeMode(const VgaPalette &screen);
	void repair(const Surface &front, HdLayer &layer);
	void resolveRect(const HdLayer &layer, int left, int top, int right, int bottom);
	void markTiles(std::vector<uint8_t> &tiles, int left, int top, int right, int bottom) const;

	Draw *_draw = nullptr;
	HdPack *_pack = nullptr;
	int _scale = 0;
	std::function<std::string()> _totName;
	std::string _lastTot; // the script file pictures were last decorated for
	HdRenderOptions _options;
	Stats _stats;

	std::vector<uint32_t> _frame;
	int _fw = 0, _fh = 0;
	bool _full = true;
	std::vector<uint8_t> _pending; // tiles to recolour at the next update (overlays)
	std::vector<uint8_t> _upload;  // tiles changed since the last update began

	// Palette mapping: the reference palette (the last non-black staged
	// palette: what HD pictures look like at full brightness), the screen
	// palette it was computed for, and the lookup tables.
	VgaPalette _ref{}, _lastScreen{};
	bool _haveRef = false, _lastTint = false;
	Mode _mode = Identity;
	std::array<uint32_t, 256> _live{}, _repairedColour{};
	std::array<uint8_t, 256> _lut{};               // FadeOut / FadeIn, per channel value
	std::array<std::array<int16_t, 3>, 256> _off{}; // Retint: screen - reference, per index
};

} // namespace gob
