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

#include "graphics/hires_text/id_plan.h"

namespace Graphics {

HiResIdPlan::HiResIdPlan()
	: id(-1), advance(kHiResAdvanceFont), missing(0), size(0), shift(0), cell(kHiResCellGame), align(kHiResAlignGame) {
}

uint HiResIdPlan::chainFor(uint32 cp) const {
	const int v = faceRules.lookup(cp);
	return v < 0 ? 0 : (uint)v;
}

HiResAdvance HiResIdPlan::advanceFor(uint32 cp) const {
	const int v = advanceRules.lookup(cp);
	return v < 0 ? advance : (HiResAdvance)v;
}

namespace {

// @p value with its `same` replaced by @p outer.
HiResFaceChain expand(const HiResFontValue &value, const HiResFaceChain &outer) {
	HiResFaceChain c;
	for (uint i = 0; i <= value.faces.size(); ++i) {
		if (value.same && i == value.sameAt) {
			c.faces.push_back(outer.faces);
			if (outer.original) {
				// The outer chain ends in the game font: nothing after it is reached.
				c.original = true;
				return c;
			}
		}
		if (i < value.faces.size())
			c.faces.push_back(value.faces[i]);
	}
	c.original = value.endsInOriginal;
	return c;
}

bool sameChain(const HiResFaceChain &a, const HiResFaceChain &b) {
	if (a.original != b.original || a.faces.size() != b.faces.size())
		return false;
	for (uint i = 0; i < a.faces.size(); ++i)
		if (a.faces[i].key() != b.faces[i].key())
			return false;
	return true;
}

uint addChain(Common::Array<HiResFaceChain> &chains, const HiResFaceChain &c) {
	for (uint i = 0; i < chains.size(); ++i)
		if (sameChain(chains[i], c))
			return i;
	chains.push_back(c);
	return chains.size() - 1;
}

} // End of anonymous namespace

HiResIdPlan compileIdPlan(const HiResMap &map, int id, Common::Array<Common::String> &warnings) {
	HiResIdPlan plan;
	plan.id = id;
	const HiResFontScope *own = map.fontIdScope(id);
	const HiResFontScope &base = map.font;

	// Scalars: [font.N], then [font].
#define HIRES_PICK(field) \
	if (own && own->field##Set) \
		plan.field = own->field; \
	else if (base.field##Set) \
		plan.field = base.field;
	HIRES_PICK(advance)
	HIRES_PICK(missing)
	HIRES_PICK(size)
	HIRES_PICK(shift)
	HIRES_PICK(cell)
	HIRES_PICK(align)
#undef HIRES_PICK

	// The id chain: [font] face, then [font.N] face with `same` = [font]'s.
	const HiResFaceChain none;
	HiResFaceChain chain = base.faceSet ? expand(base.face, none) : none;
	if (own && own->faceSet)
		chain = expand(own->face, chain);
	if (chain.empty())
		warnings.push_back(Common::String::format("font %d: no face= names a face", id));
	plan.chains.push_back(chain);

	// Range rules, [font.N] first; `same` is the id chain.
	Common::Array<HiResRangeScope> faceScopes, advanceScopes;
	const HiResFontScope *scopes[2] = { own, &base };
	for (int s = 0; s < 2; ++s) {
		if (!scopes[s])
			continue;
		HiResRangeScope f, a;
		for (uint i = 0; i < scopes[s]->rangeSpecs.size(); ++i) {
			f.specs.push_back(scopes[s]->rangeSpecs[i]);
			f.values.push_back((int)addChain(plan.chains, expand(scopes[s]->rangeValues[i], plan.chains[0])));
		}
		for (uint i = 0; i < scopes[s]->advanceSpecs.size(); ++i) {
			a.specs.push_back(scopes[s]->advanceSpecs[i]);
			a.values.push_back((int)scopes[s]->advanceValues[i]);
		}
		faceScopes.push_back(f);
		advanceScopes.push_back(a);
	}
	plan.faceRules.compile(faceScopes);
	plan.advanceRules.compile(advanceScopes);
	return plan;
}

} // End of namespace Graphics
