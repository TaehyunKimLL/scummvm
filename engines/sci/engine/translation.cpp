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

#include "sci/engine/translation.h"

#include "common/file.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace Sci {

bool ScriptStrings::load(const Common::String &languageCode) {
	// The file is named by the ScummVM language code the game was detected
	// as - "ko" for KO_KOR - so a directory can carry one table per language
	// and the detection, not the file, says which applies.
	const Common::String name = Common::String::format("sci-%s.str", languageCode.c_str());
	Common::File f;
	if (!f.open(Common::Path(name)))
		return false;
	loadFromStream(f);
	if (!_present) {
		warning("ScriptStrings: %s is malformed, ignored", name.c_str());
		return false;
	}
	return true;
}

// "\n" in the file is a newline in the string; nothing else is escaped.
static Common::String unescape(const Common::String &s) {
	Common::String out;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
			out += '\n';
			i++;
		} else {
			out += s[i];
		}
	}
	return out;
}

// One or more ASCII digits at @p p, value 0..65534 (0xFFFF is kAnyRoom).
// Advances @p p past the digits.
static bool readNum(const Common::String &line, uint &p, uint16 &out) {
	const uint start = p;
	uint32 v = 0;
	while (p < line.size() && Common::isDigit(line[p])) {
		v = v * 10 + (line[p] - '0');
		p++;
		if (v > 65534)
			return false;
	}
	out = (uint16)v;
	return p > start;
}

ScriptStrings::LineKind ScriptStrings::parseLine(const Common::String &line, Key &key,
		Common::String &text, const char *&error) {
	if (line.empty() || line[0] == '#')
		return kLineSkip;

	// script <TAB> id <TAB> text
	// script <TAB> id <TAB> room <TAB> text
	// The key is checked byte for byte: a key that parsed loosely would put
	// a translation in the wrong place, which nobody would notice.
	uint p = 0;
	if (!readNum(line, p, key.script) || p >= line.size() || line[p] != '\t') {
		error = "bad script number";
		return kLineError;
	}
	p++;
	if (!readNum(line, p, key.id) || p >= line.size() || line[p] != '\t') {
		error = "bad string id";
		return kLineError;
	}
	p++;

	// The third field is the room only if it is a number AND another tab
	// follows it; otherwise the text starts there and runs to the end of
	// the line, tabs included.
	key.room = Key::kAnyRoom;
	uint q = p;
	uint16 room;
	if (readNum(line, q, room) && q < line.size() && line[q] == '\t') {
		key.room = room;
		p = q + 1;
	}
	text = Common::String(line.c_str() + p);
	return kLineEntry;
}

bool ScriptStrings::loadFromStream(Common::SeekableReadStream &in) {
	_entries.clear();
	_loaded = false;
	_present = false;

	Common::HashMap<uint64, uint> firstLine;
	uint lineNo = 0;
	while (!in.eos()) {
		Common::String line = in.readLine();
		lineNo++;
		if (lineNo == 1 && line.hasPrefix("\xEF\xBB\xBF"))
			line.erase(0, 3);

		Key key;
		Common::String text;
		const char *error = nullptr;
		const LineKind kind = parseLine(line, key, text, error);
		if (kind == kLineSkip)
			continue;
		if (kind == kLineError) {
			warning("ScriptStrings: line %u: %s", lineNo, error);
			_entries.clear();
			return false;
		}

		// The later line wins, so a fix can be appended to a generated
		// file; say so, in case it was not meant.
		const uint64 place = placeId(key.script, key.id, key.room);
		if (firstLine.contains(place))
			warning("ScriptStrings: line %u duplicates line %u", lineNo, firstLine[place]);
		else
			firstLine[place] = lineNo;
		_entries[place] = unescape(text);
	}

	_present = true;
	_loaded = !_entries.empty();
	return _loaded;
}

void ScriptStrings::collectTexts(Common::Array<Common::String> &out) const {
	for (Common::HashMap<uint64, Common::String>::const_iterator it = _entries.begin(); it != _entries.end(); ++it)
		out.push_back(it->_value);
}

bool ScriptStrings::lookup(const Key &key, Common::String &out) const {
	if (!_loaded || !key.isSet())
		return false;
	if (key.room != Key::kAnyRoom) {
		const uint64 exact = placeId(key.script, key.id, key.room);
		if (_entries.contains(exact)) {
			out = _entries.getVal(exact);
			return true;
		}
	}
	const uint64 any = placeId(key.script, key.id, Key::kAnyRoom);
	if (_entries.contains(any)) {
		out = _entries.getVal(any);
		return true;
	}
	return false;
}

void ScriptStrings::tagBuffer(uint32 buffer, const Key &key, const Common::String &text) {
	BufferTag t;
	t.key = key;
	t.text = text;
	_bufferTags[buffer] = t;
}

ScriptStrings::Key ScriptStrings::keyOf(uint32 buffer, const Common::String &text) const {
	if (!_bufferTags.contains(buffer))
		return Key();
	const BufferTag &t = _bufferTags.getVal(buffer);
	// The buffer has been reused for something else: the tag is stale.
	if (t.text != text)
		return Key();
	return t.key;
}

} // End of namespace Sci
