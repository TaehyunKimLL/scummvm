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


#ifndef GRAPHICS_HIRES_TEXT_COVERAGE_H
#define GRAPHICS_HIRES_TEXT_COVERAGE_H

#include "common/array.h"
#include "common/str.h"
#include "common/types.h"
#include "common/ustr.h"

namespace Graphics {

class UnicodeGlyphSource;

/**
 * The code points a translation uses (I18N_TEXT_DESIGN.md section 4.4),
 * sorted and unique. Collected once at load from the translation's own text;
 * it replaces language-specific probe sets (the Hangul probes of
 * TtfGlyphSource::create(requireHangul)) for the coverage check and the
 * vertical fit.
 *
 * Only characters that are drawn are kept: C0 and C1 controls, DEL and
 * U+FEFF are skipped, and so is every byte of invalid UTF-8 (which a decoder
 * would show as U+FFFD - a face lacking U+FFFD says nothing about the text).
 */
class CodePointSet {
public:
	/** Decodes UTF-8 and adds each character. */
	void addUtf8(const char *s, uint32 len);
	void addU32(const Common::U32String &s);
	void add(uint32 cp);

	uint32 size() const { return _cps.size(); }
	bool contains(uint32 cp) const;

	/**
	 * Up to n code points spread across the set (every size/n-th, plus the
	 * first of each 128-block present), non-ASCII first. Deterministic.
	 *
	 * One ASCII code point (when present) is taken first, then the first
	 * code point of every non-ASCII block - or, when there are more blocks
	 * than places left, of evenly spaced blocks including the first and the
	 * last - then every
	 * (non-ASCII size / n)-th non-ASCII code point, then the same over
	 * ASCII, then the rest in order, until n are taken; @p out is then the
	 * non-ASCII picks ascending followed by the ASCII ones ascending. A set
	 * of at most n code points is returned whole.
	 */
	void sample(uint n, Common::Array<uint32> &out) const;

	/**
	 * The vertical-fit probes for a TrueType face (TtfGlyphSource::create()'s
	 * extraFitProbes): every mark the set holds (isFitMark(): a combining
	 * mark, or SARA AM) first - a 64-point sample() of a large Thai
	 * translation drops the tone marks, the glyphs reaching highest - then
	 * sample(n)'s code points not taken yet, until n are taken. Without
	 * marks this is sample(n) itself. Deterministic.
	 */
	void fitProbes(uint n, Common::Array<uint32> &out) const;

private:
	static bool drawn(uint32 cp);

	Common::Array<uint32> _cps;	///< ascending
};

/**
 * A code point whose ink a vertical fit must see where it is drawn: a
 * combining mark (Unicode::isCombining()), or SARA AM (Thai U+0E33, Lao
 * U+0EB3), which is NIKHAHIT + SARA AA in one glyph and draws its ring left
 * of its origin like a mark.
 */
bool isFitMark(uint32 cp);

/** What checkCoverage() found. */
struct CoverageReport {
	CoverageReport() : sampled(0), missing(0), spacingMarks(0) {}

	uint32 sampled, missing;
	Common::Array<uint32> firstMissing;	///< up to 5, ascending
	uint32 spacingMarks;				///< combining cps the face gives a non-zero advance
};

/**
 * Asks @p src for each sampled code point through metrics() (a TrueType
 * source rasterises and so caches it: these are glyphs the game will draw).
 * A code point is missing when metrics() fails; a combining one (Unicode::
 * isCombining(), or metrics().combining) counts as a spacing mark when
 * metrics() or the face's own advance() gives it a non-zero advance - the
 * face expects a shaper to place it.
 */
CoverageReport checkCoverage(UnicodeGlyphSource *src, const Common::Array<uint32> &sample);

/**
 * The one-line warnings of design section 4.4, or empty when nothing is
 * missing and no mark spaces:
 *
 *   hires text: <face> lacks 17 of 64 sampled characters of the translation
 *   (U+0E48 U+0E49 U+0E4A U+0E4B U+0E4C ...); they fall back to <fallbackName>
 *
 *   hires text: <face> draws combining marks as spacing glyphs (it needs
 *   shaping); choose a face with zero-width marks, e.g. Sukhumvit Set
 *
 * each on one line; when both apply, missing first, joined by '\n'. " ..."
 * follows the list when more are missing than are listed.
 */
Common::String coverageWarning(const Common::String &faceName, const CoverageReport &r,
							   const Common::String &fallbackName);

} // End of namespace Graphics

#endif
