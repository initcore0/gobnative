// gob2native - native reimplementation of the Coktel Vision Gob engine
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Skipping. The intro and the ending are full-screen animations (playMult)
// synchronised to the CD; Escape stops them, as in the original. Everything
// else the player sits through without control is ordinary room script:
// MENU and the INTER picture between two rooms, a room's entry function (a
// world's intermission picture waiting for its CD track to end, keyframed
// multSub sequences of the goblins arriving, fades) before its main loop,
// the exit sequence after v[60] = 2, and synchronous
// `repeat ... animate ... until v[18] == -1` loops a room's logic runs from
// its timer tick (docs/levels.md 3, docs/animation.md 2.7).
//
// Cutting such a sequence short would leave objects, goblins and variables
// wherever the script happened to be, so these are fast-forwarded instead:
// the scripts run unchanged, as many engine frames per host frame as time
// allows, and each engine frame advances the game clock and the CD timeline
// by one 60 Hz frame, exactly what a frame does at normal speed. So the
// scripts see the same sequence of frames, timer ticks and CD positions as
// when the sequence plays normally, only faster. Music and effects are
// silent meanwhile (the effect voice is timed on the game clock so that
// scripts testing it see it play for as long as it would), and the CD music
// is resynchronised to the timeline when the skip ends.

#include "engine/interpreter.h"

#include <chrono>
#include <cmath>
#include <limits>

