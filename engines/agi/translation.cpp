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

#include "agi/translation.h"

#include "common/file.h"
#include "common/stream.h"
#include "common/textconsole.h"
#include "common/util.h"

namespace Agi {

bool TextOverlay::load(const Common::String &languageCode) {
	const Common::String name = Common::String::format("agi-%s.str", languageCode.c_str());
	Common::File f;
	if (!f.open(Common::Path(name)))
		return false;
	if (!loadFromStream(f)) {
		warning("TextOverlay: %s is malformed, ignored", name.c_str());
		return false;
	}
	return true;
}

Common::String TextOverlay::unescape(const Common::String &s) {
	Common::String out;
	for (uint i = 0; i < s.size(); i++) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			const char n = s[i + 1];
			if (n == 'n' || n == 't' || n == '\\') {
				out += (n == 'n') ? '\n' : (n == 't') ? '\t' : '\\';
				i++;
				continue;
			}
		}
		out += s[i];
	}
	return out;
}

// Decimal digits at @p p, value <= @p max; advances @p p past them.
static bool readNum(const Common::String &line, uint &p, uint32 max, uint16 &out) {
	const uint start = p;
	uint32 v = 0;
	while (p < line.size() && Common::isDigit(line[p])) {
		v = v * 10 + (line[p] - '0');
		p++;
		if (v > max)
			return false;
	}
	out = (uint16)v;
	return p > start;
}

TextOverlay::LineKind TextOverlay::parseLine(const Common::String &line, Key &key,
		Common::String &text, const char *&error) {
	if (line.empty() || line[0] == '#')
		return kLineSkip;
	uint p = 1;
	if (line[0] == 'm') {
		key.kind = kKeyMessage;
		if (!readNum(line, p, 255, key.logic) || p >= line.size() || line[p] != ':') {
			error = "bad logic number";
			return kLineError;
		}
		p++;
		if (!readNum(line, p, 255, key.number) || key.number == 0) {
			error = "bad message number (1..255)";
			return kLineError;
		}
	} else if (line[0] == 'o') {
		key.kind = kKeyObject;
		key.logic = 0;
		if (!readNum(line, p, 255, key.number)) {
			error = "bad object index";
			return kLineError;
		}
	} else if (line[0] == 'v') {
		key.kind = kKeyView;
		key.logic = 0;
		if (!readNum(line, p, 255, key.number)) {
			error = "bad view number";
			return kLineError;
		}
	} else {
		error = "unknown key prefix (m<logic>:<n>, o<index> or v<view>)";
		return kLineError;
	}
	if (p >= line.size() || line[p] != '\t') {
		error = "no tab after the key";
		return kLineError;
	}
	text = Common::String(line.c_str() + p + 1);
	return kLineEntry;
}

bool TextOverlay::loadFromStream(Common::SeekableReadStream &in) {
	_entries.clear();
	_present = false;
	_loaded = false;

	Common::HashMap<uint32, uint> firstLine;
	uint lineNo = 0;
	while (!in.eos()) {
		Common::String line = in.readLine();
		lineNo++;
		if (lineNo == 1 && line.hasPrefix("\xEF\xBB\xBF"))
			line.erase(0, 3);
		if (line.hasSuffix("\r"))
			line.deleteLastChar();

		Key key;
		Common::String text;
		const char *error = nullptr;
		const LineKind kind = parseLine(line, key, text, error);
		if (kind == kLineSkip)
			continue;
		if (kind == kLineError) {
			warning("TextOverlay: line %u: %s", lineNo, error);
			_entries.clear();
			return false;
		}
		const uint32 id = pack(key.kind, key.logic, key.number);
		if (firstLine.contains(id))
			warning("TextOverlay: line %u duplicates line %u", lineNo, firstLine[id]);
		else
			firstLine[id] = lineNo;
		_entries[id] = unescape(text);
	}
	_present = true;
	_loaded = !_entries.empty();
	return true;
}

Common::String TextOverlay::legacyToUtf8(const char *bytes, Common::CodePage page) {
	return Common::String(bytes).decode(page).encode(Common::kUtf8);
}

Common::String TextOverlay::keyName(const Key &key) {
	if (key.kind == kKeyObject)
		return Common::String::format("o%u", key.number);
	if (key.kind == kKeyView)
		return Common::String::format("v%u", key.number);
	return Common::String::format("m%u:%u", key.logic, key.number);
}

TextOverlay::Key TextOverlay::unpack(uint32 id) {
	Key k;
	k.kind = (KeyKind)(id >> 24);
	k.logic = (uint16)((id >> 8) & 0xFFFF);
	k.number = (uint16)(id & 0xFF);
	return k;
}

const Common::String *TextOverlay::message(uint16 logic, uint16 number) const {
	Common::HashMap<uint32, Common::String>::const_iterator it = _entries.find(pack(kKeyMessage, logic, number));
	return it == _entries.end() ? nullptr : &it->_value;
}

const Common::String *TextOverlay::object(uint16 index) const {
	Common::HashMap<uint32, Common::String>::const_iterator it = _entries.find(pack(kKeyObject, 0, index));
	return it == _entries.end() ? nullptr : &it->_value;
}

const Common::String *TextOverlay::view(uint16 nr) const {
	Common::HashMap<uint32, Common::String>::const_iterator it = _entries.find(pack(kKeyView, 0, nr));
	return it == _entries.end() ? nullptr : &it->_value;
}

void TextOverlay::collectKeys(Common::Array<Key> &out) const {
	for (Common::HashMap<uint32, Common::String>::const_iterator it = _entries.begin(); it != _entries.end(); ++it)
		out.push_back(unpack(it->_key));
}

} // End of namespace Agi
