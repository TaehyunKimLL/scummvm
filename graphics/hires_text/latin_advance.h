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

#ifndef GRAPHICS_HIRES_TEXT_LATIN_ADVANCE_H
#define GRAPHICS_HIRES_TEXT_LATIN_ADVANCE_H

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/unicode_props.h"

namespace Graphics {

/**
 * Design sections 6.3/6.5 step 7: the advance, in game (lowres) pixels, of
 * one drawn code point under an id's resolved `advance=` value.
 *
 *   advance=game - @p gameWidth, the width of the character in the font
 *                  id's own resource face.
 *   advance=font - the face's own advance, @p faceAdvanceHires, scaled
 *                  down by @p scale: max(1, round(a / scale)), rounding half
 *                  up (falls back to @p gameWidth when the face cannot say,
 *                  @p faceAdvanceHires <= 0). @p scale is hi-res pixels per
 *                  game pixel: 2 for SCI16's hi-res text plane, 1 where
 *                  glyphs are drawn at game resolution.
 *   advance=cell, advance=(engine) - -1: neither is a game/font metric this
 *                  helper knows how to compute; the caller applies its own
 *                  cell width (design 6.3's wide/narrow cell) or its own
 *                  default rule.
 */
int advanceGamePx(HiResAdvance advance, int gameWidth, int faceAdvanceHires, int scale);

/**
 * design 6.3's cell fallback: the width, in game (lowres) pixels, assigned
 * to a glyph when the resource face has no width for it to measure at all -
 * a wide-script code point the game font never drew, say. MAX(1, (@p isWide
 * ? @p cell : @p cell / 2) / @p scale).
 */
int cellFallbackWidth(bool isWide, int cell, int scale);

/**
 * design 6.3/6.7: advance=game/font's width when the resource face's own
 * width for the game id's game code, @p gameWidth, is known - exactly
 * advanceGamePx() of it - or cellFallbackWidth() when it is not (@p
 * gameWidth <= 0, design 6.3's "where the resource face has no glyph for it
 * either, falls to the cell"). @p gameWidth must be measured on the game
 * code (the character the font id's own resource face was asked to draw
 * before any range rule, `[glyphs]` remap or target substitution), never
 * the drawn/remapped/target code point - that code may not be one the
 * resource face has anything for at all, which is exactly the case this
 * falls back for.
 */
int advanceGameOrFontPx(HiResAdvance advance, int gameWidth, int cell, bool isWide, int faceAdvanceHires, int scale);

/**
 * advanceGameOrFontPx() for one drawn glyph, with the game font's width
 * taken for @p gameCode - the character the text asked for, before any
 * range rule, `[glyphs]` remap or target substitution - and the cell
 * fallback sized by @p drawnCode, the code point actually drawn.
 *
 * @param gameFontWidth  callable `int(uint32 gameCode)`: the resource
 *                       face's width in game pixels, <= 0 when it has no
 *                       glyph for the code
 */
template<class GameFontWidth>
int advanceForGameCode(HiResAdvance advance, uint32 gameCode, uint32 drawnCode, GameFontWidth gameFontWidth,
					   int cell, int faceAdvanceHires, int scale) {
	return advanceGameOrFontPx(advance, gameFontWidth(gameCode), cell, Unicode::isWide(drawnCode),
							   faceAdvanceHires, scale);
}

} // End of namespace Graphics

#endif
