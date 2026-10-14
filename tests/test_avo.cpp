// gob2native - .AVO parser tests with synthetic level files (no game data)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "formats/avo.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace gob;

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

namespace {

struct Writer {
	std::vector<uint8_t> d;
	void u8(int v) { d.push_back(uint8_t(v)); }
	void u16(int v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
	void u32(uint32_t v) { u16(int(v & 0xFFFF)); u16(int(v >> 16)); }
	void fill(size_t n, int v = 0xEE) { d.insert(d.end(), n, uint8_t(v)); }
	void name(const std::string &s) { // a 14-byte field
		for (size_t i = 0; i < 14; i++)
			u8(i < s.size() ? s[i] : 0);
	}
};

struct StateSpec { int state, column, animation, layer, sndItem, freq, rep, frame; };

// One object's tables: the state pointer table (any non-zero value marks a
// state), 160 unused bytes, the mult object index, a pad byte, then the
// 30-byte state records in state order.
void writeObject(Writer &w, int multObj, const std::vector<StateSpec> &states) {
	for (int s = 0; s < 40; s++)
		for (int c = 0; c < 6; c++) {
			bool present = false;
			for (const StateSpec &st : states)
				present = present || (st.state == s && st.column == c);
			w.u32(present ? 0x12340000u + uint32_t(s * 6 + c) : 0);
		}
	w.fill(160);
	w.u8(multObj);
	w.u8(0xEE);
	for (int s = 0; s < 40; s++)
		for (int c = 0; c < 6; c++)
			for (const StateSpec &st : states) {
				if (st.state != s || st.column != c)
					continue;
				w.u16(st.animation);
				w.u16(st.layer);
				w.fill(8);
				w.u16(100 + s); // unk0
				w.u16(200 + s); // unk1
				w.fill(2);
				if (st.sndItem < 0) {
					w.u32(0);
					w.u16(0xEEEE);
				} else {
					w.u32(0xABCD0000u);
					w.u16(st.sndItem);
				}
				w.u16(st.freq);
				w.u16(st.rep);
				w.u16(st.frame);
			}
}

// A small level: `gobPos` goblin position records (0 makes the goblins
// read the object records, as in three real levels).
std::vector<uint8_t> buildAvo(int gobPos, size_t trailing) {
	Writer w;
	for (int i = 0; i < 26 * 28; i++)
		w.u8(i % 7 == 0 ? 1 : (i % 11 == 0 ? 3 : 0)); // pass map
	for (int i = 0; i < 26 * 28; i++)
		w.u8(i == 27 ? 0xFF : (i == 28 ? 0x05 : 0)); // items map: (1,1) = -1, (2,1) = 5
	for (int i = 0; i < 40; i++) {
		w.u16(i);
		w.u16(i < 3 ? 10 + i : 0xFFFF); // way points end with -1
	}
	for (int i = 0; i < 20; i++) {
		w.u8(i);
		w.u8(2 * i);
		w.u8(i == 1 ? 0xFC : 4); // orient -4 or 4
	}
	w.fill(32 + 76 + 4 + 20);
	w.u16(1); w.name("SCENE");
	w.u16(2); w.name("ANIM1"); w.name("ANIM2");
	w.u16(0);
	w.u16(2); w.name("PIOCHE"); w.name("FOURTEENCHARSX"); // the second fills its field
	w.fill(4 + 24);
	w.u16(gobPos);
	w.u16(3);
	for (int i = 0; i < gobPos; i++) { w.u16(10 * i + 1); w.u16(10 * i + 2); w.u16(i); w.u16(21); }
	for (int i = 0; i < 3; i++) { w.u16(100 + i); w.u16(200 + i); w.u16(9); w.u16(i); }

	w.u16(2); // goblins
	writeObject(w, 3, {{3, 0, 1, 12, -1, 0, 0, 0}, {5, 1, 2, 13, 7, 5000, 2, 4}});
	writeObject(w, 3, {{21, 0, 1, 40, 0x0102, 0x3040, 0x0101, 0x0905}});
	w.u16(3); // objects
	writeObject(w, 0, {{0, 0, 4, 1, -1, 0, 0, 0}});
	writeObject(w, 0, {});
	writeObject(w, 0, {{39, 5, 5, 2, 3, 8000, 1, 6}});

	w.u16(2); // records the engine skips
	w.fill(30); w.u32(0); w.fill(56);
	w.fill(30); w.u32(7); w.fill(56); w.fill(30);
	w.fill(50);
	w.u16(1); // the item table
	w.fill(1456);
	w.u16(3);
	for (int v : {-1, 2, 0}) {
		w.fill(20);
		w.u16(v);
		w.fill(5);
	}
	w.fill(trailing, 0);
	return w.d;
}

void testFull() {
	std::vector<uint8_t> data = buildAvo(2, 2);
	std::string err;
	auto f = parseAvo(data, &err);
	CHECK(f.has_value());
	if (!f) {
		std::printf("  error: %s\n", err.c_str());
		return;
	}
	CHECK(f->size == data.size());
	CHECK(f->consumed == data.size() - 2);
	CHECK(f->passAt(0, 0) == 1);
	CHECK(f->passAt(11, 0) == 3);
	CHECK(f->passAt(7, 0) == 1);
	CHECK(f->passAt(1, 0) == 0);
	CHECK(f->itemAt(1, 1) == -1);
	CHECK(f->itemAt(2, 1) == 5);
	CHECK(f->itemAt(3, 1) == 0);
	CHECK(f->wayPoints[2].x == 2 && f->wayPoints[2].y == 12);
	CHECK(f->wayPoints[3].y == -1);
	CHECK(f->itemPoses[1].orient == -4 && f->itemPoses[1].y == 2);
	CHECK(f->itemPoses[19].x == 19 && f->itemPoses[19].orient == 4);
	CHECK(f->nameCounts[0] == 1 && f->nameCounts[1] == 2 && f->nameCounts[2] == 0);
	CHECK(f->sounds.size() == 2);
	if (f->sounds.size() == 2) {
		CHECK(f->sounds[0] == "PIOCHE");
		CHECK(f->sounds[1] == "FOURTEENCHARSX");
	}
	CHECK(f->gobPosCount == 2 && f->objPosCount == 3);

	CHECK(f->goblins.size() == 2);
	if (f->goblins.size() == 2) {
		const AvoObject &g0 = f->goblins[0], &g1 = f->goblins[1];
		CHECK(g0.xPos == 1 && g0.yPos == 2 && g0.order == 0 && g0.state == 21);
		CHECK(g1.xPos == 11 && g1.yPos == 12 && g1.order == 1);
		CHECK(g0.multObjIndex == 3);
		CHECK(g0.stateCount() == 2 && g1.stateCount() == 1);
		const auto &s3 = g0.states[3][0];
		CHECK(s3 && s3->animation == 1 && s3->layer == 12 && s3->sndItem == -1);
		CHECK(s3 && s3->unk0 == 103 && s3->unk1 == 203);
		const auto &s5 = g0.states[5][1];
		CHECK(s5 && s5->sndItem == 7 && s5->freq == 5000 && s5->repCount == 2 && s5->sndFrame == 4);
		CHECK(!g0.states[5][0]);
		const auto &s21 = g1.states[21][0];
		CHECK(s21 && s21->sndFrame == 0x0905 && s21->sndItem == 0x0102 && s21->freq == 0x3040);
	}
	CHECK(f->objects.size() == 3);
	if (f->objects.size() == 3) {
		CHECK(f->objects[0].xPos == 100 && f->objects[2].yPos == 202 && f->objects[1].state == 1);
		CHECK(f->objects[1].stateCount() == 0);
		const auto &s = f->objects[2].states[39][5];
		CHECK(s && s->animation == 5 && s->sndItem == 3 && s->sndFrame == 6);
	}
	CHECK(f->extraRecords == 2);
	CHECK(f->hasItemToObject);
	CHECK((f->itemToObject == std::vector<int16_t>{-1, 2, 0}));
}

// With no goblin position records the goblins take the object records,
// as Map_v1 reads them (AVT005, AVT019 and AVT021 do this).
void testNoGoblinPositions() {
	std::vector<uint8_t> data = buildAvo(0, 0);
	auto f = parseAvo(data);
	CHECK(f.has_value());
	if (!f)
		return;
	CHECK(f->consumed == data.size());
	CHECK(f->goblins.size() == 2 && f->goblins[0].xPos == 100 && f->goblins[1].yPos == 201);
	CHECK(f->objects[0].xPos == 100 && f->objects[0].state == 0);
}

void testTruncated() {
	std::vector<uint8_t> data = buildAvo(2, 0);
	for (size_t cut : {size_t(1), size_t(30), size_t(2000), data.size() - 100}) {
		std::vector<uint8_t> shorter(data.begin(), data.end() - long(cut));
		std::string err;
		CHECK(!parseAvo(shorter, &err).has_value());
		CHECK(!err.empty());
	}
	CHECK(!parseAvo({}).has_value());
}

} // namespace

int main() {
	testFull();
	testNoGoblinPositions();
	testTruncated();
	if (failures) {
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("avo: all tests passed\n");
	return 0;
}
