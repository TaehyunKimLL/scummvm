/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef SCI_DEBUGSOCKET_H
#define SCI_DEBUGSOCKET_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/array.h"
#include "common/rect.h"
#include "common/str-array.h"
#include "gui/debugsocket.h"
#include "sci/engine/vm_types.h"

namespace Sci {

class SciEngine;

/**
 * SCI's commands on the debug socket (GUI::DebugSocket, gui/debugsocket.h).
 *
 * The socket itself - the transport, the recorder, `key`/`type`/`click`/
 * `move`, `save`/`load`, `record` and every console command - is the
 * engine-neutral one. What is here needs the SCI VM:
 *
 *   state                 one line of JSON: room, ego position, score, the
 *                         texts and buttons drawn, whether keys are taken
 *   dump <path>           the hires frame buffer, text plane, lowres,
 *                         control and priority buffers as raw files at
 *                         <path>_*.bin
 *   wait <cond> [<cond>]  delay the reply until the condition holds, e.g.
 *                         room == 5 / ego.x < 100 / ego in 10 20 30 40 /
 *                         text "Hello" / windows == 0 / global 15 == 3 /
 *                         sel ego loop == 2 / frames 30 (game ticks) /
 *                         idle 10 / button "Begin Game" / listening.
 *                         Two conditions joined by && or ||. The reply is
 *                         OK or TIMEOUT (after `timeout N` ticks, default
 *                         600).
 *   hold/walk/walked/release, pause/resume/step, objs, get
 *
 * It also paces the socket's keys: a key goes out only once the game has
 * polled for keys since the last pic transition (listening()), and it maps
 * `click` coordinates through the hires driver.
 *
 * Why waits: a driver script that sleeps N seconds after a click lands on
 * a different animation cel every run, and a comparison of two runs then
 * measures timing, not rendering. A script that waits for the state it
 * needs is deterministic.
 */
class DebugSocket : public GUI::DebugSocketExtension {
public:
	DebugSocket(SciEngine *engine, GUI::DebugSocket *socket);
	~DebugSocket() override;

	// GUI::DebugSocketExtension
	bool handle(const Common::String &cmd, const Common::StringArray &args, Common::String &reply) override;
	bool replyPending() const override { return _wait.active; }
	void poll() override;
	bool inputReady() override { return listening(); }
	bool paceInput() override;
	Common::Point toBackend(const Common::Point &p) override;
	Common::Point fromBackend(const Common::Point &p) override;
	Common::String recordState() override { return stateJson(); }

	/** Called once per game tick (kAnimate): the frame counter and waits. */
	void tick();
	void postAnimate();

	// The state the wait conditions and `state` read.
	struct Snapshot {
		uint16 room, prevRoom, score;
		int16 egoX, egoY, egoLoop, egoCel, egoView;
		uint32 frame;
		uint windows;
	};
	Snapshot snapshot() const;

	/** GfxText16::Box() reports every text it draws here. */
	void noteText(const char *text, const Common::Rect &rect);

	/**
	 * GfxControls16::kernelTexteditChange() reports the parser line here
	 * every frame it is live, with its content. `input` in the state is
	 * true while that keeps happening.
	 */
	void noteInput(const Common::String &text);
	bool inputLive() const;

	/**
	 * kGetEvent() reports each poll here. `listening` in the state is true
	 * once the game has asked for keyboard events on the current tick: a
	 * key sent before that (room setup, a cutscene) is simply dropped.
	 */
	void noteGetEvent(uint16 mask);
	bool listening() const;

	/**
	 * GfxTransitions::doit() reports here. A transition discards every
	 * queued input event, so a key sent while one is pending is lost;
	 * `listening` stays false until the first kGetEvent after it.
	 */
	void noteTransition();

	/**
	 * kDrawControl() reports every button it draws, with its rect in the
	 * current port. `buttons` in the state lists them with screen
	 * coordinates, so a script clicks by label, not by guessed pixel.
	 */
	void noteButton(const Common::String &label, const Common::Rect &rect);

