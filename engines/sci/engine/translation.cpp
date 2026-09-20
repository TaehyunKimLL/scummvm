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
	if (!loadFromStream(f)) {
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

bool ScriptStrings::loadFromStream(Common::SeekableReadStream &in) {
	_entries.clear();
	_loaded = false;

	uint lineNo = 0;
	while (!in.eos()) {
		Common::String line = in.readLine();
		lineNo++;
		if (line.empty() || line[0] == '#')
			continue;

		// script <TAB> id <TAB> text
		// script <TAB> id <TAB> room <TAB> text
		// The first two fields end at a tab. The third is the room only if
		// it is all digits AND another tab follows it; otherwise the text
		// starts there and runs to the end of the line, tabs included.
		uint p = 0;
		uint16 nums[2];
		for (int n = 0; n < 2; n++) {
			uint q = p;
			while (q < line.size() && line[q] != '	')
				q++;
			if (q == line.size()) {
				warning("ScriptStrings: line %u has too few fields", lineNo);
				return false;
			}
			nums[n] = (uint16)atoi(Common::String(line.c_str() + p, q - p).c_str());
			p = q + 1;
		}
		const uint16 script = nums[0];
		const uint16 id = nums[1];

		uint16 room = Key::kAnyRoom;
		uint q = p;
		while (q < line.size() && Common::isDigit(line[q]))
			q++;
		if (q > p && q < line.size() && line[q] == '	') {
			room = (uint16)atoi(Common::String(line.c_str() + p, q - p).c_str());
			p = q + 1;
		}
		const Common::String text(line.c_str() + p);

		_entries[placeId(script, id, room)] = unescape(text);
	}

	_loaded = !_entries.empty();
	return _loaded;
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
