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

#include "sci/graphics/hirestextsettings.h"

#include "common/textconsole.h"
#include "graphics/hires_text/unicode_ranges.h"

namespace Sci {

FontSettings::FontSettings()
	: original(false), size(kDefaultCell), cell(kDefaultCell), baseline(0),
	  align(Graphics::kHiResAlignGame), pixel(0) {
}

Graphics::HiResFontScope sciEngineScope() {
	Graphics::HiResFontScope scope;

	Graphics::HiResFontValue original;
	Graphics::HiResFaceEntry originalEntry;
	originalEntry.kind = Graphics::kHiResFaceOriginal;
	originalEntry.written = "original";
	original.entries.push_back(originalEntry);

	// design section 8's own default range, plus the built-in C0/control and
	// DEL spans a map cannot otherwise reach with one rule (spec 6.1: they
	// are in no named block): the game's own resource font keeps drawing
	// them, as it always has - `basic-latin` alone leaves U+0000-001F and
	// U+007F to the id's plain chain (design 6.5 step 4), which routes them
	// to whatever face the id names, and a `missing=` box would then answer
	// for them where the game's own font used to. A map may still override
	// any of the three spans explicitly.
	static const char *const kSpans[] = { "basic-latin", "U+0000-001F", "U+007F" };
	for (uint i = 0; i < ARRAYSIZE(kSpans); i++) {
		Graphics::HiResRangeSpec spec;
		Common::String error;
		Graphics::parseRangeSpec(kSpans[i], spec, error);
		scope.rangeSpecs.push_back(spec);
		scope.rangeValues.push_back(original);
		scope.advanceSpecs.push_back(spec);
		scope.advanceValues.push_back(Graphics::kHiResAdvanceGame);
	}

	return scope;
}

FontSettings resolveFontSettings(const Graphics::HiResMap &map, bool mapLoaded, int fontId,
								 const Graphics::HiResIniOverrides &ini, const Common::Path &mapDir,
								 const Common::Path &gameDir) {
	FontSettings s;

	// The map's own key warnings were already raised (and kept in
	// HiResMap::warnings) when it was loaded; what compileIdPlan() can still
	// raise here is only about the ini's own hires_text_face, which GfxCache
	// validates once per load itself (once, not once per font id) rather
	// than through this per-id call.
	Common::Array<Common::String> warnings;
	s.plan = Graphics::compileIdPlan(map, mapLoaded, fontId, ini, sciEngineScope(), mapDir, gameDir, warnings);

	s.original = s.plan.original && s.plan.ruleChains.empty();

	for (uint i = 0; i < s.plan.idChain.faces.size(); i++)
		s.faceChain.push_back(s.plan.idChain.faces[i].path.toString(Common::Path::kNativeSeparator));
	if (!s.faceChain.empty())
		s.facePath = s.faceChain[0];

	s.size = s.plan.sizeSet ? s.plan.size : FontSettings::kDefaultCell;
	s.cell = s.plan.cell == Graphics::kHiResCellGlyph ? s.size : FontSettings::kDefaultCell;
	s.baseline = s.plan.shift;
	s.align = s.plan.align;
	s.pixel = s.plan.pixel;

	return s;
}

Common::String unicodeBundleKey(const Common::String &mainPath, int size, uint32 planHash) {
	return Common::String::format("%s|%d|%08x", mainPath.c_str(), size, planHash);
}

bool planNamesAnyFace(const Graphics::HiResIdPlan &plan) {
	if (!plan.idChain.faces.empty())
		return true;
	for (uint i = 0; i < plan.ruleChains.size(); i++) {
		if (!plan.ruleChains[i].faces.empty())
			return true;
	}
	for (uint i = 0; i < plan.targets.size(); i++) {
		if (plan.targets[i].face.kind == Graphics::kHiResFaceFile)
			return true;
	}
	return false;
}

void declineFailedTargets(Graphics::HiResIdPlan &plan, const Common::Array<uint32> &failedTargetCodes) {
	for (uint i = 0; i < failedTargetCodes.size(); i++) {
		Graphics::HiResGlyphRule rule;
		rule.kind = Graphics::kHiResGlyphOriginal;
		rule.value = 0;
		rule.face = Graphics::HiResFaceEntry();
		plan.glyphs[failedTargetCodes[i]] = rule;
	}
}

namespace {

/// The FontIdFace naming @p path, or nullptr.
const FontIdFace *findFace(const Common::Array<FontIdFace> &faces, const Common::String &path) {
	for (uint i = 0; i < faces.size(); i++) {
		if (faces[i].path == path)
			return &faces[i];
	}
	return nullptr;
}

bool isExcluded(const Common::Array<Common::String> &excludedPaths, const Common::String &path) {
	for (uint i = 0; i < excludedPaths.size(); i++) {
		if (excludedPaths[i] == path)
			return true;
	}
	return false;
}

} // End of anonymous namespace

void checkPlanLoadWarnings(int fontId, const Graphics::HiResIdPlan &plan, const Common::Array<FontIdFace> &faces,
						   Common::Array<Common::String> &excludedPaths, Common::Array<uint32> &failedTargetCodes,
						   Graphics::HiResMap &map, Common::HashMap<Common::String, bool> &warnedOnceThisLoad) {
	if (!planNamesAnyFace(plan))
		return; // nothing opens: an id that is purely `original`

	auto warnOnce = [&](const Common::String &w) {
		map.warnings.push_back(w);
		if (warnedOnceThisLoad.contains(w))
			return;
		warnedOnceThisLoad[w] = true;
		warning("%s", w.c_str());
	};

	// design 5.4: every SVF the plan names (not only the id chain's own)
	// must share the first SVF's cell height.
	Common::String firstPath;
	int firstHeight = -1;
	for (uint i = 0; i < faces.size(); i++) {
		if (!faces[i].isSvf || !faces[i].source)
			continue;
		const int h = faces[i].source->cellHeight();
		if (firstHeight < 0) {
			firstHeight = h;
			firstPath = faces[i].path;
			continue;
		}
		if (h != firstHeight) {
			warnOnce(Common::String::format(
				"HIRESTXT.MAP: %s: cell height %d differs from %s's %d on %d; not used",
				faces[i].path.c_str(), h, firstPath.c_str(), firstHeight, fontId));
			excludedPaths.push_back(faces[i].path);
		}
	}

	// design 6.7 / 10.4: a [glyphs] target lacking its own code point in the
	// face it names.
	for (uint i = 0; i < plan.targets.size(); i++) {
		const Graphics::HiResGlyphTarget &t = plan.targets[i];
		Graphics::UnicodeGlyphSource *src = nullptr;
		Common::String faceText;
		if (t.face.kind == Graphics::kHiResFaceFile) {
			const Common::String path = t.face.path.toString('/');
			faceText = t.face.written;
			if (!isExcluded(excludedPaths, path)) {
				const FontIdFace *f = findFace(faces, path);
				if (f)
					src = f->source;
			}
		} else if (t.face.kind == Graphics::kHiResFaceSame) {
			faceText = "same";
			for (uint j = 0; j < plan.idChain.faces.size() && !src; j++) {
				const Common::String path = plan.idChain.faces[j].path.toString('/');
				if (isExcluded(excludedPaths, path))
					continue;
				const FontIdFace *f = findFace(faces, path);
				if (f && f->source && f->source->cells(t.cp) > 0)
					src = f->source;
			}
		} else {
			continue;
		}
		if (src && src->cells(t.cp) > 0)
			continue;
		uint32 code = 0;
		for (Common::HashMap<uint32, uint32>::const_iterator it = plan.targetIndexByCode.begin();
			 it != plan.targetIndexByCode.end(); ++it) {
			if (it->_value == i) {
				code = it->_key;
				break;
			}
		}
		failedTargetCodes.push_back(code);
		warnOnce(Common::String::format(
			"HIRESTXT.MAP: [glyphs] 0x%X -> %s:U+%04X: the face has no such glyph; the game's font draws it",
			code, faceText.c_str(), t.cp));
	}

	// design 6.4 / 10.4: missing= naming a code point no face of a chain
	// has - checked once per chain (the id chain, and every rule chain of
	// its own), not once for the id as a whole: a rule chain whose own
	// faces lack the box is warned about even when the id chain (or another
	// rule chain) happens to have it. A chain with no faces of its own, or
	// that ends in `original` (design 6.4: the box never follows `original`),
	// has nothing to check.
	if (plan.missing) {
		auto checkOneChain = [&](const Graphics::HiResFaceChain &c) {
			if (c.faces.empty() || c.endsInOriginal)
				return;
			bool found = false;
			for (uint i = 0; i < c.faces.size() && !found; i++) {
				const Common::String path = c.faces[i].path.toString('/');
				if (isExcluded(excludedPaths, path))
					continue;
				const FontIdFace *f = findFace(faces, path);
				if (f && f->source && f->source->cells(plan.missing) > 0)
					found = true;
			}
			if (!found) {
				warnOnce(Common::String::format(
					"HIRESTXT.MAP: missing=U+%04X has no effect: %s has no glyph for it",
					plan.missing, c.faces[0].written.c_str()));
			}
		};
		checkOneChain(plan.idChain);
		for (uint c = 0; c < plan.ruleChains.size(); c++)
			checkOneChain(plan.ruleChains[c]);
	}
}

} // End of namespace Sci
