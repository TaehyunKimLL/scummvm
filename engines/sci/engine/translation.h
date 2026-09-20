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

#ifndef SCI_ENGINE_TRANSLATION_H
#define SCI_ENGINE_TRANSLATION_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/hashmap.h"

namespace Common {
class SeekableReadStream;
}

namespace Sci {

/**
 * Translations for the strings a fan translation cannot patch: the ones
 * embedded in scripts.
 *
 * A fan translation replaces the game's TEXT resources with patch files
 * (text.000, text.079, ...) - the way Sierra's own localisations and every
 * existing fan patch work, and the way the engine has always loaded them.
 * Those strings need no help from this class: the resource IS the
 * translation.
 *
 * What a patch cannot reach is a string inside a script's own string block:
 * an inventory name, a parser reply, "You are carrying nothing!". The code
 * refers to it by absolute offset (lofsa), so a longer translation would
 * shift every string after it and every offset into them. KQ1 has 93 such
 * strings that reach the screen and appear in no TEXT resource. They are
 * translated at run time instead, from this table, keyed by where they
 * live: (script number, string id), the id being the one
 * Script::identifyOffsets() assigns at load and SegManager::stringKey()
 * reports at display.
 *
 * The file is sci-<lang>.str next to the game, one entry per line:
 *
 *     script <TAB> id [<TAB> room] <TAB> text
 *
 * UTF-8, '#' comments, "\n" for a newline in the text. A room number makes
 * the entry apply in that room only, for a line that means different things
 * in different places; an entry without one applies anywhere and loses to
 * one with a matching room. The language is not in the file: it is the
 * language the game was detected as, which chooses the file.
 */
class ScriptStrings {
public:
	/** Where a string lives. */
	struct Key {
		static const uint16 kAnyRoom = 0xFFFF;

		uint16 script;
		uint16 id;
		uint16 room;

		Key() : script(0xFFFF), id(0xFFFF), room(kAnyRoom) {}
		Key(uint16 s, uint16 i, uint16 r = kAnyRoom) : script(s), id(i), room(r) {}
		bool isSet() const { return script != 0xFFFF; }
	};

	ScriptStrings() : _loaded(false) {}

	/**
	 * Load sci-<lang>.str from the game directory, if the game ships one.
	 * @p languageCode is the ScummVM code ("ko"), which the caller gets
	 * from Common::getLanguageCode(); taking the string keeps this class
	 * out of common/language's link dependencies for the unit tests.
	 */
	bool load(const Common::String &languageCode);

	/** Parse a table from a stream; what load() does once the file is open. */
	bool loadFromStream(Common::SeekableReadStream &in);

	bool isLoaded() const { return _loaded; }
	uint entryCount() const { return _entries.size(); }

	/**
	 * The translation for the string at @p key, or false to keep the
	 * original. A room-specific entry wins in its room; an any-room entry
	 * answers everywhere else. @p out receives UTF-8.
	 */
	bool lookup(const Key &key, Common::String &out) const;

	/**
	 * Remember that @p buffer now holds the script string at @p key.
	 *
	 * A script string reaches the screen as a stack buffer: the script
	 * kStrCpy's it first, and the buffer is what kDisplay / kDrawControl are
	 * handed - by then nothing says which string it was. So the copy records
	 * (buffer -> key, text written) here, and the display looks it up.
	 *
	 * The buffer is reused freely, so the record carries the text and
	 * keyOf() answers only while the buffer still holds it. Measured on KQ1
	 * through the inventory and two parser replies: 28 lookups, 0 stale.
	 *
	 * @p buffer is bufferId(segment, offset) rather than a reg_t: reg_t's
	 * accessors consult the SCI version, which would drag the engine into
	 * anything that links this class (the unit tests).
	 */
	void tagBuffer(uint32 buffer, const Key &key, const Common::String &text);

	/** The key recorded for @p buffer, if it still holds @p text. */
	Key keyOf(uint32 buffer, const Common::String &text) const;

	static uint32 bufferId(uint16 segment, uint16 offset) { return ((uint32)segment << 16) | offset; }

private:
	/// (script, id, room) packed exactly; no two places share a value.
	static uint64 placeId(uint16 script, uint16 id, uint16 room) {
		return ((uint64)room << 32) | ((uint32)script << 16) | id;
	}

	bool _loaded;
	Common::HashMap<uint64, Common::String> _entries;

	struct BufferTag {
		Key key;
		Common::String text;
	};
	Common::HashMap<uint32, BufferTag> _bufferTags;
};

} // End of namespace Sci

#endif // SCI_ENGINE_TRANSLATION_H
