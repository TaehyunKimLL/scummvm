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
#ifndef GUI_DEBUGSOCKET_H
#define GUI_DEBUGSOCKET_H

#include "common/scummsys.h"
#include "common/array.h"
#include "common/events.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/str-array.h"
#include "gui/debugger.h"
#include "gui/debugsocket-protocol.h"

// The transport is built where configure enabled it (USE_DEBUG_SOCKET:
// desktop hosts). Elsewhere open() warns once and a debug_socket= key does
// nothing; the recorder and the protocol work everywhere.
#if defined(USE_DEBUG_SOCKET) && defined(WIN32)
#define DEBUGSOCKET_WIN32
#elif defined(USE_DEBUG_SOCKET) && defined(POSIX)
#define DEBUGSOCKET_POSIX
#endif

namespace Common {
class DumpFile;
}

namespace GUI {

class DebugSocket;

/**
 * Commands an engine adds to the debug socket, or spells its own way.
 *
 * handle() is tried before the generic commands, so an engine can also
 * replace one of them (SCI keeps its own `dump` and `wait <cond>`). The
 * other hooks default to "nothing engine-specific".
 */
class DebugSocketExtension {
public:
	virtual ~DebugSocketExtension() {}

	/** True when @p cmd was the engine's; @p reply is then sent back. */
	virtual bool handle(const Common::String &cmd, const Common::StringArray &args, Common::String &reply) = 0;

	/**
	 * True while the command handle() just took is still running (a wait on
	 * a game condition): its reply is not sent now, no new command is read,
	 * and the extension sends the reply itself with DebugSocket::reply().
	 */
	virtual bool replyPending() const { return false; }

	/** Once per socket poll, after queued input went out, before a command is read. */
	virtual void poll() {}

	/** Queued keys go out only while this is true (the game is reading keys). */
	virtual bool inputReady() { return true; }

	/**
	 * The input slot of this poll, before the generic key queue: true when
	 * the engine sent something of its own (SCI's held walk key) in it.
	 */
	virtual bool paceInput() { return false; }

	/** Game coordinates of `click`/`move` to the event's backend coordinates. */
	virtual Common::Point toBackend(const Common::Point &p) { return p; }
	/** And back, for a recorded click. */
	virtual Common::Point fromBackend(const Common::Point &p) { return p; }

	/** One line of state written with each recorded event; empty for none. */
	virtual Common::String recordState() { return Common::String(); }
};

/**
 * A command channel for a script that drives a game, for any engine with a
 * GUI::Debugger.
 *
 * Opened by Debugger::onFrame() when the game's config has
 * `debug_socket=<path>` (a UNIX socket; on Windows a named pipe
 * \\.\pipe\<path>). A client sends one line and gets the reply, then a line
 * holding '.' (see DebugSocketProtocol::frame()). Every console command
 * works, its debugPrintf() output being the reply; on top of those:
 *
 *   key <name>|<keycode> [ascii] [flags]
 *                        press, then release on a later poll. A name is
 *                        Return, Escape, Tab, space, BackSpace, Up, Down,
 *                        Left, Right, KP_1..KP_9, F1..F10 or one character;
 *                        anything longer that is all digits is a keycode
 *   type <text>          one key per character
 *   click <x> <y> [r]    move there, press and release (r: right button)
 *   move <x> <y>         game coordinates, 0 <= x < getWidth(), 0 <= y <
 *                        getHeight() (also after the extension maps them)
 *   wait frames <n>      reply OK after n more Debugger::onFrame() calls
 *
 * Every number is decimal digits only and range-checked
 * (DebugSocketProtocol::parse*); anything else is an error reply, and
 * nothing is sent to the game.
 *   dump <path>          the screen as g_system->lockScreen() has it: raw
 *                        rows at <path>, "w h bpp format" at <path>.txt,
 *                        the palette at <path>.pal when it is CLUT8
 *   save <slot>, load <slot>
 *                        Engine::saveGameState()/loadGameState(), run at the
 *                        next event poll (never inside the screen update
 *                        onFrame() is called from) if the engine allows it
 *                        then; the reply is OK or FAIL with the error
 *   record [<path>]      start (or, with no path, stop) a recording
 *
 * Input goes through g_system->getEventManager()->pushEvent(), one key per
 * poll at most every 40 ms, so a game that reads one key per frame sees
 * them all. `debug_record=<path>` records without a socket.
 */
class DebugSocket : public Debugger::OutputSink, public Common::EventObserver {
public:
	/**
	 * Listen on @p path (empty: no transport, a recorder only). Null, after
	 * one warning, when that fails.
	 */
	static DebugSocket *open(Debugger *console, const Common::String &path);
	~DebugSocket() override;

