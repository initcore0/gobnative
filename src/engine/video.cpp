// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IMD playback (docs/imd.md 4, docs/opcodes.md 11.3): playImd and
// getImdInfo, after ScummVM's VideoPlayer for the primary video. The video
// is decoded into a buffer the size of its target surface (the back or front
// surface, a sprite, or nothing for sound-only videos) that mirrors it, and
// each frame's rectangles are blitted into the target, so the HD layers and
// pixel sources stay in step. Playback blocks the script, as in Goblins 3,
// on the game clock (at 1x: a video counts as a cutscene), with the sound on
// the mixer's speech voice.

#include "engine/interpreter.h"

#include <algorithm>
#include <cctype>

namespace gob {

namespace {

// playImd flags.
constexpr int kFlagBackContent = 0x40;   // blit the back surface before each frame (front surface videos)
constexpr int kFlagFrontSurface = 0x80;  // draw into the front surface
constexpr int kFlagNoVideo = 0x100;      // only the sound
constexpr int kFlagOtherSurface = 0x800; // draw into sprite x
constexpr int kFlagScreenSurface = 0x400000;

constexpr int16_t kEscape = 27;

std::string upper(std::string s) {
	for (char &c : s)
		c = char(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

} // namespace

// VideoPlayer::evaluateFlags: the target surface from the flags.
void Interpreter::videoTargetFromFlags(VideoProps &props) {
	if (props.flags & kFlagFrontSurface) {
		props.sprite = draw::kFrontSurface;
	} else if (props.flags & kFlagOtherSurface) {
		props.sprite = props.x;
		props.x = 0;
	} else if (props.flags & (kFlagScreenSurface | kFlagNoVideo)) {
		props.sprite = 0;
	} else {
		props.sprite = draw::kBackSurface;
	}
}

// The surface a video draws into, or null (sound only, or a missing sprite).
Surface *Interpreter::videoTarget() {
	if (!_video || _video->sprite < 0)
		return nullptr;
	if (_video->sprite == draw::kFrontSurface)
		return &_draw.front();
	if (_video->sprite == draw::kBackSurface)
		return &_draw.back();
	return _draw.sprite(_video->sprite).get();
}

// VideoPlayer::openVideo for the primary slot: the file NAME or NAME.IMD;
// the same file stays open (and goes on where it stopped). Writes VAR 7 (the
// frame count). False when the file is missing or no IMD.
bool Interpreter::openVideo(const std::string &file, VideoProps &props) {
	std::string name = upper(file);
	if (name.find('.') == std::string::npos)
		name += ".IMD";

	if (_video && _video->name != name)
		closeVideo();

	if (!_video) {
		auto data = _files.read(name);
		if (!data)
			return false;
		auto v = std::make_unique<VideoSlot>();
		if (!v->imd.load(std::move(*data))) {
			trace("playImd: " + name + ": " + v->imd.error());
			return false;
		}
		v->name = name;
		v->voice = !_voiceArchive.empty() && upper(_files.archiveOf(name)) == upper(_voiceArchive);
		v->track = std::make_shared<std::vector<uint8_t>>();
		for (int8_t s : v->imd.soundTrack(&v->soundStartFrame))
			v->track->push_back(uint8_t(s));
		// WORKAROUND (ScummVM): Goblins 3 keeps the cursor during these.
		std::string base = name.substr(0, name.find('.'));
		v->keepCursor = _ver == 3 && (base == "CAIL1" || base == "CAIL2");
		_video = std::move(v);

		if (!(props.flags & kFlagNoVideo) && props.sprite >= 0) {
			bool ownSurf = props.sprite != draw::kFrontSurface && props.sprite != draw::kBackSurface;
			if (ownSurf) {
				int w = _video->imd.width(), h = _video->imd.height();
				if (props.flags & kFlagScreenSurface) {
					w = _draw.back().width();
					h = _draw.back().height();
				}
				if (w > 0 && h > 0)
					_draw.createSprite(props.sprite, w, h);
			}
			_video->sprite = props.sprite;
			if (!videoTarget()) {
				_video->sprite = -1;
				props.x = props.y = 0;
			} else if (!ownSurf || (props.flags & kFlagScreenSurface)) {
				if (props.x < 0 && props.y < 0)
					props.x = props.y = -1;
			} else {
				props.x = props.y = 0;
			}
		} else {
			_video->sprite = -1;
			props.x = props.y = 0;
		}
		// The decoding buffer mirrors the target (holes in sparse frames keep
		// what it shows); without one, the video's own size at 0, 0.
		if (Surface *t = videoTarget()) {
			_video->buffer = Surface(t->width(), t->height());
			_video->buffer.blit(*t);
		} else {
			_video->buffer = Surface(std::max(1, _video->imd.width()), std::max(1, _video->imd.height()));
		}
	}

	_video->imd.setXY(props.x < 0 ? -1 : props.x, props.y < 0 ? -1 : props.y);
	_video->frameRate = _video->imd.hasSound() ? _video->imd.frameRate() : double(_frameRate);
	writeVar(7, uint32_t(_video->imd.frameCount()));
	return true;
}

void Interpreter::closeVideo() {
	if (!_video)
		return;
	if (_video->soundPlaying)
		_mixer.stopSpeech();
	_video.reset();
}

// VideoPlayer::copyPalette: the video's palette into the game's, entries
// palStart..palEnd.
void Interpreter::copyVideoPalette(int palStart, int palEnd) {
	if (palStart < 0)
		palStart = 0;
	if (palEnd < 0)
		palEnd = 255;
	const auto &pal = _video->imd.palette();
	for (int i = palStart * 3; i < (palEnd + 1) * 3 && i < 768; i++)
		_draw.vgaPalette[size_t(i)] = pal[size_t(i)];
}

// VideoPlayer::playFrame for the primary video: palette commands (1 none,
// 2 the video's palette after the frame, 4 also at the first frame with a
// fade, 8 at once at the first and the last frame, 16 a fade after the
// first frame), the frame, its rectangles shown, VAR 11 = the frame, and
// Escape (or the Skip button) cancels. Returns true when the palette
// command wants a fade to the game palette now (palCmd 2, 4 and 16 at the
// palette frame; 16 fades after the frame is shown rather than before,
// which no Goblins 3 script uses).
bool Interpreter::videoFrame(VideoProps &props) {
	VideoSlot &v = *_video;
	ImdDecoder &imd = v.imd;
	Surface *target = videoTarget();
	const bool onBack = target == &_draw.back();

	if (imd.curFrame() != props.startFrame) {
		if (props.startFrame != -1) {
			if (!imd.hasSound()) // a video with sound can't be seeked, as in the original
				imd.seek(props.startFrame, v.buffer.data(), v.buffer.width(), v.buffer.height(), v.buffer.width());
		} else {
			imd.seek(-1, nullptr, 0, 0, 0);
		}
	}
	if (imd.curFrame() > props.startFrame)
		return false; // already beyond the wanted frame

	bool modifiedPal = false;
	if (props.startFrame == props.palFrame || (props.startFrame == props.endFrame && props.palCmd == 8)) {
		modifiedPal = true;
		_draw.applyPal = true;
		if (props.palCmd >= 4)
			copyVideoPalette(props.palStart, props.palEnd);
	}
	if (modifiedPal && props.palCmd == 8 && !onBack)
		_draw.setPalette();
	if (props.needBlit)
		_draw.forceBlit();

	ImdDecoder::Frame frame;
	bool decoded = imd.decodeFrame(v.buffer.data(), v.buffer.width(), v.buffer.height(), v.buffer.width(), frame);
	if (!decoded && !imd.error().empty())
		trace("playImd: " + v.name + ": " + imd.error());
	if (frame.soundStart && !v.soundPlaying && !_ffSkip && !v.track->empty() && (speech || !v.voice)) {
		_mixer.playSpeech(v.track, imd.soundFreq(), v.voice);
		v.soundPlaying = true;
	}
	if (imd.curFrame() == 0)
		v.startMs = _gameMs;
	writeVar(11, uint32_t(imd.curFrame()));

	bool fade = false;
	if (decoded) {
		for (const ImdDecoder::Rect &r : frame.dirty)
			if (target)
				target->blit(v.buffer, r.left, r.top, r.right - 1, r.bottom - 1, r.left, r.top);
		if (props.needBlit)
			_draw.forceBlit(true);
		if (modifiedPal && props.palCmd == 16 && onBack)
			_draw.forceBlit();
		if (props.palCmd > 1) {
			copyVideoPalette(props.palStart, props.palEnd);
			if (!onBack)
				_draw.setPalette();
			else
				_draw.applyPal = true;
		}
		if (modifiedPal && props.palCmd == 8 && onBack)
			_draw.setPalette();
		if (onBack) {
			for (const ImdDecoder::Rect &r : frame.dirty)
				_draw.invalidateRect(r.left, r.top, r.right - 1, r.bottom - 1);
			_draw.blitInvalidated();
		}
		fade = modifiedPal && (props.palCmd == 2 || props.palCmd == 4 || props.palCmd == 16);
	}

	// Escape cancels (the original checks the break key; ScummVM makes it
	// Escape for every video), and so does the Skip button.
	storeKey(int16_t(checkKeys()));
	if (int16_t(variables().slot(0)) == props.breakKey || (props.skippable && skipPending())) {
		_skipCutscene = false;
		if (v.soundPlaying) {
			_mixer.stopSpeech();
			v.soundPlaying = false;
		}
		// Seek to the last frame; some scripts depend on that.
		imd.seek(props.endFrame, v.buffer.data(), v.buffer.width(), v.buffer.height(), v.buffer.width());
		props.canceled = true;
		trace("playImd: " + v.name + " cancelled");
	}
	return fade;
}

Task<void> Interpreter::playVideoFrame(VideoProps &props) {
	if (videoFrame(props)) {
		co_await fadeToPalette(_draw.vgaPalette);
		_draw.clearInvalidated();
	}
}

// Mult_v2::playImd: frame `startFrame` of an IMD key's loop (palFrame to
// lastFrame) from the mult's advanceObjects, in the primary video slot,
// which stays open from one mult frame to the next. Shown at once: the
// room's own frame pacing (animatePalette) waits, as the original's single
// frame wait makes it skip its own. Goblins 3's mult videos have no sound.
void Interpreter::playMultImd(const std::string &file, const MultData::ImdKey &key, int startFrame) {
	if (key.imdFile == -1) {
		closeVideo();
		return;
	}
	VideoProps props;
	props.breakKey = kEscape;
	props.skippable = false;
	if (_draw.renderFlags & 0x100) {
		props.x = int32_t(variables().slot(55));
		props.y = int32_t(variables().slot(56));
	}
	props.flags = uint16_t((key.flags >> 8) & 0xFF);
	if (props.flags & 0x20)
		props.flags = uint16_t((props.flags & 0x9F) | 0x80);
	props.palStart = key.palStart;
	props.palEnd = key.palEnd;
	props.palFrame = key.palFrame;
	props.lastFrame = key.lastFrame;
	if (props.palFrame != -1 && props.lastFrame != -1 && props.lastFrame - props.palFrame < props.startFrame &&
	    !(key.flags & 0x4000)) {
		closeVideo();
		return;
	}
	videoTargetFromFlags(props);
	if (!openVideo(file, props))
		return;
	if (props.palFrame == -1)
		props.palFrame = 0;
	if (props.lastFrame == -1)
		props.lastFrame = _video->imd.frameCount() - 1;
	int base = startFrame % std::max(1, props.lastFrame - props.palFrame + 1);
	props.endFrame = props.lastFrame;
	props.startFrame = props.lastFrame = base + props.palFrame;
	props.flags &= 0x7F;

	// VideoPlayer::play, one frame.
	props.startFrame--;
	props.endFrame--;
	props.palFrame--;
	_draw.showCursor = _video->keepCursor ? 3 : 0;
	if (props.startFrame != props.lastFrame && props.startFrame < _video->imd.frameCount() - 1)
		if (videoFrame(props))
			_draw.setPalette();
}

// VideoPlayer::play for the primary video: frames startFrame..lastFrame
// (counting from 0), each on time: with sound, frame n at n / rate seconds
// after the first; without, 1 / setFrameRate seconds after the previous one.
Task<void> Interpreter::playVideo(VideoProps &props) {
	VideoSlot &v = *_video;
	ImdDecoder &imd = v.imd;
	if (props.startFrame < 0)
		props.startFrame = imd.curFrame() + 1;
	if (props.lastFrame < 0)
		props.lastFrame = imd.frameCount() - 1;
	if (props.endFrame < 0)
		props.endFrame = props.lastFrame;
	if (props.palFrame < 0)
		props.palFrame = props.startFrame;
	props.startFrame--;
	props.endFrame--;
	props.palFrame--;

	_draw.showCursor = v.keepCursor ? 3 : 0;
	props.breakKey = kEscape;
	const bool backwards = props.startFrame > props.lastFrame;
	_inPlayImd = true;

	while (!_quit && !(props.startFrame == props.lastFrame || props.startFrame >= imd.frameCount() - 1)) {
		co_await playVideoFrame(props);
		if (props.canceled || !_video)
			break;
		props.startFrame += backwards ? -1 : 1;

		// The frame shows; wait for the next one's time.
		_frameBudget = 0;
		if (v.keepCursor) {
			co_await _sched.nextFrame();
		} else if (imd.hasSound()) {
			double due = v.startMs + (imd.curFrame() + 1) * 1000.0 / v.frameRate;
			do {
				co_await _sched.nextFrame();
				gameTicks();
			} while (!_quit && _gameMs < due);
		} else {
			co_await sleep(uint32_t(1000.0 / std::max(1.0, v.frameRate)));
		}
	}
	_inPlayImd = false;
}

// In place of a missing positioned clip (a talking head): what the screen
// shows, for 2.5 seconds or until a key or a click.
Task<void> Interpreter::holdMissingClip() {
	const uint32_t until = gameTicks() + 2500;
	forceMouseUp(false);
	while (!_quit && !_ffSkip && !_skipCutscene && int32_t(gameTicks() - until) < 0) {
		_frameBudget = 0;
		co_await _sched.nextFrame();
		animateCursor(1);
		if (checkKeys() != 0 || _mouseButtons != 0)
			break;
	}
	forceMouseUp(false);
}

// playImd (draw 0x83): name, x, y, startFrame, lastFrame, breakKey, flags,
// palStart, palEnd. An empty name works on the open video; lastFrame -1
// closes it afterwards; startFrame -2 opens it and shows the first frame,
// -1 only opens (or closes) it.
Task<void> Interpreter::playImd() {
	std::string name = evalString();
	if (name.size() > 8)
		name.resize(8);
	VideoProps props;
	props.x = evalVal();
	props.y = evalVal();
	props.startFrame = evalVal();
	props.lastFrame = evalVal();
	props.breakKey = evalVal();
	props.flags = uint16_t(evalVal());
	props.palStart = evalVal();
	props.palEnd = evalVal();
	props.palCmd = (props.flags & 0x3F) < 16 ? uint16_t(1 << (props.flags & 0x3F)) : 0;

	if (!name.empty()) {
		videoTargetFromFlags(props);
		if (!openVideo(name, props)) {
			trace("playImd \"" + name + "\": not found");
			writeVar(11, uint32_t(-1));
			// Goblins 3: some talking heads are missing from the data (GOG
			// lacks FORT21A/B, ROI2B, PILOU; and the room video SOR). Like
			// ScummVM the script just goes on, but a talking head's subtitle
			// is erased right after the video: keep it up for a while.
			if (_ver >= 3 && props.x != -1 && props.y != -1 && props.startFrame >= 0 && props.lastFrame == -1)
				co_await holdMissingClip();
			co_return;
		}
		props.needBlit = (props.flags & kFlagBackContent) && props.sprite == draw::kFrontSurface;
		trace("playImd \"" + _video->name + "\" frames " + std::to_string(props.startFrame) + ".." +
		      std::to_string(props.lastFrame) + " flags " + std::to_string(props.flags) +
		      (_video->imd.hasSound() ? " with sound" : ""));
	}

	bool close = props.lastFrame == -1;
	if (props.startFrame == -2) {
		props.startFrame = 0;
		props.lastFrame = 0;
		close = false;
	}
	if (props.startFrame >= 0 && _video)
		co_await playVideo(props);
	if (close)
		closeVideo();
}

// getImdInfo (draw 0x84): name, then variables for x, y, frame count, width
// and height (all -1 when the video is missing). Opening it writes VAR 7.
void Interpreter::getImdInfo() {
	std::string name = upper(evalString());
	VarRef vx = readVar(), vy = readVar(), vframes = readVar(), vw = readVar(), vh = readVar();
	if (name.find('.') == std::string::npos)
		name += ".IMD";
	ImdDecoder imd;
	auto data = _files.read(name);
	if (data && imd.load(std::move(*data))) {
		writeVar(7, uint32_t(imd.frameCount()));
		writeVarOffset(vx.offset, uint32_t(imd.defaultX()));
		writeVarOffset(vy.offset, uint32_t(imd.defaultY()));
		writeVarOffset(vframes.offset, uint32_t(imd.frameCount()));
		writeVarOffset(vw.offset, uint32_t(imd.width()));
		writeVarOffset(vh.offset, uint32_t(imd.height()));
	} else {
		for (const VarRef &r : {vx, vy, vframes, vw, vh})
			writeVarOffset(r.offset, uint32_t(-1));
	}
}

} // namespace gob
