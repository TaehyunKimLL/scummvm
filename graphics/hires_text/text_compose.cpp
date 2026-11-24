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
#include "common/ptr.h"
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

bool rgbFormat(const PixelFormat &fmt) {
	return fmt.bytesPerPixel == 2 || fmt.bytesPerPixel == 4;
}

} // End of anonymous namespace

void stampSpan(byte *dst, const byte *index, const byte *coverage, int n, byte threshold) {
	for (int i = 0; i < n; ++i)
		if (coverage[i] && coverage[i] >= threshold)
			dst[i] = index[i];
}

uint32 blendPixel(const PixelFormat &fmt, uint32 dst, byte r, byte g, byte b, byte cov) {
	const byte loss[3] = { fmt.rLoss, fmt.gLoss, fmt.bLoss }, shift[3] = { fmt.rShift, fmt.gShift, fmt.bShift };
	const byte fg[3] = { r, g, b };
	uint32 out = fmt.RGBToColor(0, 0, 0);	// the alpha field, opaque
	for (int i = 0; i < 3; ++i) {
		const uint32 mask = (1u << (8 - loss[i])) - 1;
		const uint32 bgc = dst >> shift[i] & mask, fgc = fg[i] >> loss[i];
		out |= ((bgc * (255 - cov) + fgc * cov + 127) / 255) << shift[i];
	}
	return out;
}

Blend16::Blend16(const PixelFormat &fmt) {
	_rShift = fmt.rShift;
	_gShift = fmt.gShift;
	_bShift = fmt.bShift;
	_rLoss = fmt.rLoss;
	_gLoss = fmt.gLoss;
	_bLoss = fmt.bLoss;
	_rMask = (uint16)((1u << (8 - _rLoss)) - 1);
	_gMask = (uint16)((1u << (8 - _gLoss)) - 1);
	_bMask = (uint16)((1u << (8 - _bLoss)) - 1);
	_alpha = (uint16)fmt.RGBToColor(0, 0, 0);	// the alpha field, opaque, as blendPixel() sets it
	for (int i = 0; i < 4; ++i)
		_slot[i].key = 0;
}

void Blend16::fill(Slot &s, uint32 key, byte r, byte g, byte b, byte cov) {
	// blendPixel()'s arithmetic, one channel value at a time.
	const uint32 fr = r >> _rLoss, fg = g >> _gLoss, fb = b >> _bLoss;
	for (uint32 v = 0; v <= _rMask; ++v)
		s.r[v] = (uint16)(((v * (255 - cov) + fr * cov + 127) / 255) << _rShift);
	for (uint32 v = 0; v <= _gMask; ++v)
		s.g[v] = (uint16)(((v * (255 - cov) + fg * cov + 127) / 255) << _gShift);
	for (uint32 v = 0; v <= _bMask; ++v)
		s.b[v] = (uint16)(((v * (255 - cov) + fb * cov + 127) / 255) << _bShift);
	s.key = key;
}

namespace {

// The pixel @p dst of @p fmt with the text of palette index @p index at @p coverage put over it. Coverage 255 is the
// colour itself (blendPixel gives (255 * fg + 127) / 255 = fg per channel), without the arithmetic.
inline uint32 putText(const PixelFormat &fmt, uint32 dst, byte index, byte coverage, const byte *pal, Mode mode,
                      Blend16 *table) {
	const byte *c = pal + index * 3;
	if (mode == kBlend && coverage != 255) {
		if (table)
			return table->blend((uint16)dst, c[0], c[1], c[2], coverage);
		return blendPixel(fmt, dst, c[0], c[1], c[2], coverage);
	}
	return coverage < 128 ? dst : fmt.RGBToColor(c[0], c[1], c[2]);
}

} // End of anonymous namespace

void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode, Blend16 *table) {
	const int bpp = fmt.bytesPerPixel;
	Common::ScopedPtr<Blend16> own;
	if (bpp == 2 && mode == kBlend && !table) {
		own.reset(new Blend16(fmt));
		table = own.get();
	} else if (bpp != 2) {
		table = nullptr;
	}
	if (table && mode == kBlend) {
		// The 2-byte blend, the hot path on DOS (15/16-bit VBE): the colour of the last index is kept for coverage 255.
		int lastIndex = -1;
		uint16 solid = 0;
		for (int i = 0; i < n; ++i, dst += 2) {
			const byte cov = coverage[i];
			if (!cov)
				continue;
			const byte *c = paletteRGB + index[i] * 3;
			if (cov == 255) {
				if (index[i] != lastIndex) {
					lastIndex = index[i];
					solid = (uint16)fmt.RGBToColor(c[0], c[1], c[2]);
				}
				WRITE_UINT16(dst, solid);
			} else {
				WRITE_UINT16(dst, table->blend(READ_UINT16(dst), c[0], c[1], c[2], cov));
			}
		}
		return;
	}
	for (int i = 0; i < n; ++i, dst += bpp) {
		if (!coverage[i])
			continue;
		const uint32 px = readPixel(dst, bpp);
		writePixel(dst, bpp, putText(fmt, px, index[i], coverage[i], paletteRGB, mode, table));
	}
}

void convertSpan(byte *dst, const PixelFormat &fmt, const byte *frame, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode, Blend16 *table) {
	const int bpp = fmt.bytesPerPixel;
	if (bpp != 2)
		table = nullptr;
	for (int i = 0; i < n; ++i, dst += bpp) {
		const byte *c = paletteRGB + frame[i] * 3;
		uint32 px = fmt.RGBToColor(c[0], c[1], c[2]);
		if (coverage && coverage[i])
			px = putText(fmt, px, index[i], coverage[i], paletteRGB, mode, table);
		writePixel(dst, bpp, px);
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
	Common::ScopedPtr<Blend16> table(dst.format.bytesPerPixel == 2 && mode == kBlend ? new Blend16(dst.format) : nullptr);
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
				composeSpan(p, dst.format, index, coverage, n, paletteRGB, mode, table.get());
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
	Common::ScopedPtr<Blend16> table(dst.format.bytesPerPixel == 2 && mode == kBlend ? new Blend16(dst.format) : nullptr);
	for (int y = r.top; y < r.bottom; ++y) {
		for (int x = r.left; x < r.right; x += kChunk) {
			const int n = MIN<int>(kChunk, r.right - x);
			const byte *index = nullptr, *coverage = nullptr;
			if (!plane.getRow(y, x, n, idxScratch, covScratch, index, coverage))
				coverage = nullptr;
			convertSpan((byte *)dst.getBasePtr(x, y), dst.format, (const byte *)frame.getBasePtr(x, y), index, coverage, n,
			            paletteRGB, mode, table.get());
		}
	}
	return true;
}

} // End of namespace TextCompose
} // End of namespace Graphics
