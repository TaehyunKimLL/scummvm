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

#include "common/rect.h"
#include "common/scummsys.h"

#include "graphics/pixelformat.h"

namespace Graphics {

struct Surface;
class TextPlane;

/**
 * The sinks of hi-res text: a TextPlane's {index, coverage} pixels put on a
 * hi-res screen. Palettes are 256 entries of 8-bit R, G, B.
 */
namespace TextCompose {

enum Mode {
	kStamp,     ///< CLUT8 screen: the index where coverage >= 128
	kThreshold, ///< RGB screen: the palette colour where coverage >= 128
	kBlend      ///< RGB screen: the palette colour blended by coverage
};

/** (dst * (255 - a) + src * a + 127) / 255, the renderer's blend. */
inline byte blend(byte dst, byte src, byte a) {
	return (byte)(((uint32)dst * (255 - a) + (uint32)src * a + 127) / 255);
}

/**
 * The pixel @p dst of @p fmt with the colour (@p r, @p g, @p b) blended in by @p cov, in the channel widths of the format:
 * per channel (bg * (255 - cov) + fg * cov + 127) / 255 with bg the pixel's own channel value and fg the colour cut to the
 * channel (as PixelFormat::RGBToColor does). This is the arithmetic of the C renderer's RGB565/RGB555 targets
 * (graphics/svf/svf_draw.c blend565, blend555) and, at 8 bits a channel, the old 8-bit blend, so a 15/16-bit composite
 * equals the C targets exactly over one background colour. An alpha field is set opaque (all ones, as RGBToColor does).
 */
uint32 blendPixel(const PixelFormat &fmt, uint32 dst, byte r, byte g, byte b, byte cov);

/** CLUT8: @p dst[i] = @p index[i] where @p coverage[i] >= @p threshold. */
void stampSpan(byte *dst, const byte *index, const byte *coverage, int n, byte threshold = 128);

/**
 * RGB, over the pixels already in @p dst (2 or 4 bytes a pixel in @p fmt):
 * kThreshold writes the text colour at coverage >= 128, kBlend blends it in
 * by coverage. A pixel without coverage is not touched.
 */
void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode);

/**
 * RGB from a CLUT8 hi-res frame row: every pixel of @p dst becomes
 * @p frame's palette colour with the text put over it at 8 bits a channel
 * (so an RGB565 screen rounds once). @p index / @p coverage may be null:
 * no text on the row.
 */
void convertSpan(byte *dst, const PixelFormat &fmt, const byte *frame, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode);

/**
 * @p plane over @p dst inside @p rect (hi-res, clipped to both): kStamp on a
 * CLUT8 @p dst, kThreshold or kBlend on a 2 or 4 byte one. false (nothing
 * drawn) for any other pairing.
 */
bool compositeRect(const TextPlane &plane, const Common::Rect &rect, Surface &dst, const byte *paletteRGB, Mode mode);

/**
 * @p frame (CLUT8, the size of @p plane) and @p plane into @p dst (2 or 4
 * bytes a pixel, the same size) inside @p rect, through convertSpan().
 */
bool presentRect(const TextPlane &plane, const Surface &frame, const Common::Rect &rect, Surface &dst,
                 const byte *paletteRGB, Mode mode);

} // End of namespace TextCompose
} // End of namespace Graphics

#endif
