// gob2native - front end
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Overlay tabs for the user settings, game control and level select.

#include "engine/rules.h"
#include "frontend/overlay.h"
#include "frontend/settings.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace gob {

namespace {

// Languages the game ships text files for, in menu order.
struct LanguageChoice {
	Language lang;
	const char *label;
};
const LanguageChoice kLanguages[] = {
	{Language::American, "English (US)"}, {Language::British, "English (UK)"}, {Language::French, "French"},
	{Language::German, "German"},         {Language::Spanish, "Spanish"},      {Language::Italian, "Italian"},
};

const char *languageLabel(Language lang) {
	for (const LanguageChoice &c : kLanguages)
		if (c.lang == lang)
			return c.label;
	return languageName(lang);
}

// A button that asks for confirmation in a modal popup; true when confirmed.
bool confirmButton(const char *label, const char *question) {
	if (ImGui::Button(label))
		ImGui::OpenPopup(label);
	bool confirmed = false;
	if (ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::TextUnformatted(question);
		if (ImGui::Button("Yes", ImVec2(90, 0))) {
			confirmed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	return confirmed;
}

bool percentSlider(const char *label, int &value, const char *tooltip = nullptr) {
	bool changed = ImGui::SliderInt(label, &value, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp);
	if (tooltip)
		ImGui::SetItemTooltip("%s", tooltip);
	return changed;
}

const VideoFilter kFilterOrder[] = {VideoFilter::Nearest, VideoFilter::Smooth,  VideoFilter::Sharp, VideoFilter::Scale2x,
                                    VideoFilter::Scale3x, VideoFilter::Xbr, VideoFilter::Crt};
const CrtMask kMaskOrder[] = {CrtMask::None, CrtMask::ApertureGrille, CrtMask::SlotMask, CrtMask::ShadowMask};

const char *filterHelp(VideoFilter f) {
	switch (f) {
	case VideoFilter::Nearest: return "The game's own pixels, unchanged. Rows can look slightly uneven with 4:3 correction or non-whole window sizes; Sharp bilinear evens them out.";
	case VideoFilter::Smooth: return "Plain bilinear filtering: soft, blurry pixels.";
	case VideoFilter::Sharp: return "Square pixels that stay even at any size: only the pixel edges are blended.";
	case VideoFilter::Scale2x: return "Scale2x (EPX) pixel-art upscaler, then sharp bilinear to the window size.";
	case VideoFilter::Scale3x: return "Scale3x (AdvMAME3x) pixel-art upscaler, then sharp bilinear to the window size.";
	case VideoFilter::Xbr:
		return "Edge-directed upscaler in the spirit of xBR: rounds off diagonal staircases,\n"
		       "then sharp bilinear to the window size.";
	case VideoFilter::Crt:
#ifdef GOB_WEBOS
		return "CRT monitor look: scanlines, phosphor mask, glow. Heavy for a TV: about 40 frames a second on an "
		       "LG C1 (2021), where the other filters keep 60.";
#else
		return "CRT monitor look: scanlines, phosphor mask, glow; best at 3x and above.";
#endif
	}
	return "";
}

bool filterParams(FilterSettings &f) {
	bool changed = false;
	switch (f.filter) {
	case VideoFilter::Sharp:
	case VideoFilter::Scale2x:
	case VideoFilter::Scale3x:
	case VideoFilter::Xbr:
		if (f.filter == VideoFilter::Xbr)
			changed |= percentSlider("Edge rounding", f.xbrStrength, "How far corners along diagonal edges are filled in.");
		changed |= percentSlider("Softness", f.softness, "Width of the blend at pixel edges: 0% crisp, 100% plain bilinear.");
		break;
	case VideoFilter::Crt:
		changed |= percentSlider("Scanlines", f.scanlines, "Darkness of the gaps between the 200 lines.");
		changed |= percentSlider("Sharpness", f.crtSharpness, "Horizontal sharpness of the beam.");
		if (ImGui::BeginCombo("Phosphor mask", crtMaskLabel(f.mask))) {
			for (CrtMask m : kMaskOrder)
				if (ImGui::Selectable(crtMaskLabel(m), m == f.mask) && m != f.mask) {
					f.mask = m;
					changed = true;
				}
			ImGui::EndCombo();
		}
		ImGui::BeginDisabled(f.mask == CrtMask::None);
		changed |= percentSlider("Mask strength", f.maskStrength);
		ImGui::EndDisabled();
		changed |= percentSlider("Glow", f.bloom, "Light bleeding around bright areas.");
		changed |= percentSlider("Curvature", f.curvature, "Bulging screen; corners are cut off.");
		changed |= percentSlider("Vignette", f.vignette, "Darker corners.");
		changed |= ImGui::SliderInt("Brightness", &f.brightness, 50, 200, "%d%%", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("Makes up for the light that scanlines and the mask take away.");
		{
			float g = float(f.gamma);
			if (ImGui::SliderFloat("Gamma", &g, float(Settings::kMinGamma), float(Settings::kMaxGamma), "%.2f",
			                       ImGuiSliderFlags_AlwaysClamp)) {
				f.gamma = std::round(g * 100.0) / 100.0;
				changed = true;
			}
			ImGui::SetItemTooltip("Display gamma; lower is brighter in the dark tones.");
		}
		if (ImGui::Button("Reset CRT settings")) {
			FilterSettings def;
			def.filter = f.filter;
			def.softness = f.softness;
			def.xbrStrength = f.xbrStrength;
			f = def;
			changed = true;
		}
		break;
	default:
		break;
	}
	return changed;
}

const ImVec4 kErrorColour(1.0f, 0.45f, 0.4f, 1.0f), kWarningColour(1.0f, 0.8f, 0.3f, 1.0f);

void hdPackSection(OverlayHost &host) {
	Settings &s = host.settings();
	HdPack &pack = host.hdPack();
	ImGui::SeparatorText("HD pack");
	ImGui::TextWrapped("Upscaled pictures made from gobexport's export (see docs/hdpacks.md), one folder per "
	                   "pack in:");
	ImGui::TextWrapped("%s", host.hdPacksDir().empty() ? "(none)" : host.hdPacksDir().c_str());
#ifdef __EMSCRIPTEN__
	// In the browser's storage: packs are copied there.
	const std::string importing = host.importProgress();
	ImGui::BeginDisabled(!importing.empty());
	if (ImGui::Button("Import pack folder..."))
		host.importHdPack(false);
	ImGui::SameLine();
	if (ImGui::Button("Import pack (.zip)..."))
		host.importHdPack(true);
	ImGui::EndDisabled();
	if (!importing.empty())
		ImGui::TextDisabled("%s", importing.c_str());
#else
	if (ImGui::Button("Open folder##hd"))
		host.openHdPacksFolder();
#endif
	ImGui::SameLine();
	if (ImGui::Button("Rescan##hd"))
		host.reloadHdPack();

	const std::vector<std::string> names = host.hdPackNames();
	const char *current = s.hdPack.empty() ? "None" : s.hdPack.c_str();
	if (ImGui::BeginCombo("Pack", current)) {
		if (ImGui::Selectable("None", s.hdPack.empty()) && !s.hdPack.empty()) {
			s.hdPack.clear();
			host.settingsChanged();
		}
		for (const std::string &n : names)
			if (ImGui::Selectable(n.c_str(), n == s.hdPack) && n != s.hdPack) {
				s.hdPack = n;
				host.settingsChanged();
			}
		ImGui::EndCombo();
	}
	if (s.hdPack.empty())
		return;
	if (!pack.isOpen()) {
		ImGui::TextColored(kErrorColour, "The pack folder can't be read.");
		return;
	}
	const HdPack::Stats st = pack.stats();
	ImGui::Text("%zu pictures at %dx%s, %d error(s); %zu decoded (%.1f MB)", pack.size(), pack.scale(),
	            pack.mixedScales() ? " (mixed)" : "", pack.errorCount(), st.cachedImages, st.cachedBytes / 1048576.0);
	const HdRenderer &hdr = host.hdRenderer();
	if (hdr.attached()) {
		const HdRenderer::Coverage c = hdr.coverage();
		const double total = std::max<double>(1.0, double(c.hd + c.live + c.repaired));
		ImGui::Text("Shown at %dx: %.0f%% of the screen from the pack", hdr.scale(), 100.0 * double(c.hd) / total);
		if (!host.hdShown())
			ImGui::TextDisabled("(the CRT filter shows the original pictures)");
	} else if (pack.size() > 0) {
		ImGui::TextDisabled("Shown once the game runs.");
	}
	bool changed = false;
	int scope = s.hdRoomsOnly ? 1 : 0;
	changed |= ImGui::Combo("Apply pack to", &scope, "Everything\0Room backgrounds only\0");
	ImGui::SetItemTooltip("Room backgrounds only: the goblins, objects, cursors and the icon bar\n"
	                      "keep their original art (enlarged with the filter below).");
	s.hdRoomsOnly = scope == 1;
	static const char *const kSpriteFilters[] = {"Original (big pixels)", "Scale2x/3x", "Smooth edges (xBR-like)",
	                                             "Smooth (bilinear)"};
	int filter = int(s.hdSpriteFilter);
	if (ImGui::Combo("Pictures without HD art", &filter, kSpriteFilters, IM_ARRAYSIZE(kSpriteFilters))) {
		s.hdSpriteFilter = HdSpriteFilter(filter);
		changed = true;
	}
	ImGui::SetItemTooltip("How pictures the pack lacks (or doesn't apply to) are enlarged.\n"
	                      "Scale2x/3x rounds staircases without new colours; the smooth filters\n"
	                      "blend neighbouring colours. Transparent outlines stay the original ones.");
	changed |= ImGui::Checkbox("Smooth transparent edges", &s.hdSmoothEdges);
	ImGui::SetItemTooltip("Edges of HD sprites smoothed instead of following the original\n"
	                      "pixel silhouette exactly.");
	if (changed)
		host.settingsChanged();
	bool tint = host.hdTintOn();
	if (ImGui::Checkbox("Show what the pack doesn't cover", &tint))
		host.setHdTint(tint);
	ImGui::SetItemTooltip("Debug view: original pixels (no HD picture there) tinted blue,\n"
	                      "areas repaired from the 320x200 frame tinted red.");
	const std::vector<HdIssue> issues = pack.issues();
	if (issues.empty())
		return;
	const float height = std::min(160.0f, ImGui::GetTextLineHeightWithSpacing() * (float(issues.size()) + 1.5f));
	if (ImGui::BeginTable("hdissues", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg,
	                      ImVec2(0, height))) {
		ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthFixed, 140.0f);
		ImGui::TableSetupColumn("Problem");
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();
		for (const HdIssue &i : issues) {
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(i.file.empty() ? "(pack)" : i.file.c_str());
			ImGui::TableNextColumn();
			if (i.severity == HdIssue::Error)
				ImGui::TextColored(kErrorColour, "%s", i.message.c_str());
			else if (i.severity == HdIssue::Warning)
				ImGui::TextColored(kWarningColour, "%s", i.message.c_str());
			else
				ImGui::TextDisabled("%s", i.message.c_str());
		}
		ImGui::EndTable();
	}
}

void videoTab(OverlayHost &host) {
	Settings &s = host.settings();
	bool changed = false;
#ifndef GOB_WEBOS // a TV's window is the screen
	changed |= ImGui::Checkbox("Fullscreen", &s.fullscreen);
#endif
#if !defined(__EMSCRIPTEN__) && !defined(GOB_WEBOS) // the page sizes the game in a browser, a TV has one size
	ImGui::BeginDisabled(s.fullscreen);
	changed |= ImGui::SliderInt("Window scale", &s.windowScale, Settings::kMinScale, Settings::kMaxScale, "%dx",
	                            ImGuiSliderFlags_AlwaysClamp);
	ImGui::EndDisabled();
#endif
	changed |= ImGui::Checkbox("Integer scaling", &s.integerScaling);
	ImGui::SetItemTooltip("Only whole multiples of the image size; the rest is black border.");
	changed |= ImGui::Checkbox("4:3 aspect correction", &s.aspectCorrection);
#ifdef GOB_WEBOS
	changed |= ImGui::Checkbox("Key tips under the picture (yellow key)", &s.keyTips);
	ImGui::SetItemTooltip("What the remote's colour keys do, in a strip under the game.");
#endif
	ImGui::SetItemTooltip("Show the 320x200 image with tall pixels, as on a CRT monitor (320x240).");

	ImGui::SeparatorText("Filter");
	FilterSettings &f = s.filters;
	if (ImGui::BeginCombo("Filter", filterLabel(f.filter))) {
		for (VideoFilter v : kFilterOrder) {
			if (ImGui::Selectable(filterLabel(v), v == f.filter) && v != f.filter) {
				f.filter = v;
				changed = true;
			}
			ImGui::SetItemTooltip("%s", filterHelp(v));
		}
		ImGui::EndCombo();
	}
	ImGui::TextDisabled("%s", filterHelp(f.filter));
	changed |= filterParams(f);

	ImGui::SeparatorText("Text");
	changed |= ImGui::Checkbox("Sharp text", &s.sharpText);
	ImGui::SetItemTooltip("Draws the game's text at the window's resolution: the game's own 8x8 letters\n"
	                      "with their staircase edges smoothed, instead of enlarged pixels. Letters\n"
	                      "fall back to the original pixels wherever something is drawn over them.");
	if (s.sharpText) {
		ImGui::SameLine();
		if (f.filter == VideoFilter::Crt)
			ImGui::TextDisabled("(not with the CRT filter)");
		else
			ImGui::TextDisabled("(%d letters on screen)", host.sharpTextCells());
	}
	if (changed)
		host.settingsChanged();

	hdPackSection(host);

	ImGui::Separator();
	if (ImGui::Button("Take screenshot (F12)"))
		host.takeScreenshot();
	ImGui::SetItemTooltip("Saves the game image, as filtered, without this overlay\n"
	                      "(PNG, in the Screenshots folder next to settings.ini).");
}

void audioTab(OverlayHost &host) {
	Settings &s = host.settings();
	bool changed = ImGui::Checkbox("Mute", &s.mute);
	ImGui::BeginDisabled(s.mute);
	const ImGuiSliderFlags f = ImGuiSliderFlags_AlwaysClamp;
	changed |= ImGui::SliderInt("Master", &s.masterVolume, 0, 100, "%d%%", f);
	changed |= ImGui::SliderInt("Music (CD)", &s.musicVolume, 0, 100, "%d%%", f);
	changed |= ImGui::SliderInt("Effects", &s.effectsVolume, 0, 100, "%d%%", f);
	changed |= ImGui::SliderInt("PC speaker", &s.speakerVolume, 0, 100, "%d%%", f);
	const bool speech = gameRules(host.currentGame().id).hasSpeech();
	if (speech) {
		ImGui::BeginDisabled(!s.speech);
		changed |= ImGui::SliderInt("Speech", &s.speechVolume, 0, 100, "%d%%", f);
		ImGui::EndDisabled();
	}
	ImGui::EndDisabled();
	if (speech) {
		changed |= ImGui::Checkbox("Speech on", &s.speech);
		ImGui::SetItemTooltip("The characters speak their lines (English: the GOG release has only the English\n"
		                      "speech, so other languages get it too). Off, the talking heads move silently;\n"
		                      "the subtitles always show.");
	}

	ImGui::TextUnformatted("Sound effects");
	using Q = Mixer::EffectsQuality;
	ImGui::SameLine();
	if (ImGui::RadioButton("Enhanced", s.effectsQuality == Q::Enhanced) && s.effectsQuality != Q::Enhanced) {
		s.effectsQuality = Q::Enhanced;
		changed = true;
	}
	ImGui::SetItemTooltip("Smooth band-limited resampling of the 8-bit samples (no harsh aliasing above\n"
	                      "their own bandwidth), no clicks when a sound is cut off, softer footstep clicks.");
	ImGui::SameLine();
	if (ImGui::RadioButton("Original", s.effectsQuality == Q::Original) && s.effectsQuality != Q::Original) {
		s.effectsQuality = Q::Original;
		changed = true;
	}
	ImGui::SetItemTooltip("As a Sound Blaster plays them: each 8-bit sample held until the next,\n"
	                      "plain square-wave PC-speaker clicks. Crunchy.");

	ImGui::SeparatorText("Replacement music");
	MusicPack &pack = host.musicPack();
	ImGui::TextWrapped("MP3 or WAV files named after the game's CD tracks (for example CAMPAGNE.mp3 or "
	                   "mine.wav) play instead of that track's CD music.");
	ImGui::TextDisabled("Folder:");
	ImGui::SameLine();
	ImGui::TextWrapped("%s", pack.dir().empty() ? "(none)" : pack.dir().c_str());
	ImGui::BeginDisabled(pack.dir().empty());
#ifdef __EMSCRIPTEN__
	{
		const std::string importing = host.importProgress();
		ImGui::BeginDisabled(!importing.empty());
		if (ImGui::Button("Import music files..."))
			host.importMusic();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("MP3 or WAV files, copied into the browser's storage.");
	}
#else
	if (ImGui::Button("Open folder"))
		host.openMusicFolder();
#endif
	ImGui::SameLine();
	if (ImGui::Button("Rescan"))
		pack.rescan();
	ImGui::SetItemTooltip("Looks for files again; a change applies the next time the game starts a track.");
	ImGui::EndDisabled();
	changed |= ImGui::Checkbox("Play replacements to their end", &s.musicFullLength);
	ImGui::SetItemTooltip("Off: a replacement stops where the CD track would end (a shorter one is followed by\n"
	                      "silence), so the music follows the game's timing. On: it plays to its end; a room\n"
	                      "restarting the same track doesn't interrupt it, another track or a stop does.");

	const std::vector<CdAudio::Track> tracks = host.cdTracks();
	auto trackSeconds = [&](const std::string &name) -> double {
		for (const CdAudio::Track &t : tracks)
			if (t.name == name)
				return double(t.end - t.start + 1) / 75.0;
		return -1.0;
	};
	if (pack.entries().empty()) {
		ImGui::TextDisabled("No replacement files found.");
	} else {
		ImGui::Text("%d replacement%s:", int(pack.entries().size()), pack.entries().size() == 1 ? "" : "s");
		const ImGuiTableFlags tf = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit |
		                           ImGuiTableFlags_ScrollY;
		float height = ImGui::GetTextLineHeightWithSpacing() * float(std::min<size_t>(pack.entries().size(), 6) + 1) + 6;
		if (ImGui::BeginTable("replacements", 3, tf, ImVec2(0, height))) {
			ImGui::TableSetupColumn("Track");
			ImGui::TableSetupColumn("File");
			ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();
			for (const MusicPack::Entry &e : pack.entries()) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				double secs = trackSeconds(e.track);
				ImGui::TextUnformatted(e.track.c_str());
				if (secs >= 0)
					ImGui::SetItemTooltip("CD track length %d:%02d", int(secs) / 60, int(secs) % 60);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(e.file.c_str());
				ImGui::TableNextColumn();
				if (!e.info.error.empty())
					ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "unreadable: %s", e.info.error.c_str());
				else if (!tracks.empty() && secs < 0)
					ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s - not a track of this game",
					                   e.info.description.c_str());
				else
					ImGui::TextUnformatted(e.info.description.c_str());
			}
			ImGui::EndTable();
		}
	}
	if (!tracks.empty() && ImGui::TreeNode("tracks", "The game's CD tracks (%d)", int(tracks.size()))) {
		ImGui::TextDisabled("Hover a name for its length; the PLAxxx tracks are the ending's voices.");
		if (ImGui::BeginTable("tracklist", 6, ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersOuter, ImVec2(0, 120))) {
			for (const CdAudio::Track &t : tracks) {
				ImGui::TableNextColumn();
				bool replaced = pack.find(t.name) != nullptr;
				if (replaced)
					ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", t.name.c_str());
				else
					ImGui::TextUnformatted(t.name.c_str());
				double secs = double(t.end - t.start + 1) / 75.0;
				ImGui::SetItemTooltip("%d:%04.1f%s", int(secs) / 60, std::fmod(secs, 60.0), replaced ? " (replaced)" : "");
			}
			ImGui::EndTable();
		}
		ImGui::TreePop();
	}
	if (changed)
		host.settingsChanged();
}