	/** "ego", "room", or any object name the segment manager knows. */
	reg_t objByName(const Common::String &name) const;
	bool egoXY(int &x, int &y) const;

private:
	struct Cond {
		enum Kind { kRoom, kEgoX, kEgoY, kEgoIn, kText, kWindows, kGlobal, kSel, kFrames, kIdle, kInput, kNoInput, kListening, kInputText, kButton, kSeen, kBad };
		Kind kind;
		Common::String op;		// "==", "!=", "<", "<=", ">", ">="
		int32 a, b, c, d;		// operands
		Common::String obj, sel, text;
		uint32 startFrame;
		uint32 startSample;	///< _idleSamples when the wait was set up
		bool eval(DebugSocket &ds);
	};
	struct Wait {
		Common::Array<Cond> conds;
		bool anyOf;				// || rather than &&
		uint32 deadline;		// in game ticks
		uint32 deadlineMs;		// wall clock, for when ticks stop coming
		bool active;
	};

	/** Conditions once; replies OK or TIMEOUT when it is time. */
	void pollWait();

	/** Hash the lowres display; feeds `idle`. */
	void sampleDisplay();

	/// For each argument, the unsplit remainder of the command line from it.
	Common::StringArray _argTails;
	bool parseCond(const Common::Array<Common::String> &args, uint &i, Cond &c);
	bool cmp(int32 lhs, const Common::String &op, int32 rhs);

	/** Key hold: re-injects the keydown every few ticks the way SDL's
	 * auto-repeat does, so SCI's ego controller walks continuously.
	 * SCI stops on key-up, which a single press/release pair delivers
	 * after one 4-pixel step. */
	void holdKey(const Common::String &name, int ticks, int maxPx = 0);
	void releaseKey();
	void holdTick();
	/** A key-down (or, @p up, key-up) of a named key, straight to the game. */
	void pushKey(const Common::String &name, bool up = false);
	Common::String stateJson();
	Common::String objectsJson();
	bool dumpBuffers(const Common::String &path);

	SciEngine *_engine;
	GUI::DebugSocket *_socket;
	Common::String _holdName;		///< key being held down (empty = none)
	int _holdTicks;			///< ticks left in the hold
	bool _holdPending;		///< opening KEYDOWN still waiting for listening()
	int _holdMaxPx;			///< stop the walk after this many px (0 = no limit)
	int _holdStartX, _holdStartY;	///< ego position when the hold began
	Common::String _capName;	///< key whose walk is still being distance-capped
	int _capPx;			///< that cap, in pixels
	int _capFrames;			///< frames spent waiting for it to trip
	Wait _wait;
	uint32 _timeoutFrames;
	uint32 _frame;

	// Tick-level pause. `tick()` runs once per kAnimate, i.e. once per game
	// tick, while `onFrame()` runs per VM instruction -- so parking the game
	// inside tick() still leaves the socket responsive and commands land as
	// usual. That is what lets a driver step the world one tick at a time and
	// inspect between ticks: a walk can be stopped on the tick before the ego
	// would enter a lethal cell, which polling every few frames cannot do.
	bool _paused;			///< game ticks are held in tick()
	uint32 _stepTicks;		///< ticks still owed by `step`, then re-park

	// Every text drawn recently, with its frame; `wait text` looks for one
	// drawn since the wait began, `state` reports the latest frame's.
	struct TextEvent {
		uint32 frame;
		Common::String text;
		Common::Rect rect;	// screen coordinates: where it was drawn
	};
	Common::Array<TextEvent> _textLog;
	uint32 _getEventFrame;		///< last frame kGetEvent asked for keys
	uint32 _getEventCount;		///< kGetEvent polls so far
	struct Button {
		uint32 frame;
		Common::String label;
		Common::Rect rect;	// screen coordinates
	};
	Common::Array<Button> _buttons;
	uint32 _transitionPoll;		///< _getEventCount at the last transition
	uint32 _listenSince;		///< frame of the last transition
	uint16 _lastRoom;

	Common::String _recLastState;	///< last state line written, to skip repeats
	uint32 _inputPoll;		///< _getEventCount when the edit control last reported
	Common::String _inputText;	///< its content then

	// For `idle`: a checksum of the lowres display per frame.
	uint32 _lastDisplayHash;
	uint32 _idleFrames;
	// Samples taken, so `idle N` can mean "N unchanged samples since this
	// wait began" rather than "the screen happens to be N-idle right now",
	// which is trivially true whenever nothing has moved for a while and
	// made `wait ... || idle N` return before the walk it was pacing had
	// even started.
	uint32 _idleSamples;
	uint32 _lastSampleMs;
};

} // End of namespace Sci

#endif
