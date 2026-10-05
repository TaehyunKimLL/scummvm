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

#ifndef GRAPHICS_HIRES_TEXT_TEXT_COMPOSE_H
#define GRAPHICS_HIRES_TEXT_TEXT_COMPOSE_H

#include "common/scummsys.h"
#include "graphics/pixelformat.h"

namespace Graphics {

/** One hi-res pixel of text: a foreground and an outline, each a palette
 *  index plus 8-bit coverage. Colour is resolved when composited, so text
 *  follows palette changes the way the original's indexed text did. */
struct TextPixel {
	byte fgIndex;
	byte fgCoverage;
	byte outlineIndex;
	byte outlineCoverage;
};

/**
 * A text pixel without an outline, for a layer that never draws one (SCI's):
 * half the size, composed as a TextPixel with no outline coverage is.
 */
struct TextPixelFg {
	byte fgIndex;
	byte fgCoverage;
};

/** The arithmetic of hi-res text, free of engine state so it is tested alone
 *  (HIRES_COMPOSITOR_DESIGN.md §3.2). */
namespace TextCompose {

/** Coverage of pixel @p x of a glyph row: a 1bpp stencil reads 0 or 255,
 *  2bpp its level times 85, 8bpp the byte. Inline: the glyph loops call it
 *  once per pixel. */
inline byte expandCoverage(const byte *row, int x, int bpp) {
	switch (bpp) {
	case 1:
		return (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
	case 2:
		return ((row[x >> 2] >> (6 - ((x & 3) * 2))) & 3) * 85;
	default:
		return row[x];
	}
}
void expandGlyphRow(byte *dstCoverage, const byte *row, int width, int bpp, bool greyed, int screenY, int screenX0);

inline byte blend(byte dst, byte src, byte a) {
	return (byte)(((uint32)dst * (255 - a) + (uint32)src * a + 127) / 255);
}

void composeSpan(byte *dst, const Graphics::PixelFormat &fmt, const TextPixel *text, int count, const byte *paletteRGB);
void stampSpan(byte *dstIndex, const TextPixel *text, int count);
void composeSpan(byte *dst, const Graphics::PixelFormat &fmt, const TextPixelFg *text, int count, const byte *paletteRGB);
void stampSpan(byte *dstIndex, const TextPixelFg *text, int count);

/**
 * composeSpan() with the colour under the text given rather than read back:
 * @p underRGB holds each pixel's 8-bit R, G, B (3 bytes a pixel). On a
 * screen of fewer bits a channel (RGB565) the pixel in @p dst has lost what
 * the blend needs; this blends at 8 bits, as a true-colour screen does, and
 * rounds the result to @p fmt (2 or 4 bytes a pixel) once. A pixel without
 * coverage is left as it is.
 */
void composeSpanOver(byte *dst, const Graphics::PixelFormat &fmt, const TextPixelFg *text, int count,
					 const byte *underRGB, const byte *paletteRGB);

/**
 * Turn a run of 8-bit coverage into true-colour pixels with alpha: each
 * dst pixel gets RGB (r, g, b) and alpha = coverage, in @p fmt, which must
 * be 4 bytes per pixel with an 8-bit alpha channel. Used by engines that
 * blend a text line as a texture (Grim on TinyGL and OpenGL shaders).
 */
void coverageToArgb(const byte *coverage, uint32 *dst, int count, const Graphics::PixelFormat &fmt, byte r, byte g, byte b);

} // End of namespace TextCompose
} // End of namespace Graphics

#endif
