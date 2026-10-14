// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace gob {

namespace {

int percent(const Ini &ini, const char *key, int def) {
	return std::clamp(ini.getInt("audio", key, def), 0, 100);
}

int videoInt(const Ini &ini, const char *key, int def, int lo, int hi) {
	return std::clamp(ini.getInt("video", key, def), lo, hi);
}

} // namespace

const char *Settings::spriteFilterName(HdSpriteFilter f) {
	switch (f) {
	case HdSpriteFilter::Scale2x: return "scale2x";
	case HdSpriteFilter::SmoothEdges: return "smooth-edges";
	case HdSpriteFilter::Smooth: return "smooth";
	default: return "original";
	}
}

bool Settings::parseSpriteFilter(const std::string &name, HdSpriteFilter &out) {
	for (HdSpriteFilter f : {HdSpriteFilter::Original, HdSpriteFilter::Scale2x, HdSpriteFilter::SmoothEdges,
	                         HdSpriteFilter::Smooth})
		if (name == spriteFilterName(f)) {
			out = f;
			return true;
		}
	return false;
}

bool Settings::load(const std::string &path) {
	if (!_ini.load(path))
		return false;
	fullscreen = _ini.getBool("video", "fullscreen", fullscreen);
	windowScale = std::clamp(_ini.getInt("video", "scale", windowScale), kMinScale, kMaxScale);
	integerScaling = _ini.getBool("video", "integer_scaling", integerScaling);
	aspectCorrection = _ini.getBool("video", "aspect_correction", aspectCorrection);
	sharpText = _ini.getBool("video", "sharp_text", sharpText);
	hdPack = _ini.get("video", "hd_pack");
	if (!parseSpriteFilter(_ini.get("video", "hd_sprite_filter"), hdSpriteFilter) &&
	    _ini.getBool("video", "hd_native_smooth", false)) // the setting before hd_sprite_filter
		hdSpriteFilter = HdSpriteFilter::Scale2x;
	hdRoomsOnly = _ini.get("video", "hd_pack_scope") == "rooms";
	hdSmoothEdges = _ini.getBool("video", "hd_smooth_edges", hdSmoothEdges);
	FilterSettings &f = filters;
	parseFilter(_ini.get("video", "filter"), f.filter);
	f.softness = videoInt(_ini, "softness", f.softness, 0, 100);
	f.xbrStrength = videoInt(_ini, "xbr_strength", f.xbrStrength, 0, 100);
	f.scanlines = videoInt(_ini, "crt_scanlines", f.scanlines, 0, 100);
	f.crtSharpness = videoInt(_ini, "crt_sharpness", f.crtSharpness, 0, 100);
	parseCrtMask(_ini.get("video", "crt_mask"), f.mask);
	f.maskStrength = videoInt(_ini, "crt_mask_strength", f.maskStrength, 0, 100);
	f.bloom = videoInt(_ini, "crt_bloom", f.bloom, 0, 100);
	f.curvature = videoInt(_ini, "crt_curvature", f.curvature, 0, 100);
	f.vignette = videoInt(_ini, "crt_vignette", f.vignette, 0, 100);
	f.brightness = videoInt(_ini, "crt_brightness", f.brightness, 50, 200);
	if (std::string v = _ini.get("video", "crt_gamma"); !v.empty())
		if (double d = std::strtod(v.c_str(), nullptr); d > 0)
			f.gamma = std::clamp(d, kMinGamma, kMaxGamma);
	masterVolume = percent(_ini, "master", masterVolume);
	musicVolume = percent(_ini, "music", musicVolume);
	effectsVolume = percent(_ini, "effects", effectsVolume);
	speakerVolume = percent(_ini, "speaker", speakerVolume);
	speechVolume = percent(_ini, "speech", speechVolume);
	speech = _ini.getBool("audio", "speech_on", speech);
	mute = _ini.getBool("audio", "mute", mute);
	if (std::string q = _ini.get("audio", "effects_quality"); q == "original")
		effectsQuality = Mixer::EffectsQuality::Original;
	else if (q == "enhanced")
		effectsQuality = Mixer::EffectsQuality::Enhanced;
	musicFullLength = _ini.getBool("audio", "music_full_length", musicFullLength);
	if (auto lang = parseLanguage(_ini.get("game", "language")))
		language = *lang;
	if (std::string v = _ini.get("game", "speed"); !v.empty())
		if (double d = std::strtod(v.c_str(), nullptr); d > 0)
			speed = std::clamp(d, kMinSpeed, kMaxSpeed);
	turboSpeed = std::clamp(_ini.getInt("game", "turbo_speed", turboSpeed), kMinTurbo, kMaxTurbo);
	cutscenesAtNormalSpeed = _ini.getBool("game", "cutscenes_normal_speed", cutscenesAtNormalSpeed);
	return true;
}

