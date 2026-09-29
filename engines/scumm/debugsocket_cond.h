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

#ifndef SCUMM_DEBUGSOCKET_COND_H
#define SCUMM_DEBUGSOCKET_COND_H

// The `wait` conditions of SCUMM's debug socket (scumm/debugsocket.h):
// parsing and evaluation only, with no engine in sight, so both can be
// tested on their own. Header only.

#include "common/array.h"
#include "common/str.h"
#include "common/str-array.h"

namespace Scumm {

struct SocketCond {
	enum Kind { kRoom, kText, kSeen, kTalkDone, kUserPut, kLoops, kBad };
	Kind kind;
	Common::String op;			///< kRoom: "==", "!=", "<", "<=", ">", ">="
	/**
	 * kRoom: the room. kLoops: the loop count asked for as parsed; the caller
	 * adds the loop the wait starts on, so it holds the loop to reach.
	 */
	int value;
	Common::String text;		///< kText: UTF-8, quotes removed
	Common::Array<byte> bytes;	///< kSeen

	SocketCond() : kind(kBad), value(0) {}
};

/// A game state for condHolds(); see DebugSocket::snapshot().
struct SocketSnapshot {
	int room;
	int talking;	///< the talking actor, 0 when none
	int haveMsg;
	int userPut;
	uint32 loop;	///< scummLoop() calls so far
};

/// Decimal digits only, 0..1000000.
inline bool parseSocketNumber(const Common::String &s, int &out) {
	if (s.empty() || s.size() > 7)
		return false;
	int v = 0;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] < '0' || s[i] > '9')
			return false;
		v = v * 10 + (s[i] - '0');
	}
	if (v > 1000000)
		return false;
	out = v;
	return true;
}