	void setExtension(DebugSocketExtension *ext) { _ext = ext; }

	/**
	 * Polls happen on every @p n th onFrame(). SCI calls onFrame() once per
	 * VM instruction and polls every 256th; most engines call it once per
	 * screen update and poll on each.
	 */
	void setPollInterval(uint n) { _pollInterval = n ? n : 1; }

	/** Called by Debugger::onFrame(): counts the frame, polls when due. */
	void onFrame();

	/** Accept a client, send queued input, run one command. */
	void poll();

	/** Send a reply (framed) to the client, if one is connected. */
	void reply(const Common::String &text);

	/** Queue an event for the game, now. */
	void pushEvent(const Common::Event &ev);

	/** Keys, key releases still queued. */
	bool inputPending() const { return !_pendingKeys.empty() || _haveRelease; }

	bool startRecording(const Common::String &path);
	void stopRecording();
	bool recording() const { return _recFile != nullptr; }
	/** "<kind>\t<payload>\n", flushed. */
	void recordLine(char kind, const Common::String &payload);

	/** The named keys of `key`. */
	static bool keyByName(const Common::String &name, Common::KeyCode &code, uint16 &ascii);

	/**
	 * For each argument of the command being handled, the unsplit rest of
	 * its line (see DebugSocketProtocol::tokenize()).
	 */
	const Common::StringArray &argTails() const { return _argTails; }

	/** onFrame() calls so far. */
	uint32 frames() const { return _protocol.frames(); }

	// OutputSink
	void write(const char *text) override;
	// EventObserver: records, never eats.
	bool notifyEvent(const Common::Event &ev) override;

private:
	explicit DebugSocket(Debugger *console);
	bool listen(const Common::String &path);
	void pollAccept();
	bool readLine(Common::String &line);
	void send(const Common::String &bytes);
	void paceInput();
	void runCommand(const Common::String &line);
	bool genericCommand(const Common::String &cmd, const Common::StringArray &args, Common::String &out);
	void queueKey(const DebugSocketProtocol::KeySpec &k);
	void sendMove(int x, int y);
	void sendClick(int x, int y, bool right);
	bool dumpScreen(const Common::String &path);
	bool busy() const;

	/**
	 * `save`/`load` wait for a safe point: the next event poll, where the
	 * engine's own menu saves too. onFrame() runs inside the backend's
	 * updateScreen() for most engines, which is no place to save a game.
	 * This source is registered with the event dispatcher only to be
	 * polled; it never produces an event.
	 */
	class SafePoint : public Common::EventSource {
	public:
		explicit SafePoint(DebugSocket *owner) : _owner(owner) {}
		bool pollEvent(Common::Event &ev) override;
	private:
		DebugSocket *_owner;
	};
	friend class SafePoint;
	SafePoint _safePoint;
	enum SaveLoad { kNone, kSave, kLoad };
	SaveLoad _saveLoad;		///< request waiting for the safe point
	int _saveLoadSlot;
	uint32 _saveLoadDeadline;	///< g_system->getMillis() after which it fails
	static const uint32 kSaveLoadWaitMs = 5000;
	void runSaveLoad();

	Debugger *_console;
	DebugSocketExtension *_ext;
	DebugSocketProtocol _protocol;
	uint _pollInterval;
	uint _sinceLastPoll;

	Common::Array<Common::Event> _pendingKeys;	///< key-downs, one per poll
	Common::Event _pendingRelease;			///< key-up for the last key-down
	bool _haveRelease;
	uint32 _lastKeyMs;

	Common::DumpFile *_recFile;

#if defined(DEBUGSOCKET_POSIX)
	int _listenFd, _clientFd;
#elif defined(DEBUGSOCKET_WIN32)
	void *_pipe;			// HANDLE; a named pipe, one instance
	void *_connectOv;		// OVERLAPPED for the pending ConnectNamedPipe
	bool _pipeConnected;
	Common::String _pipeName;
	bool createPipe();
	void dropClient();
#endif
	Common::String _inBuf, _outBuf;
	Common::StringArray _argTails;
};

} // End of namespace GUI

#endif
