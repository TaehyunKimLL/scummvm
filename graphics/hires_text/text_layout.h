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

#ifndef GRAPHICS_HIRES_TEXT_TEXT_LAYOUT_H
#define GRAPHICS_HIRES_TEXT_TEXT_LAYOUT_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str-enc.h"
#include "common/ustr.h"

namespace Graphics {

/**
 * The shared layout stage of hi-res text.
 *
 * Engines keep their text as bytes (UTF-8 or a legacy code page, with their
 * own escape codes in between). For a layout call the bytes are decoded
 * once into a TextRun: one uint32 unit per character, with the byte offset
 * of each, so that breaking and measuring look at whole characters
 * (cp[i-1], cp[i]) and never re-decode. Line breaking is one rule set by
 * character class, never by the game's language; the lines come back as
 * unit ranges and as byte offsets into the engine's string.
 */

/** Flags of one unit of a TextRun. */
enum TextUnitFlags {
	kUnitControl   = 1 << 0,  ///< an engine escape: opaque, zero width, never split
	kUnitNewline   = 1 << 1,  ///< forces a line break after this unit
	kUnitSpace     = 1 << 2,  ///< a break opportunity that is dropped at a line end
	kUnitCombining = 1 << 3,  ///< Unicode Mn/Me: zero advance, attaches to the previous base
	kUnitWide      = 1 << 4   ///< East Asian Wide/Fullwidth
};

/** cp value of a control unit; the engine's escape code is in the bytes. */
static const uint32 kControlUnit = 0xFFFFFFFFu;

/** Turns engine bytes into units. Engines subclass it to recognise escapes. */
class TextDecoder {
public:
	virtual ~TextDecoder() {}
	/**
	 * Decode the unit at p (p < end). Returns the bytes it spans, >= 1, never
	 * past end. Sets cp (kControlUnit for an escape) and flags (kUnitControl,
	 * kUnitNewline); the run adds kUnitSpace/kUnitCombining/kUnitWide itself.
	 */
	virtual int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const = 0;
};

/**
 * UTF-8 (RFC 3629); an invalid, overlong, surrogate or truncated sequence
 * is one unit of U+FFFD, 1 byte. '\n' is a newline unit.
 */
class Utf8TextDecoder : public TextDecoder {
public:
	int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const override;
};

/**
 * A legacy code page: the character length follows SCUMM's charLength()
 * rule (engines/scumm/hires_text.cpp), and the character is converted with
 * Common::U32String(bytes, page), which needs encoding.dat for the CJK
 * pages. A character the conversion cannot map is U+FFFD. '\n' is a
 * newline unit.
 */
class CodePageTextDecoder : public TextDecoder {
public:
	explicit CodePageTextDecoder(Common::CodePage page) : _page(page) {}
	int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const override;

	/** Bytes of the character starting at p, by the page's lead byte ranges. */
	static int charLength(Common::CodePage page, const byte *p, const byte *end);

private:
	Common::CodePage _page;
};

class TextRun {
public:
	/** Decode [text, text+len) with dec. Clears the run first; keeps capacity. */
	void decode(const byte *text, uint32 len, const TextDecoder &dec);
	/** Build from code points already decoded (Grim); offsets are unit indices. */
	void assign(const Common::U32String &text);
	uint32 size() const { return _cp.size(); }
	uint32 cp(uint32 i) const { return _cp[i]; }
	byte flags(uint32 i) const { return _flags[i]; }
	/** Byte offset of unit i; byteOffset(size()) is the total length. */
	uint32 byteOffset(uint32 i) const { return _offset[i]; }

private:
	void clear();
	void push(uint32 cp, byte flags, uint32 offset);

	Common::Array<uint32> _cp, _offset;
	Common::Array<byte> _flags;
};

/** What the engine measures with, in its layout units (game px). */
class LayoutMetrics {
public:
	virtual ~LayoutMetrics() {}
	/** Advance of cp; 0 for combining marks and control units. */
	virtual int advance(uint32 cp) = 0;
	/**
	 * Width of units [from, to). Default: sum of advance() over the units
	 * that are neither control units nor combining marks (those never
	 * advance). AGS overrides (outline, kerning); SCI and SCUMM use the
	 * default.
	 */
	virtual int width(const TextRun &run, uint32 from, uint32 to);
	/**
	 * The width of [from, i+1) given widthSoFar, the width of [from, i).
	 * TextLayout::fitLine() keeps a running width through this, so a line
	 * costs O(L) advance() calls. Default: widthSoFar + advance(cp(i)),
	 * widthSoFar unchanged for control units and combining marks; it equals
	 * width(run, from, i + 1) whenever width() is the default. A metrics
	 * whose width is not additive (AGS: outline, kerning) overrides this
	 * with `return width(run, from, i + 1);`.
	 *
	 * Not const, like advance() and width(): engines measure through fonts
	 * whose lookups cache.
	 */
	virtual int extend(const TextRun &run, uint32 from, uint32 i, int widthSoFar);
};

enum HangulBreak { kHangulBreakWord = 0, kHangulBreakAny = 1 };

struct BreakRules {
	HangulBreak hangul;       ///< word: Hangul breaks at spaces (like Latin); any: like CJK ideographs
	bool kinsoku;             ///< apply the shared kinsoku table
	bool thaiFallback;        ///< break before Thai bases
	BreakRules() : hangul(kHangulBreakWord), kinsoku(true), thaiFallback(true) {}
};

struct LineSpan {
	uint32 first, end;        ///< units drawn on this line: [first, end), trailing spaces excluded
	uint32 next;              ///< first unit of the next line (after dropped spaces / the newline)
	uint32 byteStart, byteEnd, byteNext;  ///< the same three, as byte offsets
	int width;                ///< LayoutMetrics::width(run, first, end)
	bool forced;              ///< ended by a kUnitNewline unit
	bool emergency;           ///< no break opportunity fitted: split at a cluster boundary
	LineSpan() : first(0), end(0), next(0), byteStart(0), byteEnd(0), byteNext(0),
		width(0), forced(false), emergency(false) {}
};

namespace TextLayout {

/**
 * Whether a line may end between unit i-1 and unit i (0 < i < size).
 *
 * Control units that are not newlines are glued to the text after them:
 * no break right after one, and a break before a run of them is judged
 * between the unit before the run and the first unit after it. So an
 * escape (a colour change, say) in front of a word moves to the next line
 * with that word, and a line never ends inside or right after an escape.
 */
bool canBreakBefore(const TextRun &run, uint32 i, const BreakRules &rules);

/** Not between a base and its combining marks, never inside or right after
 *  a control unit sequence. 0 and size() are boundaries. */
bool isClusterBoundary(const TextRun &run, uint32 i);

/**
 * The longest line starting at from that fits maxWidth (at least one
 * cluster). A newline unit ends the line (forced). Otherwise the line ends
 * at the last break opportunity that fits; if there is none, at the last
 * cluster boundary that fits (emergency). Spaces at the end of a line are
 * excluded from [first, end) and skipped into next.
 */
LineSpan fitLine(const TextRun &run, uint32 from, int maxWidth, LayoutMetrics &m, const BreakRules &rules);

/** All lines, in order; out is cleared first. An empty run has no lines. */
void breakLines(const TextRun &run, int maxWidth, LayoutMetrics &m, const BreakRules &rules,
				Common::Array<LineSpan> &out);

} // End of namespace TextLayout

} // End of namespace Graphics

#endif
