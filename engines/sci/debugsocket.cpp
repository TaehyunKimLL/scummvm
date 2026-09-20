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

// A UNIX socket is a POSIX thing; this file is the one place in the engine
// that talks to one, and it is built only where that exists.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "sci/debugsocket.h"

#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/tokenizer.h"

#include "sci/console.h"
#include "sci/sci.h"
#include "sci/engine/kernel.h"
#include "sci/engine/seg_manager.h"
#include "sci/engine/selector.h"
#include "sci/engine/state.h"
#include "sci/engine/vm.h"
#include "sci/graphics/ports.h"
#include "sci/graphics/screen.h"
#include "sci/event.h"
#include "sci/graphics/drivers/gfxdriver.h"

#if defined(POSIX)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

namespace Sci {

DebugSocket::DebugSocket(SciEngine *engine, Console *console) :
	_engine(engine), _console(console), _lastKeyMs(0), _sinceLastPoll(0),
	_listenFd(-1), _clientFd(-1),
	_timeoutFrames(600), _frame(0), _getEventFrame(0), _getEventCount(0), _transitionPoll(0), _listenSince(0), _lastRoom(0xffff), _inputPoll(0), _haveRelease(false),
	_lastDisplayHash(0), _idleFrames(0), _idleSamples(0), _lastSampleMs(0) {
	_wait.active = false;
	_wait.anyOf = false;
	_wait.deadline = 0;
	_wait.deadlineMs = 0;
	// Our events join the backend's queue through the same mechanism the
	// keymapper's do: a registered artificial source.
	g_system->getEventManager()->getEventDispatcher()->registerSource(&_events, false);
}

DebugSocket::~DebugSocket() {
	g_system->getEventManager()->getEventDispatcher()->unregisterSource(&_events);
#if defined(POSIX)
	if (_clientFd >= 0)
		::close(_clientFd);
	if (_listenFd >= 0)
		::close(_listenFd);
#endif
}

bool DebugSocket::open(const Common::String &path) {
#if defined(POSIX)
	_listenFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (_listenFd < 0) {
		warning("DebugSocket: socket(): %s", strerror(errno));
		return false;
	}
	::unlink(path.c_str());
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
	if (::bind(_listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || ::listen(_listenFd, 1) < 0) {
		warning("DebugSocket: bind/listen %s: %s", path.c_str(), strerror(errno));
		::close(_listenFd);
		_listenFd = -1;
		return false;
	}
	::fcntl(_listenFd, F_SETFL, O_NONBLOCK);
	debug(1, "DebugSocket: listening on %s", path.c_str());
	return true;
#else
	warning("DebugSocket: not supported on this platform");
	return false;
#endif
}

void DebugSocket::pollAccept() {
#if defined(POSIX)
	if (_listenFd < 0 || _clientFd >= 0)
		return;
	int fd = ::accept(_listenFd, nullptr, nullptr);
	if (fd < 0)
		return;
	::fcntl(fd, F_SETFL, O_NONBLOCK);
	_clientFd = fd;
	_inBuf.clear();
	debug(1, "DebugSocket: client connected");
#endif
}

bool DebugSocket::readLine(Common::String &line) {
#if defined(POSIX)
	if (_clientFd < 0)
		return false;
	char buf[512];
	for (;;) {
		ssize_t n = ::read(_clientFd, buf, sizeof(buf));
		if (n > 0) {
			_inBuf += Common::String(buf, n);
			continue;
		}
		if (n == 0) {
			::close(_clientFd);
			_clientFd = -1;
			debug(1, "DebugSocket: client closed");
			return false;
		}
		break;	// EAGAIN
	}
	const uint nl = _inBuf.findFirstOf('\n');
	if (nl == Common::String::npos)
		return false;
	line = Common::String(_inBuf.c_str(), nl);
	_inBuf = Common::String(_inBuf.c_str() + nl + 1);
	if (!line.empty() && line.lastChar() == '\r')
		line.deleteLastChar();
	return true;
#else
	return false;
#endif
}

void DebugSocket::write(const char *text) {
	_outBuf += text;
}

void DebugSocket::reply(const Common::String &text) {
#if defined(POSIX)
	if (_clientFd < 0)
		return;
	Common::String all = text;
	if (!all.empty() && all.lastChar() != '\n')
		all += '\n';
	all += ".\n";
	const char *p = all.c_str();
	size_t left = all.size();
	while (left > 0) {
		ssize_t n = ::write(_clientFd, p, left);
		if (n <= 0) {
			if (errno == EAGAIN)
				continue;
			break;
		}
		p += n;
		left -= n;
	}
#endif
}

// ---- per frame ----------------------------------------------------------

void DebugSocket::tick() {
	_frame++;

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
	if (!_wait.active || !_pendingKeys.empty() || _haveRelease)
		return;
	bool done = _wait.anyOf ? false : true;
	for (uint i = 0; i < _wait.conds.size(); i++) {
		const bool r = _wait.conds[i].eval(*this);
		done = _wait.anyOf ? (done || r) : (done && r);
	}
	if (done) {
		_wait.active = false;
		reply("OK");
	} else if (_frame >= _wait.deadline || g_system->getMillis() >= _wait.deadlineMs) {
		// The tick deadline is the one that means something; the wall
		// clock is a backstop for when ticks stop coming at all, which
		// is what a modal box waiting for a keypress does.
		_wait.active = false;
		reply("TIMEOUT " + stateJson());
	}
}

void DebugSocket::onFrame() {
	// Called from the VM loop, once per instruction, which is what makes a
	// command land promptly - but a read() syscall per instruction would
	// cost more than the game does. Poll at most every kPollInstructions;
	// at SCI0 speeds that is still several times per game tick.
	if (++_sinceLastPoll < kPollInstructions)
		return;
	_sinceLastPoll = 0;

	pollAccept();

	// The screen also settles while a modal box is up, when no tick runs.
	sampleDisplay();

	// One key at a time, each held until the game is actually reading
	// keys. Two things eat a key handed over at the wrong moment:
	//   - a pic transition, whose updateScreen() drains the event queue
	//     outright, so a key sent during the wipe into a room simply
	//     never existed (3 of 8 launches failed to open KQ1's parser
	//     line for exactly this reason);
	//   - a modal edit control, which takes one key per kGetEvent.
	// listening() covers both: it is false during and just after a
	// transition, and it follows the game's own polling, so a burst is
	// paced at the rate the control consumes it. The wall-clock gap is
	// the backstop for a game that polls in a tight loop.
	{
		const uint32 now = g_system->getMillis();
		if (now - _lastKeyMs >= 40 && listening()) {
			if (_haveRelease) {
				_events.addEvent(_pendingRelease);
				_haveRelease = false;
				_lastKeyMs = now;
			} else if (!_pendingKeys.empty()) {
				_lastKeyMs = now;
				sendKey(_pendingKeys[0]);
				_pendingKeys.remove_at(0);
			}
		}
	}

	pollWait();

	if (_wait.active)
		return;		// one thing at a time: no new command while waiting
	Common::String line;
	if (readLine(line))
		runCommand(line);
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

static Common::Array<Common::String> split(const Common::String &line) {
	// Space-separated, with "..." grouping. Quotes are stripped.
	Common::Array<Common::String> out;
	Common::String cur;
	bool inQ = false, have = false;
	for (uint i = 0; i < line.size(); i++) {
		const char c = line[i];
		if (c == '"') {
			inQ = !inQ;
			have = true;
		} else if (c == ' ' && !inQ) {
			if (have)
				out.push_back(cur);
			cur.clear();
			have = false;
		} else {
			cur += c;
			have = true;
		}
	}
	if (have)
		out.push_back(cur);
	return out;
}

void DebugSocket::runCommand(const Common::String &line) {
	Common::Array<Common::String> args = split(line);
	if (args.empty()) {
		reply("");
		return;
	}
	const Common::String cmd = args[0];
	args.remove_at(0);

	_outBuf.clear();
	if (ownCommand(cmd, args)) {
		if (_wait.active)
			return;		// reply comes when the wait ends
		reply(_outBuf);
		return;
	}

	// Anything else is a console command; its debugPrintf output is ours.
	_console->setOutputSink(this);
	_console->runLine(line.c_str());
	_console->setOutputSink(nullptr);
	reply(_outBuf);
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
	if (w == "text") { c.kind = Cond::kText; if (i >= a.size()) return false; c.text = a[i++]; return true; }
	if (w == "windows") { c.kind = Cond::kWindows; return opnum(c.a); }
	if (w == "global") { c.kind = Cond::kGlobal; return num(c.a) && opnum(c.b); }
	if (w == "sel") { c.kind = Cond::kSel; if (i + 1 >= a.size()) return false; c.obj = a[i++]; c.sel = a[i++]; return opnum(c.a); }
	if (w == "frames") { c.kind = Cond::kFrames; return num(c.a); }
	if (w == "idle") { c.kind = Cond::kIdle; return num(c.a); }
	if (w == "input") { c.kind = Cond::kInput; return true; }
	if (w == "noinput") { c.kind = Cond::kNoInput; return true; }
	if (w == "listening") { c.kind = Cond::kListening; c.a = 0; if (i < a.size() && (a[i][0] >= '0' && a[i][0] <= '9')) num(c.a); return true; }
	if (w == "seen") { c.kind = Cond::kSeen; if (i >= a.size()) return false; c.text = a[i++]; return true; }
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
		if (!st || a < 0 || (uint)a >= st->variablesMax[VAR_GLOBAL])
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

bool DebugSocket::ownCommand(const Common::String &cmd, const Common::Array<Common::String> &a) {
	if (cmd == "key") {
		if (a.size() < 1) { _outBuf = "usage: key <name>"; return true; }
		_pendingKeys.push_back(a[0]);
		_outBuf = "OK";
		return true;
	}
	if (cmd == "type") {
		Common::String text;
		for (uint i = 0; i < a.size(); i++) { if (i) text += ' '; text += a[i]; }
		for (uint i = 0; i < text.size(); i++)
			_pendingKeys.push_back(Common::String(text[i]));
		_outBuf = "OK";
		return true;
	}
	if (cmd == "click" || cmd == "move") {
		if (a.size() < 2) { _outBuf = "usage: " + cmd + " <x> <y>"; return true; }
		const int x = atoi(a[0].c_str()), y = atoi(a[1].c_str());
		if (cmd == "move")
			sendMove(x, y);
		else
			sendClick(x, y, a.size() > 2 && a[2] == "r");
		_outBuf = "OK";
		return true;
	}
	if (cmd == "state") {
		_outBuf = stateJson();
		return true;
	}
	if (cmd == "timeout") {
		if (a.size() >= 1)
			_timeoutFrames = atoi(a[0].c_str());
		_outBuf = Common::String::format("timeout %u frames", _timeoutFrames);
		return true;
	}
	if (cmd == "dump") {
		if (a.size() < 1) { _outBuf = "usage: dump <path-prefix>"; return true; }
		_outBuf = dumpBuffers(a[0]) ? "OK" : "FAIL";
		return true;
	}
	if (cmd == "wait") {
		_wait.conds.clear();
		_wait.anyOf = false;
		uint i = 0;
		while (i < a.size()) {
			Cond c;
			if (!parseCond(a, i, c)) {
				_outBuf = "bad condition";
				return true;
			}
			_wait.conds.push_back(c);
			if (i < a.size()) {
				if (a[i] == "&&") { i++; }
				else if (a[i] == "||") { _wait.anyOf = true; i++; }
				else { _outBuf = "expected && or ||"; return true; }
			}
		}
		if (_wait.conds.empty()) { _outBuf = "usage: wait <cond>"; return true; }
		_wait.deadline = _frame + _timeoutFrames;
		// The wall-clock backstop: generous against the tick rate (KQ1
		// animates 10-20 times a second), so a run that is merely slow
		// still ends on the tick deadline and reports a real frame count.
		_wait.deadlineMs = g_system->getMillis() + MAX<uint32>(5000, _timeoutFrames * 200);
		_wait.active = true;
		if (!_pendingKeys.empty() || _haveRelease)
			return true;	// evaluated from onFrame()/tick() once the keys are out
		// Evaluate once now so a condition that already holds returns at once.
		bool done = _wait.anyOf ? false : true;
		for (uint k = 0; k < _wait.conds.size(); k++) {
			const bool r = _wait.conds[k].eval(*this);
			done = _wait.anyOf ? (done || r) : (done && r);
		}
		if (done) {
			_wait.active = false;
			_outBuf = "OK";
		}
		return true;
	}
	return false;
}

// ---- input ---------------------------------------------------------------

static bool keyByName(const Common::String &name, Common::KeyCode &code, uint16 &ascii) {
	struct { const char *n; Common::KeyCode k; uint16 a; } table[] = {
		{ "Return", Common::KEYCODE_RETURN, 13 }, { "Enter", Common::KEYCODE_RETURN, 13 },
		{ "Escape", Common::KEYCODE_ESCAPE, 27 }, { "Tab", Common::KEYCODE_TAB, 9 },
		{ "space", Common::KEYCODE_SPACE, ' ' }, { "BackSpace", Common::KEYCODE_BACKSPACE, 8 },
		{ "Up", Common::KEYCODE_UP, 0 }, { "Down", Common::KEYCODE_DOWN, 0 },
		{ "Left", Common::KEYCODE_LEFT, 0 }, { "Right", Common::KEYCODE_RIGHT, 0 },
		{ "KP_1", Common::KEYCODE_KP1, 0 }, { "KP_2", Common::KEYCODE_KP2, 0 }, { "KP_3", Common::KEYCODE_KP3, 0 },
		{ "KP_4", Common::KEYCODE_KP4, 0 }, { "KP_5", Common::KEYCODE_KP5, 0 }, { "KP_6", Common::KEYCODE_KP6, 0 },
		{ "KP_7", Common::KEYCODE_KP7, 0 }, { "KP_8", Common::KEYCODE_KP8, 0 }, { "KP_9", Common::KEYCODE_KP9, 0 },
		{ "F1", Common::KEYCODE_F1, 0 }, { "F2", Common::KEYCODE_F2, 0 }, { "F3", Common::KEYCODE_F3, 0 },
		{ "F4", Common::KEYCODE_F4, 0 }, { "F5", Common::KEYCODE_F5, 0 }, { "F6", Common::KEYCODE_F6, 0 },
		{ "F7", Common::KEYCODE_F7, 0 }, { "F8", Common::KEYCODE_F8, 0 }, { "F9", Common::KEYCODE_F9, 0 },
		{ "F10", Common::KEYCODE_F10, 0 },
	};
	for (uint i = 0; i < ARRAYSIZE(table); i++)
		if (name == table[i].n) { code = table[i].k; ascii = table[i].a; return true; }
	if (name.size() == 1) {
		const char c = name[0];
		code = (Common::KeyCode)(c >= 'A' && c <= 'Z' ? c + 32 : c);
		ascii = (byte)c;
		return true;
	}
	return false;
}

void DebugSocket::sendKey(const Common::String &name) {
	Common::KeyCode code;
	uint16 ascii;
	if (!keyByName(name, code, ascii)) {
		warning("DebugSocket: unknown key '%s'", name.c_str());
		return;
	}
	Common::Event ev;
	ev.kbd.keycode = code;
	ev.kbd.ascii = ascii;
	ev.kbd.flags = (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') ? Common::KBD_SHIFT : 0;
	ev.type = Common::EVENT_KEYDOWN;
	_events.addEvent(ev);
	// The release goes out on a later poll: SCI0's parser line asks
	// kGetEvent for "any key" and, handed down and up together, took the
	// release and dropped the press.
	ev.type = Common::EVENT_KEYUP;
	_pendingRelease = ev;
	_haveRelease = true;
}

// Mouse coordinates are given in game (320x200) space; the driver's
// getMousePos() divides the backend position by its scale, so the event
// has to carry the backend position. The hires drivers (the Korean
// 640x400 one) would otherwise take a click at 205,163 as 102,81.
Common::Point DebugSocket::toBackend(int x, int y) const {
	GfxDriver *drv = _engine->_gfxScreen ? _engine->_gfxScreen->gfxDriver() : nullptr;
	if (!drv)
		return Common::Point(x, y);
	return drv->mousePosToBackend(Common::Point(x, y));
}

void DebugSocket::sendMove(int x, int y) {
	Common::Event ev;
	ev.type = Common::EVENT_MOUSEMOVE;
	ev.mouse = toBackend(x, y);
	_events.addEvent(ev);
}

void DebugSocket::sendClick(int x, int y, bool right) {
	sendMove(x, y);
	Common::Event ev;
	ev.mouse = toBackend(x, y);
	ev.type = right ? Common::EVENT_RBUTTONDOWN : Common::EVENT_LBUTTONDOWN;
	_events.addEvent(ev);
	ev.type = right ? Common::EVENT_RBUTTONUP : Common::EVENT_LBUTTONUP;
	_events.addEvent(ev);
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

Common::String DebugSocket::stateJson() {
	const Snapshot s = snapshot();
	Common::String texts;
	{
		// The last few distinct texts drawn, oldest first, each with the
		// frame it was last drawn on.
		const uint from = _textLog.size() > 8 ? _textLog.size() - 8 : 0;
		for (uint i = from; i < _textLog.size(); i++) {
			if (i > from) texts += ",";
			Common::String t = _textLog[i].text;
			for (uint k = 0; k < t.size(); k++)
				if (t[k] == '"' || t[k] == '\\') t.setChar('\'', k);
				else if (t[k] == '\n' || t[k] == '\r') t.setChar(' ', k);
			const Common::Rect &r = _textLog[i].rect;
			texts += Common::String::format("[%u,\"%s\",%d,%d,%d,%d]", _textLog[i].frame, t.c_str(), r.left, r.top, r.right, r.bottom);
		}
	}
	Common::String buttons;
	for (uint i = 0; i < _buttons.size(); i++) {
		Common::String l = _buttons[i].label;
		for (uint k = 0; k < l.size(); k++)
			if (l[k] == '"' || l[k] == '\\') l.setChar('\'', k);
		if (!buttons.empty()) buttons += ",";
		const Common::Rect &r = _buttons[i].rect;
		buttons += Common::String::format("[\"%s\",%d,%d,%d,%d]", l.c_str(), r.left, r.top, r.right, r.bottom);
	}
	Common::String input = _inputText;
	for (uint k = 0; k < input.size(); k++)
		if (input[k] == '"' || input[k] == '\\') input.setChar('\'', k);
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
		if (scr->hiresTextPlane() && f.open(Common::Path(prefix + "_plane.bin"))) {
			f.write(scr->hiresTextPlane(), (uint32)w * h);
			f.close();
		}
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
