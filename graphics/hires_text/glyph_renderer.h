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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_RENDERER_H
#define GRAPHICS_HIRES_TEXT_GLYPH_RENDERER_H

#include "common/rect.h"
#include "common/scummsys.h"
#include "graphics/hires_text/font_map.h"

namespace Graphics {

struct Surface;
class HiResBitmapFont;

/**
 * How a glyph is decorated, and with what.
 *
 * The colours are palette indices: this renderer writes into the same CLUT8
 * surface the engine's own text goes to, and knows nothing about what the
 * indices mean.
 */
struct GlyphStyle {
	GlyphStyle() :
		color(0), shadowColor(0), shadowMode(kHiResShadowNone), shadowOffset(1) {}

	byte color;
	byte shadowColor;
	HiResShadowMode shadowMode;
	int shadowOffset;   ///< distance from the glyph, in destination pixels
};

/**
 * Draws glyphs of a bitmap font onto a CLUT8 surface.
 *
 * An 8bpp font stores coverage rather than a stencil, which a paletted surface
 * cannot express on its own. The renderer therefore writes the colour into the
 * text surface and the coverage into a parallel 8bpp one, leaving the caller to
 * blend the two against whatever is behind them. That parallel surface is
 * optional: without it an 8bpp font still draws, as a stencil of every pixel
 * with any coverage at all.
 *
 * Nothing here knows about scaling. A font is baked at the size it will be
 * drawn, so glyphs go down one pixel per pixel.
 */
class HiResGlyphRenderer {
public:
	/**
	 * Draw one glyph.
	 *
	 * @param dest      CLUT8 surface receiving the colour
	 * @param coverage  optional 8bpp surface receiving the coverage, or null
	 * @param font      the font to take the glyph from
	 * @param index     glyph index within that font
	 * @param x, y      top left of the glyph cell, in destination pixels
	 * @param style     colour and decoration
	 * @param dirty     if not null, extended by the area actually written
	 * @return false when the font has no such glyph
	 */
	static bool drawGlyph(Surface &dest, Surface *coverage,
						  const HiResBitmapFont &font, int index,
						  int x, int y, const GlyphStyle &style,
						  Common::Rect *dirty = nullptr);
};

} // End of namespace Graphics

#endif
