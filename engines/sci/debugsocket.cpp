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
#include "sci/engine/script.h"
#include "sci/engine/vm.h"
#include "sci/graphics/ports.h"
#include "sci/graphics/screen.h"
#include "sci/event.h"
#include "sci/graphics/drivers/gfxdriver.h"
#include "graphics/surface.h"

#if defined(POSIX)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#elif defined(WIN32)
// The Windows shape of the same thing is a named pipe: one server instance,
// message-free byte mode, PIPE_NOWAIT so ConnectNamedPipe/ReadFile return
// at once from the VM loop. The path given is used as \\.\pipe\<name>.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// windows.h defines ARRAYSIZE too; restore common/util.h's, which this
// file uses for its key table.
#undef ARRAYSIZE
#define ARRAYSIZE(x) ((int)(sizeof(x) / sizeof(x[0])))
#endif

namespace Sci {

DebugSocket::DebugSocket(SciEngine *engine, Console *console) :
	_engine(engine), _console(console), _lastKeyMs(0), _sinceLastPoll(0),
	_listenFd(-1), _clientFd(-1),
#if defined(WIN32)
	_pipe(nullptr), _connectOv(nullptr), _pipeConnected(false),
#endif
	_timeoutFrames(600), _frame(0), _getEventFrame(0), _getEventCount(0), _transitionPoll(0), _listenSince(0), _lastRoom(0xffff), _recFile(nullptr), _inputPoll(0), _haveRelease(false), _holdPending(false), _holdMaxPx(0), _holdStartX(0), _holdStartY(0), _capPx(0), _capFrames(0),
	_lastDisplayHash(0), _idleFrames(0), _idleSamples(0), _lastSampleMs(0),
	_paused(false), _stepTicks(0) {
	_wait.active = false;
	_wait.anyOf = false;
	_wait.deadline = 0;
	_wait.deadlineMs = 0;
	// Our events join the backend's queue through the same mechanism the
	// keymapper's do: a registered artificial source.
	g_system->getEventManager()->getEventDispatcher()->registerSource(&_events, false);
}

DebugSocket::~DebugSocket() {
	stopRecording();
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);
	g_system->getEventManager()->getEventDispatcher()->unregisterSource(&_events);
#if defined(POSIX)
	if (_clientFd >= 0)
		::close(_clientFd);
	if (_listenFd >= 0)
		::close(_listenFd);
#elif defined(WIN32)
	if (_pipe) {
		if (_pipeConnected)
			DisconnectNamedPipe((HANDLE)_pipe);
		CloseHandle((HANDLE)_pipe);
	}
	if (_connectOv) {
		OVERLAPPED *ov = (OVERLAPPED *)_connectOv;
		CloseHandle(ov->hEvent);
		delete ov;
	}
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
#elif defined(WIN32)
	// A bare name or a full \\.\pipe\ path both work.
	Common::String name = path;
	if (!name.hasPrefix("\\\\"))
		name = "\\\\.\\pipe\\" + name;
	_pipeName = name;
	if (!createPipe())
		return false;
	debug(1, "DebugSocket: listening on %s", name.c_str());
	return true;
#else
	warning("DebugSocket: not supported on this platform");
	return false;
#endif
}

#if defined(WIN32)
bool DebugSocket::createPipe() {
	HANDLE h = CreateNamedPipeA(_pipeName.c_str(),
	                            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
	                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
	                            1, 64 * 1024, 64 * 1024, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE) {
		warning("DebugSocket: CreateNamedPipe %s: error %lu", _pipeName.c_str(), (unsigned long)GetLastError());
		return false;
	}
	_pipe = h;
	// Start the asynchronous connect now; pollAccept() asks whether it
	// completed.
	OVERLAPPED *ov = new OVERLAPPED;
	memset(ov, 0, sizeof(*ov));
	ov->hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	_connectOv = ov;
	if (!ConnectNamedPipe(h, ov)) {
		const DWORD err = GetLastError();
		if (err == ERROR_PIPE_CONNECTED)
			SetEvent(ov->hEvent);
		else if (err != ERROR_IO_PENDING)
			warning("DebugSocket: ConnectNamedPipe: error %lu", (unsigned long)err);
	}
	_pipeConnected = false;
	return true;
}

void DebugSocket::dropClient() {
	DisconnectNamedPipe((HANDLE)_pipe);
	_pipeConnected = false;
	debug(1, "DebugSocket: client closed");
	// Listen again for the next client.
	OVERLAPPED *ov = (OVERLAPPED *)_connectOv;
	ResetEvent(ov->hEvent);
	if (!ConnectNamedPipe((HANDLE)_pipe, ov) && GetLastError() == ERROR_PIPE_CONNECTED)
		SetEvent(ov->hEvent);
}
#endif

