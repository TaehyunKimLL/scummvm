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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_RANGED_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_RANGED_H

#include "common/array.h"
#include "common/types.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/id_plan.h"

namespace Graphics {

/**
 * Which face answers one code point of a compiled HiResIdPlan (design
 * section 6.5 steps 4-6), and what to ask it for.
 */
struct HiResPick {
	/**
	 * kExhausted is an intermediate result only: no face of the chain
	 * being searched has @ref cp, and `missing` has not been tried yet
	 * (used internally while pickGlyph() works its way through the
	 * borrow/missing/game fallbacks of design 6.5 steps 5-6).
	 * pickGlyph() never returns it - the final answer is always kGame or
	 * kFace.
	 */
	enum Kind { kGame = 0, kFace, kExhausted } kind;

	/// 0 = plan.idChain, i + 1 = plan.ruleChains[i], -1 = a `[glyphs]`
	/// target (@ref face indexes plan.targets/targetSources), -2 = the
	/// `borrowed` array passed to pickGlyph() (@ref face indexes it).
	int chain;
	/// Index of the answering face within the array @ref chain names, or
	/// the target index when @ref chain is -1.
	int face;
	/// The code point to ask that face for: the real target cp, the
	/// missing box, or the cp pickGlyph() was asked for.
	uint32 cp;
	/// @ref cp is plan.missing standing in for the code point originally asked for.
	bool missingBox;
	/// The pick came from the `borrowed` array (SCUMM's nearest-charset
	/// borrowing, design 6.5 step 5), not from the plan's own chain.
	bool borrowed;
};

/**
 * Resolve @p cp through @p plan (design section 6.5 steps 3-6): a `[glyphs]`
 * target (@p cp >= kHiResTargetBase) is drawn from exactly its own face,
 * bypassing every range rule, borrowing and `missing` (design 6.7); a real
 * code point is looked up in the chain design 6.2/6.5 step 4 name, in
 * order, then - unless that chain ends in `original` - in @p borrowed (when
 * given), then the chain's own `missing` box, then @p borrowed's `missing`
 * box, and finally kGame.
 *
 * @param chainSources  chainSources[0] parallels plan.idChain.faces,
 *                       chainSources[i + 1] parallels plan.ruleChains[i].faces.
 *                       A nullptr entry means that face was not opened
 *                       (skipped, as if it lacked every code point).
 * @param targetSources  parallels plan.targets; nullptr for a `same` target
 *                        (its face is the id chain, searched via
 *                        chainSources[0] instead).
 * @param borrowed  the nearest charset's id-chain sources (SCUMM's
 *                   borrowing, design 6.5 step 5); nullptr where there is
 *                   nothing to borrow from (SCI always passes nullptr).
 *
 * Every source reachable from @p chainSources, @p targetSources and
 * @p borrowed must share one cell size and one bits-per-pixel: pickGlyph()
 * only asks whether a source has @p cp (UnicodeGlyphSource::cells()), never
 * normalises geometry across them (RangeRoutedGlyphSource's own geometry
 * comes from a single source, see below).
 */
HiResPick pickGlyph(const HiResIdPlan &plan, const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
					const Common::Array<UnicodeGlyphSource *> &targetSources, uint32 cp,
					const Common::Array<UnicodeGlyphSource *> *borrowed = nullptr);

/**
 * A glyph source that routes every code point through a compiled
 * HiResIdPlan (pickGlyph(), with no `borrowed` array - SCUMM's nearest-
 * charset borrowing is not this class's concern, only pickGlyph()'s; a
 * caller that wants it calls pickGlyph() itself). cells()/row()/advance()/
 * metrics() answer for the picked `{source, cp}`; a pick of kGame answers
 * cells() 0, so the caller's own game font draws the code point instead, as
 * with any source lacking a glyph.
 *
 * Geometry (cell size, advances, bits per pixel) comes from the first
 * non-null source of chain 0 (the id chain), or - when that chain is empty
 * or every one of its sources is nullptr - the first non-null source found
 * anywhere among @p chainSources and @p targetSources. Every source passed
 * in must therefore already share one cell size and one bits-per-pixel
 * (design section 9's "open every face of a chain at one pixel size";
 * unlike FallbackGlyphSource, this class does not check or normalise it).
 */
class RangeRoutedGlyphSource : public UnicodeGlyphSource {
public:
	RangeRoutedGlyphSource(const HiResIdPlan &plan, const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
						   const Common::Array<UnicodeGlyphSource *> &targetSources,
						   DisposeAfterUse::Flag dispose = DisposeAfterUse::NO);
	~RangeRoutedGlyphSource() override;

	/** pickGlyph() against this source's own plan and sources, cached for the last @p cp asked. */
	HiResPick pick(uint32 cp);

	byte cellWidth() const override;
	byte cellHeight() const override;
	byte advanceNarrow() const override;
	byte advanceWide() const override;
	int bitsPerPixel() const override;
	int cells(uint32 cp) override;
	const byte *row(uint32 cp, int y) override;
	int advance(uint32 cp) override;
	bool metrics(uint32 cp, GlyphMetrics &m) override;
	/** The sum of glyphCount() over every distinct source given (each counted once). */
	uint32 glyphCount() const override;
	/** Each code point to the source pick() routes it to, as the code point that source draws. */
	void prefetch(Common::Array<uint32> &cps) override;

private:
	/** The source @p p names, or nullptr for kGame/kExhausted or an out-of-range index. */
	UnicodeGlyphSource *sourceFor(const HiResPick &p) const;

	HiResIdPlan _plan;
	Common::Array<Common::Array<UnicodeGlyphSource *> > _chainSources;
	Common::Array<UnicodeGlyphSource *> _targetSources;
	DisposeAfterUse::Flag _dispose;
	UnicodeGlyphSource *_geom; ///< the source geometry (cellWidth() etc.) is taken from

	bool _haveCache;
	uint32 _cachedCp;
	HiResPick _cachedPick;
};

} // End of namespace Graphics

#endif
