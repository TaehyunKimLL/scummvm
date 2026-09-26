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

#ifndef GUI_DEBUGSOCKET_PROTOCOL_H
#define GUI_DEBUGSOCKET_PROTOCOL_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/str-array.h"
#include "common/config-manager.h"
#include "common/keyboard.h"

namespace GUI {

/**
 * The debug socket's wire protocol, without the wire.
 *
 * A client sends one command per line and reads the reply up to a line
 * holding a single '.'. This class is the part of that which does not need
 * a socket, an engine or an event manager, so it can be unit-tested:
 * splitting a line, framing a reply, and counting frames for
 * `wait frames <n>`. GUI::DebugSocket owns one and feeds it.
 */
class DebugSocketProtocol {
public:
	DebugSocketProtocol();

	/**
	 * Space-separated tokens, with "..." grouping (the quotes are dropped).
	 * @p tails, when given, receives for each token the unsplit rest of the
	 * line from where that token starts, quotes included: a condition that
	 * matches a game string wants the string verbatim.
	 */
	static void tokenize(const Common::String &line, Common::StringArray &tokens,
	                     Common::StringArray *tails = nullptr);

	/**
	 * tokenize(), then the first token as the command and the rest as its
	 * arguments. False for a line with no token at all.
	 */
	static bool parse(const Common::String &line, Common::String &cmd, Common::StringArray &args,
	                  Common::StringArray *tails = nullptr);

	/**
	 * The bytes that go on the wire for @p reply: the text, newline
	 * terminated, then a line holding '.'. A reply line starting with '.'
	 * gets one more in front (dot-stuffing, as SMTP does), so a line that is
	 * itself '.' goes out as '..' and cannot end the reply early; a client
	 * drops the first '.' of any line that starts with two.
	 */
	static Common::String frame(const Common::String &reply);

	/**
	 * A frame count as a command argument: decimal digits only, at most
	 * kMaxCount. False for anything else (a negative number, junk), which
	 * atoi() used to turn into a wait of ~4 billion frames.
	 */
	static bool parseCount(const Common::String &s, uint32 &n);
	static const uint32 kMaxCount = 100000000;

	/**
	 * Decimal digits only, value <= @p max. Every numeric argument of the
	 * generic commands goes through this: a '-' or a stray letter is an
	 * error reply, never an atoi() guess.
	 */
	static bool parseUInt(const Common::String &s, uint32 max, uint32 &n);

	/**
	 * `click`/`move`: args[0], args[1] as x, y with 0 <= x < @p w and
	 * 0 <= y < @p h (the game screen). An event with a mouse position
	 * outside it can reach engine code that indexes a buffer by it.
	 * False with @p err set otherwise.
	 */
	static bool parsePoint(const Common::StringArray &args, uint w, uint h, int &x, int &y, Common::String &err);

	/** `key`: what to press. */
	struct KeySpec {
		Common::KeyCode keycode;
		uint16 ascii;
		byte flags;
	};
	/**
	 * `key <name>` or `key <keycode> [ascii] [flags]`. One character is
	 * always that key (`key 5` is the 5 key); a longer all-digit word is a
	 * keycode, 1 .. KEYCODE_LAST-1, with ascii 0..65535 and flags a subset of
	 * the KBD_* bits (0..127). ascii and flags are only taken after a
	 * keycode. False with @p err set otherwise.
	 */
	static bool parseKey(const Common::StringArray &args, KeySpec &out, Common::String &err);

	/** The names `key` knows (Return, Escape, Tab, space, BackSpace, arrows, KP_1..9, F1..F10) or one character. */
	static bool keyByName(const Common::String &name, Common::KeyCode &code, uint16 &ascii);

	/** `save`/`load`: a slot 0..kMaxSlot. */
	static bool parseSlot(const Common::String &s, int &slot);
	static const uint32 kMaxSlot = 999;

	/**
	 * @p key from the running game's own domain (ConfMan.getActiveDomain()),
	 * never from [scummvm] or another domain: a debug_socket= written under
	 * [scummvm] would otherwise open a socket in every game. False when the
	 * domain is null or lacks the key.
	 */
	static bool gameDomainKey(const Common::ConfigManager::Domain *game, const char *key, Common::String &value);

	/** Start `wait frames <n>`; n == 0 ends at once. */
	void startFrameWait(uint32 n);
	bool frameWaitActive() const { return _waitLeft > 0; }
	/** Cancel a pending frame wait (no reply is owed any more). */
	void cancelFrameWait() { _waitLeft = 0; }

	/**
	 * Count one Debugger::onFrame(). True exactly once, on the call that
	 * ends a pending frame wait: the caller sends its reply then.
	 */
	bool onFrame();

	/** onFrame() calls counted so far. */
	uint32 frames() const { return _frames; }

private:
	uint32 _frames;
	uint32 _waitLeft;
};

} // End of namespace GUI

#endif
