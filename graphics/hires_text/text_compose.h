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

/**
 * blendPixel() on a 2-byte format by table. For one text colour and coverage the blended value of every channel value is
 * computed once (at most 32 + 64 + 32 entries) and then looked up: three lookups and an OR a pixel instead of three
 * multiply-divides, which an emulated 486DX2-66 runs at 8.8 us a covered pixel (P7 plan, section 0.3). The result is
 * blendPixel()'s, bit for bit. Four slots, chosen by coverage >> 6, keep the pairs a line of a 2 bpp face alternates
 * between (85 and 170); any other coverage only costs a rebuild. Keep one object across the rows of a rectangle.
 */
class Blend16 {
public:
	explicit Blend16(const PixelFormat &fmt);
	/** blendPixel(fmt, dst, r, g, b, cov); @p cov 1..254 (0 and 255 are the caller's: nothing, or the colour). */
	uint16 blend(uint16 dst, byte r, byte g, byte b, byte cov) {
		Slot &s = _slot[cov >> 6];
		const uint32 key = (uint32)r << 24 | (uint32)g << 16 | (uint32)b << 8 | cov;
		if (s.key != key)
			fill(s, key, r, g, b, cov);
		return (uint16)(s.r[dst >> _rShift & _rMask] | s.g[dst >> _gShift & _gMask] | s.b[dst >> _bShift & _bMask] | _alpha);
	}

private:
	struct Slot {
		uint32 key;	// r << 24 | g << 16 | b << 8 | cov; cov 0 (never asked for) marks an empty slot
		uint16 r[64], g[64], b[64];
	};
	void fill(Slot &s, uint32 key, byte r, byte g, byte b, byte cov);

	byte _rShift, _gShift, _bShift, _rLoss, _gLoss, _bLoss;
	uint16 _rMask, _gMask, _bMask, _alpha;
	Slot _slot[4];
};

/** CLUT8: @p dst[i] = @p index[i] where @p coverage[i] >= @p threshold. */
void stampSpan(byte *dst, const byte *index, const byte *coverage, int n, byte threshold = 128);

/**
 * RGB, over the pixels already in @p dst (2 or 4 bytes a pixel in @p fmt):
 * kThreshold writes the text colour at coverage >= 128, kBlend blends it in
 * by coverage. A pixel without coverage is not touched. On a 2-byte format kBlend goes through @p table when given
 * (else one made for this call).
 */
void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode, Blend16 *table = nullptr);

/**
 * RGB from a CLUT8 hi-res frame row: every pixel of @p dst becomes
 * @p frame's palette colour cut to the screen format's channels (as
 * PixelFormat::RGBToColor does) with the text blended over it in those channel widths (blendPixel()). @p index / @p coverage may be null:
 * no text on the row.
 */
void convertSpan(byte *dst, const PixelFormat &fmt, const byte *frame, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode, Blend16 *table = nullptr);

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
