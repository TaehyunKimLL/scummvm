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
// A UNIX socket is a POSIX thing; this file is the one place in ScummVM
// that talks to one, and only where that exists.
#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "gui/debugsocket.h"

#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"

#include "engines/engine.h"

#include "graphics/paletteman.h"
#include "graphics/surface.h"

#if defined(DEBUGSOCKET_POSIX)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#elif defined(DEBUGSOCKET_WIN32)
// The Windows shape of the same thing is a named pipe: one server instance,
// byte mode, overlapped so ConnectNamedPipe/ReadFile return at once from
// the game loop. The path given is used as \\.\pipe\<name>.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// windows.h defines ARRAYSIZE too; restore common/util.h's, which this
// file uses for its key table.
#undef ARRAYSIZE
#define ARRAYSIZE(x) ((int)(sizeof(x) / sizeof(x[0])))
#endif

namespace GUI {

DebugSocket::DebugSocket(Debugger *console) :
	_console(console), _ext(nullptr), _pollInterval(1), _sinceLastPoll(0),
	_haveRelease(false), _lastKeyMs(0), _recFile(nullptr),
	_safePoint(this), _saveLoad(kNone), _saveLoadSlot(0), _saveLoadDeadline(0)
#if defined(DEBUGSOCKET_POSIX)
	, _listenFd(-1), _clientFd(-1)
#elif defined(DEBUGSOCKET_WIN32)
	, _pipe(nullptr), _connectOv(nullptr), _pipeConnected(false)
#endif
	{
	g_system->getEventManager()->getEventDispatcher()->registerSource(&_safePoint, false);
}

DebugSocket *DebugSocket::open(Debugger *console, const Common::String &path) {
	DebugSocket *s = new DebugSocket(console);
	if (!path.empty() && !s->listen(path)) {
		delete s;
		return nullptr;
	}
	return s;
}

DebugSocket::~DebugSocket() {
	stopRecording();
	g_system->getEventManager()->getEventDispatcher()->unregisterSource(&_safePoint);
#if defined(DEBUGSOCKET_POSIX)
	if (_clientFd >= 0)
		::close(_clientFd);
	if (_listenFd >= 0)
		::close(_listenFd);
#elif defined(DEBUGSOCKET_WIN32)
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

// ---- transport ------------------------------------------------------------

bool DebugSocket::listen(const Common::String &path) {
#if defined(DEBUGSOCKET_POSIX)
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
#elif defined(DEBUGSOCKET_WIN32)
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
	warning("DebugSocket: not built on this platform (USE_DEBUG_SOCKET), %s ignored", path.c_str());
	return false;
#endif
}

#if defined(DEBUGSOCKET_WIN32)
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

void DebugSocket::pollAccept() {
#if defined(DEBUGSOCKET_POSIX)
	if (_listenFd < 0 || _clientFd >= 0)
		return;
	int fd = ::accept(_listenFd, nullptr, nullptr);
	if (fd < 0)
		return;
	::fcntl(fd, F_SETFL, O_NONBLOCK);
	_clientFd = fd;
	_inBuf.clear();
	debug(1, "DebugSocket: client connected");
#elif defined(DEBUGSOCKET_WIN32)
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
#if defined(DEBUGSOCKET_POSIX)
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
#elif defined(DEBUGSOCKET_WIN32)
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
#if defined(DEBUGSOCKET_POSIX) || defined(DEBUGSOCKET_WIN32)
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

void DebugSocket::send(const Common::String &all) {
#if defined(DEBUGSOCKET_POSIX)
	if (_clientFd < 0)
		return;
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
#elif defined(DEBUGSOCKET_WIN32)
	if (!_pipeConnected)
		return;
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
#else
	(void)all;
#endif
}

void DebugSocket::reply(const Common::String &text) {
	send(DebugSocketProtocol::frame(text));
}

void DebugSocket::write(const char *text) {
	_outBuf += text;
}

// ---- recorder --------------------------------------------------------------

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
	// it never sees anything. Nothing is eaten here; the manager still gets
	// each event after us. The events the socket itself injects come
	// through the dispatcher too, so a scripted run records exactly like a
	// played one.
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
	// backend's, mapped back to game space for the script - the event's
	// own: the manager's getMousePos() still holds the one from before it.
	Common::String what;
	switch (ev.type) {
	case Common::EVENT_KEYDOWN:
		what = Common::String::format("key %d %d %d", ev.kbd.keycode, ev.kbd.ascii, ev.kbd.flags);
		break;
	case Common::EVENT_LBUTTONDOWN:
	case Common::EVENT_RBUTTONDOWN: {
		const Common::Point p = _ext ? _ext->fromBackend(ev.mouse) : ev.mouse;
		what = Common::String::format("%s %d %d", ev.type == Common::EVENT_LBUTTONDOWN ? "click" : "rclick", p.x, p.y);
		break;
	}
	default:
		return false;	// movement, key-up, quit: not what a script replays
	}
	const Common::String state = _ext ? _ext->recordState() : Common::String();
	recordLine('E', what + "\t" + (state.empty() ? Common::String("{}") : state));
	return false;		// never eat: the game must still get it
}

// ---- per frame -------------------------------------------------------------

bool DebugSocket::busy() const {
	return _protocol.frameWaitActive() || _saveLoad != kNone || (_ext && _ext->replyPending());
}

void DebugSocket::onFrame() {
	if (_protocol.onFrame())
		reply("OK");		// `wait frames <n>` ends on this frame
	// A poll per call would be a read() syscall per SCI VM instruction;
	// the interval keeps that to one per _pollInterval calls.
	if (++_sinceLastPoll < _pollInterval)
		return;
	_sinceLastPoll = 0;
	poll();
}

void DebugSocket::poll() {
	pollAccept();
	paceInput();
	if (_ext)
		_ext->poll();
	if (busy())
		return;		// one thing at a time: no new command while waiting
	Common::String line;
	if (readLine(line))
		runCommand(line);
}

// One key at a time, each held until the game is reading keys, the release
// on a later poll. A game that takes one key per frame (a text edit
// control) drops all but the first of a burst, and one that polls for
// "any key" and gets down and up together took the release and dropped the
// press. The wall-clock gap is the backstop for a game polling in a tight
// loop; the extension's gate (SCI: after a pic transition, which drains the
// queue, until the game asks for keys again) is the real pacing.
void DebugSocket::paceInput() {
	const uint32 now = g_system->getMillis();
	if (now - _lastKeyMs < 40 || (_ext && !_ext->inputReady()))
		return;
	if (_ext && _ext->paceInput()) {
		_lastKeyMs = now;
	} else if (_haveRelease) {
		pushEvent(_pendingRelease);
		_haveRelease = false;
		_lastKeyMs = now;
	} else if (!_pendingKeys.empty()) {
		_lastKeyMs = now;
		Common::Event ev = _pendingKeys[0];
		_pendingKeys.remove_at(0);
		pushEvent(ev);
		ev.type = Common::EVENT_KEYUP;
		_pendingRelease = ev;
		_haveRelease = true;
	}
}

void DebugSocket::pushEvent(const Common::Event &ev) {
	g_system->getEventManager()->pushEvent(ev);
}

// ---- commands --------------------------------------------------------------

void DebugSocket::runCommand(const Common::String &line) {
	Common::String cmd;
	Common::StringArray args;
	if (!DebugSocketProtocol::parse(line, cmd, args, &_argTails)) {
		reply("");
		return;
	}

	_outBuf.clear();
	Common::String out;
	if (_ext && _ext->handle(cmd, args, out)) {
		if (_ext->replyPending())
			return;		// the extension replies when its wait ends
		reply(out);
		return;
	}
	if (genericCommand(cmd, args, out)) {
		if (_protocol.frameWaitActive() || _saveLoad != kNone)
			return;		// the reply comes when the frames have passed / at the safe point
		reply(out);
		return;
	}

	// Anything else is a console command; its debugPrintf output is ours.
	_console->setOutputSink(this);
	_console->runCommandLine(line.c_str());
	_console->setOutputSink(nullptr);
	reply(_outBuf);
}

bool DebugSocket::genericCommand(const Common::String &cmd, const Common::StringArray &a, Common::String &out) {
	if (cmd == "key") {
		DebugSocketProtocol::KeySpec k;
		if (!DebugSocketProtocol::parseKey(a, k, out))
			return true;	// out holds the error
		queueKey(k);
		out = "OK";
		return true;
	}
	if (cmd == "type") {
		Common::String text;
		for (uint i = 0; i < a.size(); i++) {
			if (i)
				text += ' ';
			text += a[i];
		}
		for (uint i = 0; i < text.size(); i++) {
			Common::StringArray one;
			one.push_back(Common::String(text[i]));
			DebugSocketProtocol::KeySpec k;
			Common::String err;
			if (DebugSocketProtocol::parseKey(one, k, err))	// one character: always a key
				queueKey(k);
		}
		out = "OK";
		return true;
	}
	if (cmd == "click" || cmd == "move") {
		const bool click = (cmd == "click");
		if (a.size() < 2 || a.size() > (click ? 3u : 2u) || (click && a.size() == 3 && a[2] != "r")) {
			out = "usage: " + cmd + " <x> <y>" + (click ? " [r]" : "");
			return true;
		}
		// Inside the game screen, before and after the extension's mapping
		// (SCI: lowres to the hires backend): an event with a position off
		// the screen can reach engine code that indexes a buffer by it.
		int x, y;
		if (!DebugSocketProtocol::parsePoint(a, g_system->getWidth(), g_system->getHeight(), x, y, out))
			return true;
		const Common::Point p = _ext ? _ext->toBackend(Common::Point(x, y)) : Common::Point(x, y);
		if (p.x < 0 || p.y < 0 || p.x >= (int)g_system->getWidth() || p.y >= (int)g_system->getHeight()) {
			out = Common::String::format("point %d,%d maps to %d,%d, outside the %dx%d screen",
			                             x, y, p.x, p.y, g_system->getWidth(), g_system->getHeight());
			return true;
		}
		if (click)
			sendClick(x, y, a.size() > 2);
		else
			sendMove(x, y);
		out = "OK";
		return true;
	}
	if (cmd == "wait") {
		if (a.size() != 2 || a[0] != "frames") {
			out = "usage: wait frames <n>";
			return true;
		}
		uint32 n;
		if (!DebugSocketProtocol::parseCount(a[1], n)) {
			out = Common::String::format("bad frame count '%s' (0..%u)", a[1].c_str(), DebugSocketProtocol::kMaxCount);
			return true;
		}
		_protocol.startFrameWait(n);
		out = "OK";
		return true;
	}
	if (cmd == "dump") {
		if (a.empty()) {
			out = "usage: dump <path>";
			return true;
		}
		out = dumpScreen(a[0]) ? "OK" : "FAIL";
		return true;
	}
	if (cmd == "save" || cmd == "load") {
		int slot;
		if (a.size() != 1 || !DebugSocketProtocol::parseSlot(a[0], slot)) {
			out = Common::String::format("usage: %s <slot> (0..%u)", cmd.c_str(), DebugSocketProtocol::kMaxSlot);
			return true;
		}
		if (!g_engine) {
			out = "FAIL no engine";
			return true;
		}
		// Not now: onFrame() - and so this - runs inside the backend's
		// updateScreen() for most engines. runSaveLoad() does it at the next
		// event poll and sends the reply; until then no command is read.
		_saveLoad = (cmd == "save") ? kSave : kLoad;
		_saveLoadSlot = slot;
		_saveLoadDeadline = g_system->getMillis() + kSaveLoadWaitMs;
		return true;
	}
	if (cmd == "record") {
		if (a.empty()) {
			stopRecording();
			out = "OK";
		} else {
			out = startRecording(a[0]) ? "OK" : "FAIL";
		}
		return true;
	}
	return false;
}

bool DebugSocket::SafePoint::pollEvent(Common::Event &ev) {
	_owner->runSaveLoad();
	return false;
}

// At an event poll: the engine is between its own steps, which is where the
// global main menu saves and loads too. The engine still decides whether
// now is a time it can: SCI refuses while a script is nested or the user
// has no control, AGS inside a script or a blocking call. A refusal is
// retried at later polls for kSaveLoadWaitMs, then reported.
void DebugSocket::runSaveLoad() {
	if (_saveLoad == kNone || !g_engine)
		return;
	const bool save = (_saveLoad == kSave);
	const int slot = _saveLoadSlot;
	Common::U32String why;
	if (save ? !g_engine->canSaveGameStateCurrently(&why) : !g_engine->canLoadGameStateCurrently(&why)) {
		if ((int32)(g_system->getMillis() - _saveLoadDeadline) < 0)
			return;		// try again at the next poll
		_saveLoad = kNone;
		reply(Common::String::format("FAIL cannot %s now%s%s", save ? "save" : "load",
		                             why.empty() ? "" : ": ", why.encode().c_str()));
		return;
	}
	_saveLoad = kNone;	// before the call: saving may poll events itself
	const Common::Error err = save
		? g_engine->saveGameState(slot, Common::String::format("dbg%d", slot))
		: g_engine->loadGameState(slot);
	if (err.getCode() == Common::kNoError)
		reply("OK");
	else
		reply(Common::String::format("FAIL %d %s", (int)err.getCode(), err.getDesc().c_str()));
}

// ---- input -----------------------------------------------------------------

bool DebugSocket::keyByName(const Common::String &name, Common::KeyCode &code, uint16 &ascii) {
	return DebugSocketProtocol::keyByName(name, code, ascii);
}

void DebugSocket::queueKey(const DebugSocketProtocol::KeySpec &k) {
	Common::Event ev;
	ev.type = Common::EVENT_KEYDOWN;
	ev.kbd.keycode = k.keycode;
	ev.kbd.ascii = k.ascii;
	ev.kbd.flags = k.flags;
	_pendingKeys.push_back(ev);
}

void DebugSocket::sendMove(int x, int y) {
	Common::Event ev;
	ev.type = Common::EVENT_MOUSEMOVE;
	ev.mouse = _ext ? _ext->toBackend(Common::Point(x, y)) : Common::Point(x, y);
	pushEvent(ev);
}

void DebugSocket::sendClick(int x, int y, bool right) {
	sendMove(x, y);
	Common::Event ev;
	ev.mouse = _ext ? _ext->toBackend(Common::Point(x, y)) : Common::Point(x, y);
	ev.type = right ? Common::EVENT_RBUTTONDOWN : Common::EVENT_LBUTTONDOWN;
	pushEvent(ev);
	ev.type = right ? Common::EVENT_RBUTTONUP : Common::EVENT_LBUTTONUP;
	pushEvent(ev);
}

// ---- dump ------------------------------------------------------------------

// What the backend has as the game screen, exactly: an engine that draws
// Korean text draws it here, so a dump shows whether the probe reached the
// renderer without trusting anything the engine says about it.
bool DebugSocket::dumpScreen(const Common::String &path) {
	Graphics::Surface *s = g_system->lockScreen();
	if (!s)
		return false;
	bool ok = true;
	const Graphics::PixelFormat pf = s->format;
	Common::DumpFile f;
	if (f.open(Common::Path(path))) {
		for (int y = 0; y < s->h; y++)
			f.write((const byte *)s->getBasePtr(0, y), s->w * pf.bytesPerPixel);
		f.close();
	} else {
		ok = false;
	}
	const int w = s->w, h = s->h;
	g_system->unlockScreen();

	if (f.open(Common::Path(path + ".txt"))) {
		f.writeString(Common::String::format("%d %d %d %s\n", w, h, pf.bytesPerPixel * 8,
		                                     pf.bytesPerPixel == 1 ? "CLUT8" : pf.toString().c_str()));
		f.close();
	} else {
		ok = false;
	}
	if (pf.bytesPerPixel == 1) {
		byte pal[768];
		g_system->getPaletteManager()->grabPalette(pal, 0, 256);
		if (f.open(Common::Path(path + ".pal"))) {
			f.write(pal, sizeof(pal));
			f.close();
		} else {
			ok = false;
		}
	}
	return ok;
}

} // End of namespace GUI
