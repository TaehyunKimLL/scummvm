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

#ifndef GRAPHICS_HIRES_TEXT_FONT_VALUE_H
#define GRAPHICS_HIRES_TEXT_FONT_VALUE_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/str.h"

namespace Graphics {

/**
 * The shared font-value and `[glyphs]` value layer for hi-res text
 * configuration (design sections 5 and 6.7): a value grammar shared by the
 * map's `face=`/`range.<spec>=` keys and the ini `hires_text_face`, and the
 * `[glyphs]` grammar (a code point, an offset, or a targeted glyph naming
 * exactly one face).
 */

/// Virtual code point base of a `[glyphs]` targeted-glyph rule (design
/// section 6.7's implementation contract): `kHiResTargetBase + index`,
/// outside Unicode, decoded by the ranged glyph source into `{face, cp}`.
static const uint32 kHiResTargetBase = 0x110000;

/// SCI only: virtual code point base meaning "the game's font draws this
/// game code" (design vocabulary; not produced by this file's parsers yet).
static const uint32 kHiResGameCodeBase = 0x120000;

/** What one font-value entry (section 5.2) resolves to. */
enum HiResFaceKind {
	kHiResFaceFile = 0,  ///< a face opened from @p path (an SVFN or TrueType file)
	kHiResFaceSame,      ///< `same`: the enclosing scope's chain
	kHiResFaceOriginal   ///< `original`: the game's own font draws it
};

/** One entry of a font value, or the single face named by a targeted glyph. */
struct HiResFaceEntry {
	HiResFaceKind kind;
	Common::String written; ///< the entry exactly as written (diagnostics)
	Common::Path path;      ///< resolved path; empty for `same`/`original`
};

/**
 * A parsed font value (design section 5.1): an ordered list of faces, `same`
 * and `original` included as entries in place.
 */
struct HiResFontValue {
	Common::Array<HiResFaceEntry> entries;

	/** No entry survived parsing. */
	bool empty() const { return entries.empty(); }

	/** Any entry is `same`. */
	bool hasSame() const {
		for (uint i = 0; i < entries.size(); ++i) {
			if (entries[i].kind == kHiResFaceSame)
				return true;
		}
		return false;
	}

	/** The chain ends in `original` (entries after it were dropped). */
	bool endsInOriginal() const {
		return !entries.empty() && entries.back().kind == kHiResFaceOriginal;
	}
};

/** The map's `[fonts]` name table: name (case-insensitive) -> path as written. */
typedef Common::HashMap<Common::String, Common::String,
		Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> HiResFaceNames;

/**
 * Whether @p name is a legal `[fonts]` name (design section 3.2): one or
 * more of `[A-Za-z0-9_-]`, and not (case-insensitive) `same`, `original` or
 * `data`.
 */
bool isValidFaceName(const Common::String &name);

/**
 * Parse a bare code point: `u+XXXX`, `U+XXXX` or `0xXXXX` (1-6 hex digits,
 * design section 3.1). Does not accept a leading `+` (that is the
 * `[glyphs]` offset grammar, section 6.7).
 */
bool parseCodePointValue(const Common::String &text, uint32 &out);

/**
 * Parse a font value (design section 5.1): a comma-separated list of
 * `same`, `original`, a `[fonts]` name, or a path. A `[fonts]` name is
 * resolved with `HiResFontMap::resolvePath()` against @p namesBaseDir (the
 * map's own folder); a literal path is resolved against @p pathBaseDir (the
 * map's folder for a map key, the game folder for an ini key, section 4).
 *
 * An entry that is none of these is dropped with one warning
 * (`"HIRESTXT.MAP: unknown face name '<x>'"`); entries after `original`
 * are dropped with one warning naming them
 * (`"HIRESTXT.MAP: entries after 'original' are ignored: '<x>', ..."`).
 * Warnings are appended to @p warnings verbatim (design section 10.2's
 * wording); this function never calls warning() itself.
 *
 * @return false when no entry survived (out.empty()).
 */
bool parseFontValue(const Common::String &text, const HiResFaceNames &names,
					const Common::Path &namesBaseDir, const Common::Path &pathBaseDir,
					HiResFontValue &out, Common::Array<Common::String> &warnings);

/** What a parsed `[glyphs]` value (design section 6.7) means. */
enum HiResGlyphKind {
	kHiResGlyphOriginal = 0,  ///< the game's font draws the game code
	kHiResGlyphCodePoint,     ///< draw code point `value`; range rules apply
	kHiResGlyphOffset,        ///< draw `c + value`; range rules apply
	kHiResGlyphTarget,        ///< draw code point `value` from exactly `face`
	kHiResGlyphTargetOffset   ///< draw `c + value` from exactly `face`
};

/** One parsed `[glyphs]` value. @p face is set only for the two targeted kinds. */
struct HiResGlyphRule {
	HiResGlyphKind kind;
	uint32 value;
	HiResFaceEntry face;
};

/**
 * Parse a `[glyphs]` value (design section 6.7): `original`; `u+XXXX` /
 * `U+XXXX`; `+0xNNNN`; or a targeted glyph `<face>:u+XXXX` / `<face>:+0xNNNN`,
 * where `<face>` is one font-value entry (a `[fonts]` name, a path, or
 * `same`; both name and path resolve against @p baseDir, the map's own
 * folder - `[glyphs]` is map-only). The value is split at its *last* colon,
 * and only when the text after it starts with `u+`/`U+`/`+` (so `data:` and
 * drive letters in @p face survive).
 *
 * @param error set on failure: `"a [glyphs] target names one face, not a
 *              chain"` when @p face would be a comma-separated chain;
 *              otherwise a short reason (original as a target face, an
 *              unknown face name, or a code point/offset that does not
 *              parse). Never prefixed with "HIRESTXT.MAP:" - the caller
 *              builds the full warning with the key's own context.
 * @return false on failure.
 */
bool parseGlyphRule(const Common::String &text, const HiResFaceNames &names, const Common::Path &baseDir,
					HiResGlyphRule &out, Common::String &error);

} // End of namespace Graphics

#endif
