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

#ifndef GRAPHICS_HIRES_TEXT_UNICODE_PROPS_H
#define GRAPHICS_HIRES_TEXT_UNICODE_PROPS_H

#include "common/scummsys.h"

namespace Graphics {

/**
 * Character properties the hi-res text layout needs, by code point and
 * never by the game's language setting. The tables in unicode_props.cpp are
 * generated from Python's unicodedata by harness/i18n/c11/gen_unicode_props.py;
 * the Unicode version they were generated from is named at the top of that
 * file. Every function is a pure lookup and works in every build.
 */
namespace Unicode {

/** East Asian Width W (Wide) or F (Fullwidth): the glyph takes two cells. */
bool isWide(uint32 cp);

/**
 * General category Mn (nonspacing mark) or Me (enclosing mark): the glyph
 * is drawn over or under the preceding base and does not advance the pen.
 * Spacing marks (Mc) are not combining here.
 */
bool isCombining(uint32 cp);

/** A Thai character that may begin a syllable-ish unit:
 *  U+0E01..U+0E2E (consonants), U+0E40..U+0E44 (leading vowels),
 *  U+0E4F..U+0E5B (signs and digits). */
bool isThaiBase(uint32 cp);

/** A Thai vowel written before the consonant it follows in speech,
 *  U+0E40..U+0E44: a line never breaks after one. */
bool isThaiLeadingVowel(uint32 cp);

/** A Thai spacing vowel that follows its consonant, U+0E30, U+0E32,
 *  U+0E33, U+0E45: a line never breaks before one. */
bool isThaiFollowingVowel(uint32 cp);

/**
 * Kinsoku: cp must not begin a line. SCI's QfG1 PC-98 table
 * (text16_shiftJIS_punctuation_SCI01, decoded as CP932) plus the closing
 * brackets and iteration marks JIS X 4051 adds. The ASCII closers that are
 * only no-start after a wide unit depend on context, so they are left to
 * the layout stage.
 */
bool kinsokuNoStart(uint32 cp);

/** Kinsoku: cp (an opening bracket) must not end a line. The ASCII openers
 *  that are only no-end before a wide unit are left to the layout stage. */
bool kinsokuNoEnd(uint32 cp);

} // End of namespace Unicode

} // End of namespace Graphics

#endif
