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


#ifndef AGS_SHARED_FONT_GLYPH_FONT_DRAW_H
#define AGS_SHARED_FONT_GLYPH_FONT_DRAW_H

#include "common/rect.h"
#include "common/scummsys.h"
#include "graphics/pixelformat.h"

namespace Graphics {
struct Surface;
class UnicodeGlyphSource;
}

namespace AGS3 {

/**
 * Where a glyph the map's fonts lack comes from: the game's own renderer of
 * the same font (I18N_TEXT_DESIGN.md section 4.4, "next map font, then the
 * game's own renderer"). The map's chain itself is the glyph source.
 */
class GlyphFallback {
public:
	virtual ~GlyphFallback() {}
	/** Advance of cp in the game's own font; 0 when it has none either. */
	virtual int charWidth(uint32 cp) = 0;
	/** Draw cp at (x, y), the top of the line, into the drawer's target. */
	virtual void drawChar(uint32 cp, int x, int y, uint32 colour) = 0;
};

/**
 * The drawing half of GlyphFontRenderer, free of the engine's globals so it
 * is tested alone: text as code points, placed per glyph from
 * UnicodeGlyphSource::metrics() (design section 4.2).
 *
 * - A glyph is drawn with its pen origin at column metrics().originX of
 *   its rows, so ink left of the origin (a Thai mark's negative bearing) is
 *   kept; the pen then advances by metrics().advance.
 * - A combining mark never advances: it is drawn against the pen position
 *   after the previous base, which is where the pen already is.
 * - Coverage c of a pixel: into a 16/32-bit target with alpha on,
 *   blendPixel(); into an 8-bit target, or with alpha off, the colour where
 *   c >= 128.
 */
class GlyphTextDrawer {
public:
	GlyphTextDrawer() : _src(nullptr), _alpha(true) {}
	GlyphTextDrawer(Graphics::UnicodeGlyphSource *src, bool alpha) : _src(src), _alpha(alpha) {}

	void setSource(Graphics::UnicodeGlyphSource *src) { _src = src; }
	Graphics::UnicodeGlyphSource *source() const { return _src; }
	void setAlpha(bool alpha) { _alpha = alpha; }
	bool alpha() const { return _alpha; }

	/** The pen advance of cp: 0 for a combining mark, the fallback's width
	 *  when the source lacks cp (0 without a fallback). */
	int charAdvance(uint32 cp, GlyphFallback *fallback);

	/** Sum of charAdvance() over the text. */
	int textWidth(const uint32 *cps, uint count, GlyphFallback *fallback);

	/**
	 * Draw the text with its line top at y, the pen starting at x. Pixels
	 * outside clip (inclusive left/top, exclusive right/bottom) or the
	 * surface are left alone. A code point the source lacks goes to
	 * fallback->drawChar() (nothing without a fallback).
	 */
	void drawText(Graphics::Surface &dst, const Common::Rect &clip, const uint32 *cps, uint count,
				  int x, int y, uint32 colour, GlyphFallback *fallback);

	/**
	 * One pixel of coverage c (1..255) of colour over dst, in fmt (2 or 4
	 * bytes per pixel), the way alfont's anti-aliased text composes:
	 * c == 255 writes colour; a transparent pixel (the magenta mask colour,
	 * or alpha 0) takes colour's RGB with alpha c (32-bit) or colour where
	 * c >= 128 (16-bit, which has no alpha); otherwise each RGB channel is
	 * TextCompose::blend(dst, colour, c) and dst keeps its alpha.
	 */
	static uint32 blendPixel(uint32 dst, uint32 colour, byte c, const Graphics::PixelFormat &fmt);

private:
	Graphics::UnicodeGlyphSource *_src;
	bool _alpha;
};

} // namespace AGS3

#endif
