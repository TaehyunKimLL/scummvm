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

#ifndef GRAPHICS_HIRES_TEXT_JAMO_SET_H
#define GRAPHICS_HIRES_TEXT_JAMO_SET_H

#include "common/array.h"
#include "common/str.h"
#include "common/types.h"

namespace Graphics {

/// Piece n of a jamo-set font is the glyph at this code point plus n
/// (plane 15, private use).
static const uint32 kJamoPieceBase = 0xF0000;

/**
 * The JSET block of a jamo-set SVFN (graphics/hires_text/README.md): how a
 * DOS Hangul system picked, for a syllable, the pieces (initial, medial,
 * final) it was drawn from. Parsed and validated here; the font's own
 * glyphs are not looked at (JamoGlyphSource::usable() checks those).
 *
 * The three selection tables are indexed (leadClass * 21 + vowel) * tailClasses
 * + tailClass, vowel 0..20 in Unicode order, and give a set; the piece
 * tables then give a piece number per (set, jamo).
 */
struct JamoSetTable {
	JamoSetTable();

	byte leadClasses, tailClasses, initSets, medSets, finSets, advance;
	byte leadClass[19], tailClass[28];	///< tailClass[0]: the class of "no final"
	Common::Array<byte> initSet, medSet, finSet;	///< LC * 21 * TC each
	Common::Array<uint16> initPiece, medPiece, finPiece;	///< NI * 19, NM * 21, NF * 27
	uint16 compatPiece[51];

	/**
	 * Pieces (piece numbers) of syllable @p cp in draw order: initial, medial
	 * and, when it has a final, final (@p count 2 or 3). A piece number may
	 * be 0xFFFF, a blank. False if @p cp is not a syllable.
	 */
	bool pieces(uint32 cp, uint16 out[3], int &count) const;

	/// Piece number for compatibility jamo @p cp (U+3131..U+3163), or 0xFFFF.
	uint16 compat(uint32 cp) const;
};

/// Bytes a JSET block with these counts takes.
uint32 jamoSetSize(uint lead, uint tail, uint init, uint med, uint fin);

/// Parses @p size bytes at @p data; false with @p error set (naming the field) when malformed.
bool parseJamoSetTable(const byte *data, uint32 size, JamoSetTable &out, Common::String &error);

} // End of namespace Graphics

#endif
