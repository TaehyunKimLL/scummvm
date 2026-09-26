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


#ifndef SCI_GRAPHICS_LATINADVANCE_H
#define SCI_GRAPHICS_LATINADVANCE_H

#include "graphics/hires_text/font_map.h"

namespace Sci {

/**
 * hires_text_latin=proportional: the advance, in game (lowres) pixels, of one
 * ASCII character drawn by the TrueType face. Engine-free, so the one rule
 * that both measuring (getCharWidth) and drawing (the pen advance, which
 * GfxText16 takes from getCharWidth) follow is tested alone.
 *
 *   metrics=game - @p gameWidth, the width of the character in the font id's
 *                  own resource face. Layout is identical to latin=off.
 *   metrics=font - the face's own advance, max(1, round(@p ttfAdvanceHires /
 *                  @p scale)), rounding half up. When the face cannot say
 *                  (@p ttfAdvanceHires <= 0) it falls back to @p gameWidth.
 *
 * @param scale  hi-res pixels per game pixel: 2 for SCI16's hi-res text
 *               plane, 1 where glyphs are drawn at game resolution
 */
int latinAdvanceGamePx(Graphics::HiResMetricsSource metrics, int gameWidth, int ttfAdvanceHires, int scale);

} // End of namespace Sci

#endif
