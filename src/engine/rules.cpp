// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/rules.h"

#include <cctype>
#include <cstring>

namespace gob {

namespace {

bool contains(const std::vector<const char *> &names, const std::string &tot) {
	for (const char *n : names)
		if (tot == n)
			return true;
	return false;
}

// Gobliiins: the intro (with the title and credits) and the ending are
// cutscenes; INTER, the picture shown before each level (and the password
// and game over screens), is the transition fast-forwarding runs through.
// The game has no saves (progress is kept by access codes).
const GameRules kGob1Rules{
	{"INTRO", "FINAL"},
	{"INTER"},
	LevelSystem::Gob1,
	SaveFormat::None,
	{},
	{},
	{},
	nullptr,
	{},
};

const GameRules kGob2Rules{
	{"INTRO", "INTRO0", "FINAL"},
	{"INTER", "MENU"},
	LevelSystem::Gob2,
	SaveFormat::Gob2,
	// INTRO0 calls the colour-code question from these two places.
	{{"INTRO0", 1722}, {"INTRO0", 1746}},
	// From ScummVM: some versions leave the MOVEMENT menu item selectable
	// in the dreamland room (GOB06), which crashes when it is clicked.
	{{"GOB06", -1, 7}},
	{},
	nullptr,
	{},
};

// Goblins Quest 3: DEMO (the intro video and the title) and FINAL are
// cutscenes; the INTER intermissions between rooms are what fast-forwarding
// runs through. MENU runs the rooms; speech in a language without its
// archive is the English speech.
const GameRules kGob3Rules{
	{"DEMO", "FINAL"},
	{"INTER0", "INTER2", "INTER3", "INTER4", "INTER7", "INTER8", "INTER9", "INTER12", "INTER13", "INTER14"},
	LevelSystem::Gob3,
	SaveFormat::Gob3,
	{},
	{},
	{"FRGOB3.ITK", "DEGOB3.ITK", "USGOB3.ITK", "ITGOB3.ITK"},
	"USGOB3.ITK",
	{{"ECHEQUI1", "ECHIQUI1"}, {"ECHEQUI2", "ECHIQUI2"}},
};

} // namespace

bool GameRules::isCutsceneTot(const std::string &tot) const {
	return contains(cutsceneTots, tot);
}

bool GameRules::isTransitionTot(const std::string &tot) const {
	return contains(transitionTots, tot);
}

bool GameRules::isCopyProtectionCall(const std::string &tot, uint16_t offset) const {
	for (const CallSkip &c : copyProtectionCalls)
		if (c.offset == offset && tot == c.tot)
			return true;
	return false;
}

bool GameRules::ignoresSwitchTotSub(const std::string &tot, int16_t index, int16_t function) const {
	for (const SwitchTotSkip &s : ignoredSwitchTotSubs)
		if (s.index == index && s.function == function && tot == s.tot)
			return true;
	return false;
}

bool GameRules::isVoiceArchive(const std::string &archive) const {
	std::string name = archive;
	for (char &c : name)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return contains(voiceArchives, name);
}

const char *GameRules::archiveFallback(const std::string &archive) const {
	if (!isVoiceArchive(archive) || !voiceFallback)
		return nullptr;
	std::string name = archive;
	for (char &c : name)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return name == voiceFallback ? nullptr : voiceFallback;
}

std::string GameRules::cdTrackName(const std::string &name) const {
	for (const TrackRename &r : trackRenames) {
		size_t n = std::strlen(r.script);
		bool same = name.size() == n;
		for (size_t i = 0; same && i < n; i++)
			same = std::toupper(static_cast<unsigned char>(name[i])) == r.script[i];
		if (same)
			return r.track;
	}
	return name;
}

const GameRules &gameRules(GameId id) {
	switch (id) {
	case GameId::Gob1: return kGob1Rules;
	case GameId::Gob3: return kGob3Rules;
	default: return kGob2Rules;
	}
}

} // namespace gob
