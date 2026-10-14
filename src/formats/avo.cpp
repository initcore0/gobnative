// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// .AVO parser (docs/goblins-v1.md section 1). The order and the skipped
// byte counts follow ScummVM's Map_v1::loadMapObjects (map_v1.cpp:64-140).

#include "formats/avo.h"

namespace gob {

namespace {

// Little-endian reader that records reads past the end instead of failing
// on the spot, so the parser reads straight through like the original.
class Reader {
public:
	explicit Reader(const std::vector<uint8_t> &d) : _d(d) {}

	size_t pos() const { return _pos; }
	void seek(size_t p) { _pos = p; }
	void skip(size_t n) { _pos += n; }
	bool overrun() const { return _overrun; }

	uint8_t u8() {
		if (_pos >= _d.size()) {
			_overrun = true;
			_pos++;
			return 0;
		}
		return _d[_pos++];
	}
	uint16_t u16() { uint16_t v = u8(); return uint16_t(v | (u8() << 8)); }
	uint32_t u32() { uint32_t v = u16(); return v | (uint32_t(u16()) << 16); }

private:
	const std::vector<uint8_t> &_d;
	size_t _pos = 0;
	bool _overrun = false;
};

bool fail(std::string *error, const std::string &why) {
	if (error)
		*error = why;
	return false;
}

// Map_v1::loadGoblins / loadObjects: `count` objects, each taking its
// position record from `posTable` (advanced per object), then its state
// pointer table, 160 unused bytes, the mult object index and a pad byte,
// then a 30-byte record for every non-null state pointer.
bool readObjects(Reader &r, size_t &posTable, int count, std::vector<AvoObject> &out) {
	out.resize(size_t(count));
	for (AvoObject &o : out) {
		size_t back = r.pos();
		r.seek(posTable);
		o.xPos = int16_t(r.u16());
		o.yPos = int16_t(r.u16());
		o.order = int16_t(r.u16());
		o.state = int16_t(r.u16());
		posTable = r.pos();
		r.seek(back);

		uint32_t present[AvoObject::kStates][AvoObject::kColumns];
		for (auto &row : present)
			for (uint32_t &p : row)
				p = r.u32();
		r.skip(160);
		o.multObjIndex = r.u8();
		r.skip(1);

		for (int s = 0; s < AvoObject::kStates; s++)
			for (int c = 0; c < AvoObject::kColumns; c++) {
				if (present[s][c] == 0)
					continue;
				AvoState st;
				st.animation = int16_t(r.u16());
				st.layer = int16_t(r.u16());
				r.skip(8);
				st.unk0 = int16_t(r.u16());
				st.unk1 = int16_t(r.u16());
				r.skip(2);
				if (r.u32() == 0) {
					r.skip(2);
					st.sndItem = -1;
				} else {
					st.sndItem = int16_t(r.u16());
				}
				st.freq = int16_t(r.u16());
				st.repCount = int16_t(r.u16());
				st.sndFrame = int16_t(r.u16());
				o.states[size_t(s)][size_t(c)] = st;
			}
	}
	return !r.overrun();
}

} // namespace

int AvoObject::stateCount() const {
	int n = 0;
	for (const auto &row : states)
		for (const auto &c : row)
			n += c.has_value();
	return n;
}

std::optional<AvoFile> parseAvo(const std::vector<uint8_t> &data, std::string *error) {
	AvoFile f;
	f.size = data.size();
	Reader r(data);

	for (int8_t &p : f.pass)
		p = int8_t(r.u8());
	for (int16_t &i : f.items)
		i = int8_t(r.u8());
	for (AvoFile::WayPoint &w : f.wayPoints) {
		w.x = int16_t(r.u16());
		w.y = int16_t(r.u16());
	}
	for (AvoFile::ItemPos &p : f.itemPoses) {
		p.x = int8_t(r.u8());
		p.y = int8_t(r.u8());
		p.orient = int8_t(r.u8());
	}

	r.skip(32 + 76 + 4 + 20);
	for (int &n : f.nameCounts) {
		n = r.u16();
		r.skip(size_t(n) * 14);
	}

	int sounds = r.u16();
	if (sounds > AvoFile::kMaxSounds)
		return fail(error, std::to_string(sounds) + " sounds (at most 20)"), std::nullopt;
	for (int i = 0; i < sounds; i++) {
		std::string name;
		bool ended = false; // NUL-terminated, the rest of the field is padding
		for (int k = 0; k < 14; k++) {
			uint8_t c = r.u8();
			ended = ended || c == 0;
			if (!ended)
				name += char(c);
		}
		f.sounds.push_back(name);
	}

	r.skip(4 + 24);
	f.gobPosCount = r.u16();
	f.objPosCount = r.u16();
	size_t gobsPos = r.pos();
	r.skip(size_t(f.gobPosCount) * 8);
	size_t objsPos = r.pos();
	r.skip(size_t(f.objPosCount) * 8);

	int gobs = r.u16();
	if (gobs > AvoFile::kMaxGoblins)
		return fail(error, std::to_string(gobs) + " goblins (at most 4)"), std::nullopt;
	if (!readObjects(r, gobsPos, gobs, f.goblins))
		return fail(error, "goblin tables run past the end"), std::nullopt;

	int objs = r.u16();
	if (objs > AvoFile::kMaxObjects)
		return fail(error, std::to_string(objs) + " objects (at most 20)"), std::nullopt;
	if (!readObjects(r, objsPos, objs, f.objects))
		return fail(error, "object tables run past the end"), std::nullopt;

	f.extraRecords = r.u16();
	for (int i = 0; i < f.extraRecords; i++) {
		r.skip(30);
		uint32_t flag = r.u32();
		r.skip(56);
		if (flag != 0)
			r.skip(30);
	}

	r.skip(50);
	f.hasItemToObject = r.u16() != 0;
	if (f.hasItemToObject) {
		r.skip(1456);
		int count = r.u16();
		if (count > AvoFile::kMaxObjects)
			return fail(error, std::to_string(count) + " item links (at most 20)"), std::nullopt;
		for (int i = 0; i < count; i++) {
			r.skip(20);
			f.itemToObject.push_back(int16_t(r.u16()));
			r.skip(5);
		}
	}
	if (r.overrun() || r.pos() > data.size()) // skips past the end count too
		return fail(error, "data ends early (" + std::to_string(data.size()) + " bytes, " +
		                       std::to_string(r.pos()) + " needed)"),
		       std::nullopt;
	f.consumed = r.pos();
	return f;
}

} // namespace gob
