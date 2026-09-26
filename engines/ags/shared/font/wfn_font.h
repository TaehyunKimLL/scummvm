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

//=============================================================================
//
// WFNFont - an immutable AGS font object.
//
//-----------------------------------------------------------------------------
//
// WFN format:
// - signature            ( 15 )
// - offsets table offset (  2 )
// - characters table (for unknown number of char items):
// -     width            (  2 )
// -     height           (  2 )
// -     pixel bits       ( (width / 8 + 1) * height )
// -     any unknown data
// - offsets table (for X chars):
// -     character offset (  2 )
//
// NOTE: unfortunately, at the moment the format does not provide means to
// know the number of supported characters for certain, and the size of the
// data (file) is used to determine that.
//
// Korean extension (extfntN.wfn, shipped by the Korean fan patches next to
// agsfntN.wfn): the same layout, but the offsets table offset and every
// character offset are 4 bytes, and the table holds exactly 2350 offsets,
// the KS X 1001 Hangul syllables in code order. Code points from 256 up that
// are one of those syllables draw the extension's glyph.
//
//=============================================================================

#ifndef AGS_SHARED_FONT_WFN_FONT_H
#define AGS_SHARED_FONT_WFN_FONT_H

#include "common/std/vector.h"
#include "ags/shared/core/types.h"

namespace AGS3 {

namespace AGS {
namespace Shared {
class Stream;
} // namespace Shared
} // namespace AGS

enum WFNError {
	kWFNErr_NoError,
	kWFNErr_BadSignature,
	kWFNErr_BadTableAddress,
	kWFNErr_HasBadCharacters,
	kWFNErr_BadCharCount      // extension: the table does not hold 2350 offsets
};

struct WFNChar {
	uint16_t       Width;
	uint16_t       Height;
	const uint8_t *Data;

	WFNChar();

	inline size_t GetRowByteCount() const {
		return (Width + 7) / 8;
	}

	inline size_t GetRequiredPixelSize() const {
		return GetRowByteCount() * Height;
	}

	// Ensure character's width & height fit in given number of pixel bytes
	void RestrictToBytes(size_t bytes);
};


class WFNFont {
public:
	inline uint16_t GetCharCount() const {
		return static_cast<uint16_t>(_refs.size());
	}

	// Get WFN character for the given code; if the character is missing, returns empty character.
	// With an extension loaded, a code from 256 up is looked up among its KS X 1001 syllables.
	const WFNChar &GetChar(uint32_t code) const;

	void Clear();
	// Reads WFNFont object, using data_size bytes from stream; if data_size = 0,
	// the available stream's length is used instead. Returns error code.
	WFNError ReadFromFile(AGS::Shared::Stream *in, const soff_t data_size = 0);

	// Reads the Korean extension (extfntN.wfn) from the whole stream; on any
	// error but kWFNErr_HasBadCharacters the extension is left unloaded.
	WFNError ReadExtFromFile(AGS::Shared::Stream *in);
	// Same, from memory; data is copied. Every offset is checked against size.
	WFNError ReadExtFromData(const uint8_t *data, size_t size);
	void ClearExt();
	inline bool HasExt() const {
		return !_extItems.empty();
	}
	inline size_t GetExtCharCount() const {
		return _extItems.size();
	}
	// Height of the extension's first glyph (U+AC00), 0 without an extension
	uint16_t GetExtHeight() const;

protected:
	std::vector<const WFNChar *> _refs;      // reference array, contains pointers to elements of _items
	std::vector<WFNChar>        _items;     // actual character items
	std::vector<uint8_t>        _pixelData; // pixel data array
	WFNChar                     _emptyChar; // substitutes bad and missing characters
	std::vector<WFNChar>        _extItems;  // extension: one item per KS X 1001 syllable
	std::vector<uint8_t>        _extData;   // extension: the file's bytes, items point into it
};

} // namespace AGS3

#endif
