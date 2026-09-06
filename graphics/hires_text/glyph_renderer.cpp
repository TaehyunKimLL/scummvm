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

#include "graphics/hires_text/glyph_renderer.h"

#include "graphics/hires_text/bitmap_font.h"
#include "graphics/surface.h"

namespace Graphics {

// Where the decoration goes, as multiples of the style's offset.
//
// A drop shadow is one copy below and right. An outline surrounds the glyph.
// The stroke form is an outline weighted towards the lower left, which is what
// the heavier of the shipped fonts were drawn to sit on.
static const int8 kDropX[] = { 1 };
static const int8 kDropY[] = { 1 };

static const int8 kOutlineX[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int8 kOutlineY[] = { -1, -1, -1, 0, 0, 1, 1, 1 };

static const int8 kStrokeX[] = { -1, 0, 1, -1, 1, -1, 0, 1, -1, -1, -2 };
static const int8 kStrokeY[] = { -1, -1, -1, 0, 0, 1, 1, 1, 2, 1, 0 };

/// Coverage of one pixel of a glyph, 0 for nothing at all.
static inline byte glyphCoverage(const byte *row, int x, int bpp) {
	if (bpp == 1)
		return (row[x >> 3] & (0x80 >> (x & 7))) ? 0xFF : 0;
	return row[x];
}

bool HiResGlyphRenderer::drawGlyph(Surface &dest, Surface *coverage,
								   const HiResBitmapFont &font, int index,
								   int x, int y, const GlyphStyle &style,
								   Common::Rect *dirty) {
	const byte *pixels = font.glyphData(index);
	if (!pixels)
		return false;

	GlyphBitmap glyph;
	glyph.pixels = pixels;
	glyph.pitch = font.glyphPitch();
	glyph.width = font.cellWidth();
	glyph.height = font.cellHeight();
	glyph.bpp = font.bpp();

	return drawGlyph(dest, coverage, glyph, x, y, style, dirty);
}

bool HiResGlyphRenderer::drawGlyph(Surface &dest, Surface *coverage,
								   const HiResGlyphSource &source, uint32 codepoint,
								   int x, int y, const GlyphStyle &style,
								   Common::Rect *dirty) {
	GlyphBitmap glyph;
	if (!source.glyph(codepoint, glyph))
		return false;

	return drawGlyph(dest, coverage, glyph, x, y, style, dirty);
}

bool HiResGlyphRenderer::drawGlyph(Surface &dest, Surface *coverage,
								   const GlyphBitmap &glyph,
								   int x, int y, const GlyphStyle &style,
								   Common::Rect *dirty) {
	if (!glyph.pixels || glyph.width <= 0 || glyph.height <= 0)
		return false;

	// The coverage surface only makes sense for a glyph that has coverage to
	// record, and only where it is large enough to hold it.
	Surface *cov = (coverage && glyph.bpp == 8 && coverage->getPixels()) ? coverage : nullptr;

	const int8 *offX = nullptr;
	const int8 *offY = nullptr;
	int copies = 0;

	switch (style.shadowMode) {
	case kHiResShadowDrop:
		offX = kDropX; offY = kDropY; copies = ARRAYSIZE(kDropX);
		break;
	case kHiResShadowOutline:
		offX = kOutlineX; offY = kOutlineY; copies = ARRAYSIZE(kOutlineX);
		break;
	case kHiResShadowStroke:
		offX = kStrokeX; offY = kStrokeY; copies = ARRAYSIZE(kStrokeX);
		break;
	default:
		break;
	}

	// A decoration in the same colour as the text cannot be seen and only
	// costs time - and, at 8bpp, would thicken the glyph for no reason.
	if (style.shadowColor == style.color)
		copies = 0;

	// A rasteriser hands back a glyph placed relative to the pen; a baked
	// bitmap font has no offset of its own.
	const int baseX = x + glyph.originX;
	const int baseY = y + glyph.originY;
	const int step = style.shadowOffset;

	// The decoration is laid down in full before the body, so that a later
	// pixel of the same glyph - or of the next character along - cannot paint
	// over a stroke that is already there.
	for (int pass = copies ? 0 : 1; pass < 2; ++pass) {
		const int passCopies = pass ? 1 : copies;

		for (int c = 0; c < passCopies; ++c) {
			const int ox = pass ? 0 : offX[c] * step;
			const int oy = pass ? 0 : offY[c] * step;
			const byte ink = pass ? style.color : style.shadowColor;

			for (int gy = 0; gy < glyph.height; ++gy) {
				const int py = baseY + gy + oy;
				if (py < 0 || py >= dest.h)
					continue;

				const byte *row = glyph.pixels + gy * glyph.pitch;

				for (int gx = 0; gx < glyph.width; ++gx) {
					const byte cv = glyphCoverage(row, gx, glyph.bpp);
					if (!cv)
						continue;

					const int px = baseX + gx + ox;
					if (px < 0 || px >= dest.w)
						continue;

					const bool inCoverage = cov && px < cov->w && py < cov->h;

					// A decoration pixel must not eat into the body, and a
					// fainter pixel must never replace a stronger one that is
					// already down - otherwise the outline of one glyph
					// erodes the stroke of its neighbour.
					if (!pass && inCoverage &&
						*(const byte *)cov->getBasePtr(px, py) >= cv)
						continue;

					*(byte *)dest.getBasePtr(px, py) = ink;

					if (inCoverage)
						*(byte *)cov->getBasePtr(px, py) = cv;
				}
			}
		}
	}

	if (dirty) {
		// The decoration reaches beyond the cell, so the caller is told about
		// the whole area that may have changed.
		const int margin = copies ? step * 2 : 0;
		const Common::Rect touched(baseX - margin, baseY - margin,
								   baseX + glyph.width + margin,
								   baseY + glyph.height + margin);
		if (dirty->isEmpty())
			*dirty = touched;
		else
			dirty->extend(touched);
	}

	return true;
}

} // End of namespace Graphics
