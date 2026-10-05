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


#include "graphics/hires_text/keyed_compose.h"
#include "common/debug.h"

// MMX: x86 only, built for the one function with the target attribute (the
// rest of the program stays plain i586: a Pentium 75 has no MMX).
// Not clang on i386: its <mmintrin.h> wants SSE2 there, not just MMX.
#if defined(__GNUC__) && !(defined(__clang__) && defined(__i386__)) && (defined(__i386__) || defined(__x86_64__))
#define KEYED_COMPOSE_MMX 1
#include <cpuid.h>
#include <mmintrin.h>

namespace {

/// m == 2, height > 0: eight game pixels doubled to sixteen, selected
/// against sixteen text pixels, per step; the rest of a row as the scalar
/// loop does it. Every path through it ends in the EMMS, and the cases that
/// need none are decided by rowsMmxImpl() outside it: the compiler may move
/// MMX instructions anywhere in this function.
__attribute__((target("mmx"), noinline))
void rowsMmxBody(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
				 int width, int height, byte key) {
	const __m64 keys = _mm_set1_pi8((char)key);
	const int outWidth = width * 2;
	const int blocks = width / 8;
	for (int h = 0; h < height * 2; ++h) {
		const byte *srcRow = src + (h / 2) * (width + srcSkip);
		for (int b = 0; b < blocks; ++b) {
			__m64 bg, t0, t1;
			memcpy(&bg, srcRow + 8 * b, 8);
			memcpy(&t0, text + 16 * b, 8);
			memcpy(&t1, text + 16 * b + 8, 8);
			const __m64 lo = _mm_unpacklo_pi8(bg, bg);	// pixels 0-3, each twice
			const __m64 hi = _mm_unpackhi_pi8(bg, bg);	// pixels 4-7
			const __m64 k0 = _mm_cmpeq_pi8(t0, keys);
			const __m64 k1 = _mm_cmpeq_pi8(t1, keys);
			const __m64 o0 = _mm_or_si64(_mm_and_si64(k0, lo), _mm_andnot_si64(k0, t0));
			const __m64 o1 = _mm_or_si64(_mm_and_si64(k1, hi), _mm_andnot_si64(k1, t1));
			memcpy(dst + 16 * b, &o0, 8);
			memcpy(dst + 16 * b + 8, &o1, 8);
		}
		for (int x = blocks * 8; x < width; ++x) {
			const byte bg = srcRow[x];
			const byte a = text[2 * x], c = text[2 * x + 1];
			dst[2 * x] = (a == key) ? bg : a;
			dst[2 * x + 1] = (c == key) ? bg : c;
		}
		dst += outWidth;
		text += outWidth + textSkip;
	}
	_mm_empty();	// EMMS: the x87 registers are the FPU's again
}

/// Plain code (no MMX): what the MMX body does not take goes to the scalar
/// loop, without any MMX register having been touched.
void rowsMmxImpl(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
				 int width, int height, int m, byte key) {
	if (m != 2 || height <= 0)
		Graphics::KeyedCompose::rowsScalar(dst, src, srcSkip, text, textSkip, width, height, m, key);
	else
		rowsMmxBody(dst, src, srcSkip, text, textSkip, width, height, key);
}

} // End of anonymous namespace
#endif

namespace Graphics {
namespace KeyedCompose {

void rowsScalar(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
				int width, int height, int m, byte key) {
	const int outWidth = width * m;
	for (int h = 0; h < height * m; ++h) {
		const byte *srcRow = src + (h / m) * (width + srcSkip);
		if (m == 2) {
			for (int x = 0; x < width; ++x) {
				const byte bg = srcRow[x];
				const byte t0 = text[2 * x], t1 = text[2 * x + 1];
				dst[2 * x] = (t0 == key) ? bg : t0;
				dst[2 * x + 1] = (t1 == key) ? bg : t1;
			}
		} else {
			int w = 0;
			for (int x = 0; x < width; ++x) {
				const byte bg = srcRow[x];
				for (int k = 0; k < m; ++k, ++w)
					dst[w] = (text[w] == key) ? bg : text[w];
			}
		}
		dst += outWidth;
		text += outWidth + textSkip;
	}
}

#ifdef KEYED_COMPOSE_MMX
const RowsFn rowsMmx = rowsMmxImpl;
#else
const RowsFn rowsMmx = nullptr;
#endif

static bool g_mmxAllowed = true;
static const char *g_mmxOffWhy = nullptr;
static RowsFn g_chosen = nullptr;

void setMmxAllowed(bool allowed, const char *why) {
	g_mmxAllowed = allowed;
	g_mmxOffWhy = allowed ? nullptr : why;
	g_chosen = nullptr;	// chosen again at the next rows()
}

bool haveMmx() {
#ifdef KEYED_COMPOSE_MMX
	// __get_cpuid() checks the highest leaf first, and on i386 that check
	// first flips EFLAGS.ID: a 486 without CPUID answers no instead of
	// faulting.
	unsigned int a, b, c, d;
	if (!__get_cpuid(1, &a, &b, &c, &d))
		return false;
	return (d & bit_MMX) != 0;
#else
	return false;
#endif
}

bool usesMmx() {
	return g_mmxAllowed && rowsMmx && haveMmx();
}

void rows(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
		  int width, int height, int m, byte key) {
	if (!g_chosen) {
		const bool mmx = usesMmx();
		g_chosen = mmx ? rowsMmx : rowsScalar;
		if (!g_mmxAllowed && rowsMmx)
			debug(1, "hi-res text: keyed compose scalar (MMX forced off: %s)",
				  g_mmxOffWhy ? g_mmxOffWhy : "by the platform");
		else
			debug(1, "hi-res text: keyed compose %s", mmx ? "MMX" : "scalar");
	}
	g_chosen(dst, src, srcSkip, text, textSkip, width, height, m, key);
}

} // End of namespace KeyedCompose
} // End of namespace Graphics
