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


// The drawing half of GlyphFontRenderer, free of the engine's globals so the
// unit tests link it alone (test/engines/ags/glyph_renderer.h).

#include "ags/shared/font/glyph_font_draw.h"
#include "graphics/surface.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/text_compose.h"

namespace AGS3 {

int GlyphTextDrawer::charAdvance(uint32 cp, GlyphFallback *fallback) {
	Graphics::GlyphMetrics m;
	if (_src && _src->metrics(cp, m))
		return m.combining ? 0 : m.advance;
	return fallback ? fallback->charWidth(cp) : 0;
}

int GlyphTextDrawer::textWidth(const uint32 *cps, uint count, GlyphFallback *fallback) {
	int w = 0;
	for (uint i = 0; i < count; i++)
		w += charAdvance(cps[i], fallback);
	return w;
}

uint32 GlyphTextDrawer::blendPixel(uint32 dst, uint32 colour, byte c, const Graphics::PixelFormat &fmt) {
	if (c == 255)
		return colour;
	byte da, dr, dg, db;
	fmt.colorToARGB(dst, da, dr, dg, db);
	byte ca, cr, cg, cb;
	fmt.colorToARGB(colour, ca, cr, cg, cb);
	const bool hasAlpha = fmt.aBits() > 0;
	const bool transparent = (dr == 255 && dg == 0 && db == 255) || (hasAlpha && da == 0);
	if (transparent) {
		if (hasAlpha)
			return fmt.ARGBToColor(c, cr, cg, cb);
		return c >= 128 ? colour : dst;
	}
	return fmt.ARGBToColor(da,
						   Graphics::TextCompose::blend(dr, cr, c),
						   Graphics::TextCompose::blend(dg, cg, c),
						   Graphics::TextCompose::blend(db, cb, c));
}

void GlyphTextDrawer::drawText(Graphics::Surface &dst, const Common::Rect &clip, const uint32 *cps, uint count,
							   int x, int y, uint32 colour, GlyphFallback *fallback) {
	Common::Rect area(0, 0, dst.w, dst.h);
	area.clip(clip);
	if (area.isEmpty())
		return;
	const int bytesPP = dst.format.bytesPerPixel;
	const bool blend = _alpha && (bytesPP == 2 || bytesPP == 4);

	int pen = x;
	for (uint i = 0; i < count; i++) {
		const uint32 cp = cps[i];
		Graphics::GlyphMetrics m;
		if (!_src || !_src->metrics(cp, m)) {
			if (fallback) {
				fallback->drawChar(cp, pen, y, colour);
				pen += fallback->charWidth(cp);
			}
			continue;
		}
		// A mark is drawn where the pen stands after its base; its own
		// negative bearing (originX) puts it over the base.
		const int gx = pen - m.originX;
		if (!m.combining)
			pen += m.advance;
		if (gx >= area.right)
			continue;

		const int bpp = _src->bitsPerPixel();
		const int rowWidth = _src->cellWidth() * 2;
		const int rows = _src->cellHeight();
		for (int ry = 0; ry < rows; ry++) {
			const int py = y + ry;
			if (py < area.top || py >= area.bottom)
				continue;
			const byte *row = _src->row(cp, ry);
			if (!row)
				continue;
			for (int rx = 0; rx < rowWidth; rx++) {
				const int px = gx + rx;
				if (px < area.left || px >= area.right)
					continue;
				const byte c = Graphics::TextCompose::expandCoverage(row, rx, bpp);
				if (c == 0)
					continue;
				byte *p = (byte *)dst.getBasePtr(px, py);
				if (!blend) {
					if (c < 128)
						continue;
					if (bytesPP == 1)
						*p = (byte)colour;
					else if (bytesPP == 2)
						*(uint16 *)p = (uint16)colour;
					else if (bytesPP == 4)
						*(uint32 *)p = colour;
					continue;
				}
				if (bytesPP == 2)
					*(uint16 *)p = (uint16)blendPixel(*(uint16 *)p, colour, c, dst.format);
				else
					*(uint32 *)p = blendPixel(*(uint32 *)p, colour, c, dst.format);
			}
		}
	}
}

} // namespace AGS3
