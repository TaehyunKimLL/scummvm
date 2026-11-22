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

#include "sci/textmode.h"

namespace Sci {

TextMode decideTextMode(const TextModeInputs &in, Common::String &notes) {
	TextMode mode;
	notes.clear();

	// The target's language wins over the detection entry only when the game
	// folder has a translation for it: language=ko on a plain English folder
	// changes nothing.
	mode.language = in.detected;
	if (in.requested != Common::UNK_LANG && in.requested != in.detected) {
		if (in.translationFolder)
			mode.language = in.requested;
		else
			notes += "language set, but the game folder has no translation for it; ";
	}

	const bool korean = mode.language == Common::KO_KOR;
	Common::String enc = in.textEncoding;
	enc.trim();
	enc.toLowercase();
	if (!enc.empty() && enc != "auto" && enc != "utf8" && enc != "utf-8" && enc != "cp949") {
		notes += Common::String::format("text_encoding=%s is not auto, utf8 or cp949; auto used; ", in.textEncoding.c_str());
		enc = "auto";
	}

	if (!in.koreanText) {
		// The off switch keeps upstream exactly: a CP949 fan entry stays CP949.
		mode.encoding = (korean && in.detected == Common::KO_KOR) ? TextMode::kCp949 : TextMode::kSingle;
		if (mode.language != in.detected)
			mode.language = in.detected;
		notes += "korean_text=false; ";
		return mode;
	}

	if (enc == "utf8" || enc == "utf-8")
		mode.encoding = TextMode::kUtf8;
	else if (enc == "cp949")
		mode.encoding = TextMode::kCp949;
	else if (korean && in.stringTable)
		mode.encoding = TextMode::kUtf8;
	else if (korean && in.translationFolder && mode.language == in.requested) {
		// A translation folder is a translation even when its table is missing or
		// misspelt: never the silent CP949 path of an English game.
		mode.encoding = TextMode::kUtf8;
		notes += Common::String::format("translation folder has no sci-%s.str; ", Common::getLanguageFolderCode(mode.language));
	}
	else if (korean)
		mode.encoding = TextMode::kCp949;
	else
		mode.encoding = TextMode::kSingle;

	if (mode.utf8()) {
		if (in.version < SCI_VERSION_0_EARLY || in.version > SCI_VERSION_1_1)
			notes += "hi-res text needs SCI0 to SCI1.1; ";
		else if (!in.hiresText)
			notes += "hires_text=false; ";
		else if (!korean)
			notes += "hi-res text is drawn for Korean only (the upscaled driver row); ";
		else
			mode.hires = true;
	}
	return mode;
}

} // End of namespace Sci
