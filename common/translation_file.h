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

#ifndef COMMON_TRANSLATION_FILE_H
#define COMMON_TRANSLATION_FILE_H

#include "common/str.h"

namespace Common {

/**
 * The fingerprint header of a game translation file: the first line of a script string table (sci-ko.str,
 * agi-ko.str) and the header text of a version 3 parser word table (sci-ko.dat, agi-ko.dat):
 *
 *     #! format=1 game=<gameid> fingerprint=<md5> engine=<sci|agi> [vocab=<md5>]
 *
 * Fields are key=value, separated by spaces or tabs, in any order; a key of a later format is skipped. An md5 is 32
 * hex digits (written lower case, compared ignoring case). The fingerprint is the md5 an engine computes from the
 * game files the translation's keys depend on (which files is the engine's definition); vocab is the md5 of the
 * vocabulary resource a word table's groups come from.
 */
struct TranslationFileHeader {
	int format;         ///< 1; 0 when the file has no header
	String game;        ///< the game id
	String fingerprint; ///< md5, lower case
	String engine;      ///< "sci", "agi"
	String vocab;       ///< md5, lower case, or empty

	TranslationFileHeader() : format(0) {}

	bool present() const { return format != 0; }

	/**
	 * Read a header line (without its line end). A line not starting with "#!" is no header: false with @p error
	 * empty. A "#!" line that is not a valid format 1 header: false with @p error saying why. @p out is reset first.
	 */
	static bool parse(const String &line, TranslationFileHeader &out, String &error);

	/** The header line, fields in the order above, vocab only when set. */
	String toString() const;
};

/** What checkTranslationHeader() found. */
enum TranslationCheck {
	kTranslationMatches,   ///< every field the game named is the file's
	kTranslationUnchecked, ///< the file has no header (a pack made before headers): use it, unchecked
	kTranslationMismatch   ///< a field differs: do not use the file
};

/**
 * Compare a file's header with what the game expects. Every non-empty field of @p expected is compared: game and
 * engine exactly, fingerprint and vocab ignoring case. Warns once for a file without a header ("loaded unchecked")
 * and once for a mismatch (each differing field with both values, then "not loaded").
 */
TranslationCheck checkTranslationHeader(const TranslationFileHeader &found, const TranslationFileHeader &expected,
                                        const String &fileName);

/** Whether @p s is 32 hex digits. */
bool isMd5String(const String &s);

} // End of namespace Common

#endif
