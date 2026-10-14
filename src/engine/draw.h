// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Drawing state and sprite operations. Scripts draw into the back surface;
// changed areas are tracked as "invalidated" rectangles and copied to the
// front surface (what the player sees) by blitInvalidated(). The palette is
// staged in vgaPalette (6-bit VGA values) and applied to the screen palette
// explicitly, optionally with a fade.

#pragma once

#include "formats/tot.h"
#include "gfx/font.h"
#include "gfx/image.h"
#include "gfx/surface.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gob {

namespace draw {
enum Op : int {
	BlitSurf = 0, PutPixel = 1, FillRect = 2, DrawLine = 3, Invalidate = 4, LoadSprite = 5,
	PrintText = 6, DrawBar = 7, ClearRect = 8, FillRectAbs = 9, DrawLetter = 10,
};

enum RenderFlag : int16_t {
	NoInvalidate = 0x0001, CapturePush = 0x0002, Collisions = 0x0004, CapturePop = 0x0008,
	UseDeltas = 0x0010, NoBlitInvalidated = 0x0200, NoSubtitles = 0x0400,
};

constexpr int kSpriteCount = 100;
constexpr int kFrontSurface = 20;
constexpr int kBackSurface = 21;
constexpr int kAnimSurface = 22;
constexpr int kCursorSurface = 23;
constexpr int kCaptureSurface = 30;
} // namespace draw

using VgaPalette = std::array<uint8_t, 768>; // 6-bit RGB triples

// How the script uses a decoded picture: drawn into the screen (a room's
// background or overlay, a mult's static key), into another sprite (MENU's
// icon bar), as an animation's picture sheet, or as a cursor.
enum class PictureUse : uint8_t { Screen, Sprite, Animation, Cursor };

class Draw {
public:
	using ResourceLoader = std::function<std::optional<Resource>(uint16_t)>;

	static constexpr int kScreenW = 320;
	static constexpr int kScreenH = 200;

	Draw();

	// New back surface of `width` x `height` (the front surface too when its
	// size differs), with a split of `split` rows at the bottom (below).
	void initScreen(int width = kScreenW, int height = kScreenH, int split = 0);

	// --- screen size, split and scrolling (Goblins 3; docs/graphics.md 9).
	// The front and back surfaces can be larger than the 320x200 display
	// (472x200 rooms, 320x240): the display shows the window of the front
	// surface at the view offset and, with a split, its bottom splitHeight
	// rows are the front surface's rows from splitStart on, unscrolled (the
	// room's text bar).
	int scrollX = 0, scrollY = 0; // the scripts' scroll offset (Draw::_scrollOffsetX/Y)
	int viewX = 0, viewY = 0;     // the window shown (Video::_scrollOffsetX/Y)
	int splitHeight = 0, splitStart = kScreenH;
	int surfaceWidth() const { return _back->width(); }
	int surfaceHeight() const { return _back->height(); }
	// The display is not simply the front surface.
	bool composed() const {
		return _front->width() != kScreenW || _front->height() != kScreenH || viewX != 0 || viewY != 0;
	}
	// What the player sees, 320x200: the front surface itself, or the view
	// composed from it (with its own HD layer, kept up to date from the front
	// surface's changed tiles).
	Surface &screenSurface();
	// A point of the display on the front surface (and back).
	void displayToSurface(int &x, int &y) const {
		if (splitHeight > 0 && y >= kScreenH - splitHeight)
			y += splitStart - (kScreenH - splitHeight);
		else {
			x += viewX;
			y += viewY;
		}
	}

	// --- sprite operation parameters (set by opcodes, then spriteOperation)
	int16_t spriteLeft = 0, spriteTop = 0, spriteRight = 0, spriteBottom = 0;
	int16_t destSpriteX = 0, destSpriteY = 0;
	int16_t backColor = 0, frontColor = 0;
	int16_t transparency = 0;
	int16_t sourceSurface = 0, destSurface = 0;
	int16_t renderFlags = 0;
	int16_t backDeltaX = 0, backDeltaY = 0;

	// --- text
	static constexpr int kFontCount = 16;
	std::array<std::optional<Font>, kFontCount> fonts;
	uint16_t fontIndex = 0;
	std::string textToPrint;
	const Font *font(int index) const {
		return index >= 0 && index < kFontCount && fonts[index] ? &*fonts[index] : nullptr;
	}

