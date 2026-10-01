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

#include "graphics/hires_text/glyph_source_ranged.h"

#include "graphics/hires_text/font_value.h"

namespace Graphics {

namespace {

/** The first index in @p sources whose cells(cp) > 0, skipping nullptrs ("not opened"); -1 for none. */
int findInSources(const Common::Array<UnicodeGlyphSource *> &sources, uint32 cp) {
	for (uint i = 0; i < sources.size(); ++i) {
		if (sources[i] && sources[i]->cells(cp) > 0)
			return (int)i;
	}
	return -1;
}

/** Search @p sources (named by @p chainIndex, design 6.5 step 5) for @p cp: kFace when found, else kExhausted. */
HiResPick searchChain(const Common::Array<UnicodeGlyphSource *> &sources, int chainIndex, uint32 cp) {
	HiResPick p;
	p.chain = chainIndex;
	p.cp = cp;
	p.missingBox = false;
	p.borrowed = false;

	const int idx = findInSources(sources, cp);
	if (idx >= 0) {
		p.kind = HiResPick::kFace;
		p.face = idx;
	} else {
		p.kind = HiResPick::kExhausted;
		p.face = -1;
	}
	return p;
}

/** Index of @p chain within @p chainSources (0 = idChain, i + 1 = ruleChains[i]); -1 when it names none of them. */
int chainIndexOf(const HiResIdPlan &plan, const HiResFaceChain *chain) {
	if (chain == &plan.idChain)
		return 0;
	for (uint i = 0; i < plan.ruleChains.size(); ++i) {
		if (chain == &plan.ruleChains[i])
			return (int)i + 1;
	}
	return -1;
}

/** The virtual-target branch of design 6.5 step 2 / 6.7: bypasses range rules, borrowing and `missing` entirely. */
HiResPick pickTarget(const HiResIdPlan &plan, const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
					 const Common::Array<UnicodeGlyphSource *> &targetSources, uint32 cp) {
	HiResPick miss;
	miss.kind = HiResPick::kGame;
	miss.chain = -1;
	miss.face = -1;
	miss.cp = cp;
	miss.missingBox = false;
	miss.borrowed = false;

	const HiResGlyphTarget *t = plan.target(cp);
	if (!t)
		return miss;

	if (t->face.kind == kHiResFaceFile) {
		const uint32 index = cp - kHiResTargetBase;
		UnicodeGlyphSource *src = (index < targetSources.size()) ? targetSources[index] : nullptr;
		if (src && src->cells(t->cp) > 0) {
			HiResPick p;
			p.kind = HiResPick::kFace;
			p.chain = -1;
			p.face = (int)index;
			p.cp = t->cp;
			p.missingBox = false;
			p.borrowed = false;
			return p;
		}
		return miss;
	}

	// kHiResFaceSame: the face is the id chain itself (chainSources[0]).
	if (!chainSources.empty()) {
		const int idx = findInSources(chainSources[0], t->cp);
		if (idx >= 0) {
			HiResPick p;
			p.kind = HiResPick::kFace;
			p.chain = 0;
			p.face = idx;
			p.cp = t->cp;
			p.missingBox = false;
			p.borrowed = false;
			return p;
		}
	}
	return miss;
}

UnicodeGlyphSource *findGeometrySource(const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
									   const Common::Array<UnicodeGlyphSource *> &targetSources) {
	if (!chainSources.empty()) {
		for (uint i = 0; i < chainSources[0].size(); ++i)
			if (chainSources[0][i])
				return chainSources[0][i];
	}
	for (uint c = 0; c < chainSources.size(); ++c) {
		for (uint i = 0; i < chainSources[c].size(); ++i)
			if (chainSources[c][i])
				return chainSources[c][i];
	}
	for (uint i = 0; i < targetSources.size(); ++i)
		if (targetSources[i])
			return targetSources[i];
	return nullptr;
}

} // End of anonymous namespace

HiResPick pickGlyph(const HiResIdPlan &plan, const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
					const Common::Array<UnicodeGlyphSource *> &targetSources, uint32 cp,
					const Common::Array<UnicodeGlyphSource *> *borrowed) {
	if (cp >= kHiResTargetBase)
		return pickTarget(plan, chainSources, targetSources, cp);

	HiResPick game;
	game.kind = HiResPick::kGame;
	game.chain = 0;
	game.face = -1;
	game.cp = cp;
	game.missingBox = false;
	game.borrowed = false;

	const HiResFaceChain *chain = plan.chainFor(cp);
	if (!chain)
		return game; // id off, chain empty, or exactly `original` (design 6.5 step 3)

	const int chainIndex = chainIndexOf(plan, chain);
	if (chainIndex < 0 || (uint)chainIndex >= chainSources.size())
		return game; // defensive: caller supplied no sources for this chain

	const Common::Array<UnicodeGlyphSource *> &sources = chainSources[(uint)chainIndex];

	// Design 6.5 step 5: the first face of the chain with the glyph.
	HiResPick p = searchChain(sources, chainIndex, cp);
	if (p.kind == HiResPick::kFace)
		return p;

	// p.kind == kExhausted: no face of the chain has cp. `original` stops
	// the search here - never borrow, never box (design 6.5 step 4's
	// "original anywhere ends the chain there").
	if (chain->endsInOriginal) {
		p.kind = HiResPick::kGame;
		return p;
	}

	// Step 2: the nearest charset's sources (SCUMM's borrowing).
	if (borrowed) {
		HiResPick b = searchChain(*borrowed, -2, cp);
		if (b.kind == HiResPick::kFace) {
			b.borrowed = true;
			return b;
		}
	}

	// Design 6.4 order, step 3: the missing box, first in the chain, then in `borrowed`.
	if (plan.missing) {
		HiResPick m = searchChain(sources, chainIndex, plan.missing);
		if (m.kind == HiResPick::kFace) {
			m.missingBox = true;
			return m;
		}
		if (borrowed) {
			HiResPick mb = searchChain(*borrowed, -2, plan.missing);
			if (mb.kind == HiResPick::kFace) {
				mb.missingBox = true;
				mb.borrowed = true;
				return mb;
			}
		}
	}

	p.kind = HiResPick::kGame;
	return p;
}

RangeRoutedGlyphSource::RangeRoutedGlyphSource(const HiResIdPlan &plan,
											   const Common::Array<Common::Array<UnicodeGlyphSource *> > &chainSources,
											   const Common::Array<UnicodeGlyphSource *> &targetSources,
											   DisposeAfterUse::Flag dispose)
	: _plan(plan), _chainSources(chainSources), _targetSources(targetSources), _dispose(dispose),
	  _geom(findGeometrySource(_chainSources, _targetSources)), _haveCache(false), _cachedCp(0) {
}

RangeRoutedGlyphSource::~RangeRoutedGlyphSource() {
	if (_dispose != DisposeAfterUse::YES)
		return;

	Common::Array<UnicodeGlyphSource *> seen;
	for (uint c = 0; c < _chainSources.size(); ++c) {
		for (uint i = 0; i < _chainSources[c].size(); ++i) {
			UnicodeGlyphSource *src = _chainSources[c][i];
			if (!src)
				continue;
			bool already = false;
			for (uint j = 0; j < seen.size() && !already; ++j)
				already = (seen[j] == src);
			if (!already)
				seen.push_back(src);
		}
	}
	for (uint i = 0; i < _targetSources.size(); ++i) {
		UnicodeGlyphSource *src = _targetSources[i];
		if (!src)
			continue;
		bool already = false;
		for (uint j = 0; j < seen.size() && !already; ++j)
			already = (seen[j] == src);
		if (!already)
			seen.push_back(src);
	}
	for (uint i = 0; i < seen.size(); ++i)
		delete seen[i];
}

HiResPick RangeRoutedGlyphSource::pick(uint32 cp) {
	if (!_haveCache || _cachedCp != cp) {
		_cachedPick = pickGlyph(_plan, _chainSources, _targetSources, cp);
		_cachedCp = cp;
		_haveCache = true;
	}
	return _cachedPick;
}

UnicodeGlyphSource *RangeRoutedGlyphSource::sourceFor(const HiResPick &p) const {
	if (p.kind != HiResPick::kFace)
		return nullptr;
	if (p.chain == -1)
		return (p.face >= 0 && (uint)p.face < _targetSources.size()) ? _targetSources[(uint)p.face] : nullptr;
	if (p.chain >= 0 && (uint)p.chain < _chainSources.size()) {
		const Common::Array<UnicodeGlyphSource *> &s = _chainSources[(uint)p.chain];
		return (p.face >= 0 && (uint)p.face < s.size()) ? s[(uint)p.face] : nullptr;
	}
	return nullptr; // -2 (borrowed): pickGlyph() is always called here with no `borrowed` array.
}

byte RangeRoutedGlyphSource::cellWidth() const {
	return _geom ? _geom->cellWidth() : 0;
}

byte RangeRoutedGlyphSource::cellHeight() const {
	return _geom ? _geom->cellHeight() : 0;
}

byte RangeRoutedGlyphSource::advanceNarrow() const {
	return _geom ? _geom->advanceNarrow() : 0;
}

byte RangeRoutedGlyphSource::advanceWide() const {
	return _geom ? _geom->advanceWide() : 0;
}

int RangeRoutedGlyphSource::bitsPerPixel() const {
	return _geom ? _geom->bitsPerPixel() : 8;
}

void RangeRoutedGlyphSource::prefetch(Common::Array<uint32> &cps) {
	// Each code point to the source that would draw it, as the code point
	// that source is asked for (pick()), a source's together.
	Common::Array<UnicodeGlyphSource *> sources;
	Common::Array<Common::Array<uint32> > wanted;
	Common::Array<uint32> rest;
	for (uint i = 0; i < cps.size(); ++i) {
		const HiResPick p = pick(cps[i]);
		UnicodeGlyphSource *src = sourceFor(p);
		if (!src) {
			rest.push_back(cps[i]);
			continue;
		}
		uint k = 0;
		while (k < sources.size() && sources[k] != src)
			++k;
		if (k == sources.size()) {
			sources.push_back(src);
			wanted.push_back(Common::Array<uint32>());
		}
		wanted[k].push_back(p.cp);
	}
	for (uint k = 0; k < sources.size(); ++k)
		sources[k]->prefetch(wanted[k]);
	cps.swap(rest);
}

int RangeRoutedGlyphSource::cells(uint32 cp) {
	UnicodeGlyphSource *src = sourceFor(pick(cp));
	return src ? src->cells(_cachedPick.cp) : 0;
}

const byte *RangeRoutedGlyphSource::row(uint32 cp, int y) {
	UnicodeGlyphSource *src = sourceFor(pick(cp));
	return src ? src->row(_cachedPick.cp, y) : nullptr;
}

int RangeRoutedGlyphSource::advance(uint32 cp) {
	UnicodeGlyphSource *src = sourceFor(pick(cp));
	return src ? src->advance(_cachedPick.cp) : 0;
}

bool RangeRoutedGlyphSource::metrics(uint32 cp, GlyphMetrics &m) {
	UnicodeGlyphSource *src = sourceFor(pick(cp));
	return src ? src->metrics(_cachedPick.cp, m) : false;
}

uint32 RangeRoutedGlyphSource::glyphCount() const {
	Common::Array<UnicodeGlyphSource *> seen;
	uint32 total = 0;
	for (uint c = 0; c < _chainSources.size(); ++c) {
		for (uint i = 0; i < _chainSources[c].size(); ++i) {
			UnicodeGlyphSource *src = _chainSources[c][i];
			if (!src)
				continue;
			bool already = false;
			for (uint j = 0; j < seen.size() && !already; ++j)
				already = (seen[j] == src);
			if (already)
				continue;
			seen.push_back(src);
			total += src->glyphCount();
		}
	}
	for (uint i = 0; i < _targetSources.size(); ++i) {
		UnicodeGlyphSource *src = _targetSources[i];
		if (!src)
			continue;
		bool already = false;
		for (uint j = 0; j < seen.size() && !already; ++j)
			already = (seen[j] == src);
		if (already)
			continue;
		seen.push_back(src);
		total += src->glyphCount();
	}
	return total;
}

} // End of namespace Graphics