inline int socketHexDigit(char c) {
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/**
 * Parse the arguments of `wait` (the words after it, as
 * GUI::DebugSocketProtocol::tokenize() splits a line: a quoted word is one
 * word, its quotes removed):
 *
 *   room <op> N        op one of == != < <= > >=
 *   text "<UTF-8>"     a substring of one string the game drew
 *   seen <hex>         the game's own bytes of a drawn string, an even
 *                      number of hex digits
 *   talkdone           no actor talking and no message on screen
 *   userput            the player has control (userPut > 0)
 *   loops N            N more scummLoop() calls
 *
 * followed, in any order, by `timeout N` (in loops; @p timeoutLoops is left
 * alone without one) and `freeze`. On failure @p error says why.
 */
inline bool parseSocketWait(const Common::StringArray &args, SocketCond &cond, uint32 &timeoutLoops,
                            bool &freeze, Common::String &error) {
	cond = SocketCond();
	freeze = false;
	if (args.empty()) {
		error = "usage: wait <cond> [timeout N] [freeze]";
		return false;
	}
	uint i = 0;
	const Common::String &w = args[i++];
	if (w == "room") {
		if (i + 2 > args.size()) {
			error = "usage: wait room <op> <n>";
			return false;
		}
		const Common::String &op = args[i++];
		if (op != "==" && op != "!=" && op != "<" && op != "<=" && op != ">" && op != ">=") {
			error = "bad operator '" + op + "'";
			return false;
		}
		if (!parseSocketNumber(args[i], cond.value)) {
			error = "bad room '" + args[i] + "'";
			return false;
		}
		i++;
		cond.kind = SocketCond::kRoom;
		cond.op = op;
	} else if (w == "text") {
		if (i >= args.size() || args[i].empty()) {
			error = "usage: wait text \"<substring>\"";
			return false;
		}
		Common::String t = args[i++];
		// A caller that did not tokenize: the quotes are not part of it.
		if (t.size() >= 2 && t.firstChar() == '"' && t.lastChar() == '"')
			t = Common::String(t.c_str() + 1, t.size() - 2);
		if (t.empty()) {
			error = "empty text";
			return false;
		}
		cond.kind = SocketCond::kText;
		cond.text = t;
	} else if (w == "seen") {
		if (i >= args.size()) {
			error = "usage: wait seen <hex bytes>";
			return false;
		}
		const Common::String &h = args[i++];
		if (h.empty() || (h.size() & 1)) {
			error = "seen wants an even number of hex digits";
			return false;
		}
		for (uint k = 0; k < h.size(); k += 2) {
			const int hi = socketHexDigit(h[k]), lo = socketHexDigit(h[k + 1]);
			if (hi < 0 || lo < 0) {
				error = "bad hex '" + h + "'";
				return false;
			}
			cond.bytes.push_back((byte)(hi * 16 + lo));
		}
		cond.kind = SocketCond::kSeen;
	} else if (w == "talkdone") {
		cond.kind = SocketCond::kTalkDone;
	} else if (w == "userput") {
		cond.kind = SocketCond::kUserPut;
	} else if (w == "loops") {
		if (i >= args.size() || !parseSocketNumber(args[i], cond.value)) {
			error = "usage: wait loops <n>";
			return false;
		}
		i++;
		cond.kind = SocketCond::kLoops;
	} else {
		error = "unknown condition '" + w + "'";
		return false;
	}

	while (i < args.size()) {
		const Common::String &opt = args[i++];
		if (opt == "freeze") {
			freeze = true;
		} else if (opt == "timeout") {
			int n;
			if (i >= args.size() || !parseSocketNumber(args[i], n) || n == 0) {
				error = "usage: timeout <loops>";
				return false;
			}
			i++;
			timeoutLoops = (uint32)n;
		} else {
			error = "unexpected '" + opt + "'";
			return false;
		}
	}
	return true;
}

inline bool socketContains(const Common::Array<byte> &hay, const Common::Array<byte> &needle) {
	if (needle.empty() || needle.size() > hay.size())
		return false;
	for (uint i = 0; i + needle.size() <= hay.size(); i++) {
		uint k = 0;
		while (k < needle.size() && hay[i + k] == needle[k])
			k++;
		if (k == needle.size())
			return true;
	}
	return false;
}

/**
 * Whether @p c holds in state @p s. @p textsUtf8 and @p textsRaw are the
 * strings the condition may look at (the caller picks which: those drawn
 * since the wait's reference point), decoded and as the game's bytes.
 */
inline bool condHolds(const SocketCond &c, const SocketSnapshot &s,
                      const Common::Array<Common::String> &textsUtf8,
                      const Common::Array<Common::Array<byte> > &textsRaw) {
	switch (c.kind) {
	case SocketCond::kRoom:
		if (c.op == "==") return s.room == c.value;
		if (c.op == "!=") return s.room != c.value;
		if (c.op == "<") return s.room < c.value;
		if (c.op == "<=") return s.room <= c.value;
		if (c.op == ">") return s.room > c.value;
		if (c.op == ">=") return s.room >= c.value;
		return false;
	case SocketCond::kText:
		for (uint i = 0; i < textsUtf8.size(); i++)
			if (textsUtf8[i].contains(c.text))
				return true;
		return false;
	case SocketCond::kSeen:
		for (uint i = 0; i < textsRaw.size(); i++)
			if (socketContains(textsRaw[i], c.bytes))
				return true;
		return false;
	case SocketCond::kTalkDone:
		return s.talking == 0 && s.haveMsg == 0;
	case SocketCond::kUserPut:
		return s.userPut > 0;
	case SocketCond::kLoops:
		return s.loop >= (uint32)c.value;
	default:
		return false;
	}
}

/**
 * Why a wait (or `run`) has to end with no loop to check it on, or null.
 * "paused" while the engine is paused - a modal dialog; go() then runs no
 * loop at all - and "stalled" when no loop has ended for @p limitMs since
 * @p progressMs (the last loop end, or when the wait came).
 */
inline const char *socketStall(bool paused, uint32 nowMs, uint32 progressMs, uint32 limitMs) {
	if (paused)
		return "paused";
	if (nowMs - progressMs >= limitMs)
		return "stalled";
	return nullptr;
}

} // End of namespace Scumm

#endif
