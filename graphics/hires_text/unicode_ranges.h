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

#ifndef GRAPHICS_HIRES_TEXT_UNICODE_RANGES_H
#define GRAPHICS_HIRES_TEXT_UNICODE_RANGES_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str.h"

namespace Graphics {

/**
 * The shared Unicode-range layer for hi-res text configuration
 * (design section 6): a fixed block-name table, a parser for the
 * `range.<spec>` / `advance.<spec>` / `origin.<spec>` grammar (section 6.1),
 * and a compiled range table that resolves the "narrowest span wins, `wide`
 * is least specific, first scope that answers wins" precedence of section
 * 6.2 into O(1)/O(log n) lookups with no precedence left to work out at draw
 * time (section 6.2.2).
 */

/** An inclusive Unicode code point span. */
struct HiResSpan {
	uint32 lo;
	uint32 hi;
};

/** What a parsed range spec matched. */
enum HiResSpecKind {
	kHiResSpecSpan = 0,
	kHiResSpecWide
};

/**
 * One parsed `<spec>` (a block name, an explicit `U+XXXX[-YYYY]` span, or
 * `wide`). @p spelled keeps the text as written, for diagnostics: two
 * spellings of the same span (a block name and its equivalent `U+` form)
 * are the same rule (section 6.2.1), a comparison the map loader (task 4)
 * makes by span, not by @p spelled.
 */
struct HiResRangeSpec {
	HiResSpecKind kind;
	HiResSpan span;
	Common::String spelled;
};

/**
 * Look up a block name from the fixed table of design section 6.1
 * (case-insensitive). Returns false for anything not in the table,
 * including partial names like "latin".
 */
bool lookupUnicodeBlock(const Common::String &name, HiResSpan &out);

/**
 * Parse one `<spec>` (design section 6.1): `wide` (case-insensitive), a
 * block name, or an explicit `U+XXXX` / `U+XXXX-YYYY` / `U+XXXX-U+YYYY`
 * span (1-6 hex digits per code point, low <= high, both <= U+10FFFF). The
 * `0x..` hex form is not accepted here: it is reserved for `[glyphs]` keys.
 *
 * On failure @p error is set to `"unknown range '<text>'"` (the text is
 * neither `wide`, a known block, nor shaped like a `U+` span) or
 * `"malformed range '<text>'"` (it is shaped like a `U+` span but its
 * digits or bounds are invalid).
 */
bool parseRangeSpec(const Common::String &text, HiResRangeSpec &out, Common::String &error);

/**
 * One scope's range rules (one `[font]`/`[font.N]` section, already merged
 * with its `:q` qualifier per section 6.2.1): parallel arrays, @p specs[i]
 * maps to @p values[i]. Values are the caller's own ids (font-value table
 * indices, advance/origin enumerators, ...) and are always >= 0; -1 is
 * reserved by HiResRangeTable::lookup() to mean "no rule".
 */
struct HiResRangeScope {
	Common::Array<HiResRangeSpec> specs;
	Common::Array<int> values;
};

/**
 * A compiled, precedence-free range table (section 6.2.2): scopes given to
 * compile() are in most-specific-first order (typically `[font.N]` then
 * `[font]` then the engine scope). lookup() resolves a code point to the
 * value of the winning rule, or -1 when no scope has one.
 */
class HiResRangeTable {
public:
	HiResRangeTable();

	/** Compile the scopes, most specific first, into sorted runs. */
	void compile(const Common::Array<HiResRangeScope> &scopes);

	/** The winning value for @p cp, or -1 when no rule applies. */
	int lookup(uint32 cp) const;

	/** True when compile() has never been called, or ran on no scopes. */
	bool empty() const;

	/** FNV-1a over the compiled runs; stable across equal input, so it can
	 *  key a cache of anything built from this table (section 6.2.2's page
	 *  cache is one; higher layers may key their own caches on it too). */
	uint32 hash() const;

private:
	struct Run {
		uint32 lo, hi;
		int narrow;
		int wide;
	};

	int findRun(uint32 cp) const;

	Common::Array<Run> _runs;
	uint16 _page[256];
};

} // End of namespace Graphics

#endif
