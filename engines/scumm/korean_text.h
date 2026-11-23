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

} // End of namespace Scumm

#endif