void gameplayTab(OverlayHost &host) {
	Settings &s = host.settings();
	if (ImGui::BeginCombo("Language", languageLabel(s.language))) {
		for (const LanguageChoice &c : kLanguages)
			if (host.currentGame().offersLanguage(c.lang) && ImGui::Selectable(c.label, c.lang == s.language) &&
			    c.lang != s.language) {
				s.language = c.lang;
				host.settingsChanged();
			}
		ImGui::EndCombo();
	}
	if (s.language != host.runningLanguage()) {
		ImGui::TextWrapped("The game is running in %s; the new language applies after a restart.",
		                   languageLabel(host.runningLanguage()));
		if (confirmButton("Restart game to apply", "Restart from the intro? Unsaved progress is lost."))
			host.restartGame();
	}
	ImGui::Separator();
	if (confirmButton("Restart game", "Restart from the intro? Unsaved progress is lost."))
		host.restartGame();
	ImGui::SameLine();
	if (confirmButton("Back to library", "Leave the game for the game library? Unsaved progress is lost."))
		host.backToLibrary();
#ifndef __EMSCRIPTEN__ // a page is closed, not quit
	ImGui::SameLine();
	if (confirmButton("Quit", "Quit the game? Unsaved progress is lost."))
		host.quit();
#endif
}

