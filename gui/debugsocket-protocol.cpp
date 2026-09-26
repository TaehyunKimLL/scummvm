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

#include "gui/debugsocket-protocol.h"

#include "common/util.h"

namespace GUI {

DebugSocketProtocol::DebugSocketProtocol() : _frames(0), _waitLeft(0) {
}

void DebugSocketProtocol::tokenize(const Common::String &line, Common::StringArray &out,
                                   Common::StringArray *tails) {
	out.clear();
	if (tails)
		tails->clear();
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
}

bool DebugSocketProtocol::parse(const Common::String &line, Common::String &cmd,
                                Common::StringArray &args, Common::StringArray *tails) {
	tokenize(line, args, tails);
	if (args.empty()) {
		cmd.clear();
		return false;
	}
	cmd = args[0];
	args.remove_at(0);
	if (tails)
		tails->remove_at(0);
	return true;
}

Common::String DebugSocketProtocol::frame(const Common::String &reply) {
	Common::String out;
	uint lineStart = 0;
	for (uint i = 0; i <= reply.size(); i++) {
		if (i < reply.size() && reply[i] != '\n')
			continue;
		// [lineStart, i) is one line; a trailing newline adds no empty line.
		if (i == reply.size() && lineStart == i)
			break;
		if (lineStart < i && reply[lineStart] == '.')
			out += '.';
		out += Common::String(reply.c_str() + lineStart, i - lineStart);
		out += '\n';
		lineStart = i + 1;
	}
	out += ".\n";
	return out;
}

bool DebugSocketProtocol::parseUInt(const Common::String &s, uint32 max, uint32 &n) {
	if (s.empty() || s.size() > 9)
		return false;
	uint32 v = 0;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] < '0' || s[i] > '9')
			return false;
		v = v * 10 + (s[i] - '0');
	}
	if (v > max)
		return false;
	n = v;
	return true;
}

bool DebugSocketProtocol::parseCount(const Common::String &s, uint32 &n) {
	return parseUInt(s, kMaxCount, n);
}

bool DebugSocketProtocol::parsePoint(const Common::StringArray &a, uint w, uint h, int &x, int &y, Common::String &err) {
	if (a.size() < 2) {
		err = "need <x> <y>";
		return false;
	}
	uint32 px, py;
	if (w == 0 || h == 0 || !parseUInt(a[0], w - 1, px) || !parseUInt(a[1], h - 1, py)) {
		err = Common::String::format("bad point '%s' '%s': x 0..%d, y 0..%d", a[0].c_str(), a[1].c_str(), (int)w - 1, (int)h - 1);
		return false;
	}
	x = (int)px;
	y = (int)py;
	return true;
}

static bool keyNameEq(const Common::String &a, const char *b) {
	if (a.size() != strlen(b))
		return false;
	for (uint i = 0; i < a.size(); i++)
		if (tolower(a[i]) != tolower(b[i]))
			return false;
	return true;
}

bool DebugSocketProtocol::keyByName(const Common::String &name, Common::KeyCode &code, uint16 &ascii) {
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
		code = (Common::KeyCode)(c >= 'A' && c <= 'Z' ? c + 32 : (byte)c);
		ascii = (byte)c;
		return true;
	}
	return false;
}

bool DebugSocketProtocol::parseKey(const Common::StringArray &a, KeySpec &out, Common::String &err) {
	if (a.empty() || a.size() > 3) {
		err = "usage: key <name>|<keycode> [ascii] [flags]";
		return false;
	}
	const Common::String &k = a[0];
	bool numeric = k.size() > 1;
	for (uint i = 0; i < k.size() && numeric; i++)
		numeric = (k[i] >= '0' && k[i] <= '9');
	if (!numeric) {
		if (a.size() > 1) {
			err = "ascii and flags go after a keycode, not a key name";
			return false;
		}
		if (!keyByName(k, out.keycode, out.ascii)) {
			err = "unknown key " + k;
			return false;
		}
		out.flags = (k.size() == 1 && k[0] >= 'A' && k[0] <= 'Z') ? Common::KBD_SHIFT : 0;
		return true;
	}
	uint32 code, ascii = 0, flags = 0;
	if (!parseUInt(k, Common::KEYCODE_LAST - 1, code) || code == Common::KEYCODE_INVALID) {
		err = Common::String::format("bad keycode '%s' (1..%d)", k.c_str(), (int)Common::KEYCODE_LAST - 1);
		return false;
	}
	if (a.size() > 1 && !parseUInt(a[1], 0xFFFF, ascii)) {
		err = Common::String::format("bad ascii '%s' (0..65535)", a[1].c_str());
		return false;
	}
	if (a.size() > 2 && !parseUInt(a[2], Common::KBD_NON_STICKY | Common::KBD_STICKY, flags)) {
		err = Common::String::format("bad flags '%s' (0..%d)", a[2].c_str(), (int)(Common::KBD_NON_STICKY | Common::KBD_STICKY));
		return false;
	}
	if (flags & ~(uint32)(Common::KBD_NON_STICKY | Common::KBD_STICKY)) {
		err = Common::String::format("bad flags '%s'", a[2].c_str());
		return false;
	}
	out.keycode = (Common::KeyCode)code;
	// Without an explicit ascii, a keycode below 0x80 is its own character.
	out.ascii = a.size() > 1 ? (uint16)ascii : (code < 0x80 ? (uint16)code : 0);
	out.flags = (byte)flags;
	return true;
}

bool DebugSocketProtocol::parseSlot(const Common::String &s, int &slot) {
	uint32 n;
	if (!parseUInt(s, kMaxSlot, n))
		return false;
	slot = (int)n;
	return true;
}

bool DebugSocketProtocol::gameDomainKey(const Common::ConfigManager::Domain *game, const char *key, Common::String &value) {
	if (!game || !game->contains(key))
		return false;
	value = game->getVal(key);
	return true;
}

void DebugSocketProtocol::startFrameWait(uint32 n) {
	_waitLeft = n;
}

bool DebugSocketProtocol::onFrame() {
	_frames++;
	if (_waitLeft == 0)
		return false;
	return --_waitLeft == 0;
}

} // End of namespace GUI
