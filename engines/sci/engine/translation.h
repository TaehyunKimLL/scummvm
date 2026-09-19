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

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/ustr.h"

namespace Sci {

/**
 * A SCITRS translation bundle: Unicode, source-keyed, language-neutral.
 *
 * Unlike the legacy Text.MAP/Text.Res overlay (see TextOverlay), entries are
 * keyed by the ORIGINAL text rather than by resource number and index, so a
 * script patch that renumbers or reorders strings cannot silently mistranslate,
 * and an entry that is missing degrades to the original rather than to garbage.
 *
 * Strings are stored as UTF-8 and returned as Common::U32String, so the file
 * carries no byte encoding and one bundle format serves every language.
 *
 * The format is specified in docs/i18n/SCITRS_FORMAT.md and bundles are built
 * by harness/i18n/m5mktrs.py, which validates what it writes.
 */
class Translation {
public:
	Translation() : _loaded(false), _entries(nullptr), _entryCount(0),
		_buckets(nullptr), _bucketCount(0), _pool(nullptr), _poolLen(0) {}

	/**
	 * Look for a bundle in the game directory and index the requested
	 * language. Returns true only if that language is present and the file
	 * passes every structural check; a partially valid bundle is rejected
	 * whole, because a half-indexed translation is worse than none.
	 */
	bool load(const Common::String &language);

	/**
	 * Parse an already-read bundle. What load() calls after opening the
	 * file; public so a test can build a bundle in memory and exercise the
	 * lookup without a data directory.
	 */
	bool loadFromMemory(const Common::Array<byte> &bytes, const Common::String &language);

	/**
	 * Load whichever language the bundle declares first.
	 *
	 * A bundle names its own language, so requiring the caller to guess it
	 * means a ja bundle silently does not load when the caller asked for ko.
	 * Used when the user has expressed no preference.
	 */
	bool loadAny();

	bool isLoaded() const { return _loaded; }

	/**
	 * Where a string came from. The bundle is keyed by the source text, so
	 * this is a hint, not the key: it picks between entries whose source is
	 * the same but whose translation differs by place - LB1 has 86 such
	 * groups - and a hint that matches nothing is simply ignored.
	 *
	 * Two kinds of place exist. A TEXT resource string is (resource, index)
	 * and comes through lookupText(). A string embedded in a script - an
	 * inventory description, a parser reply, the game's title - is
	 * (script, string id), numbered by Script::identifyOffsets() at load.
	 * KQ1 has 49 of the latter that appear in no TEXT resource.
	 */
	struct Key {
		enum Kind { kNone = 0, kText = 1, kScript = 2 };
		static const uint16 kAnyRoom = 0xFFFF;

		Kind kind;
		uint16 number;	///< resource or script number
		uint16 index;	///< string index within it
		uint16 room;	///< the room the string was displayed in, or kAnyRoom

		Key() : kind(kNone), number(0xFFFF), index(0xFFFF), room(kAnyRoom) {}
		Key(Kind k, uint16 n, uint16 i, uint16 r = kAnyRoom) : kind(k), number(n), index(i), room(r) {}
		static Key text(uint16 res, uint16 idx, uint16 r = kAnyRoom) { return Key(kText, res, idx, r); }
		static Key script(uint16 nr, uint16 id, uint16 r = kAnyRoom) { return Key(kScript, nr, id, r); }
		bool isSet() const { return kind != kNone; }
	};

	/**
	 * Translate @p source. Returns false when this bundle has no entry, in
	 * which case the caller must keep the original text.
	 *
	 * @p out receives UTF-8 - the pool's own encoding, copied, never
	 * transcoded. Everything downstream of this call reads UTF-8: the heap
	 * holds it, the string ops count it, GfxText16 walks it.
	 */
	bool translate(const Common::String &source, Common::String &out,
	               const Key &key = Key()) const;

	uint entryCount() const { return _entryCount; }
	const Common::String &language() const { return _language; }

	/**
	 * Remember where a heap buffer's contents came from.
	 *
	 * A script string on its way to the screen is first kStrCpy'd into a
	 * stack buffer, and the buffer is what kDisplay / kDrawControl are
	 * handed - by then nothing says which script string it was. So the copy
	 * records (buffer -> key, text, room) here, and the display looks it up.
	 *
	 * A buffer is reused freely, so the record carries the text that was
	 * written and keyOf() returns nothing when the buffer no longer holds
	 * it. Measured on KQ1: 28 display lookups, 0 stale.
	 *
	 * The buffer is identified by its raw (segment, offset) so this class
	 * stays free of the VM: reg_t::getSegment() consults the SCI version,
	 * which drags the engine into anything that links it (the unit tests).
	 */
	void tagBuffer(uint32 buffer, const Key &key, const Common::String &text);

	/** The key recorded for @p buffer, if it still holds @p text. */
	Key keyOf(uint32 buffer, const Common::String &text) const;

	static uint32 bufferId(uint16 segment, uint16 offset) { return ((uint32)segment << 16) | offset; }

	/** Whitespace normalisation applied to every key. Public for testing. */
	static Common::String normalise(const Common::String &s);
	static uint32 hash(const Common::String &normalised);

private:
	struct Entry {
		uint32 hash;
		uint32 srcOffset;
		uint32 dstOffset;
		uint16 number;
		uint16 index;
		uint16 room;	///< Key::kAnyRoom when the entry does not care
		byte kind;		///< Key::Kind; 0 in bundles written before it existed
	};

	const char *poolString(uint32 offset) const;

	bool _loaded;
	Common::String _language;
	/** Sources already warned about for a hint miss; mutable because a
	 *  lookup is logically const and the warning is a side channel. */
	mutable Common::HashMap<uint32, bool> _warnedFallback;

	struct BufferTag {
		Key key;
		Common::String text;
	};
	Common::HashMap<uint32, BufferTag> _bufferTags;	///< packed reg_t -> tag
	Common::Array<byte> _data;
	Common::Array<Entry> _entryTable;

	const Entry *_entries;
	uint32 _entryCount;
	const uint32 *_buckets;
	uint32 _bucketCount;
	const char *_pool;
	uint32 _poolLen;
};

} // End of namespace Sci

#endif // SCI_ENGINE_TRANSLATION_H
