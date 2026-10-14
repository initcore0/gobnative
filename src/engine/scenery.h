// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenery (docs/animation.md section 1): up to ten loaded animation
// resources, each a set of layers whose frames are composed from rectangular
// pieces cut out of "picture" sprites, and up to ten statics: layered
// backgrounds made of a back picture and pieces drawn in plane order.
// Gobliins 2 never uses statics; Gobliiins draws its rooms and mults with
// them.

#pragma once

#include "engine/draw.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace gob {

class Scenery {
public:
	using ResourceLoader = std::function<std::optional<Resource>(uint16_t)>;

	static constexpr int kAnimCount = 10;
	static constexpr int kMaxPictures = 7;

	struct PictureDesc {
		int16_t pieceTableId, width, height, spriteResId;
	};

	struct Piece {
		uint8_t pictByte;
		uint8_t pieceIndex;
		int8_t destX, destY;
		int8_t notFinal;
	};

	struct Layer {
		int16_t unknown0 = 0, posX = 0, posY = 0, animDeltaX = 0, animDeltaY = 0;
		int8_t transp = 0;
		int16_t framesCount = 0;
		std::vector<Piece> pieces;
		std::vector<size_t> frameStart; // index of each frame's first piece
	};

	struct PieceRect { int16_t left, right, top, bottom; };

	struct Anim {
		int pictCount = 0; // 0 = slot free
		int resId = -1;
		std::vector<Layer> layers;
		std::vector<std::vector<PieceRect>> pieces; // per picture
		std::array<int, kMaxPictures> pictToSprite{};
		std::array<int, kMaxPictures> pictResId{}; // sprite resource of each picture (the animation sheet)
	};

	// A static's layer: a back picture and planes of pieces.
	struct StaticPlane {
		uint8_t pictIndex, pieceIndex, drawOrder;
		int16_t destX, destY;
		int8_t transp;
	};
	struct StaticLayer {
		int16_t backResId = -1;
		std::vector<StaticPlane> planes;
	};
	struct Static {
		int pictCount = -1; // -1 = slot free
		int resId = -1;
		std::vector<StaticLayer> layers;
		std::vector<std::vector<PieceRect>> pieces; // per picture
		std::array<int, kMaxPictures> pictToSprite{};
	};

	explicit Scenery(Draw &draw);

	// Planes are drawn in drawOrder 0 .. maxDrawOrder - 1 (40 in Gobliins 2,
	// 100 in Gobliiins).
	int maxDrawOrder = 40;
	// The static (or 10000 + count) and layer that updateStatic(order)
	// redraws over objects; the high bytes can name a second one.
	int16_t curStatic = -1, curStaticLayer = -1;

	// Mult area: clipping window for flag 2 (set by initMult).
	int areaLeft = 0, areaTop = 0, areaWidth = 320, areaHeight = 200;

	// Bounds output of updateAnim (and its flag-4 clip window input).
	int16_t toRedrawLeft = 0, toRedrawTop = 0, toRedrawRight = 0, toRedrawBottom = 0;
	// Rectangle of the first piece drawn by the last measuring updateAnim.
	int16_t pieceLeft = 0, pieceTop = 0, pieceRight = 0, pieceBottom = 0;

	// Returns the slot already holding `resId`, or -1.
	int findAnim(int resId) const;
	int lowestFreeSlot() const;
	bool resourceExists(int resId, const ResourceLoader &res) const { return res(uint16_t(resId)).has_value(); }

	// Loads an animation into `slot`. Returns false if the resource is missing.
	bool loadAnim(int slot, int resId, const std::vector<PictureDesc> &pics, const ResourceLoader &res);
	void freeAnim(int slot);
	void reset(); // frees everything (on TOT load)

	// Statics. findStatic returns the slot holding `resId` or -1, and
	// lowers `index` to the lowest free slot below it.
	int findStatic(int resId, int &index) const;
	// Loads static `resId` into `slot`; `backs` gives each layer's back
	// picture. Returns false if the resource is missing.
	bool loadStatic(int slot, int resId, const std::vector<int16_t> &backs, const std::vector<PictureDesc> &pics,
	                const ResourceLoader &res);
	void freeStatic(int slot);
	int staticPictCount(int slot) const { return slot >= 0 && slot < kAnimCount ? _statics[slot].pictCount : -1; }
	int staticLayersCount(int slot) const {
		return slot >= 0 && slot < kAnimCount ? int(_statics[slot].layers.size()) : 0;
	}
	// Draws layer `layer` of static `slot` into the back surface: its back
	// picture, then its planes in order.
	void renderStatic(int slot, int layer, const ResourceLoader &res);
	// Redraws the planes of order >= `orderFrom` of the current static(s)
	// inside the toRedraw* window (over objects that moved).
	void updateStatic(int orderFrom);
	void updateStatic(int orderFrom, int slot, int layer);

	const Anim &anim(int slot) const { return _anims[slot]; }
	int layersCount(int slot) const { return slot >= 0 && slot < kAnimCount ? int(_anims[slot].layers.size()) : 0; }
	const Layer *layer(int slot, int layer) const;

	// Composes frame `frame` of `layer` of anim `slot` at (dx, dy) (1000 =
	// the layer's own position), drawing to the back surface if `doDraw`,
	// and/or computing toRedraw* bounds. Flags: 2 clip to the mult area,
	// 4 clip to the preset toRedraw* window (bounds not recomputed).
	void updateAnim(int layer, int frame, int slot, int flags, int dx, int dy, bool doDraw);

private:
	// The sprite holding picture `pic` (shared by resource id between
	// animations and statics, allocated downward from sprite 19).
	int acquireSprite(const PictureDesc &pic, const ResourceLoader &res);
	void releaseSprite(int sprite);
	static std::vector<PieceRect> loadPieces(int resId, const ResourceLoader &res);

	Draw &_draw;
	std::array<Anim, kAnimCount> _anims;
	std::array<Static, kAnimCount> _statics;
	std::array<int, 20> _spriteRefs{};
	std::array<int, 20> _spriteResId{};
};

} // namespace gob
