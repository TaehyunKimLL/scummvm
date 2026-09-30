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

#ifndef SCI_GRAPHICS_TEXTLATIN_H
#define SCI_GRAPHICS_TEXTLATIN_H

#include "common/scummsys.h"
#include "graphics/hires_text/id_plan.h"

namespace Sci {

/**
 * hires_text_log: which face actually drew a glyph, for GfxText16's
 * per-line tally (Box/Draw/DrawStatus). Answered by GfxFontSet::classify()
 * and GfxFontUnicodeAdapter::classify(), which mirror the same choice their
 * own draw() already makes - this adds no new decision, just a name for the
 * existing one, so it costs nothing when hires_text_log is off (GfxText16
 * only calls classify() when the flag resolved true).
 */
enum TextFaceKind {
	kTextFaceResource,	///< the game's own resource face
	kTextFaceLegacy,	///< korean.fnt / SJIS.FNT, addressed by byte pair
	kTextFaceUnicode,	///< the id's own chain, by its plain default routing
	kTextFaceRule		///< a range rule's chain drew it, not the plain default
};

/** The arithmetic of hi-res text, free of engine state so it is tested alone. */
namespace TextCompose {

/**
 * The `[glyphs]` step (design section 6.5 step 2) for the code
 * GfxText16::glyphChar() returns. @p chr is the game code @p plan was
 * compiled for: a decoded code point (GfxText16::readChar() always hands on
 * one - see its own comment - so this is true for a legacy code page and a
 * UTF-8 translation alike) or one of the game's own single-byte values.
 *
 * An `original` rule, or the id being off entirely (design 6.5 step 3, folded
 * into @p plan by compileIdPlan() so this function need not know about it),
 * answers `Graphics::kHiResGameCodeBase + chr`: the game's own font draws
 * @p chr, unchanged - GfxFontSet/GfxFontUnicodeAdapter decode that back to
 * @p chr for the resource face. Otherwise the result is a real code point
 * (design 6.5 step 3 on: a plain decode, or a `[glyphs]` remap/offset) or a
 * virtual targeted-glyph one (`Graphics::HiResIdPlan::target()`).
 */
uint32 glyphCode(const Graphics::HiResIdPlan &plan, uint32 chr);

/**
 * Whether a (post-glyphCode) @p code is drawn by the Unicode face set
 * (design 6.5 steps 4-6) rather than the game's own resource font: false for
 * a declined code (`code >= Graphics::kHiResGameCodeBase`); for a
 * targeted-glyph code (`Graphics::kHiResTargetBase` and up), whether @p plan
 * actually produced it; otherwise whether @p plan's chain for it is
 * non-null (design 6.2/6.5 step 4) - for the printable ASCII range this is
 * null unless a map rule says otherwise, per the SCI engine scope's
 * `range.basic-latin=original` (sciEngineScope(), hirestextsettings.h).
 */
bool goesToUnicodeFace(const Graphics::HiResIdPlan &plan, uint32 code);

} // End of namespace TextCompose
} // End of namespace Sci

#endif
