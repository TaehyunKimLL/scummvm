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

#include "graphics/hires_text/text_compose.h"

#include "common/endian.h"
#include "common/util.h"

#include "graphics/hires_text/text_plane.h"
#include "graphics/surface.h"

namespace Graphics {
namespace TextCompose {

namespace {

enum { kChunk = 256 };

inline uint32 readPixel(const byte *p, int bpp) {
	return bpp == 2 ? READ_UINT16(p) : READ_UINT32(p);
}

inline void writePixel(byte *p, int bpp, uint32 c) {
	if (bpp == 2)
		WRITE_UINT16(p, c);
	else
		WRITE_UINT32(p, c);
}

// The text over (r, g, b); false when it leaves the pixel as it is.
inline bool putText(byte &r, byte &g, byte &b, byte index, byte coverage, const byte *pal, Mode mode) {
	if (!coverage)
		return false;
	const byte *c = pal + index * 3;
	if (mode == kBlend) {
		r = blend(r, c[0], coverage);
		g = blend(g, c[1], coverage);
		b = blend(b, c[2], coverage);
		return true;
	}
	if (coverage < 128)
		return false;
	r = c[0];
	g = c[1];
	b = c[2];
	return true;
}

bool rgbFormat(const PixelFormat &fmt) {
	return fmt.bytesPerPixel == 2 || fmt.bytesPerPixel == 4;
}

} // End of anonymous namespace

void stampSpan(byte *dst, const byte *index, const byte *coverage, int n, byte threshold) {
	for (int i = 0; i < n; ++i)
		if (coverage[i] && coverage[i] >= threshold)
			dst[i] = index[i];
}

void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode) {
	const int bpp = fmt.bytesPerPixel;
	for (int i = 0; i < n; ++i, dst += bpp) {
		if (!coverage[i])
			continue;
		byte r, g, b;
		fmt.colorToRGB(readPixel(dst, bpp), r, g, b);
		if (putText(r, g, b, index[i], coverage[i], paletteRGB, mode))
			writePixel(dst, bpp, fmt.RGBToColor(r, g, b));
	}
}

void convertSpan(byte *dst, const PixelFormat &fmt, const byte *frame, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode) {
	const int bpp = fmt.bytesPerPixel;
	for (int i = 0; i < n; ++i, dst += bpp) {
		const byte *c = paletteRGB + frame[i] * 3;
		byte r = c[0], g = c[1], b = c[2];
		if (coverage)
			putText(r, g, b, index[i], coverage[i], paletteRGB, mode);
		writePixel(dst, bpp, fmt.RGBToColor(r, g, b));
	}
}

bool compositeRect(const TextPlane &plane, const Common::Rect &rect, Surface &dst, const byte *paletteRGB, Mode mode) {
	if (!plane.exists() || !dst.getPixels())
		return false;
	const bool clut8 = dst.format.isCLUT8();
	if ((mode == kStamp) != clut8 || (!clut8 && (!rgbFormat(dst.format) || !paletteRGB)))
		return false;
	Common::Rect r(rect);
	r.clip(Common::Rect(MIN<int>(plane.getWidth(), dst.w), MIN<int>(plane.getHeight(), dst.h)));
	byte idxScratch[kChunk], covScratch[kChunk];
	for (int y = r.top; y < r.bottom; ++y) {
		if (!plane.rowHasText(y))
			continue;
		for (int x = r.left; x < r.right; x += kChunk) {
			const int n = MIN<int>(kChunk, r.right - x);
			const byte *index, *coverage;
			if (!plane.getRow(y, x, n, idxScratch, covScratch, index, coverage))
				break;
			byte *p = (byte *)dst.getBasePtr(x, y);
			if (clut8)
				stampSpan(p, index, coverage, n);
			else
				composeSpan(p, dst.format, index, coverage, n, paletteRGB, mode);
		}
	}
	return true;
}

bool presentRect(const TextPlane &plane, const Surface &frame, const Common::Rect &rect, Surface &dst,
                 const byte *paletteRGB, Mode mode) {
	if (!plane.exists() || !frame.getPixels() || !dst.getPixels() || !frame.format.isCLUT8() || !rgbFormat(dst.format) ||
	    !paletteRGB || mode == kStamp)
		return false;
	Common::Rect r(rect);
	r.clip(Common::Rect(MIN<int>(plane.getWidth(), MIN<int>(frame.w, dst.w)), MIN<int>(plane.getHeight(), MIN<int>(frame.h, dst.h))));
	byte idxScratch[kChunk], covScratch[kChunk];
	for (int y = r.top; y < r.bottom; ++y) {
		for (int x = r.left; x < r.right; x += kChunk) {
			const int n = MIN<int>(kChunk, r.right - x);
			const byte *index = nullptr, *coverage = nullptr;
			if (!plane.getRow(y, x, n, idxScratch, covScratch, index, coverage))
				coverage = nullptr;
			convertSpan((byte *)dst.getBasePtr(x, y), dst.format, (const byte *)frame.getBasePtr(x, y), index, coverage, n,
			            paletteRGB, mode);
		}
	}
	return true;
}

} // End of namespace TextCompose
} // End of namespace Graphics