bool DebugSocket::startRecording(const Common::String &path) {
	stopRecording();
	_recFile = new Common::DumpFile();
	if (!_recFile->open(Common::Path(path))) {
		warning("DebugSocket: cannot write recording %s", path.c_str());
		delete _recFile;
		_recFile = nullptr;
		return false;
	}
	// Priority above DefaultEventManager's (kEventManPriority = 0): that
	// one queues every event and returns true, so an observer at or below
	// it never sees anything - measured, 21 state lines and zero events.
	// Nothing is eaten here; the manager still gets each event after us.
	// The events the socket itself injects come through the dispatcher
	// too, so a scripted run records exactly like a played one.
	g_system->getEventManager()->getEventDispatcher()->registerObserver(this, 5, false);
	debug(1, "DebugSocket: recording to %s", path.c_str());
	return true;
}

void DebugSocket::stopRecording() {
	if (!_recFile)
		return;
	g_system->getEventManager()->getEventDispatcher()->unregisterObserver(this);
	_recFile->close();
	delete _recFile;
	_recFile = nullptr;
}

void DebugSocket::recordLine(char kind, const Common::String &payload) {
	if (!_recFile)
		return;
	Common::String line = Common::String::format("%c\t%s\n", kind, payload.c_str());
	_recFile->write(line.c_str(), line.size());
	// Flushed per line: a recording is most wanted after the run that
	// crashed, and DumpFile writes to <path>.tmp and renames on close, so
	// a killed process leaves the .tmp - readable, but only if flushed.
	_recFile->flush();
}