bool Settings::save(const std::string &path) {
	_ini.setBool("video", "fullscreen", fullscreen);
	_ini.setInt("video", "scale", windowScale);
	_ini.setBool("video", "integer_scaling", integerScaling);
	_ini.setBool("video", "aspect_correction", aspectCorrection);
	_ini.setBool("video", "sharp_text", sharpText);
	_ini.set("video", "hd_pack", hdPack);
	_ini.set("video", "hd_sprite_filter", spriteFilterName(hdSpriteFilter));
	_ini.set("video", "hd_pack_scope", hdRoomsOnly ? "rooms" : "all");
	_ini.setBool("video", "hd_smooth_edges", hdSmoothEdges);
	const FilterSettings &f = filters;
	_ini.set("video", "filter", filterName(f.filter));
	_ini.setInt("video", "softness", f.softness);
	_ini.setInt("video", "xbr_strength", f.xbrStrength);
	_ini.setInt("video", "crt_scanlines", f.scanlines);
	_ini.setInt("video", "crt_sharpness", f.crtSharpness);
	_ini.set("video", "crt_mask", crtMaskName(f.mask));
	_ini.setInt("video", "crt_mask_strength", f.maskStrength);
	_ini.setInt("video", "crt_bloom", f.bloom);
	_ini.setInt("video", "crt_curvature", f.curvature);
	_ini.setInt("video", "crt_vignette", f.vignette);
	_ini.setInt("video", "crt_brightness", f.brightness);
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%.2f", f.gamma);
	_ini.set("video", "crt_gamma", buf);
	_ini.setInt("audio", "master", masterVolume);
	_ini.setInt("audio", "music", musicVolume);
	_ini.setInt("audio", "effects", effectsVolume);
	_ini.setInt("audio", "speaker", speakerVolume);
	_ini.setInt("audio", "speech", speechVolume);
	_ini.setBool("audio", "speech_on", speech);
	_ini.setBool("audio", "mute", mute);
	_ini.set("audio", "effects_quality", effectsQuality == Mixer::EffectsQuality::Original ? "original" : "enhanced");
	_ini.setBool("audio", "music_full_length", musicFullLength);
	_ini.set("game", "language", languageName(language));
	std::snprintf(buf, sizeof(buf), "%g", speed);
	_ini.set("game", "speed", buf);
	_ini.setInt("game", "turbo_speed", turboSpeed);
	_ini.setBool("game", "cutscenes_normal_speed", cutscenesAtNormalSpeed);
	return _ini.save(path);
}

Mixer::Volumes Settings::mixerVolumes() const {
	Mixer::Volumes v;
	v.master = mute ? 0.0f : masterVolume / 100.0f;
	v.music = musicVolume / 100.0f;
	v.effects = effectsVolume / 100.0f;
	v.speaker = speakerVolume / 100.0f;
	v.speech = speechVolume / 100.0f;
	return v;
}

} // namespace gob