	void spriteOperation(int operation, const ResourceLoader &resources);

	// Draws a string as the text operation does (Font::drawString). When it
	// goes to the front or back surface and recordText is on, every glyph
	// cell is recorded for sharp text (below).
	int drawText(Surface &dst, const Font &f, const std::string &s, int x, int y, uint8_t front, uint8_t back,
	             bool transparent);

	// --- sharp text (docs/hdpacks.md, "Sharp text"). Each glyph cell drawn
	// on the screen surfaces is remembered with the pixels it left behind and
	// the background under its ink. A front end can draw a cell's glyph at
	// window resolution exactly while the front surface still holds those
	// pixels: whatever overwrites the cell later (a fill, a sprite, a
	// capture restore, other text) makes the check fail and the native
	// pixels show again; a restore that brings the very same pixels back
	// brings the sharp glyph back too.
	static constexpr int kCellW = 8, kCellH = 16; // the largest glyphs recorded (Goblins 3's fonts are 8x10)
	struct TextCell {
		int16_t x = 0, y = 0; // screen position of the cell
		uint8_t w = 0, h = 0; // glyph size, at most kCellW x kCellH
		uint8_t front = 0;    // ink colour (palette index)
		Font::Rows ink{};     // row y: bit x set where the glyph has ink (Font::glyphRows)
		std::array<uint8_t, kCellW * kCellH> expect{}; // the cell's pixels as drawn (rows of 8)
		std::array<uint8_t, kCellW * kCellH> clean{};  // the background under the text
		bool pure = true;                 // the cell shows only this glyph over its background
		uint32_t serial = 0;              // drawing order
		bool inked(int x, int y) const { return (ink[size_t(y)] >> x) & 1; }
	};
	static constexpr size_t kMaxTextCells = 2048;
	bool recordText = false;
	const std::vector<TextCell> &textCells() const { return _textCells; }
	// Copies the cells the front surface shows exactly as drawn (and pure).
	void visibleText(std::vector<TextCell> &out) const;
	void clearText() { _textCells.clear(); }

	// --- HD compositing (docs/hdpacks.md, "The HD compositor"). With an HD
	// scale set, every sprite carries an HdLayer at that scale that its
	// drawing operations keep in step, and every picture decoded from a
	// resource (decodePicture) goes through pictureHook, which can give it
	// its HD version before it is drawn.
	using PictureHook = std::function<void(Surface &picture, int resId, bool transparent, PictureUse use)>;
	PictureHook pictureHook;
	void setHdScale(int scale); // 0: no layers
	int hdScale() const { return _hdScale; }
	// Decodes sprite resource `resId` (`res`) into a new surface; null if it
	// isn't a sprite. `transparent`: drawn with colour 0 transparent.
	std::unique_ptr<Surface> decodePicture(const Resource &res, int resId, bool transparent, PictureUse use);

	// --- pixel sources (docs/hdpacks.md, "Switching packs"). With tracking
	// on, every sprite records where its pixels came from (gfx/pixelsource.h)
	// and every decoded picture is registered: its script file (scriptName),
	// resource id, transparency and original pixels. A compositor can then
	// rebuild all layers for another pack or scale at any moment.
	struct PictureSource {
		std::string tot;
		int resId = 0;
		bool transparent = false;
		PictureUse use = PictureUse::Screen;
		int width = 0, height = 0;
		std::vector<uint8_t> pixels;
	};
	std::function<std::string()> scriptName; // the running script file (set by the interpreter)
	void setTrackSources(bool on);
	bool trackingSources() const { return _trackSources; }
	// Picture `id` (1-based), or null.
	const PictureSource *pictureSource(int id) const {
		return id >= 1 && size_t(id) <= _pictures.size() ? &_pictures[size_t(id) - 1] : nullptr;
	}
	// Every surface (front, back and the sprites), each once.
	std::vector<Surface *> surfaces();
	// Sets the HD scale and rebuilds every sprite's layer from its pixel
	// sources (Surface::rebuildHd); `picture` gives a registered picture's
	// layer at that scale. Returns the pixels that had to be made live.
	size_t rebuildHd(int scale, const std::function<const HdLayer *(int pic)> &picture,
	                 const std::vector<uint8_t> *only = nullptr);

