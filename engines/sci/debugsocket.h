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
#include "common/events.h"
#include "common/rect.h"
#include "common/file.h"
#include "gui/debugger.h"
#include "sci/engine/vm_types.h"

namespace Sci {

class SciEngine;
class Console;

/**
 * A command channel for a script that drives the game.
 *
 * Every existing console command becomes callable from outside: a client
 * connects to the UNIX socket named by the `debug_socket` config key, sends
 * one line, and gets the command's output back followed by a line holding
 * a single '.'. The engine polls the socket once per frame from
 * Console::onFrame(), so a command runs between two game ticks, never in
 * the middle of one.
 *
 * On top of that, a few commands that only make sense from outside:
 *
 *   key <name>            press and release a key (Return, Escape, Tab,
 *                         F1..F12, Up/Down/Left/Right, KP_1..KP_9, or a
 *                         single character)
 *   type <text>           the characters of <text>, one key each
 *   click <x> <y> [r]     press and release a mouse button at lowres x,y
 *   move <x> <y>          move the mouse
 *   state                 one line of JSON: room, ego position, score, ...
 *   dump <path>           the hires frame buffer, text plane and lowres
 *                         buffer as raw files at <path>.*
 *   wait <cond> [<cond>]  delay the reply until the condition holds, e.g.
 *                         room == 5 / ego.x < 100 / ego in 10 20 30 40 /
 *                         text "Hello" / windows == 0 / global 15 == 3 /
 *                         sel ego loop == 2 / frames 30 / idle 10.
 *                         Two conditions joined by && or ||. The reply is
 *                         OK or TIMEOUT (after `timeout N` frames, default
 *                         600).
 *   record <path>           start writing a recording there; `record` with
 *                         no argument stops. debug_record=<path> in the
 *                         game's ini does the same without a socket, so a
 *                         human can just play. The file is flushed per
 *                         line; a run killed before it ends leaves
 *                         <path>.tmp, which is the same content.
 *
 * Why: a driver script that sleeps N seconds after a click lands on a
 * different animation cel every run, and a comparison of two runs then
 * measures timing, not rendering. A script that waits for the state it
 * needs is deterministic.
 */
class DebugSocket : public GUI::Debugger::OutputSink, public Common::EventObserver {
public:
	DebugSocket(SciEngine *engine, Console *console);
	~DebugSocket() override;

	/** Bind and listen. Returns false (and logs) when that fails. */
	bool open(const Common::String &path);

	/**
	 * Start writing a recording to @p path: one line per game tick with the
	 * state, one line per input event with the state it arrived in.
	 * A human plays the game normally; harness/i18n/rec2script.py turns the
	 * file into a driver script whose waits are conditions on the state the
	 * player was actually in, not the seconds they took to get there.
	 */
	bool startRecording(const Common::String &path);
	void stopRecording();

	/** EventObserver: every event the dispatcher hands out, never eaten. */
	bool notifyEvent(const Common::Event &ev) override;

	/** Called often (VM loop, event poll): accepts a client, runs a command. */
	void onFrame();

	/** Called once per game tick (kAnimate): the frame counter and waits. */
	void tick();

	// OutputSink
	void write(const char *text) override;

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

	void pollAccept();
	bool readLine(Common::String &line);
	void reply(const Common::String &text);
	void runCommand(const Common::String &line);
	bool ownCommand(const Common::String &cmd, const Common::Array<Common::String> &args);
	/// For each argument, the unsplit remainder of the command line from it.
	Common::Array<Common::String> _argTails;
	bool parseCond(const Common::Array<Common::String> &args, uint &i, Cond &c);
	bool cmp(int32 lhs, const Common::String &op, int32 rhs);

	void sendKey(const Common::String &name);
	/** Key hold: re-injects the keydown every few ticks the way SDL's
	 * auto-repeat does, so SCI's ego controller walks continuously.
	 * SCI stops on key-up, which a single press/release pair delivers
	 * after one 4-pixel step. */
	void holdKey(const Common::String &name, int ticks);
	void releaseKey();
	void holdTick();
	void sendClick(int x, int y, bool right);
	Common::Point toBackend(int x, int y) const;
	void sendMove(int x, int y);
	Common::String stateJson();
	Common::String objectsJson();
	bool dumpBuffers(const Common::String &path);

	SciEngine *_engine;
	Console *_console;
	Common::ArtificialEventSource _events;
	// One key at a time, each held until the game has polled for keys at
	// least once since it was queued (see _keyArmedAt). A text edit control
	// reads one key per kGetEvent, so a burst in one frame left all but the
	// first on the floor; and a key handed over during a pic transition was
	// dropped outright, because GfxTransitions::updateScreen() discards
	// every pending event to keep the wipe smooth.
	Common::Array<Common::String> _pendingKeys;
	Common::Event _pendingRelease;	///< key-up for the last key-down sent
	bool _haveRelease;
	Common::String _holdName;		///< key being held down (empty = none)
	int _holdTicks;			///< ticks left in the hold
	bool _holdPending;		///< opening KEYDOWN still waiting for listening()
	uint32 _lastKeyMs;
	// Console::onFrame() fires once per VM instruction; polling the socket
	// that often is all syscall and no progress.
	static const uint kPollInstructions = 256;
	uint _sinceLastPoll;
	int _listenFd, _clientFd;		// POSIX
#if defined(WIN32)
	void *_pipe;				// HANDLE; a named pipe, one instance
	void *_connectOv;			// OVERLAPPED for the pending ConnectNamedPipe
	bool _pipeConnected;
	Common::String _pipeName;
	bool createPipe();
	void dropClient();
#endif
	Common::String _inBuf, _outBuf;
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

	Common::DumpFile *_recFile;	///< recording, or null
	Common::String _recLastState;	///< last state line written, to skip repeats
	void recordLine(char kind, const Common::String &payload);
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
