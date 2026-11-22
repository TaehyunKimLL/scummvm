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

#ifndef GRAPHICS_HIRES_TEXT_ID_PLAN_H
#define GRAPHICS_HIRES_TEXT_ID_PLAN_H

#include "common/array.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/unicode_ranges.h"

namespace Graphics {

/** A face chain with every `same` expanded: faces in order, then (original) the game's font. */
struct HiResFaceChain {
	HiResFaceChain() : original(false) {}
	Common::Array<HiResFaceRef> faces;
	bool original;
	bool empty() const { return faces.empty() && !original; }
};

/**
 * One engine font id's settings, every precedence resolved: [font.N] over
 * [font] over the defaults. chains[0] is the id's own chain; a range rule
 * routes a code point to another chain.
 */
struct HiResIdPlan {
	HiResIdPlan();

	int id;
	Common::Array<HiResFaceChain> chains;
	HiResRangeTable faceRules;        ///< value: index into chains
	HiResRangeTable advanceRules;     ///< value: a HiResAdvance
	HiResAdvance advance;
	uint32 missing;
	int size;                         ///< 0: the faces' own size
	int shift;
	HiResCellMode cell;
	HiResAlign align;

	/** The chain that draws @p cp: 0 unless a range rule names another. */
	uint chainFor(uint32 cp) const;
	HiResAdvance advanceFor(uint32 cp) const;
};

/** Compiles id @p id of @p map (a map with no [font.N] for it uses [font]). */
HiResIdPlan compileIdPlan(const HiResMap &map, int id, Common::Array<Common::String> &warnings);

} // End of namespace Graphics

#endif
