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

#include "common/algorithm.h"

namespace Graphics {

namespace {

/**
 * Design section 6.5 step 4: the chain named by one font-value rule, with
 * every `same` entry expanded in place into @p idChain, and (when the value
 * has no `same` and does not end in `original`) @p idChain appended after
 * it.
 */
HiResFaceChain buildRuleChain(const HiResFontValue &value, const HiResFaceChain &idChain) {
	HiResFaceChain result;
	bool anySame = false;
	bool sameIsLastEntry = false;

	for (uint i = 0; i < value.entries.size(); ++i) {
		const HiResFaceEntry &e = value.entries[i];
		if (e.kind == kHiResFaceFile) {
			result.faces.push_back(e);
		} else if (e.kind == kHiResFaceSame) {
			anySame = true;
			for (uint j = 0; j < idChain.faces.size(); ++j)
				result.faces.push_back(idChain.faces[j]);
			sameIsLastEntry = (i + 1 == value.entries.size());
		}
		// kHiResFaceOriginal carries no face of its own, and the font-value
		// parser guarantees it is never anything but the last entry.
	}

	if (value.endsInOriginal()) {
		result.endsInOriginal = true;
	} else if (!anySame) {
		for (uint j = 0; j < idChain.faces.size(); ++j)
			result.faces.push_back(idChain.faces[j]);
		result.endsInOriginal = idChain.endsInOriginal;
	} else if (sameIsLastEntry) {
		result.endsInOriginal = idChain.endsInOriginal;
	} else {
		result.endsInOriginal = false;
	}

	return result;
}

/** One scope's face-rule specs, compiled into chains appended to @p ruleChains. */
HiResRangeScope buildFaceScope(const HiResFontScope &scope, const HiResFaceChain &idChain,
								Common::Array<HiResFaceChain> &ruleChains) {
	HiResRangeScope out;
	out.specs = scope.rangeSpecs;
	for (uint i = 0; i < scope.rangeValues.size(); ++i) {
		ruleChains.push_back(buildRuleChain(scope.rangeValues[i], idChain));
		out.values.push_back((int)ruleChains.size() - 1);
	}
	return out;
}

/** One scope's advance./origin. specs, passed through into @p dest, indices
 *  re-based to it. */
template<typename ValueT>
HiResRangeScope buildValueScope(const Common::Array<HiResRangeSpec> &specs, const Common::Array<ValueT> &values,
								 Common::Array<ValueT> &dest) {
	HiResRangeScope out;
	out.specs = specs;
	for (uint i = 0; i < values.size(); ++i) {
		dest.push_back(values[i]);
		out.values.push_back((int)dest.size() - 1);
	}
	return out;
}

// ---- hash() helpers (FNV-1a, byte at a time, matching HiResRangeTable::hash()) ----

inline void mixByte(uint32 &h, byte b) {
	h ^= b;
	h *= 16777619u;
}

void mixU32(uint32 &h, uint32 v) {
	for (uint b = 0; b < 4; ++b)
		mixByte(h, (byte)(v >> (b * 8)));
}

void mixStr(uint32 &h, const Common::String &s) {
	for (uint i = 0; i < s.size(); ++i)
		mixByte(h, (byte)s[i]);
	mixByte(h, 0);
}

void mixFace(uint32 &h, const HiResFaceEntry &e) {
	mixU32(h, (uint32)e.kind);
	mixStr(h, e.path.toString('/'));
}

void mixChain(uint32 &h, const HiResFaceChain &c) {
	mixU32(h, (uint32)c.faces.size());
	for (uint i = 0; i < c.faces.size(); ++i)
		mixFace(h, c.faces[i]);
	mixByte(h, c.endsInOriginal ? 1 : 0);
}

} // End of anonymous namespace

HiResIdPlan::HiResIdPlan()
	: original(false), advance(kHiResAdvanceEngine), forcedAdvance(kHiResAdvanceEngine),
	  origin(kHiResOriginGame), missing(0), size(0), sizeSet(false), pixel(0), shift(0),
	  cell(kHiResCellGame), align(kHiResAlignGame), mirror(kHiResMirrorNone), mirrorSet(false) {
}

const HiResFaceChain *HiResIdPlan::chainFor(uint32 cp) const {
	if (cp >= kHiResTargetBase)
		return nullptr; // design 6.7: targeted glyphs never consult the range table

	const int idx = faceRules.lookup(cp);
	const HiResFaceChain &chain = (idx >= 0) ? ruleChains[(uint)idx] : idChain;
	if (chain.faces.empty())
		return nullptr; // "the game's font draws it": off, exhausted, or exactly `original`
	return &chain;
}

HiResAdvance HiResIdPlan::advanceFor(uint32 cp) const {
	if (cp >= kHiResTargetBase) {
		const HiResGlyphTarget *t = target(cp);
		if (t)
			cp = t->cp;
	}

	if (forcedAdvance != kHiResAdvanceEngine)
		return forcedAdvance;

	const int idx = advanceRules.lookup(cp);
	if (idx >= 0)
		return advanceValues[(uint)idx];

	return advance; // id-wide, or kHiResAdvanceEngine when nothing set it
}

HiResOrigin HiResIdPlan::originFor(uint32 cp) const {
	if (cp >= kHiResTargetBase) {
		const HiResGlyphTarget *t = target(cp);
		if (t)
			cp = t->cp;
	}

	const int idx = originRules.lookup(cp);
	if (idx >= 0)
		return originValues[(uint)idx];

	return origin; // id-wide, or kHiResOriginGame when nothing set it
}

HiResGlyphStep HiResIdPlan::glyphFor(uint32 code, uint32 decoded, uint32 &cp) const {
	HiResGlyphRule rule;
	if (glyphs.tryGetVal(code, rule)) {
		if (rule.kind == kHiResGlyphOriginal)
			return kHiResGlyphStepGame;

		if (rule.kind == kHiResGlyphCodePoint) {
			cp = rule.value;
			return kHiResGlyphStepDraw;
		}
		if (rule.kind == kHiResGlyphOffset) {
			cp = code + rule.value;
			return kHiResGlyphStepDraw;
		}

		// kHiResGlyphTarget / kHiResGlyphTargetOffset: a virtual code point
		// naming this code's entry in `targets` (design 6.7's contract).
		uint32 index = 0;
		targetIndexByCode.tryGetVal(code, index);
		cp = kHiResTargetBase + index;
		return kHiResGlyphStepDraw;
	}

	cp = decoded;
	return kHiResGlyphStepDraw;
}

const HiResGlyphTarget *HiResIdPlan::target(uint32 cp) const {
	if (cp < kHiResTargetBase)
		return nullptr;
	const uint32 index = cp - kHiResTargetBase;
	if (index >= targets.size())
		return nullptr;
	return &targets[index];
}

uint32 HiResIdPlan::hash() const {
	uint32 h = 2166136261u;

	mixByte(h, original ? 1 : 0);
	mixChain(h, idChain);

	mixU32(h, (uint32)ruleChains.size());
	for (uint i = 0; i < ruleChains.size(); ++i)
		mixChain(h, ruleChains[i]);

	mixU32(h, faceRules.hash());
	mixU32(h, advanceRules.hash());
	mixU32(h, originRules.hash());

	mixU32(h, (uint32)advanceValues.size());
	for (uint i = 0; i < advanceValues.size(); ++i)
		mixU32(h, (uint32)advanceValues[i]);
	mixU32(h, (uint32)originValues.size());
	for (uint i = 0; i < originValues.size(); ++i)
		mixU32(h, (uint32)originValues[i]);

	mixU32(h, (uint32)advance);
	mixU32(h, (uint32)forcedAdvance);
	mixU32(h, (uint32)origin);
	mixU32(h, missing);
	mixU32(h, (uint32)size);
	mixByte(h, sizeSet ? 1 : 0);
	mixU32(h, (uint32)pixel);
	mixU32(h, (uint32)shift);
	mixU32(h, (uint32)cell);
	mixU32(h, (uint32)align);
	mixU32(h, (uint32)mirror);
	mixByte(h, mirrorSet ? 1 : 0);

	// The glyph table's HashMap iteration order is not guaranteed stable, so
	// hash it by sorted game code.
	Common::Array<uint32> codes;
	for (HiResGlyphTable::const_iterator it = glyphs.begin(); it != glyphs.end(); ++it)
		codes.push_back(it->_key);
	Common::sort(codes.begin(), codes.end());
	mixU32(h, (uint32)codes.size());
	for (uint i = 0; i < codes.size(); ++i) {
		HiResGlyphRule rule;
		glyphs.tryGetVal(codes[i], rule);
		mixU32(h, codes[i]);
		mixU32(h, (uint32)rule.kind);
		mixU32(h, rule.value);
		mixFace(h, rule.face);
	}

	mixU32(h, (uint32)targets.size());
	for (uint i = 0; i < targets.size(); ++i) {
		mixFace(h, targets[i].face);
		mixU32(h, targets[i].cp);
	}

	return h;
}

HiResIdPlan compileIdPlan(const HiResMap &map, bool mapLoaded, int id, const HiResIniOverrides &ini,
						  const HiResFontScope &engineScope, const Common::Path &mapDir,
						  const Common::Path &gameDir, Common::Array<Common::String> &warnings) {
	HiResIdPlan plan;

	static const HiResFontScope kEmptyScope;
	static const HiResFaceNames kEmptyFaces;

	const HiResFontScope *idScopePtr = mapLoaded ? map.fontIdScope(id) : nullptr;
	const HiResFontScope &idScope = idScopePtr ? *idScopePtr : kEmptyScope;
	const HiResFontScope &fontScope = mapLoaded ? map.font : kEmptyScope;
	const HiResFaceNames &faceNames = mapLoaded ? map.faces : kEmptyFaces;

	// ---- the id chain (design 5.3) and the `original` decision (6.5 step 3) ----

	HiResFontValue resolvedFace;
	bool haveFace = false;
	bool faceFromIni = false;

	if (ini.faceSet) {
		if (ini.face.equalsIgnoreCase("same")) {
			warnings.push_back("hires_text_face=same has no meaning; ignoring it");
		} else {
			HiResFontValue fv;
			Common::Array<Common::String> local;
			const bool any = parseFontValue(ini.face, faceNames, mapDir, gameDir, fv, local);
			for (uint i = 0; i < local.size(); ++i)
				warnings.push_back(local[i]);
			if (any) {
				resolvedFace = fv;
				haveFace = true;
				faceFromIni = true;
			}
		}
	}
	if (!haveFace && idScopePtr && idScope.faceSet) {
		resolvedFace = idScope.face;
		haveFace = true;
	}
	if (!haveFace && fontScope.faceSet) {
		resolvedFace = fontScope.face;
		haveFace = true;
	}

	const bool resolvedIsOriginal = haveFace && resolvedFace.entries.size() == 1 &&
									 resolvedFace.entries[0].kind == kHiResFaceOriginal;
	plan.original = resolvedIsOriginal;
	const bool iniOriginal = resolvedIsOriginal && faceFromIni;

	for (uint i = 0; i < resolvedFace.entries.size(); ++i) {
		if (resolvedFace.entries[i].kind == kHiResFaceFile)
			plan.idChain.faces.push_back(resolvedFace.entries[i]);
		// kHiResFaceSame has nothing left to expand into at the id-chain
		// level itself, and kHiResFaceOriginal carries no face.
	}
	plan.idChain.endsInOriginal = resolvedFace.endsInOriginal();

	// ---- faceRules / ruleChains (design 6.2, 6.5 steps 3-4) ----

	Common::Array<HiResRangeScope> faceScopes;
	if (!iniOriginal) {
		if (plan.original) {
			// Only the id's own [font.N] range rules apply; neither
			// [font]'s nor the engine scope's do.
			if (idScopePtr)
				faceScopes.push_back(buildFaceScope(idScope, plan.idChain, plan.ruleChains));
		} else {
			if (idScopePtr)
				faceScopes.push_back(buildFaceScope(idScope, plan.idChain, plan.ruleChains));
			faceScopes.push_back(buildFaceScope(fontScope, plan.idChain, plan.ruleChains));
			faceScopes.push_back(buildFaceScope(engineScope, plan.idChain, plan.ruleChains));
		}
	}
	plan.faceRules.compile(faceScopes);

	// ---- advanceRules / originRules (design 6.2/6.3): always all three scopes ----

	Common::Array<HiResRangeScope> advScopes, orgScopes;
	advScopes.push_back(idScopePtr ? buildValueScope(idScope.advanceSpecs, idScope.advanceValues, plan.advanceValues)
									: HiResRangeScope());
	advScopes.push_back(buildValueScope(fontScope.advanceSpecs, fontScope.advanceValues, plan.advanceValues));
	advScopes.push_back(buildValueScope(engineScope.advanceSpecs, engineScope.advanceValues, plan.advanceValues));
	plan.advanceRules.compile(advScopes);

	orgScopes.push_back(idScopePtr ? buildValueScope(idScope.originSpecs, idScope.originValues, plan.originValues)
									: HiResRangeScope());
	orgScopes.push_back(buildValueScope(fontScope.originSpecs, fontScope.originValues, plan.originValues));
	orgScopes.push_back(buildValueScope(engineScope.originSpecs, engineScope.originValues, plan.originValues));
	plan.originRules.compile(orgScopes);

	// ---- id-wide scalars (principle 3: ini > [font.N] > [font]; no engine
	// scope fallback beyond the literal "none" sentinel) ----

	plan.forcedAdvance = ini.advanceSet ? ini.advance : kHiResAdvanceEngine;

	if (idScopePtr && idScope.advanceSet)
		plan.advance = idScope.advance;
	else if (fontScope.advanceSet)
		plan.advance = fontScope.advance;
	else
		plan.advance = kHiResAdvanceEngine;

	if (idScopePtr && idScope.originSet)
		plan.origin = idScope.origin;
	else if (fontScope.originSet)
		plan.origin = fontScope.origin;
	else
		plan.origin = kHiResOriginGame;

	if (idScopePtr && idScope.missingSet)
		plan.missing = idScope.missing;
	else if (fontScope.missingSet)
		plan.missing = fontScope.missing;
	else
		plan.missing = 0;

	if (ini.sizeSet) {
		plan.size = ini.size;
		plan.sizeSet = true;
	} else if (idScopePtr && idScope.sizeSet) {
		plan.size = idScope.size;
		plan.sizeSet = true;
	} else if (fontScope.sizeSet) {
		plan.size = fontScope.size;
		plan.sizeSet = true;
	} else {
		plan.size = 0;
		plan.sizeSet = false;
	}

	if (idScopePtr && idScope.pixelSet)
		plan.pixel = idScope.pixel;
	else if (fontScope.pixelSet)
		plan.pixel = fontScope.pixel;
	else
		plan.pixel = 0;

	if (idScopePtr && idScope.shiftSet)
		plan.shift = idScope.shift;
	else if (fontScope.shiftSet)
		plan.shift = fontScope.shift;
	else
		plan.shift = 0;

	if (idScopePtr && idScope.cellSet)
		plan.cell = idScope.cell;
	else if (fontScope.cellSet)
		plan.cell = fontScope.cell;
	else
		plan.cell = kHiResCellGame;

	if (idScopePtr && idScope.alignSet)
		plan.align = idScope.align;
	else if (fontScope.alignSet)
		plan.align = fontScope.align;
	else
		plan.align = kHiResAlignGame;

	if (idScopePtr && idScope.mirrorSet) {
		plan.mirror = idScope.mirror;
		plan.mirrorSet = true;
	} else if (fontScope.mirrorSet) {
		plan.mirror = fontScope.mirror;
		plan.mirrorSet = true;
	} else {
		plan.mirror = kHiResMirrorNone;
		plan.mirrorSet = false;
	}

	// ---- [glyphs]/[glyphs.N] and targeted glyphs (design 6.7) ----

	if (mapLoaded)
		plan.glyphs = map.glyphs;
	Common::HashMap<int, HiResGlyphTable>::const_iterator glyphIdIt =
		mapLoaded ? map.glyphIds.find(id) : map.glyphIds.end();
	if (mapLoaded && glyphIdIt != map.glyphIds.end()) {
		const HiResGlyphTable &over = glyphIdIt->_value;
		for (HiResGlyphTable::const_iterator it = over.begin(); it != over.end(); ++it)
			plan.glyphs[it->_key] = it->_value;
	}

	Common::Array<uint32> codes;
	for (HiResGlyphTable::const_iterator it = plan.glyphs.begin(); it != plan.glyphs.end(); ++it)
		codes.push_back(it->_key);
	Common::sort(codes.begin(), codes.end());

	for (uint i = 0; i < codes.size(); ++i) {
		const uint32 code = codes[i];
		HiResGlyphRule rule;
		plan.glyphs.tryGetVal(code, rule);
		if (rule.kind == kHiResGlyphTarget || rule.kind == kHiResGlyphTargetOffset) {
			HiResGlyphTarget t;
			t.face = rule.face;
			t.cp = (rule.kind == kHiResGlyphTarget) ? rule.value : (code + rule.value);
			plan.targets.push_back(t);
			plan.targetIndexByCode[code] = (uint32)(plan.targets.size() - 1);
		}
	}

	return plan;
}

} // End of namespace Graphics
