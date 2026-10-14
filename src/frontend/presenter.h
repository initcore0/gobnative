// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenGL presenter: uploads the game frame (RGBA) to a texture and draws it
// into a letterboxed viewport of the window's drawable area through a small
// filter pipeline:
//
//   320x200 frame --[pixel-art upscaler: Scale2x, Scale3x, xBR-like]--> 2x-4x
//   render target --[final pass: nearest, bilinear, sharp bilinear, CRT]-->
//   viewport (any size and shape: window scale, 4:3 correction)
//
// With an HD pack the source is the HD compositor's frame, N times 320x200
// (setSourceScale), updated in rectangles; the pixel-art upscalers are then
// skipped (the frame is already high resolution) and the final pass scales
// it to the viewport.
//
// The upscalers' output goes through sharp bilinear to the final size. An
// optional sharp text pass (setTextLayer) then blends the game's text,
// rendered from glyph distance fields, over the viewport. The
// render target is reused from frame to frame and only recreated when its
// size changes. Needs a current GL 3.2+ core context with gl::load() done.
// The shaders are GLSL 1.50 written to port to GLSL ES 3.00 (texelFetch,
// no derivatives or desktop-only built-ins).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gob {

enum class VideoFilter { Nearest, Smooth, Sharp, Scale2x, Scale3x, Xbr, Crt };
enum class CrtMask { None, ApertureGrille, SlotMask, ShadowMask };

// Filter choice and parameters (percentages unless noted), as kept in the
// settings file.
struct FilterSettings {
	VideoFilter filter = VideoFilter::Sharp;
	int softness = 0;       // sharp bilinear (also after the upscalers): 0 crisp .. 100 plain bilinear
	int xbrStrength = 100;  // how far the xBR-like filter rounds edges off
	// CRT
	int scanlines = 65;     // scanline darkness
	int crtSharpness = 50;  // horizontal sharpness of the beam
	CrtMask mask = CrtMask::ApertureGrille;
	int maskStrength = 40;
	int bloom = 30;         // glow around bright areas
	int curvature = 0;      // barrel distortion of the screen
	int vignette = 25;      // darker corners
	int brightness = 105;   // makes up for the light scanlines and the mask take away
	double gamma = 2.2;     // display gamma (the image is treated as a 2.4 gamma CRT signal)
};

const char *filterName(VideoFilter f);  // settings file name ("sharp")
const char *filterLabel(VideoFilter f); // menu label
const char *crtMaskName(CrtMask m);
const char *crtMaskLabel(CrtMask m);
bool parseFilter(const std::string &name, VideoFilter &f);
bool parseCrtMask(const std::string &name, CrtMask &m);

class GlPresenter {
public:
	struct Rect {
		int x = 0, y = 0, w = 0, h = 0; // drawable pixels, origin top left
	};

	bool init(int frameW, int frameH, std::string &error);
	void shutdown();

	// Where the frame goes in a drawable area of `dw` x `dh` pixels.
	// `aspect` shows the frame as 4:3, `integer` keeps whole multiples.
	Rect layout(int dw, int dh, bool aspect, bool integer) const;

	// Uploads `rgba` (frameW x frameH, top row first), clears the drawable
	// to black and draws the frame into `vp` with filter `f`. Leaves the
	// window's framebuffer bound with the full-drawable viewport.
	void draw(const uint8_t *rgba, int dw, int dh, const Rect &vp, const FilterSettings &f);
	// The source frame is frameW x frameH times `scale` (1: the game's own
	// frame; more: an HD frame). Changing it reallocates the texture.
	void setSourceScale(int scale);
	int sourceScale() const { return _srcScale; }
	// Uploads rectangles `rects` (source pixels) of `rgba`, the whole source
	// frame (RGBA8, top row first).
	void upload(const uint8_t *rgba, const std::vector<Rect> &rects);
	// Draws the last uploaded frame again (for timing).
	void redraw(int dw, int dh, const Rect &vp, const FilterSettings &f);

	// Sharp text layer, drawn over the filtered frame (not with the CRT
	// filter, whose curvature it can't follow). `map` and `colors` are
	// frameW x frameH RGBA (see SharpText); `atlas` atlasSize^2 distance
	// field texels, uploaded when `atlasDirty`. Null `map` turns it off.
	struct TextLayer {
		const uint8_t *map = nullptr, *colors = nullptr, *atlas = nullptr;
		bool atlasDirty = false;
		int atlasSize = 0, cellStride = 0, slotsPerRow = 0, samples = 0;
		float spread = 1.0f;
	};
	void setTextLayer(const TextLayer &layer);

	// Reads rectangle `r` of the back buffer as RGB bytes, top row first.
	std::vector<uint8_t> readPixels(const Rect &r, int dh);

	// Writes the whole back buffer to `path`: BMP if it ends in ".bmp",
	// else PNG (call before swapping).
	bool screenshot(const std::string &path, int dw, int dh);

private:
	struct Target {
		unsigned fbo = 0, texture = 0;
		int w = 0, h = 0;
	};
	enum Program { kNearest, kSmooth, kSharp, kScale2x, kScale3x, kXbr, kCrt, kText, kProgramCount };

	bool ensureTarget(Target &t, int w, int h);
	void runPass(Program p, unsigned texture, int srcW, int srcH, int outW, int outH, const FilterSettings &f);

	int _frameW = 0, _frameH = 0;
	int _srcScale = 1, _srcW = 0, _srcH = 0; // the source texture
	unsigned _texture = 0, _vao = 0;
	unsigned _programs[kProgramCount] = {};
	Target _upscaled;

	void drawTextLayer(const Rect &vp, int dh);
	unsigned _textMap = 0, _textColors = 0, _textAtlas = 0;
	int _atlasSize = 0;
	bool _textOn = false;
	TextLayer _text;
};

} // namespace gob
