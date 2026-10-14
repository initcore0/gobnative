// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formats/anim.h"

#include <algorithm>

namespace gob {

namespace {

int le16s(const std::vector<uint8_t> &d, size_t o) {
	return o + 1 < d.size() ? int(int16_t(d[o] | (d[o + 1] << 8))) : 0;
}

} // namespace

std::optional<AnimResourceData> parseAnimResource(const std::vector<uint8_t> &d) {
	if (d.size() < 2)
		return std::nullopt;
	AnimResourceData out;
	const int layersCount = le16s(d, 0);
	if (layersCount < 0 || size_t(2 + layersCount * 2) > d.size())
		return std::nullopt;
	for (int i = 0; i < layersCount; i++) {
		const size_t off = uint16_t(le16s(d, 2 + size_t(i) * 2));
		AnimLayerData l;
		l.posX = le16s(d, off + 2);
		l.posY = le16s(d, off + 4);
		l.deltaX = le16s(d, off + 6);
		l.deltaY = le16s(d, off + 8);
		l.transparent = off + 10 < d.size() && d[off + 10] != 0;
		const int framesCount = std::max(0, le16s(d, off + 11));
		size_t p = off + 13;
		for (int f = 0; f < framesCount; f++) {
			std::vector<AnimFramePiece> frame;
			while (p + 5 <= d.size()) {
				const uint8_t pictByte = d[p];
				const int8_t dx = int8_t(d[p + 2]), dy = int8_t(d[p + 3]), notFinal = int8_t(d[p + 4]);
				// High bits of pictByte extend the 8-bit offsets by multiples
				// of 128, growing their magnitude (Scenery::updateAnim).
				const int hx = (pictByte >> 6) & 3, hy = (pictByte >> 4) & 3;
				AnimFramePiece pc;
				pc.picture = (pictByte & 15) - 1;
				pc.pieceIndex = d[p + 1];
				pc.destX = dx >= 0 ? dx + hx * 128 : dx - hx * 128;
				pc.destY = dy >= 0 ? dy + hy * 128 : dy - hy * 128;
				frame.push_back(pc);
				p += 5;
				if (notFinal != 1)
					break;
			}
			l.frames.push_back(std::move(frame));
		}
		out.layers.push_back(std::move(l));
	}
	return out;
}

std::vector<AnimPieceRect> parsePieceTable(const std::vector<uint8_t> &d) {
	std::vector<AnimPieceRect> out;
	for (size_t o = 0; o + 8 <= d.size(); o += 8) {
		AnimPieceRect r;
		r.left = le16s(d, o);
		r.right = le16s(d, o + 2);
		r.top = le16s(d, o + 4);
		r.bottom = le16s(d, o + 6);
		r.valid = !(r.left == -1 && r.right == -1 && r.top == -1 && r.bottom == -1) && r.left <= r.right &&
		          r.top <= r.bottom;
		out.push_back(r);
	}
	return out;
}

} // namespace gob