namespace gob {

namespace {

constexpr double kSkipFrameMs = 1000.0 / 60; // game time per engine frame while skipping
constexpr int kSkipMaxFrames = 1200;         // engine frames per host frame (20 s of game time)
constexpr double kNoControlMs = 250;         // the room's loop hasn't waited for input this long
constexpr double kIdleGraceMs = 3000;        // idle, but keyframed sequences go on (ambient ones)
constexpr double kSkipMaxMs = 180000;        // give up after this much game time

constexpr uint32_t kVarLevelExit = 60; // 0 playing, 2 walking out, 1 leave now
constexpr uint32_t kVarMultSlots = 18; // v[18]..v[21]: frame of multSub slot 0..3, -1 = idle
constexpr uint32_t kGob3LeaveRoom = 3670; // Goblins 3: byte, 1 = the room's main loop ends

} // namespace

// A scripted sequence that fast-forwarding may run through: see interpreter.h.
bool Interpreter::inSkippableSequence() const {
	if (!_vars || _cur != int(_envs.size()) - 1 || _waitingForPlayer)
		return false; // a function of an outer TOT (the icon bar), or waiting for a key or click
	if (!_rules.hasFastForward())
		return false;
	if (_rules.isTransitionTot(_envs.back()->name))
		return true;
	if (_rules.levels == LevelSystem::Gob1) {
		// Gobliiins: a level's start (its story text, the room fading in) up
		// to its main loop, and what it runs instead of its main loop (lost,
		// solved: the access code until a click).
		if (currentLevelTotV1().empty())
			return false;
		if (!_roomLoopReached)
			return true;
		return !_atLevelLoop && _gameMs - _lastControlMs > kNoControlMs;
	}
	if (_rules.levels == LevelSystem::Gob3) {
		// Goblins 3: a room's entry up to its main loop, the room being left
		// (b@3670), and what its logic runs instead of the main loop.
		if (!roomUnderMenuV3())
			return false;
		if (!_roomLoopReached || _vars->read8(kGob3LeaveRoom) != 0)
			return true;
		return !_atLevelLoop && _gameMs - _lastControlMs > kNoControlMs;
	}
	if (!roomUnderMenu())
		return false;
	if (!_roomLoopReached || _vars->slot(kVarLevelExit) != 0)
		return true; // entering or leaving
	return !_atLevelLoop && _gameMs - _lastControlMs > kNoControlMs;
}

bool Interpreter::canSkip() const {
	return inCutscene() || (!_ffSkip && inSkippableSequence());
}

void Interpreter::skipCutscene() {
	// A video inside a scripted sequence (Goblins 3's intermissions and
	// dialogues) is fast-forwarded with the rest of the sequence.
	if (inCutscene() && !(_inPlayImd && !_ffSkip && inSkippableSequence()))
		_skipCutscene = true;
	else if (!_ffSkip && inSkippableSequence())
		startFastForward();
}

void Interpreter::startFastForward() {
	_ffSkip = true;
	_ffStartMs = _gameMs;
	_ffIdleSince = -1;
	_ffCdChanged = false;
	_ffSample = {};
	_mixer.stopSample(0, 50);
	_mixer.stopSample(Mixer::kSpeakerChannel);
	if (_video && _video->soundPlaying) { // a video's speech (Goblins 3)
		_mixer.stopSpeech();
		_video->soundPlaying = false;
	}
	_frameCredit = 0;
	trace("skip: fast-forwarding " + _envs.back()->name);
}

// Why the fast-forward is over, or null while it goes on. Checked after
// every engine frame.
const char *Interpreter::fastForwardEnd() {
	if (_quit || !_sched.waiting())
		return "game ended";
	if (inCutscene() && !_inPlayImd)
		return "a cutscene starts"; // the intro and the ending have their own skip
	if (_waitingForPlayer)
		return "the game waits for the player";
	const std::string &top = _envs.back()->name;
	if (_rules.levels == LevelSystem::Gob1) {
		if (!_rules.isTransitionTot(top) && currentLevelTotV1().empty())
			return "another screen starts";
		if (_gameMs - _ffStartMs > kSkipMaxMs)
			return "time limit";
		if (levelRunningV1() && _roomLoopReached && _atLevelLoop)
			return "the player has control";
		return nullptr;
	}
	if (_rules.levels == LevelSystem::Gob3) {
		const bool room = _envs.size() >= 2 && _envs[_envs.size() - 2]->name == "MENU" && top.rfind("EMAP", 0) == 0;
		if (!_rules.isTransitionTot(top) && !room && top != "MENU")
			return "another screen starts";
		if (_gameMs - _ffStartMs > kSkipMaxMs)
			return "time limit";
		if (roomUnderMenuV3() && _roomLoopReached && _atLevelLoop && levelIdleV3())
			return "the player has control";
		return nullptr;
	}
	if (!_rules.isTransitionTot(top) && !levelWorld(top))
		return "another screen starts";
	if (_gameMs - _ffStartMs > kSkipMaxMs)
		return "time limit";

	if (roomUnderMenu() && _roomLoopReached && levelIdle()) {
		if (!_atLevelLoop)
			return nullptr; // between two waits of the main loop (its timer tick)
		bool sequences = false;
		for (uint32_t i = 0; i < 4; i++)
			sequences |= _vars->slot(kVarMultSlots + i) != uint32_t(-1);
		if (!sequences)
			return "the player has control";
		if (_ffIdleSince < 0)
			_ffIdleSince = _gameMs;
		else if (_gameMs - _ffIdleSince >= kIdleGraceMs)
			return "the player has control (animations still running)";
	} else {
		_ffIdleSince = -1;
	}
	return nullptr;
}

void Interpreter::stopFastForward(const char *why) {
	_ffSkip = false;
	resyncCdAudio();
	if (std::isinf(_ffSample.end)) // a looping effect started meanwhile goes on
		blasterPlay(_ffSample.slot, _ffSample.repCount, _ffSample.freq, 0);
	_ffSample = {};
	_hostLast = _host.ticks();
	_frameCredit = 0;
	trace("skip: done, " + std::string(why) + " (" + std::to_string(int(_gameMs - _ffStartMs)) +
	      " ms of game time)");
}

void Interpreter::fastForwardFrame() {
	using Clock = std::chrono::steady_clock;
	const auto t0 = Clock::now();
	gameTicks(); // real time since the last host frame counts as usual
	for (int i = 0; i < kSkipMaxFrames && _ffSkip; i++) {
		if (!_sched.waiting()) {
			stopFastForward("game ended");
			break;
		}
		_gameMs += kSkipFrameMs;
		_cdClockOffset += kSkipFrameMs;
		_sched.resume();
		if (!_ffSkip && _rules.levels == LevelSystem::Gob1)
			break; // stopped by the frame (a level request)
		if (const char *why = fastForwardEnd()) {
			stopFastForward(why);
			break;
		}
		if (skipFrameBudgetMs > 0 &&
		    std::chrono::duration<double, std::milli>(Clock::now() - t0).count() >= skipFrameBudgetMs)
			break;
	}
}

// After a fast-forward the CD timeline is ahead of the music the mixer was
// playing: play what the timeline says from where it says.
void Interpreter::resyncCdAudio() {
	const CdAudio::Track *t = _cd.current();
	int32_t pos = t ? _cd.position({}, cdTicks()) : -1;
	if (!t || pos < 0 || pos >= int32_t(t->end - t->start + 1)) {
		_mixer.stopCd(); // over for the game (a room restarts its loop track itself)
		return;
	}
	if (!_ffCdChanged && t->name == _cdReplacing && _mixer.cdPlaying())
		return; // a replacement file can't seek; it goes on
	std::string note;
	startCdMusic(*t, pos, note);
	trace("skip: CD music \"" + t->name + "\" from " + std::to_string(pos / 75) + " s" + note);
}

} // namespace gob
