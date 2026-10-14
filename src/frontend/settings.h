// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// User settings, persisted as settings.ini next to the save games. Keys the
// current build doesn't know are kept when the file is rewritten.

#pragma once

#include "audio/mixer.h"
#include "engine/hdrender.h"
#include "formats/tot.h"
#include "frontend/ini.h"
#include "frontend/presenter.h"

#include <string>

namespace gob {

struct Settings {
	// [video]
	bool fullscreen = false;
	int windowScale = 3;        // 2..6, window size in multiples of the (aspect-corrected) image
	bool integerScaling = true; // only whole multiples of the image size
	bool aspectCorrection = true; // show 320x200 as 4:3 (pixels 6:5 tall)
	FilterSettings filters;       // post-processing filter and its parameters
	bool sharpText = true;        // the game's text drawn at window resolution (sharp_text)
	std::string hdPack;           // HD pack folder name in HDPacks/, empty = none (hd_pack)
	HdSpriteFilter hdSpriteFilter = HdSpriteFilter::Original; // pictures without HD art (hd_sprite_filter)
	bool hdRoomsOnly = false;     // only room backgrounds and overlays from the pack (hd_pack_scope = rooms)
	bool hdSmoothEdges = false;   // HD pictures' transparent edges smoothed, not exact (hd_smooth_edges)

	// [audio] volumes in percent
	int masterVolume = 100, musicVolume = 100, effectsVolume = 100, speakerVolume = 100;
	int speechVolume = 100; // Goblins 3
	bool speech = true;     // Goblins 3: the talking heads speak (subtitles always show)
	bool mute = false;
	Mixer::EffectsQuality effectsQuality = Mixer::EffectsQuality::Enhanced; // effects_quality
	bool musicFullLength = false; // replacement music plays to its end (music_full_length)

	// [game]
	Language language = Language::American;
	double speed = 1.0;      // game speed (the overlay offers 1, 1.2, 1.5 and 2)
	int turboSpeed = 4;      // speed while the turbo key (Tab) is held
	bool cutscenesAtNormalSpeed = true; // intro and ending always at 1x

	static constexpr int kMinScale = 2, kMaxScale = 6;
	static constexpr double kMinSpeed = 0.5, kMaxSpeed = 8.0;
	static constexpr int kMinTurbo = 2, kMaxTurbo = 8;
	static constexpr double kMinGamma = 1.6, kMaxGamma = 3.0;

	static const char *spriteFilterName(HdSpriteFilter f);
	static bool parseSpriteFilter(const std::string &name, HdSpriteFilter &out);

	bool load(const std::string &path);
	bool save(const std::string &path);

	Mixer::Volumes mixerVolumes() const;

private:
	Ini _ini; // the file as loaded, so unknown keys are written back
};

} // namespace gob
