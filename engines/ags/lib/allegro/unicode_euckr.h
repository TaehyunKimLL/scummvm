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


#ifndef AGS_LIB_ALLEGRO_UNICODE_EUCKR_H
#define AGS_LIB_ALLEGRO_UNICODE_EUCKR_H

#include "common/scummsys.h"

namespace AGS3 {

/**
 * The EUC-KR text format (U_EUCKR) of the Korean fan translations' legacy
 * .tra files. Text stays bytes; a KS X 1001 Hangul pair (both bytes in
 * 0xA1..0xFE) reads as one character, its Unicode code point, so the font
 * renderers look glyphs up by code point as they do under U_UTF8. Every
 * other byte - ASCII, AGS's '[' line break, Windows-1252 letters next to
 * ASCII, pairs outside the Hangul block - reads as itself, as under U_ASCII.
 *
 * Kept apart from unicode.cpp and free of the engine's globals so the unit
 * tests can link it alone.
 */
extern int euckr_getc(const char *s);
extern int euckr_getx(char **s);
extern int euckr_setc(char *s, int c);
extern int euckr_width(const char *s);
extern int euckr_cwidth(int c);
extern int euckr_isok(int c);

} // namespace AGS3

#endif
