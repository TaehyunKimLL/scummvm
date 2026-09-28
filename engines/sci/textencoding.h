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

#ifndef SCI_TEXTENCODING_H
#define SCI_TEXTENCODING_H

#include "common/language.h"
#include "common/str.h"
#include "common/str-enc.h"

namespace Sci {

/**
 * The text_encoding ini key: what the bytes of the game's text mean,
 * stated by the player instead of inferred. Same name and values as the
 * AGS key (engines/ags/engine/ac/translation.cpp): auto (default), euc-kr,
 * cp949, utf8, utf-8, ascii.
 *
 * Precedence: an explicit value beats detection, the Korean Text.MAP
 * overlay and the sci-<lang>.str manifest. `auto` is exactly the behaviour
 * without the key.
 */
enum TextEncodingSetting {
	kTextEncodingAuto,	///< decided by language=, detection and manifests, as before
	kTextEncodingEucKr,	///< euc-kr / cp949: the Korean double-byte path, whatever language= says
	kTextEncodingUtf8,	///< utf8 / utf-8: the heap holds UTF-8
	kTextEncodingAscii	///< ascii: single-byte text, no double-byte path at all
};

/**
 * Parse a text_encoding value (case-insensitive). An empty or unknown value
 * is kTextEncodingAuto; @p known is false for an unknown one, so the caller
 * can warn.
 */
TextEncodingSetting parseTextEncoding(const Common::String &value, bool &known);

/** The ini spelling of a setting ("auto", "euc-kr", "utf-8", "ascii"). */
const char *textEncodingName(TextEncodingSetting enc);

/**
 * The code page double-byte text is decoded with, given the setting and the
 * language (getLanguage()). @p languageCodePage is what the language alone
 * gives - the auto answer.
 */
Common::CodePage textEncodingCodePage(TextEncodingSetting enc, Common::CodePage languageCodePage);

/** Whether heap strings are UTF-8; @p autoAnswer is the detection/manifest answer. */
bool textEncodingHeapIsUtf8(TextEncodingSetting enc, bool autoAnswer);

/**
 * Whether the Korean text path is on - EUC-KR decoding, korean.fnt / font
 * banks, the font 1001 switch, the upscaled driver. euc-kr turns it on with
 * no language= at all; ascii turns it off; otherwise it follows KO_KOR.
 */
bool textEncodingKorean(TextEncodingSetting enc, Common::Language language);

} // End of namespace Sci

#endif // SCI_TEXTENCODING_H