	// --- surfaces
	std::shared_ptr<Surface> &sprite(int index);
	void createSprite(int index, int width, int height);
	void freeSprite(int index);
	Surface &front() { return *_front; }
	Surface &back() { return *_back; }

	// --- invalidation
	void invalidateRect(int left, int top, int right, int bottom);
	void dirtiedRect(int surface, int left, int top, int right, int bottom);
	void blitInvalidated();
	void forceBlit(bool backwards = false);
	// Forgets the invalidated rectangles (the front surface was redrawn as a
	// whole).
	void clearInvalidated() {
		_invalidated.clear();
		_noInvalidated = true;
	}

	// --- cursor (docs/graphics.md 5). The cursor strip is sprite 23: one
	// cursorWidth-wide cell per frame. Drawn as an overlay at present time.
	int cursorWidth = 16, cursorHeight = 16;
	int cursorIndex = 0;     // logical cursor shown; -1 has special meaning
	int cursorAnim = 0;      // current animation frame
	uint32_t cursorTimeKey = 0;
	std::array<int8_t, 40> cursorAnimLow{}, cursorAnimHigh{}, cursorAnimDelays{};
	int32_t cursorHotspotXVar = -1, cursorHotspotYVar = -1;
	uint8_t showCursor = 0;  // bit 0: inside animateCursor; bit 1: visible
	int cursorFrame = 0;     // frame currently installed
	int cursorHotX = 0, cursorHotY = 0;

	Surface *cursorStrip() { auto &s = sprite(draw::kCursorSurface); return s.get(); }
	bool cursorVisible() const { return (showCursor & 6) != 0; }
	// Version 1 (Draw_v1): the cursor shows the hotspot's cursor cell, never
	// an animation frame, and blitCursor hides a cursor animateCursor didn't
	// install (an index with no animation).
	bool v1Cursor = false;
	void blitCursor() {
		if (!cursorStrip() || cursorIndex == -1)
			return;
		if (v1Cursor) {
			if (showCursor == 2)
				showCursor = 0;
			return;
		}
		showCursor = uint8_t((showCursor & ~2) | ((showCursor & 1) << 1));
	}
	bool anythingInvalidated() const { return !_noInvalidated; }

	// --- palette
	VgaPalette vgaPalette{};
	// The last palette that was not all black when it was applied or faded
	// to (what the room's pictures look like at full brightness).
	VgaPalette litPalette{};
	bool applyPal = false;
	bool paletteCleared = false;
	std::vector<VgaPalette> paletteStack;

	void setPalette();   // screen palette := vgaPalette
	void clearPalette(); // screen palette := black
	const Palette &screenPalette() const { return _screenPal; }
	const VgaPalette &screenVga() const { return _screenVga; }

	// One fade step toward `target` by `step` (6-bit units); returns true
	// once the screen palette matches the target.
	bool fadeStep(const VgaPalette &target, int step);

private:
	static Palette toRgb(const VgaPalette &vga);
	int surfaceIndex(int index) const { return index >= 100 ? index - 80 : index; }

	std::array<std::shared_ptr<Surface>, draw::kSpriteCount> _sprites;
	std::shared_ptr<Surface> _front;
	std::shared_ptr<Surface> _back;

	// The composed display (screenSurface) and what it was composed from.
	std::shared_ptr<Surface> _view;
	bool _viewValid = false;
	int _viewFromX = 0, _viewFromY = 0, _viewSplit = -1;
	const Surface *_viewFront = nullptr;
	void composeView(int left, int top, int right, int bottom); // a front rectangle (inclusive) into the view

	struct Rect { int left, top, right, bottom; };
	std::vector<Rect> _invalidated;
	bool _noInvalidated = true;

	int _hdScale = 0;
	void attachLayer(Surface &s);

	bool _trackSources = false;
	std::vector<PictureSource> _pictures;
	int registerPicture(const Surface &pic, int resId, bool transparent, PictureUse use);

	std::vector<TextCell> _textCells;
	uint32_t _textSerial = 0;
	void recordCell(TextCell cell, const std::array<uint8_t, kCellW * kCellH> &before, bool transparent,
	                uint8_t back);

	VgaPalette _screenVga{};
	Palette _screenPal{};
};

} // namespace gob
