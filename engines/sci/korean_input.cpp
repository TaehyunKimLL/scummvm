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

#include "sci/korean_input.h"
#include "sci/utf8.h"

namespace Sci {

Common::String KoreanEditLine::encode(uint32 cp) {
	byte buf[4];
	const int n = encodeUtf8Char(cp, buf);
	return Common::String((const char *)buf, n);
}

uint KoreanEditLine::prevBoundary(const Common::String &text, uint pos) {
	return pos == 0 ? 0 : utf8PrevBoundary((const byte *)text.c_str(), text.size(), pos);
}

uint KoreanEditLine::nextBoundary(const Common::String &text, uint pos) {
	return utf8NextBoundary((const byte *)text.c_str(), text.size(), pos);
}

void KoreanEditLine::snapshot(const Common::String &text, uint cursor) {
	_undoHangul = _hangul;
	_undoRunStart = _runStart;
	_undoComposing = _composing;
	_undoText = text;
	_undoCursor = cursor;
}

void KoreanEditLine::undo(Common::String &text, uint &cursor) {
	_hangul = _undoHangul;
	_runStart = _undoRunStart;
	_composing = _undoComposing;
	text = _undoText;
	cursor = _undoCursor;
}

bool KoreanEditLine::writeRun(Common::String &text, uint &cursor, uint maxBytes) {
	const Common::U32String run = _hangul.text();
	Common::String bytes;
	for (uint i = 0; i < run.size(); ++i)
		bytes += encode(run[i]);
	const Common::String head(text.c_str(), _runStart);
	const Common::String tail(text.c_str() + cursor);
	if (head.size() + bytes.size() + tail.size() > maxBytes)
		return false;
	text = head + bytes + tail;
	cursor = _runStart + bytes.size();
	if (_hangul.empty())
		_composing = false;
	return true;
}

bool KoreanEditLine::adopt(Common::String &text, uint &cursor, bool hangulMode, uint32 osImeCp, uint maxBytes) {
	if (text == _last)
		return false;
	commit();
	_last = text;
	if (text.size() != 1 || cursor != 1)
		return false;
	if (osImeCp >= 0x100 && text[0] == kOsImeSurrogateKey) {
		const Common::String s = encode(osImeCp);
		if (s.size() > maxBytes)
			return false;
		text = s;
		cursor = s.size();
		return true;
	}
	if (hangulMode && Common::HangulComposer::isJamoKey(text[0])) {
		const Common::String original = text;
		const uint originalCursor = cursor;
		const char first = text[0];
		text.clear();
		cursor = 0;
		_runStart = 0;
		_composing = true;
		_hangul.feed(first);
		if (writeRun(text, cursor, maxBytes))
			return true;
		// the first key does not fit: nothing changes, no half-built run
		text = original;
		cursor = originalCursor;
		_hangul.reset();
		_composing = false;
		return false;
	}
	return false;
}

KoreanEditLine::Result KoreanEditLine::key(uint16 key, bool ctrl, bool hangulMode, uint32 osImeCp,
                                            Common::String &text, uint &cursor, uint maxBytes, bool alt) {
	if (alt)
		return kIgnored;
	snapshot(text, cursor);
	if (cursor > text.size())
		cursor = text.size();

	if (osImeCp >= 0x100 && key == kOsImeSurrogateKey && !ctrl) {
		commit();
		const Common::String s = encode(osImeCp);
		if (text.size() + s.size() > maxBytes)
			return kRefused;
		text.insertString(s, cursor);
		cursor += s.size();
		return kChanged;
	}

	if (hangulMode && !ctrl && key < 0x80 && Common::HangulComposer::isJamoKey((char)key)) {
		if (!_composing) {
			_hangul.reset();
			_runStart = cursor;
			_composing = true;
		}
		_hangul.feed((char)key);
		if (!writeRun(text, cursor, maxBytes)) {
			undo(text, cursor);
			return kRefused;
		}
		return kChanged;
	}

	if (key == kKeyBackspace && _composing) {
		_hangul.backspace();
		writeRun(text, cursor, maxBytes);
		return kChanged;
	}

	// Any other key ends the syllable, then acts on code points.
	commit();
	switch (key) {
	case kKeyBackspace:
		if (cursor == 0)
			return kIgnored;
		{
			const uint from = prevBoundary(text, cursor);
			text.erase(from, cursor - from);
			cursor = from;
		}
		return kChanged;
	case kKeyDelete:
		if (cursor >= text.size())
			return kIgnored;
		text.erase(cursor, nextBoundary(text, cursor) - cursor);
		return kChanged;
	case kKeyHome:
		cursor = 0;
		return kChanged;
	case kKeyEnd:
		cursor = text.size();
		return kChanged;
	case kKeyLeft:
		if (cursor == 0)
			return kIgnored;
		cursor = prevBoundary(text, cursor);
		return kChanged;
	case kKeyRight:
		if (cursor >= text.size())
			return kIgnored;
		cursor = nextBoundary(text, cursor);
		return kChanged;
	case kKeyEtx:
		if (!ctrl)
			return kIgnored;
		text.clear();
		cursor = 0;
		return kChanged;
	default:
		break;
	}
	if (ctrl && key == 'c') {
		text.clear();
		cursor = 0;
		return kChanged;
	}
	if (key > 31 && key < 127 && !ctrl) {
		if (text.size() + 1 > maxBytes)
			return kRefused;
		text.insertChar((char)key, cursor++);
		return kChanged;
	}
	return kIgnored;
}

} // End of namespace Sci