bool DebugSocket::notifyEvent(const Common::Event &ev) {
	if (!_recFile)
		return false;
	// The state the event arrived in is what a generated script waits for;
	// the event itself is what it then does. Mouse coordinates are the
	// backend's, so they are mapped back to game space for the script.
	Common::String what;
	switch (ev.type) {
	case Common::EVENT_KEYDOWN:
		what = Common::String::format("key %d %d %d", ev.kbd.keycode, ev.kbd.ascii, ev.kbd.flags);
		break;
	case Common::EVENT_LBUTTONDOWN:
	case Common::EVENT_RBUTTONDOWN: {
		// The event's own coordinates, scaled down by the driver: asking
		// the manager for getMousePos() here returns the position from
		// BEFORE this event, which for a click with no preceding move is
		// 0,0 - measured, every recorded click came out "click 0 0".
		Common::Point p(ev.mouse);
		GfxDriver *drv = _engine->_gfxScreen ? _engine->_gfxScreen->gfxDriver() : nullptr;
		if (drv) {
			const Common::Point probe = drv->mousePosToBackend(Common::Point(1000, 1000));
			if (probe.x > 0 && probe.y > 0) {
				p.x = p.x * 1000 / probe.x;
				p.y = p.y * 1000 / probe.y;
			}
		}
		what = Common::String::format("%s %d %d", ev.type == Common::EVENT_LBUTTONDOWN ? "click" : "rclick", p.x, p.y);
		break;
	}
	default:
		return false;	// movement, key-up, quit: not what a script replays
	}
	recordLine('E', what + "\t" + stateJson());
	return false;		// never eat: the game must still get it
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
#elif defined(WIN32)
	if (!_pipe || _pipeConnected)
		return;
	OVERLAPPED *ov = (OVERLAPPED *)_connectOv;
	if (WaitForSingleObject(ov->hEvent, 0) == WAIT_OBJECT_0) {
		_pipeConnected = true;
		_inBuf.clear();
		debug(1, "DebugSocket: client connected");
	}
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
#elif defined(WIN32)
	if (!_pipeConnected)
		return false;
	char buf[512];
	for (;;) {
		DWORD avail = 0;
		if (!PeekNamedPipe((HANDLE)_pipe, nullptr, 0, nullptr, &avail, nullptr)) {
			dropClient();	// ERROR_BROKEN_PIPE: the client went away
			return false;
		}
		if (avail == 0)
			break;
		// Overlapped handle: a synchronous read needs an OVERLAPPED anyway,
		// and with avail > 0 bytes waiting it completes at once.
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		DWORD n = 0;
		if (!ReadFile((HANDLE)_pipe, buf, MIN<DWORD>(avail, sizeof(buf)), &n, &ov)) {
			if (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult((HANDLE)_pipe, &ov, &n, TRUE)) {
				dropClient();
				return false;
			}
		}
		if (n == 0)
			break;
		_inBuf += Common::String(buf, n);
	}
#else
	return false;
#endif
#if defined(POSIX) || defined(WIN32)
	const uint nl = _inBuf.findFirstOf('\n');
	if (nl == Common::String::npos)
		return false;
	line = Common::String(_inBuf.c_str(), nl);
	_inBuf = Common::String(_inBuf.c_str() + nl + 1);
	if (!line.empty() && line.lastChar() == '\r')
		line.deleteLastChar();
	return true;
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
#elif defined(WIN32)
	if (!_pipeConnected)
		return;
	Common::String all = text;
	if (!all.empty() && all.lastChar() != '\n')
		all += '\n';
	all += ".\n";
	const char *p = all.c_str();
	DWORD left = all.size();
	while (left > 0) {
		OVERLAPPED ov;
		memset(&ov, 0, sizeof(ov));
		DWORD n = 0;
		if (!WriteFile((HANDLE)_pipe, p, left, &n, &ov)) {
			if (GetLastError() != ERROR_IO_PENDING || !GetOverlappedResult((HANDLE)_pipe, &ov, &n, TRUE)) {
				dropClient();
				break;
			}
		}
		p += n;
		left -= n;
	}
#endif
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
		onFrame();
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
	if (_recFile) {
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
			recordLine('S', s);
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
	if (!_wait.active || !_pendingKeys.empty() || _haveRelease || _holdPending)
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

// Defined below, next to the key table; needed by onFrame()'s hold gate.
static bool keyByName(const Common::String &name, Common::KeyCode &code, uint16 &ascii);

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
			if (_holdPending) {
				// Opening press of a hold: same gate as `key`, so a
				// toggled walk cannot lose its start or its stop.
				Common::KeyCode code;
				uint16 ascii;
				if (keyByName(_holdName, code, ascii)) {
					Common::Event ev;
					ev.kbd.keycode = code;
					ev.kbd.ascii = ascii;
					ev.kbd.flags = 0;
					ev.type = Common::EVENT_KEYDOWN;
					_events.addEvent(ev);
				}
				_holdPending = false;
				_lastKeyMs = now;
			} else if (_haveRelease) {
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

// Space-separated, with "..." grouping; quotes are stripped. `tails`, when
// given, receives for each token the UNSPLIT remainder of the line starting
// at that token - what a condition like `seen` wants, since a game string
// has spaces and quotes of its own and tokenising it leaves a fragment.
static Common::Array<Common::String> split(const Common::String &line,
                                           Common::Array<Common::String> *tails = nullptr) {
	Common::Array<Common::String> out;
	Common::String cur;
	bool inQ = false, have = false;
	uint tokenStart = 0;
	for (uint i = 0; i < line.size(); i++) {
		const char c = line[i];
		if (c == '"') {
			if (!have)
				tokenStart = i;
			inQ = !inQ;
			have = true;
		} else if (c == ' ' && !inQ) {
			if (have) {
				out.push_back(cur);
				if (tails)
					tails->push_back(Common::String(line.c_str() + tokenStart));
			}
			cur.clear();
			have = false;
		} else {
			if (!have)
				tokenStart = i;
			cur += c;
			have = true;
		}
	}
	if (have) {
		out.push_back(cur);
		if (tails)
			tails->push_back(Common::String(line.c_str() + tokenStart));
	}
	return out;
}

void DebugSocket::runCommand(const Common::String &line) {
	Common::Array<Common::String> tails;
	Common::Array<Common::String> args = split(line, &tails);
	if (args.empty()) {
		reply("");
		return;
	}
	const Common::String cmd = args[0];
	args.remove_at(0);
	tails.remove_at(0);
	_argTails = tails;

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
			_outBuf = "paused";
		} else if (cmd == "resume") {
			_paused = false;
			_stepTicks = 0;
			_outBuf = "running";
		} else {
			const uint32 n = a.size() >= 1 ? (uint32)atoi(a[0].c_str()) : 1;
			_paused = true;
			_stepTicks = MAX<uint32>(1, n);
			_outBuf = Common::String::format("step %u", _stepTicks);
		}
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

	if (cmd == "hold") {
		if (a.size() < 1) { _outBuf = "usage: hold <name> [ticks]"; return true; }
		const int ticks = a.size() > 1 ? atoi(a[1].c_str()) : 400;
		const int maxPx = a.size() > 2 ? atoi(a[2].c_str()) : 0;
		holdKey(a[0], ticks, maxPx);
		_outBuf = "OK";
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
			_outBuf = "usage: walk <north|south|east|west> <px> [maxframes]";
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
		if (!key) { _outBuf = "bad direction"; return true; }
		const int px = atoi(a[1].c_str());
		if (px <= 0) { _outBuf = "bad distance"; return true; }
		const int frames = a.size() > 2 ? atoi(a[2].c_str()) : 240;
		holdKey(key, frames, px);
		// Report where the ego ends up; the caller waits on `stepped`.
		_outBuf = "OK";
		return true;
	}
	if (cmd == "walked") {
		// True once no capped walk is outstanding.
		_outBuf = (_holdName.empty() && _capName.empty() && !_holdPending)
			? "yes" : "no";
		return true;
	}
	if (cmd == "release") {
		releaseKey();
		_outBuf = "OK";
		return true;
	}

	if (cmd == "objs") {
		_outBuf = objectsJson();
		return true;
	}
	if (cmd == "get") {
		// get <objName> <selector> -> raw value (or "obj" if pointer-valued)
		if (a.size() < 2) { _outBuf = "usage: get <obj> <selector>"; return true; }
		SegManager *sm = _engine->getEngineState()->_segMan;
		reg_t o = objByName(a[0]);
		if (o.isNull()) { _outBuf = "noobj"; return true; }
		const int selId = _engine->getKernel()->findSelector(a[1].c_str());
		if (selId < 0) { _outBuf = "nosel"; return true; }
		reg_t v = readSelector(sm, o, selId);
		if (v.getSegment() != 0)
			_outBuf = Common::String::format("obj %04x:%04x", v.getSegment(), v.getOffset());
		else
			_outBuf = Common::String::format("%d", v.toUint16());
		return true;
	}
	if (cmd == "save" || cmd == "load") {
		if (a.size() < 1) { _outBuf = "usage: " + cmd + " <slot>"; return true; }
		const int slot = atoi(a[0].c_str());
		Common::Error err = (cmd == "save")
			? g_sci->saveGameState(slot, Common::String::format("dbg%d", slot))
			: g_sci->loadGameState(slot);
		_outBuf = (err.getCode() == Common::kNoError) ? "OK" : "FAIL";
		return true;
	}
	if (cmd == "record") {
		if (a.empty()) {
			stopRecording();
			_outBuf = "OK";
		} else {
			_outBuf = startRecording(a[0]) ? "OK" : "FAIL";
		}
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
		if (!_pendingKeys.empty() || _haveRelease || _holdPending)
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

static bool keyNameEq(const Common::String &a, const char *b) {
	if (a.size() != strlen(b))
		return false;
	for (uint i = 0; i < a.size(); i++)
		if (tolower(a[i]) != tolower(b[i]))
			return false;
	return true;
}

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
		if (keyNameEq(name, table[i].n)) { code = table[i].k; ascii = table[i].a; return true; }
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

void DebugSocket::holdKey(const Common::String &name, int ticks, int maxPx) {
	releaseKey();
	Common::KeyCode code;
	uint16 ascii;
	if (!keyByName(name, code, ascii)) {
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
	Common::KeyCode code;
	uint16 ascii;
	if (keyByName(_holdName, code, ascii)) {
		// KEYUP only. Do NOT queue another press of the same key here.
		//
		// That was added to "stop" a toggled SCI0 walk, but the distance
		// cap in holdTick() already stops it, and a press arriving after
		// the walk is stopped switches it back ON with nothing left to
		// bound it. Measured: `hold KP_2 3 12` sent on its own moves the
		// ego 0-2 px, while the same call followed by this release ran
		// 49-57 px into the moat.
		Common::Event ev;
		ev.kbd.keycode = code;
		ev.kbd.ascii = ascii;
		ev.kbd.flags = 0;
		ev.type = Common::EVENT_KEYUP;
		_events.addEvent(ev);
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
			Common::KeyCode code;
			uint16 ascii;
			if (keyByName(_capName, code, ascii)) {
				Common::Event ev;
				ev.kbd.keycode = code;
				ev.kbd.ascii = ascii;
				ev.kbd.flags = 0;
				ev.type = Common::EVENT_KEYDOWN;
				_events.addEvent(ev);
			}
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
				Common::KeyCode code;
				uint16 ascii;
				if (keyByName(_holdName, code, ascii)) {
					Common::Event ev;
					ev.kbd.keycode = code;
					ev.kbd.ascii = ascii;
					ev.kbd.flags = 0;
					ev.type = Common::EVENT_KEYDOWN;
					_events.addEvent(ev);
				}
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
		if (scr->hiresTextPlane() && f.open(Common::Path(prefix + "_plane.bin"))) {
			f.write(scr->hiresTextPlane(), (uint32)w * h);
			f.close();
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
