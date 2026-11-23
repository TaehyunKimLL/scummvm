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

#ifndef SCUMM_KOREAN_TEXT_H
#define SCUMM_KOREAN_TEXT_H

#include "common/array.h"
#include "common/scummsys.h"
#include "common/str-enc.h"

namespace Scumm {

/**
 * Korean text of the SCUMM engine (v1-v6), held as UTF-8 (DESIGN.md section 3).
 *
 * In Korean mode every message string the engine holds is UTF-8 bytes; the SCUMM escapes stay as they are: 0xFF or
 * 0xFE, a code, and two argument bytes unless the code is 1, 2, 3 or 8 (resStrLen(), convertMessageToString()).
 * Neither 0xFE nor 0xFF occurs in UTF-8, so an escape is found without knowing the text around it. CP949 is an input
 * format only: a CP949 bundle and the Korean inside MI1/DOTT's data are converted once, by toEngineText(), where text
 * enters the engine (ScummEngine::translateText()). korean.fnt is indexed through kKsx1001Hangul: no encoding.dat.
 */

/** The 2350 KS X 1001 Hangul syllables in korean.fnt glyph order (CP949 B0A1..C8FE), ascending code points. */
extern const uint16 kKsx1001Hangul[2350];

/** The korean.fnt glyph number of @p cp, or -1 when the font has none. */
int ksx1001Index(uint32 cp);

/** The code point of the CP949 pair if it is one of the 2350 syllables, else 0. */
uint32 ksx1001CodePoint(byte lead, byte trail);

/** Bytes of the escape at @p p (0xFF/0xFE + code [+ 2]) clipped to @p end; 0 when @p p is no escape. */
int scummEscapeLength(const byte *p, const byte *end);

/**
 * Like scummEscapeLength() but strict: 0 when @p p is no escape, -1 when the escape (4 bytes, or 2 for the codes 1, 2, 3, 8)
 * does not fit before @p end, else its length. A truncated escape must not be copied: its NUL would be read as an argument.
 */
int scummEscapeFull(const byte *p, const byte *end);

/** The NUL that ends the text at @p s, a 0xFF escape skipped (its argument byte may be 0; 0xFE is also a CP949 trail byte), or @p end. */
const byte *scummTextEnd(const byte *s, const byte *end);

/** One strict UTF-8 code point at @p p and its length; 0xFFFFFFFF (length 1) for a byte that starts none. */
uint32 decodeUtf8(const byte *p, const byte *end, int &len);

/** Appends the UTF-8 of @p cp. */
void appendUtf8(uint32 cp, Common::Array<byte> &out);

/** Whether [@p p, @p end) (escapes skipped) is valid UTF-8 engine text; plain ASCII is. A 2-byte character above U+00FF is not
 * (it is far likelier a CP949 syllable such as C4 AB). */
bool textIsUtf8(const byte *p, const byte *end);

/**
 * CP949 [@p src, @p end) to UTF-8, appended to @p out (no NUL), by upstream's rule: escapes (0xFF/0xFE) copied, a
 * lead 0xB0-0xD0 takes the next byte (so a trail 0xFE is no escape), any other byte >= 0x80 is a charset glyph and
 * becomes U+0080..U+00FF. The 2350 syllables come from kKsx1001Hangul; Hanja and extension syllables through
 * encoding.dat when it is there; what cannot be read becomes '?', counted in @p unmapped. An escape cut by @p end is dropped,
 * or copied as it is when @p keepCutEscape (a bundle string whose terminator is its last argument).
 */
void cp949ToUtf8(const byte *src, const byte *end, Common::Array<byte> &out, uint &unmapped, bool keepCutEscape = false);

/**
 * The engine-text boundary: copies the text at @p src (up to its NUL, escape-aware) into @p dst as UTF-8. Text that is
 * already valid UTF-8 (a translation, a stored resource, ASCII) is copied unchanged, anything else is read as CP949.
 * So calling it twice changes nothing. At most @p dstSize - 1 bytes are written, cut after the last whole character
 * or escape that fits, then a NUL. Returns the length written.
 */
int toEngineText(const byte *src, int srcMax, byte *dst, int dstSize, uint *unmapped = nullptr);

/** The code page engine text of a Korean target is in: UTF-8 for v1-v6 (\p utf8), CP949 for The Dig, FT and COMI. */
Common::CodePage koreanEngineCodePage(bool utf8);

/** The jongsung bits of the last Hangul syllable in [@p p, @p end) (bit 0: a final, bit 1: ㄹ); 0 without one. */
int koreanFinalBits(const byte *p, const byte *end);

} // End of namespace Scumm

#endif