const double kSpeedPresets[] = {1.0, 1.2, 1.5, 2.0};

void speedTab(OverlayHost &host) {
	Settings &s = host.settings();
	bool changed = false;
	ImGui::TextUnformatted("Game speed");
	bool preset = false;
	for (double p : kSpeedPresets) {
		char label[16];
		std::snprintf(label, sizeof(label), "%gx", p);
		ImGui::SameLine();
		if (ImGui::RadioButton(label, s.speed == p) && s.speed != p) {
			s.speed = p;
			changed = true;
		}
		preset |= s.speed == p;
	}
	if (!preset)
		ImGui::Text("Custom speed from settings.ini: %gx", s.speed);
#ifdef GOB_WEBOS
#define GOB_TURBO_KEY "the blue key" // the remote's, a toggle there (tools/gobnative.cpp)
#else
#define GOB_TURBO_KEY "Tab"
#endif
#ifdef GOB_WEBOS
	changed |= ImGui::SliderInt("Fast (" GOB_TURBO_KEY ")", &s.turboSpeed, Settings::kMinTurbo, Settings::kMaxTurbo,
	                            "%dx", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("While fast is on (" GOB_TURBO_KEY ") the game runs at this speed instead.");
#else
	changed |= ImGui::SliderInt("Turbo (hold " GOB_TURBO_KEY ")", &s.turboSpeed, Settings::kMinTurbo,
	                            Settings::kMaxTurbo, "%dx", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("While " GOB_TURBO_KEY " is held the game runs at this speed instead.");
#endif
	changed |= ImGui::Checkbox("Cutscenes at normal speed", &s.cutscenesAtNormalSpeed);
	ImGui::SetItemTooltip("The intro and the ending are timed to the CD music; at other speeds they drift out of\n"
	                      "sync. Turbo doesn't apply to them either while this is on.");
	if (changed)
		host.settingsChanged();
	ImGui::Separator();
	ImGui::BeginDisabled(!host.canSkip());
	if (ImGui::Button("Skip cutscene"))
		host.skipCutscene();
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("The intro and the ending, the pictures between rooms and worlds, goblins entering\n"
	                      "or leaving a room and other scenes during which you have no control. Scenes in\n"
	                      "rooms are fast-forwarded, so the game ends up exactly where they would have left it.");
	ImGui::SameLine();
	ImGui::TextDisabled("(or Escape)");
}

// Rooms in play order, grouped by world; jump to one, quick save and load.
class LevelsTab {
public:
	explicit LevelsTab(OverlayHost &host) : _host(host) {}

	void draw() {
		const std::string current = _host.currentLevel();
		// Goblins 3 records the state of each room the player reaches.
		const bool roomStates = !_levels.empty() && _levels.front().tot.rfind("EMAP", 0) == 0;
		if (_levels.empty() || _language != _host.runningLanguage() || (roomStates && current != _listedIn)) {
			_levels = _host.levels();
			_language = _host.runningLanguage();
			_listedIn = current;
		}
		const bool worlds = !_levels.empty() && _levels.front().world != 0; // Gobliins 2; Gobliiins has levels
		if (_selected.empty())
			_selected = current;
		if (const LevelInfo *l = find(current); l && worlds)
			ImGui::Text("Now in world %d: %s", l->world, label(*l).c_str());
		else if (l)
			ImGui::Text("Now in %s", label(*l).c_str());
		else if (worlds)
			ImGui::TextDisabled("No room is running (intro, ending or between rooms).");
		else
			ImGui::TextDisabled("No level is running (intro, ending, access code or between levels).");

		char saveLabel[64], loadLabel[64];
		if (*_host.quickSaveShortcut())
			std::snprintf(saveLabel, sizeof(saveLabel), "Quick save (%s)", _host.quickSaveShortcut());
		else
			std::snprintf(saveLabel, sizeof(saveLabel), "Quick save");
		if (*_host.quickLoadShortcut())
			std::snprintf(loadLabel, sizeof(loadLabel), "Quick load (%s)", _host.quickLoadShortcut());
		else
			std::snprintf(loadLabel, sizeof(loadLabel), "Quick load");
		ImGui::BeginDisabled(current.empty());
		if (ImGui::Button(saveLabel))
			_host.quickSave();
		ImGui::EndDisabled();
		ImGui::SetItemTooltip(worlds ? "Saves into the game's last save slot, named QUICKSAVE."
		                             : "Saves the level as it is now (the game itself has no saves).");
		ImGui::SameLine();
		ImGui::BeginDisabled(!_host.hasQuickSave());
		if (ImGui::Button(loadLabel))
			_host.quickLoad();
		ImGui::EndDisabled();

		ImGui::Separator();
		bool openJump = false;
		if (ImGui::BeginChild("rooms", ImVec2(0, 260), ImGuiChildFlags_Borders)) {
			int world = 0;
			for (const LevelInfo &l : _levels) {
				if (l.world != world) {
					world = l.world;
					ImGui::SeparatorText(("World " + std::to_string(world)).c_str());
				}
				std::string text = label(l) + (l.worldStart ? "  (start of the world)" : "");
				if (!l.code.empty())
					text += "  - code " + l.code;
				if (roomStates && !l.reached)
					text += "  - not reached yet";
				if (l.tot == current)
					text += "  - you are here";
				ImGui::PushID(l.tot.c_str());
				if (ImGui::Selectable(text.c_str(), _selected == l.tot, ImGuiSelectableFlags_AllowDoubleClick)) {
					_selected = l.tot;
					openJump |= ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();

		const LevelInfo *sel = find(_selected);
		ImGui::BeginDisabled(!sel);
		if (ImGui::Button("Jump") || (openJump && sel))
			ImGui::OpenPopup("Jump to room");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled(roomStates ? "Starts the room as you first reached it."
		                    : worlds   ? "Starts the room as if you had just arrived there."
		                               : "Starts the level as its access code does, with full energy.");
		if (ImGui::BeginPopupModal("Jump to room", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (sel && roomStates) {
				ImGui::Text("Start %s in world %d?", label(*sel).c_str(), sel->world);
				ImGui::TextUnformatted(sel->reached ? "The room starts as you first reached it, with what you carried then;\n"
				                                      "progress since your last save is lost."
				                                    : "You haven't reached this room yet: it starts without the items and\n"
				                                      "progress of the rooms before it, and may not be winnable.\n"
				                                      "Progress since your last save is lost.");
			} else if (sel && worlds) {
				ImGui::Text("Start %s in world %d?", label(*sel).c_str(), sel->world);
				ImGui::TextUnformatted("The game continues from the start of that world, with the items\n"
				                       "carried into it; progress since your last save is lost.");
			} else if (sel) {
				ImGui::Text("Start %s?", label(*sel).c_str());
				ImGui::TextUnformatted("The level starts from the beginning with full energy;\n"
				                       "progress in the current level is lost.");
			}
			if (ImGui::Button("Jump", ImVec2(90, 0)) && sel) {
				_host.jumpToLevel(sel->tot);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}

private:
	const LevelInfo *find(const std::string &tot) const {
		for (const LevelInfo &l : _levels)
			if (l.tot == tot)
				return &l;
		return nullptr;
	}
	static std::string label(const LevelInfo &l) {
		return (l.name.empty() ? std::string("(unnamed)") : l.name) + " (" + l.tot + ")";
	}

	OverlayHost &_host;
	std::vector<LevelInfo> _levels;
	std::string _listedIn; // the room running when _levels was read (Goblins 3)
	Language _language = Language::American;
	std::string _selected;
};

} // namespace

void addSettingsTabs(Overlay &overlay, OverlayHost &host) {
	overlay.addTab("Video", [&host] { videoTab(host); });
	overlay.addTab("Audio", [&host] { audioTab(host); });
	overlay.addTab("Gameplay", [&host] { gameplayTab(host); });
	overlay.addTab("Speed", [&host] { speedTab(host); });
	if (gameRules(host.currentGame().id).hasLevelSelect())
		overlay.addTab("Levels", [tab = std::make_shared<LevelsTab>(host)] { tab->draw(); });
}

} // namespace gob
