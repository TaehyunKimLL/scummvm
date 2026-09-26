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

#ifndef GRAPHICS_HIRES_TEXT_CODEPAGE_KR_H
#define GRAPHICS_HIRES_TEXT_CODEPAGE_KR_H

#include "common/scummsys.h"

namespace Graphics {

/**
 * The Korean code page as the fan patches write it: EUC-KR (the KS X 1001
 * part of CP949), a lead byte and a trail byte both in 0xA1..0xFE.
 *
 * Engines keep such text as bytes and turn a pair into a code point only
 * at the glyph interface; legacy bitmap fonts order their glyphs by
 * KS X 1001 Hangul index. Both directions come from a static table of the
 * 2350 KS X 1001 Hangul syllables, so nothing here needs encoding.dat.
 */
namespace KoreanCodePage {

/** Syllables in the KS X 1001 Hangul block (rows 0xB0..0xC8, 94 each). */
static const int kKsx1001HangulCount = 2350;

/** Whether hi and lo are both in 0xA1..0xFE, i.e. form an EUC-KR pair. */
bool isEucKrPair(byte hi, byte lo);

/**
 * The KS X 1001 Hangul index of a pair: (hi-0xB0)*94 + (lo-0xA1) for hi in
 * 0xB0..0xC8 and lo in 0xA1..0xFE, else -1.
 */
int ksx1001HangulIndex(byte hi, byte lo);

/** The KS X 1001 Hangul index of code point cp, or -1 when cp is not one of
 *  the 2350 syllables (e.g. U+D7A3, which only CP949's extension has). */
int ksx1001HangulIndexOf(uint32 cp);

/**
 * The code point of an EUC-KR pair in the KS X 1001 Hangul block, or 0 for
 * any other pair (symbols, Hanja and CP949's extension are not in the
 * table).
 */
uint32 decodeEucKrPair(byte hi, byte lo);

} // End of namespace KoreanCodePage
} // End of namespace Graphics

#endif
