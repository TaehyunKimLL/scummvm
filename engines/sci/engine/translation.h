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

	bool isLoaded() const { return _loaded; }

	/**
	 * Translate @p source. Returns false when this bundle has no entry, in
	 * which case the caller must keep the original text.
	 *
	 * @p resourceHint and @p indexHint are optional fast-path hints; a hint
	 * that does not match is ignored, so a stale hint can never produce a
	 * wrong string.
	 */
	bool translate(const Common::String &source, Common::U32String &out,
	               uint16 resourceHint = 0xFFFF, uint16 indexHint = 0xFFFF) const;

	uint entryCount() const { return _entryCount; }
	const Common::String &language() const { return _language; }

	/** Whitespace normalisation applied to every key. Public for testing. */
	static Common::String normalise(const Common::String &s);
	static uint32 hash(const Common::String &normalised);

private:
	struct Entry {
		uint32 hash;
		uint32 srcOffset;
		uint32 dstOffset;
		uint16 resource;
		uint16 index;
	};

	const char *poolString(uint32 offset) const;

	bool _loaded;
	Common::String _language;
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
