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

#include "sci/textencoding.h"

namespace Sci {

TextEncodingSetting parseTextEncoding(const Common::String &value, bool &known) {
	known = true;
	if (value.empty() || value.equalsIgnoreCase("auto"))
		return kTextEncodingAuto;
	if (value.equalsIgnoreCase("euc-kr") || value.equalsIgnoreCase("cp949"))
		return kTextEncodingEucKr;
	if (value.equalsIgnoreCase("utf8") || value.equalsIgnoreCase("utf-8"))
		return kTextEncodingUtf8;
	if (value.equalsIgnoreCase("ascii"))
		return kTextEncodingAscii;
	known = false;
	return kTextEncodingAuto;
}

const char *textEncodingName(TextEncodingSetting enc) {
	switch (enc) {
	case kTextEncodingEucKr:
		return "euc-kr";
	case kTextEncodingUtf8:
		return "utf-8";
	case kTextEncodingAscii:
		return "ascii";
	case kTextEncodingAuto:
	default:
		return "auto";
	}
}

Common::CodePage textEncodingCodePage(TextEncodingSetting enc, Common::CodePage languageCodePage) {
	switch (enc) {
	case kTextEncodingEucKr:
		return Common::kWindows949;
	case kTextEncodingAscii:
		return Common::kLatin1;
	case kTextEncodingUtf8:
		// The heap is UTF-8 and walked as such (heapStringsAreUtf8()); the
		// code page is still what the language's fonts are keyed by.
	case kTextEncodingAuto:
	default:
		return languageCodePage;
	}
}

bool textEncodingHeapIsUtf8(TextEncodingSetting enc, bool autoAnswer) {
	switch (enc) {
	case kTextEncodingUtf8:
		return true;
	case kTextEncodingEucKr:
	case kTextEncodingAscii:
		return false;
	case kTextEncodingAuto:
	default:
		return autoAnswer;
	}
}

bool textEncodingKorean(TextEncodingSetting enc, Common::Language language) {
	switch (enc) {
	case kTextEncodingEucKr:
		return true;
	case kTextEncodingAscii:
		return false;
	case kTextEncodingUtf8:
	case kTextEncodingAuto:
	default:
		return language == Common::KO_KOR;
	}
}

} // End of namespace Sci
