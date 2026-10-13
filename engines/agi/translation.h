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

#ifndef AGI_TRANSLATION_H
#define AGI_TRANSLATION_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/str-enc.h"

namespace Common {
class SeekableReadStream;
}

namespace Agi {

/**
 * A translation of an AGI game's texts that leaves the game files alone.
 *
 * AGI scripts refer to a message by its number only (print 3 is the third
 * string of the current logic's message block) and to an object by its
 * index, so a translation can be keyed by where a string lives and swapped
 * in at load: a longer string shifts nothing. The file is agi-<lang>.str in
 * the game folder (AGI-KO.STR on DOS), UTF-8, one entry per line:
 *
 *     m<logic>:<n> TAB text      message n (1-based) of LOGIC.<logic>
 *     o<index> TAB text          inventory object <index> (0-based)
 *
 * '#' comments, a UTF-8 BOM skipped, escapes \n \t \\. A malformed line
 * rejects the whole table (a wrong key would put a translation in the
 * wrong place, unnoticed); a duplicate key is a warning and the later wins.
 * The file being there at all - comments only included - is what switches
 * the engine into UTF-8 text mode (AgiBase::textIsUtf8()), like SCI's
 * sci-<lang>.str.
 */
class TextOverlay {
public:
	enum KeyKind { kKeyMessage = 0, kKeyObject = 1, kKeyView = 2 };

	struct Key {
		KeyKind kind;
		uint16 logic;   ///< messages only
		uint16 number;  ///< message number (1-based), object index or view number (0-based)
		Key() : kind(kKeyMessage), logic(0), number(0) {}
	};

	enum LineKind { kLineSkip, kLineEntry, kLineError };

	TextOverlay() : _present(false), _loaded(false) {}

	/** agi-<languageCode>.str through SearchMan; true when present. */
	bool load(const Common::String &languageCode);

	/** The table from an open stream; true when it parsed (entries or not). */
	bool loadFromStream(Common::SeekableReadStream &in);

	/**
	 * One line, its end stripped: blank and '#' lines are skipped; anything
	 * else is an entry or kLineError with @p error saying why. Numbers are
	 * decimal, logic 0..255, message 1..255, object 0..255, view 0..255
	 * (v<view>: the view's description).
	 */
	static LineKind parseLine(const Common::String &line, Key &key, Common::String &text, const char *&error);

	static Common::String unescape(const Common::String &s);

	/**
	 * A string the overlay does not translate, in the game's code page,
	 * as UTF-8: in UTF-8 text mode every displayed string is UTF-8, and a
	 * byte >= 0x80 left raw would be decoded as a broken sequence.
	 */
	static Common::String legacyToUtf8(const char *bytes, Common::CodePage page);

	static Common::String keyName(const Key &key);

	bool isPresent() const { return _present; }
	bool isLoaded() const { return _loaded; }
	uint entryCount() const { return _entries.size(); }

	const Common::String *message(uint16 logic, uint16 number) const;
	const Common::String *object(uint16 index) const;
	/** A view's description (show.obj's inventory close-up text). */
	const Common::String *view(uint16 nr) const;

	void collectKeys(Common::Array<Key> &out) const;

private:
	static uint32 pack(KeyKind kind, uint16 logic, uint16 number) {
		return ((uint32)kind << 24) | ((uint32)logic << 8) | number;
	}
	static Key unpack(uint32 id);

	bool _present;
	bool _loaded;
	Common::HashMap<uint32, Common::String> _entries;
};

} // End of namespace Agi

#endif
