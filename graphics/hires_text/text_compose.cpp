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

#include "common/endian.h"
#include "graphics/hires_text/text_compose.h"

namespace Graphics {
namespace TextCompose {

byte expandCoverage(const byte *row, int x, int bpp) {
	switch (bpp) {
	case 1:
		return (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
	case 2:
		return ((row[x >> 2] >> (6 - ((x & 3) * 2))) & 3) * 85;
	default:
		return row[x];
	}
}

void expandGlyphRow(byte *dstCoverage, const byte *row, int width, int bpp, bool greyed, int screenY, int screenX0) {
	for (int x = 0; x < width; x++) {
		byte c = expandCoverage(row, x, bpp);
		// The engine's checkerboard for disabled text: drop every other pixel.
		if (greyed && (screenY % 2) == ((screenX0 + x) % 2))
			c = 0;
		dstCoverage[x] = c;
	}
}

static void blendRGB(byte &r, byte &g, byte &b, const byte *pal, byte index, byte a) {
	r = blend(r, pal[index * 3 + 0], a);
	g = blend(g, pal[index * 3 + 1], a);
	b = blend(b, pal[index * 3 + 2], a);
}

void composeSpan(byte *dst, const Graphics::PixelFormat &fmt, const TextPixel *text, int count, const byte *paletteRGB) {
	const int bpp = fmt.bytesPerPixel;
	for (int i = 0; i < count; i++, dst += bpp) {
		const TextPixel &t = text[i];
		if (!t.fgCoverage && !t.outlineCoverage)
			continue;
		uint32 c = (bpp == 2) ? READ_UINT16(dst) : READ_UINT32(dst);
		byte r, g, b;
		fmt.colorToRGB(c, r, g, b);
		if (t.outlineCoverage)
			blendRGB(r, g, b, paletteRGB, t.outlineIndex, t.outlineCoverage);
		if (t.fgCoverage)
			blendRGB(r, g, b, paletteRGB, t.fgIndex, t.fgCoverage);
		c = fmt.RGBToColor(r, g, b);
		if (bpp == 2)
			WRITE_UINT16(dst, c);
		else
			WRITE_UINT32(dst, c);
	}
}

void stampSpan(byte *dstIndex, const TextPixel *text, int count) {
	for (int i = 0; i < count; i++) {
		if (text[i].fgCoverage >= 128)
			dstIndex[i] = text[i].fgIndex;
		else if (text[i].outlineCoverage >= 128)
			dstIndex[i] = text[i].outlineIndex;
	}
}

} // End of namespace TextCompose
} // End of namespace Graphics
