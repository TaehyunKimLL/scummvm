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

#if defined(POSIX)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#elif defined(WIN32)
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
	_listenFd(-1), _clientFd(-1)
#if defined(WIN32)
	, _pipe(nullptr), _connectOv(nullptr), _pipeConnected(false)
#endif
	{
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

// ---- transport ------------------------------------------------------------

bool DebugSocket::listen(const Common::String &path) {
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

void DebugSocket::send(const Common::String &all) {
#if defined(POSIX)
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
#elif defined(WIN32)
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
	return _protocol.frameWaitActive() || (_ext && _ext->replyPending());
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
		if (_protocol.frameWaitActive())
			return;		// onFrame() replies when the frames have passed
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
		if (a.empty()) {
			out = "usage: key <name>|<keycode> [ascii] [flags]";
			return true;
		}
		out = queueKey(a) ? "OK" : "unknown key " + a[0];
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
			queueKey(one);
		}
		out = "OK";
		return true;
	}
	if (cmd == "click" || cmd == "move") {
		if (a.size() < 2) {
			out = "usage: " + cmd + " <x> <y>" + (cmd == "click" ? " [r]" : "");
			return true;
		}
		const int x = atoi(a[0].c_str()), y = atoi(a[1].c_str());
		if (cmd == "move")
			sendMove(x, y);
		else
			sendClick(x, y, a.size() > 2 && a[2] == "r");
		out = "OK";
		return true;
	}
	if (cmd == "wait") {
		if (a.size() != 2 || a[0] != "frames") {
			out = "usage: wait frames <n>";
			return true;
		}
		_protocol.startFrameWait((uint32)atoi(a[1].c_str()));
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
		if (a.empty()) {
			out = "usage: " + cmd + " <slot>";
			return true;
		}
		if (!g_engine) {
			out = "FAIL";
			return true;
		}
		const int slot = atoi(a[0].c_str());
		Common::Error err = (cmd == "save")
			? g_engine->saveGameState(slot, Common::String::format("dbg%d", slot))
			: g_engine->loadGameState(slot);
		out = (err.getCode() == Common::kNoError) ? "OK" : "FAIL";
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

// ---- input -----------------------------------------------------------------

static bool keyNameEq(const Common::String &a, const char *b) {
	if (a.size() != strlen(b))
		return false;
	for (uint i = 0; i < a.size(); i++)
		if (tolower(a[i]) != tolower(b[i]))
			return false;
	return true;
}

bool DebugSocket::keyByName(const Common::String &name, Common::KeyCode &code, uint16 &ascii) {
	static const struct { const char *n; Common::KeyCode k; uint16 a; } table[] = {
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
		if (keyNameEq(name, table[i].n)) {
			code = table[i].k;
			ascii = table[i].a;
			return true;
		}
	if (name.size() == 1) {
		const char c = name[0];
		code = (Common::KeyCode)(c >= 'A' && c <= 'Z' ? c + 32 : c);
		ascii = (byte)c;
		return true;
	}
	return false;
}

// `key Return`, `key a`, `key 13 13 0`. One character is always a name
// (`key 5` is the 5 key); a longer all-digit word is a keycode, with the
// ascii value and the modifier flags as optional numbers after it.
bool DebugSocket::queueKey(const Common::StringArray &a) {
	Common::Event ev;
	ev.type = Common::EVENT_KEYDOWN;
	const Common::String &k = a[0];
	bool numeric = k.size() > 1;
	for (uint i = 0; i < k.size() && numeric; i++)
		numeric = (k[i] >= '0' && k[i] <= '9');
	if (numeric) {
		ev.kbd.keycode = (Common::KeyCode)atoi(k.c_str());
		ev.kbd.ascii = a.size() > 1 ? (uint16)atoi(a[1].c_str())
		                            : (ev.kbd.keycode < 0x80 ? (uint16)ev.kbd.keycode : 0);
		ev.kbd.flags = a.size() > 2 ? (byte)atoi(a[2].c_str()) : 0;
	} else {
		Common::KeyCode code;
		uint16 ascii;
		if (!keyByName(k, code, ascii)) {
			warning("DebugSocket: unknown key '%s'", k.c_str());
			return false;
		}
		ev.kbd.keycode = code;
		ev.kbd.ascii = ascii;
		ev.kbd.flags = (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z') ? Common::KBD_SHIFT : 0;
	}
	_pendingKeys.push_back(ev);
	return true;
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
