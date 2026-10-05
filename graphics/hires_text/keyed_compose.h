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


#ifndef GRAPHICS_HIRES_TEXT_KEYED_COMPOSE_H
#define GRAPHICS_HIRES_TEXT_KEYED_COMPOSE_H

#include "common/scummsys.h"

namespace Graphics {

/**
 * Keyed hi-res text over a game picture on a paletted screen: every pixel of
 * the text plane that is not @p key is the text's index, every one that is
 * shows the game pixel under it, each game pixel repeated @p m times across
 * and down. What a compositor that only keys (no coverage, no decoration)
 * writes, as one select per pixel rather than runs of kinds.
 */
namespace KeyedCompose {

/**
 * @param dst       width x m bytes per row, height x m rows, packed
 * @param src       the game picture, width bytes per row, @p srcSkip bytes after each
 * @param text      the text plane at the output size, @p textSkip bytes after each row
 */
typedef void (*RowsFn)(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
					   int width, int height, int m, byte key);

/** The reference: plain C++, any m. */
void rowsScalar(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
				int width, int height, int m, byte key);

/**
 * The same with MMX for m == 2 (rowsScalar() for any other m), byte for byte
 * its output. Only call it when haveMmx() says so; null where it is not
 * built (not x86 GCC). Leaves the FPU usable (EMMS).
 */
extern const RowsFn rowsMmx;

/** The CPU has MMX (CPUID, after the EFLAGS ID-bit check a 486 needs). */
bool haveMmx();

/**
 * Whether rows() may use MMX (default true). A platform where MMX
 * instructions fault although the CPU has them (CR0.EM set: the DPMI host
 * emulates the FPU) turns it off before the first rows(); @p why goes in the
 * log. Takes effect at the next rows().
 */
void setMmxAllowed(bool allowed, const char *why = nullptr);

/** rows() runs MMX: built, allowed and haveMmx(). */
bool usesMmx();

/** rowsMmx when usesMmx(), else rowsScalar; chosen at the first call. */
void rows(byte *dst, const byte *src, int srcSkip, const byte *text, int textSkip,
		  int width, int height, int m, byte key);

} // End of namespace KeyedCompose
} // End of namespace Graphics

#endif
