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


#ifndef AGI_SAVED_STRINGS_H
#define AGI_SAVED_STRINGS_H

#include "common/endian.h"
#include "common/str.h"
#include "common/stream.h"

namespace Agi {

/**
 * The script strings of a save made in UTF-8 text mode (TextMode::kUtf8).
 *
 * The save stays version 11, readable by upstream ScummVM: its string table
 * holds each slot's longest prefix of whole characters that fits the 40-byte
 * field (legacySlot()). The whole UTF-8 strings and the English originals
 * compare.strings also accepts (AgiGame::stringAnswer) follow the version 11
 * body as a trailer, which upstream's loader never reads:
 *
 *     "AGKO" u8 version (1) u8 count, then count x (u8 length, bytes) for the
 *     strings, then count x (u8 length, bytes) for the answers.
 *
 * Every length is below the slot's byte stride and every string is valid
 * UTF-8, or the whole trailer is refused (the version 11 table is used).
 */
enum {
	kSavedStringsTag = MKTAG('A', 'G', 'K', 'O'),
	kSavedStringsVersion = 1,
	kLegacySlotBytes = 40 ///< MAX_STRINGLEN: a version 11 slot, terminator included
};

/** The version 11 slot for @p text: its longest prefix of whole UTF-8 characters of at most 39 bytes, NUL padded. */
void legacySlot(const char *text, char out[kLegacySlotBytes]);

/** The trailer for @p count slots of @p table (@p stride bytes apart) and @p answers. */
void writeSavedStrings(Common::WriteStream &out, const char *table, uint stride, uint count, const Common::String *answers);

enum SavedStringsResult {
	kSavedStringsNone,   ///< no trailer at the stream's position (an upstream save, or one made in English): nothing changed
	kSavedStringsRead,   ///< table and answers replaced
	kSavedStringsBroken  ///< a trailer that is cut or malformed: nothing changed
};

/** Reads the trailer at the stream's position into @p table and @p answers, all or nothing. */
SavedStringsResult readSavedStrings(Common::ReadStream &in, char *table, uint stride, uint count, Common::String *answers);

/**
 * A save without the trailer read in UTF-8 text mode: each of the @p count
 * slots holds the game's code page; turn a slot with bytes >= 0x80 into
 * UTF-8, cut to @p maxCells cells and @p stride - 1 bytes. ASCII stays as it is.
 */
void convertLegacyStrings(char *table, uint stride, uint count, int16 maxCells);

/**
 * What loadGame() does with the table after readSavedStrings(): a code page
 * table (no trailer at all) is converted; a table read from the trailer is
 * final, and a table that already holds UTF-8 (a Korean save written again by
 * an unmodified build, or one whose trailer is cut) is left as it is.
 */
void finishSavedStrings(SavedStringsResult result, char *table, uint stride, uint count, int16 maxCells);

} // End of namespace Agi

#endif
