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

#include "sci/debugsocket.h"

#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "sci/console.h"
#include "sci/sci.h"
#include "sci/engine/kernel.h"
#include "sci/engine/seg_manager.h"
#include "sci/engine/selector.h"
#include "sci/engine/state.h"
#include "sci/engine/script.h"
#include "sci/engine/vm.h"
#include "sci/graphics/ports.h"
#include "sci/graphics/screen.h"
#include "sci/graphics/textlayer.h"
#include "sci/event.h"
#include "sci/graphics/drivers/gfxdriver.h"
#include "graphics/surface.h"

namespace Sci {

DebugSocket::DebugSocket(SciEngine *engine, GUI::DebugSocket *socket) :
	_engine(engine), _socket(socket),
	_timeoutFrames(600), _frame(0), _getEventFrame(0), _getEventCount(0), _transitionPoll(0), _listenSince(0), _lastRoom(0xffff), _inputPoll(0), _holdPending(false), _holdMaxPx(0), _holdStartX(0), _holdStartY(0), _capPx(0), _capFrames(0),
	_lastDisplayHash(0), _idleFrames(0), _idleSamples(0), _lastSampleMs(0),
	_paused(false), _stepTicks(0) {
	_wait.active = false;
	_wait.anyOf = false;
	_wait.deadline = 0;
	_wait.deadlineMs = 0;
}

DebugSocket::~DebugSocket() {
}

// A recorded click: the event's own coordinates, scaled down by the
// driver. Asking the manager for getMousePos() instead returns the
// position from BEFORE this event, which for a click with no preceding
// move is 0,0 - measured, every recorded click came out "click 0 0".
Common::Point DebugSocket::fromBackend(const Common::Point &backend) {
	Common::Point p(backend);
	GfxDriver *drv = _engine->_gfxScreen ? _engine->_gfxScreen->gfxDriver() : nullptr;
	if (drv) {
		const Common::Point probe = drv->mousePosToBackend(Common::Point(1000, 1000));
		if (probe.x > 0 && probe.y > 0) {
			p.x = p.x * 1000 / probe.x;
			p.y = p.y * 1000 / probe.y;
		}
	}
	return p;
}

// ---- per frame ----------------------------------------------------------

// Called right after kAnimate has moved the actors: the walk budget has
// to be judged on the positions the frame actually produced, not on the
// ones it started from.
void DebugSocket::postAnimate() {
	holdTick();
}

void DebugSocket::tick() {
	_frame++;
	holdTick();

	// Tick-level pause: park the game here, but keep servicing the socket
	// from onFrame() so `state`, `dump`, `get` and `resume` still work. A
	// driver can therefore advance the world one tick at a time and look
	// between ticks -- the difference between stopping a walk on the tick
	// before the ego steps onto a lethal cell and discovering the fall
	// several frames later.
	while (_paused && _stepTicks == 0 && !_engine->shouldQuit()) {
		_socket->onFrame();
		g_system->delayMillis(2);
	}
	if (_stepTicks > 0)
		_stepTicks--;

	// Buttons belong to the screen they were drawn on; a room change or
	// a transition (new picture) takes them with it.
	{
		const uint16 room = _engine->getEngineState()->currentRoomNumber();
		if (room != _lastRoom) {
			_buttons.clear();
			_lastRoom = room;
		}
	}
	sampleDisplay();
	// One state line per tick while recording, skipping ticks where nothing
	// a condition could test has changed. The key deliberately drops `frame`
	// and `idle`, which move every tick: keeping them would write a line per
	// tick, ~14,000 for a twenty-minute session, and none of the extra lines
	// would tell a generated script anything.
	if (_socket->recording()) {
		const Common::String s = stateJson();
		Common::String key = s;
		for (const char *field : { "\"frame\":", "\"idle\":" }) {
			const char *at = strstr(key.c_str(), field);
			if (!at)
				continue;
			const uint start = at - key.c_str() + strlen(field);
			uint end = start;
			while (end < key.size() && key[end] != ',' && key[end] != '}')
				end++;
			key = Common::String(key.c_str(), start) + Common::String(key.c_str() + end);
		}
		if (key != _recLastState) {
			_socket->recordLine('S', s);
			_recLastState = key;
		}
	}

	pollWait();
}

// `idle`: how many consecutive samples the lowres display has not changed.
// Sampled from both tick() and the event poll, because a modal text box
// stops calling kAnimate entirely - counting only ticks, `idle` never
// advanced while a box was up, which is exactly when a harness wants to
// know the screen has settled. Rate-limited to one sample per 16ms: the
// event poll runs every 256 VM instructions, so an unlimited sampler ran
// through "12 idle samples" in well under a millisecond and `idle N`
// meant nothing at all.
void DebugSocket::sampleDisplay() {
	const uint32 now = g_system->getMillis();
	if (now - _lastSampleMs < 16)
		return;
	_lastSampleMs = now;
	const byte *d = _engine->_gfxScreen ? _engine->_gfxScreen->displayScreen() : nullptr;
	const uint n = d ? _engine->_gfxScreen->displayPixels() : 0;
	if (!n)
		return;
	uint32 h = 2166136261u;
	for (uint i = 0; i < n; i += 7)
		h = (h ^ d[i]) * 16777619u;
	if (h == _lastDisplayHash)
		_idleFrames++;
	else
		_idleFrames = 0;
	_lastDisplayHash = h;
	_idleSamples++;
}

void DebugSocket::pollWait() {
	if (!_wait.active || _socket->inputPending() || _holdPending)
		return;
	bool done = _wait.anyOf ? false : true;
	for (uint i = 0; i < _wait.conds.size(); i++) {
		const bool r = _wait.conds[i].eval(*this);
		done = _wait.anyOf ? (done || r) : (done && r);
	}
	if (done) {
		_wait.active = false;
		_socket->reply("OK");
	} else if (_frame >= _wait.deadline || g_system->getMillis() >= _wait.deadlineMs) {
		// The tick deadline is the one that means something; the wall
		// clock is a backstop for when ticks stop coming at all, which
		// is what a modal box waiting for a keypress does.
		_wait.active = false;
		_socket->reply("TIMEOUT " + stateJson());
	}
}

// Once per socket poll (every 256th VM instruction), after the socket has
// sent its queued input. The screen also settles while a modal box is up,
// when no tick runs, so `idle` is sampled here as well as in tick().
void DebugSocket::poll() {
	sampleDisplay();
	pollWait();
}

// The input slot of a poll, taken before the socket's own key queue: the
// opening press of a hold goes through the same gate as `key` (the socket
// asks listening() first), so a toggled walk cannot lose its start or its
// stop. Why the gate at all: two things eat a key handed over at the wrong
// moment -
//   - a pic transition, whose updateScreen() drains the event queue
//     outright, so a key sent during the wipe into a room simply never
//     existed (3 of 8 launches failed to open KQ1's parser line for
//     exactly this reason);
//   - a modal edit control, which takes one key per kGetEvent.
// listening() covers both: it is false during and just after a transition,
// and it follows the game's own polling, so a burst is paced at the rate
// the control consumes it.
bool DebugSocket::paceInput() {
	if (!_holdPending)
		return false;
	pushKey(_holdName);
	_holdPending = false;
	return true;
}

// `ego` and `room` are the two objects every wait cares about; both live
// in globals, and the room's name changes every room (rm1, rm86, ...).
reg_t DebugSocket::objByName(const Common::String &name) const {
	EngineState *s = _engine->getEngineState();
	if (name == "ego")
		return s->variables[VAR_GLOBAL][kGlobalVarEgo];
	if (name == "room")
		return s->variables[VAR_GLOBAL][kGlobalVarCurrentRoom];
	return s->_segMan->findObjectByName(name);
}

// Ego position, for the hold distance limit below.
bool DebugSocket::egoXY(int &x, int &y) const {
	EngineState *s = _engine->getEngineState();
	if (!s)
		return false;
	reg_t ego = s->variables[VAR_GLOBAL][kGlobalVarEgo];
	if (ego.isNull())
		return false;
	const int xSel = _engine->getKernel()->findSelector("x");
	const int ySel = _engine->getKernel()->findSelector("y");
	if (xSel < 0 || ySel < 0)
		return false;
	x = readSelectorValue(s->_segMan, ego, xSel);
	y = readSelectorValue(s->_segMan, ego, ySel);
	return true;
}

void DebugSocket::noteGetEvent(uint16 mask) {
	_getEventCount++;
	if (mask & kSciEventKeyDown)
		_getEventFrame = _frame;
}

void DebugSocket::noteButton(const Common::String &label, const Common::Rect &rect) {
	Button b;
	b.frame = _frame;
	b.label = label;
	b.rect = rect;
	// Port-relative to screen: the port's origin is where the control's
	// window sits (the title screen draws in the picture window at y=10).
	GfxPorts *ports = _engine->_gfxPorts;
	if (ports && ports->getPort())
		b.rect.translate(ports->getPort()->left, ports->getPort()->top);
	for (uint i = 0; i < _buttons.size(); i++)
		if (_buttons[i].label == label) {
			_buttons.remove_at(i);
			break;
		}
	_buttons.push_back(b);
	while (_buttons.size() > 16)
		_buttons.remove_at(0);
}

void DebugSocket::noteTransition() {
	_transitionPoll = _getEventCount;
	_listenSince = _frame;
	_buttons.clear();
}

// Keys are taken once the game polls for them on the current tick - and
// not on the poll a transition is about to flush: the room script draws
// the picture, kAnimate runs the transition, and everything queued in
// between is discarded. Require a poll after the last transition.
bool DebugSocket::listening() const {
	return _getEventFrame != 0 && _getEventFrame + 1 >= _frame && _getEventCount > _transitionPoll;
}

// `listening N` also asks that the polling has gone on for N ticks since
// the last transition: a room's entry script (the walk in from the
// door) polls too, so a bare `listening` is true before the room is
// ready for a command, and its `script` selector is not yet set on the
// first frames either.

void DebugSocket::noteInput(const Common::String &text) {
	_inputPoll = _getEventCount;
	_inputText = text;
}

// A live edit control is serviced after every event poll (the script's
// handleEvent loop feeds each event to it). Measured in polls, not
// ticks: while the line is up the game is modal and kAnimate - the tick
// - never runs, and while a response box is up after Return, likewise.
bool DebugSocket::inputLive() const {
	return _inputPoll != 0 && _inputPoll + 3 >= _getEventCount;
}

void DebugSocket::noteText(const char *text, const Common::Rect &rect) {
	TextEvent e;
	e.frame = _frame;
	e.text = text;
	e.rect = rect;
	GfxPorts *ports = _engine->_gfxPorts;
	if (ports && ports->getPort())
		e.rect.translate(ports->getPort()->left, ports->getPort()->top);
	// Collapse a redraw of the same text on the same frame (the title menu
	// redraws its four buttons every frame) so the log holds history, not
	// repetition.
	for (uint i = 0; i < _textLog.size(); i++)
		if (_textLog[i].text == e.text) {
			_textLog.remove_at(i);
			break;
		}
	_textLog.push_back(e);
	while (_textLog.size() > 32)
		_textLog.remove_at(0);
}

// ---- commands -----------------------------------------------------------

// A JSON string body: the state is parsed by the client, and a `seen`
// condition is matched against the engine's own text, so the two must be
// the same bytes. Replacing quotes with apostrophes to keep the JSON
// valid - which an earlier version did - silently rewrote the text: the
// game says "xyzzy." and the recording said 'xyzzy.', so a generated
// script waited for a string that was never drawn.
static Common::String jsonEscape(const Common::String &in) {
	Common::String out;
	for (uint i = 0; i < in.size(); i++) {
		const char ch = in[i];
		switch (ch) {
		case '"':  out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if ((byte)ch < 0x20)
				out += Common::String::format("\\u%04x", ch);
			else
				out += ch;
		}
	}
	return out;
}

bool DebugSocket::cmp(int32 lhs, const Common::String &op, int32 rhs) {
	if (op == "==") return lhs == rhs;
	if (op == "!=") return lhs != rhs;
	if (op == "<") return lhs < rhs;
	if (op == "<=") return lhs <= rhs;
	if (op == ">") return lhs > rhs;
	if (op == ">=") return lhs >= rhs;
	return false;
}

bool DebugSocket::parseCond(const Common::Array<Common::String> &a, uint &i, Cond &c) {
	c.kind = Cond::kBad;
	c.a = c.b = c.c = c.d = 0;
	c.startFrame = _frame;
	c.startSample = _idleSamples;
	if (i >= a.size())
		return false;
	const Common::String w = a[i++];
	auto num = [&](int32 &v) -> bool { if (i >= a.size()) return false; v = atoi(a[i++].c_str()); return true; };
	auto opnum = [&](int32 &v) -> bool { if (i + 1 >= a.size()) return false; c.op = a[i++]; v = atoi(a[i++].c_str()); return true; };

	if (w == "room") { c.kind = Cond::kRoom; return opnum(c.a); }
	if (w == "ego.x") { c.kind = Cond::kEgoX; return opnum(c.a); }
	if (w == "ego.y") { c.kind = Cond::kEgoY; return opnum(c.a); }
	if (w == "ego" && i < a.size() && a[i] == "in") { i++; c.kind = Cond::kEgoIn; return num(c.a) && num(c.b) && num(c.c) && num(c.d); }
	if (w == "windows") { c.kind = Cond::kWindows; return opnum(c.a); }
	if (w == "global") { c.kind = Cond::kGlobal; return num(c.a) && opnum(c.b); }
	if (w == "sel") { c.kind = Cond::kSel; if (i + 1 >= a.size()) return false; c.obj = a[i++]; c.sel = a[i++]; return opnum(c.a); }
	if (w == "frames") { c.kind = Cond::kFrames; return num(c.a); }
	if (w == "idle") { c.kind = Cond::kIdle; return num(c.a); }
	if (w == "input") { c.kind = Cond::kInput; return true; }
	if (w == "noinput") { c.kind = Cond::kNoInput; return true; }
	if (w == "listening") { c.kind = Cond::kListening; c.a = 0; if (i < a.size() && (a[i][0] >= '0' && a[i][0] <= '9')) num(c.a); return true; }
	// `seen` and `text` take the REST OF THE LINE, not one token: a game
	// string contains spaces and often quotes of its own ("xyzzy."), and
	// tokenising it meant a generated script waited on a fragment.
	if (w == "seen" || w == "text") {
		c.kind = (w == "seen") ? Cond::kSeen : Cond::kText;
		if (i >= a.size())
			return false;
		// The rest of the line verbatim, quotes and all.
		c.text = (i < _argTails.size()) ? _argTails[i] : a[i];
		i = a.size();
		return true;
	}
	if (w == "button") { c.kind = Cond::kButton; if (i >= a.size()) return false; c.text = a[i++]; return true; }
	if (w == "inputText") { c.kind = Cond::kInputText; if (i >= a.size()) return false; c.text = a[i++]; return true; }
	return false;
}

bool DebugSocket::Cond::eval(DebugSocket &ds) {
	const Snapshot s = ds.snapshot();
	switch (kind) {
	case kRoom: return ds.cmp(s.room, op, a);
	case kEgoX: return ds.cmp(s.egoX, op, a);
	case kEgoY: return ds.cmp(s.egoY, op, a);
	case kEgoIn: return s.egoX >= a && s.egoY >= b && s.egoX <= c && s.egoY <= d;
	case kText: {
		// A box is drawn once and sits there, so "drawn since the wait
		// began" is the test, not "drawn this frame".
		for (uint i = 0; i < ds._textLog.size(); i++)
			if (ds._textLog[i].frame >= startFrame && strstr(ds._textLog[i].text.c_str(), text.c_str()))
				return true;
		return false;
	}
	case kWindows: return ds.cmp(s.windows, op, a);
	case kGlobal: {
		EngineState *st = ds._engine->getEngineState();
		if (!st || a < 0 || a >= st->variablesMax[VAR_GLOBAL])
			return false;
		return ds.cmp(st->variables[VAR_GLOBAL][a].toSint16(), op, b);
	}
	case kSel: {
		SegManager *sm = ds._engine->getEngineState()->_segMan;
		reg_t o = ds.objByName(obj);
		if (o.isNull())
			return false;
		const int selId = ds._engine->getKernel()->findSelector(sel.c_str());
		if (selId < 0)
			return false;
		reg_t v = readSelector(sm, o, selId);
		// A pointer-valued selector (script, mover, cycler: an object or
		// null) compares as 0 / 1, so `sel room script == 0` means "the
		// room's cutscene script is gone".
		if (v.getSegment() != 0)
			return ds.cmp(1, op, a);
		return ds.cmp(v.toSint16(), op, a);
	}
	case kFrames: return (int32)(ds._frame - startFrame) >= a;
	case kIdle: return (int32)ds._idleFrames >= a &&
		(int32)(ds._idleSamples - startSample) >= a;
	case kInput: return ds.inputLive();
	case kNoInput: return !ds.inputLive();
	case kListening: return ds.listening() && (int32)(ds._frame - ds._listenSince) >= a;
	case kInputText: return ds.inputLive() && ds._inputText == text;
	case kSeen:
		for (uint i = 0; i < ds._textLog.size(); i++)
			if (ds._textLog[i].text.contains(text))
				return true;
		return false;
	case kButton:
		for (uint i = 0; i < ds._buttons.size(); i++)
			if (ds._buttons[i].label.contains(text))
				return true;
		return false;
	default: return false;
	}
}

bool DebugSocket::handle(const Common::String &cmd, const Common::StringArray &a, Common::String &out) {
	_argTails = _socket->argTails();
	if (cmd == "state") {
		out = stateJson();
		return true;
	}
	if (cmd == "pause" || cmd == "resume" || cmd == "step") {
		// Tick-level control. `tick()` runs once per game tick and parks
		// there while paused, so the world stops between ticks while the
		// socket stays live: a driver can advance one tick, read the state,
		// and stop before the ego commits to a lethal cell.
		if (cmd == "pause") {
			// A frame-wait can never complete once ticks stop, so drop any
			// pending wait rather than letting it strand the client.
			_wait.active = false;
			_paused = true;
			_stepTicks = 0;
			out = "paused";
		} else if (cmd == "resume") {
			_paused = false;
			_stepTicks = 0;
			out = "running";
		} else {
			const uint32 n = a.size() >= 1 ? (uint32)atoi(a[0].c_str()) : 1;
			_paused = true;
			_stepTicks = MAX<uint32>(1, n);
			out = Common::String::format("step %u", _stepTicks);
		}
		return true;
	}
	if (cmd == "timeout") {
		if (a.size() >= 1)
			_timeoutFrames = atoi(a[0].c_str());
		out = Common::String::format("timeout %u frames", _timeoutFrames);
		return true;
	}
	if (cmd == "dump") {
		if (a.size() < 1) { out = "usage: dump <path-prefix>"; return true; }
		out = dumpBuffers(a[0]) ? "OK" : "FAIL";
		return true;
	}

	if (cmd == "hold") {
		if (a.size() < 1) { out = "usage: hold <name> [ticks]"; return true; }
		const int ticks = a.size() > 1 ? atoi(a[1].c_str()) : 400;
		const int maxPx = a.size() > 2 ? atoi(a[2].c_str()) : 0;
		holdKey(a[0], ticks, maxPx);
		out = "OK";
		return true;
	}
	if (cmd == "walk") {
		// walk <north|south|east|west> <px> [maxframes]
		//
		// NOT called "step": that name is already the tick-stepper.
		//
		// Walk a bounded distance and stop, with the engine doing both.
		// This is the primitive the harness actually wants: expressing
		// "move east 50 px" as hold/poll/release over the socket cannot
		// work, because the round trip is slower than the walk (a 3-tick
		// pulse was traced covering 43 px between two samples, enough to
		// cross a river bank and drown). Here the distance is measured
		// between ticks, so the walk stops where it was told to.
		if (a.size() < 2) {
			out = "usage: walk <north|south|east|west> <px> [maxframes]";
			return true;
		}
		static const struct { const char *name; const char *key; } dirs[] = {
			{ "north",     "KP_8" }, { "south",     "KP_2" },
			{ "west",      "KP_4" }, { "east",      "KP_6" },
			// SCI0 walks diagonals off the corner keys, and a route that
			// can only move on the axes takes a staircase of tiny hops
			// where one diagonal would do -- each hop another chance to
			// stall against a wall it is sliding along.
			{ "northwest", "KP_7" }, { "northeast", "KP_9" },
			{ "southwest", "KP_1" }, { "southeast", "KP_3" },
			{ "nw",        "KP_7" }, { "ne",        "KP_9" },
			{ "sw",        "KP_1" }, { "se",        "KP_3" },
		};
		const char *key = nullptr;
		for (uint i = 0; i < ARRAYSIZE(dirs); i++)
			if (a[0] == dirs[i].name || a[0] == dirs[i].key)
				key = dirs[i].key;
		if (!key) { out = "bad direction"; return true; }
		const int px = atoi(a[1].c_str());
		if (px <= 0) { out = "bad distance"; return true; }
		const int frames = a.size() > 2 ? atoi(a[2].c_str()) : 240;
		holdKey(key, frames, px);
		// Report where the ego ends up; the caller waits on `stepped`.
		out = "OK";
		return true;
	}
	if (cmd == "walked") {
		// True once no capped walk is outstanding.
		out = (_holdName.empty() && _capName.empty() && !_holdPending)
			? "yes" : "no";
		return true;
	}
	if (cmd == "release") {
		releaseKey();
		out = "OK";
		return true;
	}

	if (cmd == "objs") {
		out = objectsJson();
		return true;
	}
	if (cmd == "get") {
		// get <objName> <selector> -> raw value (or "obj" if pointer-valued)
		if (a.size() < 2) { out = "usage: get <obj> <selector>"; return true; }
		SegManager *sm = _engine->getEngineState()->_segMan;
		reg_t o = objByName(a[0]);
		if (o.isNull()) { out = "noobj"; return true; }
		const int selId = _engine->getKernel()->findSelector(a[1].c_str());
		if (selId < 0) { out = "nosel"; return true; }
		reg_t v = readSelector(sm, o, selId);
		if (v.getSegment() != 0)
			out = Common::String::format("obj %04x:%04x", v.getSegment(), v.getOffset());
		else
			out = Common::String::format("%d", v.toUint16());
		return true;
	}
	if (cmd == "wait") {
		_wait.conds.clear();
		_wait.anyOf = false;
		uint i = 0;
		while (i < a.size()) {
			Cond c;
			if (!parseCond(a, i, c)) {
				out = "bad condition";
				return true;
			}
			_wait.conds.push_back(c);
			if (i < a.size()) {
				if (a[i] == "&&") { i++; }
				else if (a[i] == "||") { _wait.anyOf = true; i++; }
				else { out = "expected && or ||"; return true; }
			}
		}
		if (_wait.conds.empty()) { out = "usage: wait <cond>"; return true; }
		_wait.deadline = _frame + _timeoutFrames;
		// The wall-clock backstop: generous against the tick rate (KQ1
		// animates 10-20 times a second), so a run that is merely slow
		// still ends on the tick deadline and reports a real frame count.
		_wait.deadlineMs = g_system->getMillis() + MAX<uint32>(5000, _timeoutFrames * 200);
		_wait.active = true;
		if (_socket->inputPending() || _holdPending)
			return true;	// evaluated from onFrame()/tick() once the keys are out
		// Evaluate once now so a condition that already holds returns at once.
		bool done = _wait.anyOf ? false : true;
		for (uint k = 0; k < _wait.conds.size(); k++) {
			const bool r = _wait.conds[k].eval(*this);
			done = _wait.anyOf ? (done || r) : (done && r);
		}
		if (done) {
			_wait.active = false;
			out = "OK";
		}
		return true;
	}
	return false;
}

// ---- input ---------------------------------------------------------------

void DebugSocket::pushKey(const Common::String &name, bool up) {
	Common::KeyCode code;
	uint16 ascii;
	if (!GUI::DebugSocket::keyByName(name, code, ascii))
		return;
	Common::Event ev;
	ev.kbd.keycode = code;
	ev.kbd.ascii = ascii;
	ev.kbd.flags = 0;
	ev.type = up ? Common::EVENT_KEYUP : Common::EVENT_KEYDOWN;
	_socket->pushEvent(ev);
}

void DebugSocket::holdKey(const Common::String &name, int ticks, int maxPx) {
	releaseKey();
	Common::KeyCode code;
	uint16 ascii;
	if (!GUI::DebugSocket::keyByName(name, code, ascii)) {
		warning("DebugSocket: unknown hold key '%s'", name.c_str());
		return;
	}
	_holdName = name;
	_holdTicks = ticks;
	_holdMaxPx = maxPx;
	_capName.clear();
	if (!egoXY(_holdStartX, _holdStartY)) {
		_holdStartX = _holdStartY = 0;
		_holdMaxPx = 0;
	}
	// The opening KEYDOWN goes through the same listening() gate as
	// `key`, instead of straight into the queue. A key handed over while
	// the game is not polling is simply dropped (a pic transition's
	// updateScreen() drains the queue outright), and for a TOGGLED SCI0
	// walk that is not a lost step but a lost STOP: the walk that the
	// caller believes it ended keeps running. Measured in rm1 with an
	// identical 6-tick pulse repeated from the same spot:
	//     dy = [0, 0, 61, 0, 0, 0, 0, 0]
	// The 61 px sample walked from y=82 into the moat at y=127..148 and
	// drowned, purely because one KEYDOWN of the pair was eaten. Pacing
	// the press to the game's own kGetEvent loop makes the pulse land
	// every time.
	_holdPending = true;
}

void DebugSocket::releaseKey() {
	if (_holdName.empty())
		return;
	if (_holdPending) {
		// The press never reached the game, so there is nothing to undo
		// and sending a KEYUP alone would be a stray event.
		_holdPending = false;
		_holdName = "";
		_holdTicks = 0;
		return;
	}
	{
		// KEYUP only. Do NOT queue another press of the same key here.
		//
		// That was added to "stop" a toggled SCI0 walk, but the distance
		// cap in holdTick() already stops it, and a press arriving after
		// the walk is stopped switches it back ON with nothing left to
		// bound it. Measured: `hold KP_2 3 12` sent on its own moves the
		// ego 0-2 px, while the same call followed by this release ran
		// 49-57 px into the moat.
		pushKey(_holdName, true);
	}
	_holdName = "";
	_holdTicks = 0;
	_holdMaxPx = 0;
}

void DebugSocket::holdTick() {
	if (_holdName.empty()) {
		// The hold is over, but an SCI0 walk does not stop with the key:
		// it is a toggle, and the ego keeps going. Traced with a 12 px
		// cap in force, a pulse still covered 57 px because the cap died
		// with the hold. Keep enforcing it until the ego stops moving.
		if (_capName.empty())
			return;
		int cx, cy;
		if (!egoXY(cx, cy)) {
			_capName.clear();
			return;
		}
		// Stop as soon as EITHER axis has covered the budget.
		//
		// Chebyshev (max of the two) looks right for a diagonal, but it
		// keeps walking while one axis is pinned against a wall: a
		// "southwest 20" measured -10 x and +35 y, sliding down the wall
		// into the moat and drowning the ego. Taking the max of the two
		// axes against the budget bounds BOTH, which is what a caller
		// asking for a 20 px step means.
		if (MAX(ABS(cx - _holdStartX), ABS(cy - _holdStartY)) >= _capPx) {
			// Stop by toggling the key off. This is the ONE place a
			// second press is correct: the walk is still running here,
			// so the press ends it rather than restarting it.
			pushKey(_capName);
			_capName.clear();
		} else if (++_capFrames > 240) {
			_capName.clear();		// gave up: it is not walking
		}
		return;
	}
	if (_holdPending)
		return;		// the press has not reached the game yet
	if (--_holdTicks <= 0) {
		if (_holdMaxPx > 0) {
			_capName = _holdName;	// keep watching past the release
			_capPx = _holdMaxPx;
			_capFrames = 0;
		}
		releaseKey();
		return;
	}
	// Distance cap, checked by the ENGINE every tick.
	//
	// A caller cannot enforce this over the socket: the round trip is
	// slower than the walk. Traced in KQ1 rm1, one 3-tick pulse moved the
	// ego (86,102) -> (88,145), 43 px, clearing an 18 px hazard lookahead
	// in a single unobserved step and drowning it in the moat. Here the
	// check runs between ticks, so the walk stops within a pixel or two
	// of the limit no matter what the socket is doing.
	if (_holdMaxPx > 0) {
		int ex, ey;
		if (egoXY(ex, ey)) {
			const int dx = ex - _holdStartX, dy = ey - _holdStartY;
			if (MAX(ABS(dx), ABS(dy)) >= _holdMaxPx) {
				// Toggle the walk off, the same way the post-hold watch
				// below does. releaseKey() alone sends a KEYUP, which
				// SCI0 ignores for a toggled walk -- the ego would carry
				// on past the budget with nothing watching.
				pushKey(_holdName);
				releaseKey();
				return;
			}
		}
	}
	// NO auto-repeat.
	//
	// SDL-style repeats are right for a game that walks while a key is
	// physically down, and wrong for SCI0, where an arrow key TOGGLES
	// the walk: every repeat flips it, so whether the ego ends up moving
	// depends on whether an even or odd number of repeats happened to be
	// emitted. Worse, the repeats bypassed the listening() gate that
	// paces every other key, so the count was not even reproducible.
	// Measured with an identical 6-tick pulse repeated from one spot:
	//     dy = [0, 0, 26, 45, 0, ...]   then a 49 px run into the moat
	// The long samples are not fast walks; they are walks whose "stop"
	// was cancelled by a stray repeat. One press starts the walk, the
	// release below stops it, and the caller polls in between.
}

// Mouse coordinates are given in game (320x200) space; the driver's
// getMousePos() divides the backend position by its scale, so the event
// has to carry the backend position. The hires drivers (the Korean
// 640x400 one) would otherwise take a click at 205,163 as 102,81.
Common::Point DebugSocket::toBackend(const Common::Point &p) {
	GfxDriver *drv = _engine->_gfxScreen ? _engine->_gfxScreen->gfxDriver() : nullptr;
	if (!drv)
		return p;
	return drv->mousePosToBackend(p);
}

// ---- state ---------------------------------------------------------------

DebugSocket::Snapshot DebugSocket::snapshot() const {
	Snapshot s;
	memset(&s, 0, sizeof(s));
	s.frame = _frame;
	EngineState *st = _engine->getEngineState();
	if (!st || !st->_segMan)
		return s;
	s.room = st->currentRoomNumber();
	if (st->variablesMax[VAR_GLOBAL] > kGlobalVarPreviousRoomNo)
		s.prevRoom = st->variables[VAR_GLOBAL][kGlobalVarPreviousRoomNo].toUint16();
	if (st->variablesMax[VAR_GLOBAL] > kGlobalVarScore)
		s.score = st->variables[VAR_GLOBAL][kGlobalVarScore].toUint16();
	const reg_t ego = st->variables[VAR_GLOBAL][kGlobalVarEgo];
	if (!ego.isNull() && st->_segMan->isObject(ego)) {
		s.egoX = readSelectorValue(st->_segMan, ego, SELECTOR(x));
		s.egoY = readSelectorValue(st->_segMan, ego, SELECTOR(y));
		s.egoLoop = readSelectorValue(st->_segMan, ego, SELECTOR(loop));
		s.egoCel = readSelectorValue(st->_segMan, ego, SELECTOR(cel));
		s.egoView = readSelectorValue(st->_segMan, ego, SELECTOR(view));
	}
	if (_engine->_gfxPorts)
		s.windows = _engine->_gfxPorts->windowCount();
	return s;
}

// Where are the interactive things? The scripts know: every room builds
// its scene out of Actor instances. SegManager can list them by class,
// and each carries x/y/view — so `objs` answers "where is the door /
// the rock / the tree" without any pixel guessing.
//
// The walk covers every loaded segment, and segments from rooms visited
// earlier stay loaded, so an unfiltered answer mixes rooms and reads as
// a blocking actor that is not in the room at all. Measured in rm3: objs
// listed `monsterTail1 (186,172)`, which exists in no KQ1 room script --
// it is the moat serpent's still-loaded artefact from rm1 -- and the
// harness blamed a walk stall on it. Likewise rm18 reported `elf
// (100,150)`, which is the class's literal x/y property on an object
// that had not been spawned.
//
// So report the segment each object came from and mark the one the
// current room owns, and skip template definitions whose coordinates are
// just class defaults. Clones always belong to the live room.
Common::String DebugSocket::objectsJson() {
	EngineState *s = _engine->getEngineState();
	if (!s || !s->_segMan)
		return "[]";
	SegManager *segMan = s->_segMan;
	Common::String out = "[";

	// Which objects are worth reporting.
	//
	// `objs` answers "where is the door / the rock / the elf" without
	// pixel guessing, but it walks every loaded segment, and segments from
	// rooms already visited stay loaded. Unfiltered, rm3 reported
	// `monsterTail1 (186,172)` -- the moat serpent's artefact from rm1 --
	// and the harness blamed a westward stall on an actor that was not in
	// the room.
	//
	// Three provenance rules were implemented and measured; all three
	// failed, so this reports position and nothing about ownership.
	//
	//   Room script segment. `get room script` answers 0: a SCI0 room is a
	//   script *segment* and owns no `script` property. Comparing actor
	//   segments against 0 classified every actor as not-in-room, the rm3
	//   rock that really does block the path included.
	//
	//   isSaved. No KQ1 object has it set -- nothing calls `Save: self` --
	//   so it came back false for all of them.
	//
	//   The object's own room. Objects answer `get <name> room` with
	//   `nosel`; a SCI object carries no room back-pointer, so there is
	//   nothing to compare against the current room.
	//
	// What survives is what can be seen. Class templates and artefacts of
	// rooms already left sit at unset, off-screen or garbage coordinates
	// (birdie answered x=65518), while an actor standing in the scene has a
	// position on the picture. `inRoom` therefore means "on screen", and
	// the name is a lie we cannot fix from here: it says nothing about
	// which room. rm18's unspawned `elf` passes it at its class literal
	// (100,150) -- spawn state is a separate question, answered by
	// `signal` (0 while a cycler has not moved an actor).
	//
	// A caller that must know the room should read the room script and
	// match names, not infer anything from this list.

	// Every object in every script segment and clone table, following
	// SegManager::findObjectsByName's own walk. Room scripts AddToRoom()
	// what matters, so a room's interesting actors are the ones with a
	// view and non-zero coords; the rest (zeroed template instances in
	// script 0's object map) are filtered out below.
	const Common::Array<SegmentObj *> &heap = segMan->getSegments();
	for (uint seg = 1; seg < heap.size(); seg++) {
		SegmentObj *mobj = heap[seg];
		if (!mobj)
			continue;
		Common::Array<reg_t> addrs;
		if (mobj->getType() == SEG_TYPE_SCRIPT) {
			const Script *scr = (const Script *)mobj;
			const ObjMap &objects = scr->getObjectMap();
			for (ObjMap::const_iterator it = objects.begin(); it != objects.end(); ++it)
				addrs.push_back(make_reg(seg, it->_value.getPos().getOffset()));
		} else if (mobj->getType() == SEG_TYPE_CLONES) {
			const CloneTable *ct = (const CloneTable *)mobj;
			for (uint idx = 0; idx < ct->size(); ++idx) {
				if (ct->isValidEntry(idx))
					addrs.push_back(make_reg(seg, idx));
			}
		}
		for (uint i = 0; i < addrs.size(); i++) {
			const reg_t addr = addrs[i];
			Object *o = segMan->getObject(addr);
			if (!o || o->isFreed() || o->isClass())
				continue;
			const uint16 view = (uint16)readSelectorValue(segMan, addr, SELECTOR(view));
			const uint16 x = (uint16)readSelectorValue(segMan, addr, SELECTOR(x));
			const uint16 y = (uint16)readSelectorValue(segMan, addr, SELECTOR(y));
			if (view == 0 || (x == 0 && y == 0))
				continue;   // template instance or parked off-scene
			const Common::String nm = segMan->getObjectName(addr);
			if (nm == "ego")
				continue;
			// On-screen means real: see the note above on why room
			// ownership could not be decided from the heap.
			const bool inRoom = (x < 800 && y < 600);
			if (out.size() > 1)
				out += ",";
			out += Common::String::format("{\"name\":\"%s\",\"seg\":%u,\"x\":%u,\"y\":%u,\"view\":%u,\"loop\":%u,\"cel\":%u,\"inRoom\":%s}",
				jsonEscape(nm).c_str(), (uint)seg, (uint)x, (uint)y,
				(uint)view,
				(uint)readSelectorValue(segMan, addr, SELECTOR(loop)),
				(uint)readSelectorValue(segMan, addr, SELECTOR(cel)),
				inRoom ? "true" : "false");
		}
	}
	out += "]";
	return out;
}

Common::String DebugSocket::stateJson() {
	const Snapshot s = snapshot();
	Common::String texts;
	{
		// The last few distinct texts drawn, oldest first, each with the
		// frame it was last drawn on.
		const uint from = _textLog.size() > 8 ? _textLog.size() - 8 : 0;
		for (uint i = from; i < _textLog.size(); i++) {
			if (i > from) texts += ",";
			const Common::String t = jsonEscape(_textLog[i].text);
			const Common::Rect &r = _textLog[i].rect;
			texts += Common::String::format("[%u,\"%s\",%d,%d,%d,%d]", _textLog[i].frame, t.c_str(), r.left, r.top, r.right, r.bottom);
		}
	}
	Common::String buttons;
	for (uint i = 0; i < _buttons.size(); i++) {
		const Common::String l = jsonEscape(_buttons[i].label);
		if (!buttons.empty()) buttons += ",";
		const Common::Rect &r = _buttons[i].rect;
		buttons += Common::String::format("[\"%s\",%d,%d,%d,%d]", l.c_str(), r.left, r.top, r.right, r.bottom);
	}
	const Common::String input = jsonEscape(_inputText);
	return Common::String::format(
		"{\"frame\":%u,\"room\":%u,\"prevRoom\":%u,\"score\":%u,"
		"\"ego\":{\"x\":%d,\"y\":%d,\"loop\":%d,\"cel\":%d,\"view\":%d},"
		"\"windows\":%u,\"idle\":%u,\"listening\":%s,\"input\":%s,\"inputText\":\"%s\",\"buttons\":[%s],\"texts\":[%s]}",
		s.frame, s.room, s.prevRoom, s.score, s.egoX, s.egoY, s.egoLoop, s.egoCel, s.egoView,
		s.windows, _idleFrames, listening() ? "true" : "false", inputLive() ? "true" : "false", inputLive() ? input.c_str() : "", buttons.c_str(), texts.c_str());
}

bool DebugSocket::dumpBuffers(const Common::String &prefix) {
	GfxScreen *scr = _engine->_gfxScreen;
	if (!scr)
		return false;
	bool ok = true;
	Common::DumpFile f;

	// lowres display buffer
	if (f.open(Common::Path(prefix + "_low.bin"))) {
		f.write(scr->displayScreen(), scr->displayPixels());
		f.close();
	} else ok = false;

	// hires composited output, when the driver keeps one
	Common::Array<byte> buf(640 * 400 * 4);
	uint16 w = 0, h = 0;
	if (scr->gfxDriver()->copyScaledBitmap(buf.begin(), buf.size(), w, h)) {
		if (f.open(Common::Path(prefix + "_scaled.bin"))) {
			f.write(buf.begin(), (uint32)w * h);
			f.close();
		} else ok = false;
		if (const TextLayer *tl = scr->textLayer()) {
			if (f.open(Common::Path(prefix + "_layer.bin"))) {
				for (uint16 y = 0; y < tl->height(); y++)
					f.write(tl->row(y), (uint32)tl->width() * sizeof(TextPixel));
				f.close();
			}
		}
	}

	// What the player sees: the backend screen after the driver's composite.
	if (Graphics::Surface *s = g_system->lockScreen()) {
		if (f.open(Common::Path(prefix + "_out.bin"))) {
			for (int y = 0; y < s->h; y++)
				f.write((const byte *)s->getBasePtr(0, y), s->w * s->format.bytesPerPixel);
			f.close();
		} else ok = false;
		const Graphics::PixelFormat &pf = s->format;
		if (f.open(Common::Path(prefix + "_out.txt"))) {
			f.writeString(Common::String::format("%d %d %d %d %d %d %d %d %d %d %d\n", s->w, s->h, pf.bytesPerPixel,
				8 - pf.rLoss, 8 - pf.gLoss, 8 - pf.bLoss, 8 - pf.aLoss, pf.rShift, pf.gShift, pf.bShift, pf.aShift));
			f.close();
		}
		g_system->unlockScreen();
	}

	// control-plane map: the picture's walkability flags, which is what a
	// driver needs to know before it picks a click target. SCI0 control
	// values: 0x00 water, 0x04 ignore, 0x08 special, 0x0f status line.
	if (f.open(Common::Path(prefix + "_ctl.bin"))) {
		const uint16 cw = scr->getWidth(), ch = scr->getHeight();
		Common::Array<byte> ctl(cw * ch);
		for (uint16 y = 0; y < ch; y++)
			for (uint16 x = 0; x < cw; x++)
				ctl[y * cw + x] = scr->getControl(x, y);
		f.write(ctl.begin(), ctl.size());
		f.close();
	}

	// priority plane: what an object standing in a walkable cell can still
	// block - the guards in room 1 stop ego though every control byte
	// along his row reads walkable.
	if (f.open(Common::Path(prefix + "_pri.bin"))) {
		const uint16 cw = scr->getWidth(), ch = scr->getHeight();
		Common::Array<byte> pri(cw * ch);
		for (uint16 y = 0; y < ch; y++)
			for (uint16 x = 0; x < cw; x++)
				pri[y * cw + x] = scr->getPriority(x, y);
		f.write(pri.begin(), pri.size());
		f.close();
	}

	// palette
	byte pal[768];
	scr->gfxDriver()->copyCurrentPalette(pal, 0, 256);
	if (f.open(Common::Path(prefix + "_pal.bin"))) {
		f.write(pal, 768);
		f.close();
	}
	return ok;
}

} // End of namespace Sci
