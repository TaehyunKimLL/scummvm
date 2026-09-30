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
	// design 6.5 steps 3-4: a real code point (not yet a target) with no
	// chain naming a face for it at all - the id is off, its own resolved
	// face is `original`, or nothing names a face - falls to the game's own
	// font too. A virtual targeted-glyph code (design 6.7) never consults
	// the range table, so it always continues.
	if (cp < Graphics::kHiResTargetBase && !plan.chainFor(cp))
		return Graphics::kHiResGameCodeBase + chr;
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
