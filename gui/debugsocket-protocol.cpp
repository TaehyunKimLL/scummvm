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
