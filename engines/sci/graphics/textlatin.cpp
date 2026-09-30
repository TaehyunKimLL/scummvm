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

#include "sci/graphics/textlatin.h"
#include "graphics/hires_text/font_value.h"

namespace Sci {
namespace TextCompose {

uint32 glyphCode(const Graphics::HiResIdPlan &plan, uint32 chr) {
	uint32 cp = chr;
	if (plan.glyphFor(chr, chr, cp) == Graphics::kHiResGlyphStepGame)
		return Graphics::kHiResGameCodeBase + chr;
	// design 6.5 steps 3-4: the game's own font draws it only for an
	// *explicit* `original` - the id itself (design 5.3's `original`, or the
	// ini `hires_text_face=original`), or a range rule whose own chain is
	// exactly `original` (an empty chain, e.g. the engine scope's
	// `range.basic-latin=original`). `HiResIdPlan::chainFor()` cannot tell
	// that apart from "no rule matched and the id names no face at all"
	// (design 5.3's empty id chain) - both give an empty chain - so this
	// decides it directly instead: with no matching rule at all, an empty id
	// chain is NOT declined, it continues to `cp` unchanged, so the caller's
	// own coverage search (the `.uni` bundle, `korean.fnt`, `SJIS.FNT`, a
	// banked font) still runs exactly as it does for a plan-less font.
	if (cp < Graphics::kHiResTargetBase) {
		const int idx = plan.faceRules.lookup(cp);
		const bool ruleIsOriginal = idx >= 0 && plan.ruleChains[(uint)idx].faces.empty();
		const bool idIsOriginal = idx < 0 && plan.original;
		if (ruleIsOriginal || idIsOriginal)
			return Graphics::kHiResGameCodeBase + chr;
	}
	return cp;
}

bool goesToUnicodeFace(const Graphics::HiResIdPlan &plan, uint32 code) {
	if (code >= Graphics::kHiResGameCodeBase)
		return false;
	if (code >= Graphics::kHiResTargetBase)
		return plan.target(code) != nullptr;
	return plan.chainFor(code) != nullptr;
}

} // End of namespace TextCompose
} // End of namespace Sci
