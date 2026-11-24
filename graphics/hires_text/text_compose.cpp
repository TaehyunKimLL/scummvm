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

// The MMX composite (see text_compose.h): DOS i386 builds, or any x86 build given -DHIRES_TEXT_MMX (the host tests).
#if defined(HIRES_TEXT_MMX) || (defined(__DJGPP__) && defined(__i386__))
#define TEXT_COMPOSE_HAVE_MMX 1
#include <cpuid.h>
#include <mmintrin.h>
#include <string.h>
#endif

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

bool Blend16::supports(const PixelFormat &fmt) {
	if (fmt.bytesPerPixel != 2 || fmt.rLoss < 2 || fmt.gLoss < 2 || fmt.bLoss < 2 || fmt.rLoss > 8 || fmt.gLoss > 8 ||
	    fmt.bLoss > 8)
		return false;
	return fmt.rShift + (8 - fmt.rLoss) <= 16 && fmt.gShift + (8 - fmt.gLoss) <= 16 && fmt.bShift + (8 - fmt.bLoss) <= 16;
}

Blend16::Blend16(const PixelFormat &fmt) {
	assert(supports(fmt));
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
		_slot[i].key = 0, _slot[i].filled = false;
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
	s.filled = true;
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


#ifdef TEXT_COMPOSE_HAVE_MMX
namespace {

bool g_useMMX = false;
int g_mmxMode = 1;	// 1 runs only (partial blocks through the table, the default), 0 vector arithmetic on partial blocks
bool g_mmxProbed = false, g_mmxPresent = false;

bool probeMMX() {
	if (!g_mmxProbed) {
		g_mmxProbed = true;
		unsigned a, b, c, d;
		// __get_cpuid() is 0 on a CPU without CPUID (the i486 baseline): no MMX then.
		if (__get_cpuid(1, &a, &b, &c, &d))
			g_mmxPresent = (d >> 23 & 1) != 0;
	}
	return g_mmxPresent;
}

// The 2-byte kBlend of @p n pixels, 4 at a time, as the table path computes it. Per channel (v the pixel's channel, f the
// colour cut to the channel, a the coverage): (v * (255 - a) + f * a + 127) / 255, with the division by 255 as
// (y + 1 + (y >> 8)) >> 8, which is exact for every y below 65535 (here y <= 63 * 255 + 127). Coverage 0 gives v and 255
// gives f, so the formula needs no case but "coverage 0 leaves the pixel alone" (alpha bits too): a mask keeps it. Blocks
// of four zero coverages are skipped, of four 255s stored from the palette without arithmetic. Returns the pixels done
// (a multiple of 4); the caller finishes the tail. Every path out of the MMX region goes through the one _mm_empty().
__attribute__((target("mmx"), noinline))
int composeBlend16MMX(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n, const byte *pal,
                      Blend16 *table, int mode) {
	const int rS = fmt.rShift, gS = fmt.gShift, bS = fmt.bShift, rL = fmt.rLoss, gL = fmt.gLoss, bL = fmt.bLoss;
	const __m64 zero = _mm_setzero_si64(), c255 = _mm_set1_pi16(255), c127 = _mm_set1_pi16(127), one = _mm_set1_pi16(1);
	const __m64 rMask = _mm_set1_pi16((short)((1u << (8 - rL)) - 1)), gMask = _mm_set1_pi16((short)((1u << (8 - gL)) - 1)),
	            bMask = _mm_set1_pi16((short)((1u << (8 - bL)) - 1));
	const __m64 alpha = _mm_set1_pi16((short)fmt.RGBToColor(0, 0, 0));
	const __m64 rSc = _mm_cvtsi32_si64(rS), gSc = _mm_cvtsi32_si64(gS), bSc = _mm_cvtsi32_si64(bS);
	int lastIndex = -1, bcIndex = -1;
	uint16 solid = 0;
	__m64 bcR = zero, bcG = zero, bcB = zero;
	int i = 0;
	for (; i + 4 <= n; i += 4, dst += 8) {
		uint32 cov4;
		memcpy(&cov4, coverage + i, 4);
		if (!cov4)
			continue;
		if (cov4 == 0xFFFFFFFFu) {
			for (int k = 0; k < 4; ++k) {
				const byte *c = pal + index[i + k] * 3;
				if (index[i + k] != lastIndex) {
					lastIndex = index[i + k];
					solid = (uint16)fmt.RGBToColor(c[0], c[1], c[2]);
				}
				WRITE_UINT16(dst + 2 * k, solid);
			}
			continue;
		}
		if (mode == 1) {
			// Runs only: partial blocks pixel by pixel through the table.
			for (int k = 0; k < 4; ++k) {
				const byte cv = coverage[i + k];
				if (!cv)
					continue;
				const byte *c = pal + index[i + k] * 3;
				if (cv == 255) {
					if (index[i + k] != lastIndex) {
						lastIndex = index[i + k];
						solid = (uint16)fmt.RGBToColor(c[0], c[1], c[2]);
					}
					WRITE_UINT16(dst + 2 * k, solid);
				} else {
					WRITE_UINT16(dst + 2 * k, table->blend(READ_UINT16(dst + 2 * k), c[0], c[1], c[2], cv));
				}
			}
			continue;
		}
		const byte *c0 = pal + index[i] * 3, *c1 = pal + index[i + 1] * 3, *c2 = pal + index[i + 2] * 3,
		           *c3 = pal + index[i + 3] * 3;
		__m64 d, cv;
		memcpy(&d, dst, 8);
		cv = _mm_cvtsi32_si64((int)cov4);
		const __m64 a = _mm_unpacklo_pi8(cv, zero);
		const __m64 ia = _mm_sub_pi16(c255, a);
		__m64 fr, fg, fb;
		if (index[i] == index[i + 1] && index[i] == index[i + 2] && index[i] == index[i + 3]) {
			// One colour for the four pixels (a text run): the broadcast vectors are kept while the index stays.
			if (index[i] != bcIndex) {
				bcIndex = index[i];
				bcR = _mm_set1_pi16((short)(c0[0] >> rL));
				bcG = _mm_set1_pi16((short)(c0[1] >> gL));
				bcB = _mm_set1_pi16((short)(c0[2] >> bL));
			}
			fr = bcR, fg = bcG, fb = bcB;
		} else {
			fr = _mm_set_pi16(c3[0] >> rL, c2[0] >> rL, c1[0] >> rL, c0[0] >> rL);
			fg = _mm_set_pi16(c3[1] >> gL, c2[1] >> gL, c1[1] >> gL, c0[1] >> gL);
			fb = _mm_set_pi16(c3[2] >> bL, c2[2] >> bL, c1[2] >> bL, c0[2] >> bL);
		}
		__m64 out = alpha;
		for (int ch = 0; ch < 3; ++ch) {
			const __m64 sc = ch == 0 ? rSc : ch == 1 ? gSc : bSc;
			const __m64 mk = ch == 0 ? rMask : ch == 1 ? gMask : bMask;
			const __m64 f = ch == 0 ? fr : ch == 1 ? fg : fb;
			const __m64 v = _mm_and_si64(_mm_srl_pi16(d, sc), mk);
			__m64 y = _mm_add_pi16(_mm_add_pi16(_mm_mullo_pi16(v, ia), _mm_mullo_pi16(f, a)), c127);
			const __m64 q = _mm_srli_pi16(_mm_add_pi16(_mm_add_pi16(y, one), _mm_srli_pi16(y, 8)), 8);
			out = _mm_or_si64(out, _mm_sll_pi16(q, sc));
		}
		const __m64 keep = _mm_cmpeq_pi16(a, zero);
		const __m64 res = _mm_or_si64(_mm_and_si64(keep, d), _mm_andnot_si64(keep, out));
		memcpy(dst, &res, 8);
	}
	_mm_empty();
	return i;
}

} // End of anonymous namespace

bool mmxSupported() {
	return probeMMX();
}

bool setUseMMX(bool on) {
	g_useMMX = on && probeMMX();
	return g_useMMX;
}

bool useMMX() {
	return g_useMMX;
}

void setMMXMode(int mode) {
	g_mmxMode = mode;
}
#else
bool mmxSupported() {
	return false;
}

bool setUseMMX(bool) {
	return false;
}

bool useMMX() {
	return false;
}

void setMMXMode(int) {
}
#endif

void composeSpan(byte *dst, const PixelFormat &fmt, const byte *index, const byte *coverage, int n,
                 const byte *paletteRGB, Mode mode, Blend16 *table) {
	const int bpp = fmt.bytesPerPixel;
	Common::ScopedPtr<Blend16> own;
	if (table && !Blend16::supports(fmt))
		table = nullptr;
	if (bpp == 2 && mode == kBlend && !table && Blend16::supports(fmt)) {
		own.reset(new Blend16(fmt));
		table = own.get();
	} else if (bpp != 2) {
		table = nullptr;
	}
	if (table && mode == kBlend) {
		// The 2-byte blend, the hot path on DOS (15/16-bit VBE): the colour of the last index is kept for coverage 255.
		int lastIndex = -1;
		uint16 solid = 0;
		int i = 0;
#ifdef TEXT_COMPOSE_HAVE_MMX
		if (g_useMMX && n >= 4) {
			i = composeBlend16MMX(dst, fmt, index, coverage, n, paletteRGB, table, g_mmxMode);
			dst += 2 * i;
		}
#endif
		for (; i < n; ++i, dst += 2) {
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
	if (!Blend16::supports(fmt))
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
	Common::ScopedPtr<Blend16> table(Blend16::supports(dst.format) && mode == kBlend ? new Blend16(dst.format) : nullptr);
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
	Common::ScopedPtr<Blend16> table(Blend16::supports(dst.format) && mode == kBlend ? new Blend16(dst.format) : nullptr);
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
