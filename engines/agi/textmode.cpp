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


#include "agi/textmode.h"

namespace Agi {

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

	Common::String enc = in.textEncoding;
	enc.trim();
	enc.toLowercase();
	if (!enc.empty() && enc != "auto" && enc != "utf8" && enc != "utf-8") {
		notes += Common::String::format("text_encoding=%s is not auto or utf8 for AGI; auto used; ", in.textEncoding.c_str());
		enc = "auto";
	}

	if (!in.koreanText) {
		mode.language = in.detected;
		notes += "korean_text=false; ";
		return mode;
	}

	const bool translated = mode.language != in.detected;
	if (enc == "utf8" || enc == "utf-8") {
		// A global text_encoding=utf8 (the Korean text switch writes it for every game)
		// is for games with a translation; a plain game keeps its code page unless
		// its own section asks.
		if (in.translationFolder || in.stringTable || in.textEncodingOwn)
			mode.encoding = TextMode::kUtf8;
		else
			notes += "text_encoding=utf8 ignored: no translation for this game and the key is not in its own section; ";
	} else if (translated && in.stringTable)
		mode.encoding = TextMode::kUtf8;
	else if (translated && in.translationFolder) {
		// A translation folder is a translation even when its table is missing or misspelt.
		mode.encoding = TextMode::kUtf8;
		notes += "translation folder has no agi string table; ";
	}

	if (mode.utf8()) {
		if (!in.hiresText)
			notes += "hires_text=false: characters AGI's font lacks draw as '?'; ";
		else
			mode.hires = true;
	}
	return mode;
}

} // End of namespace Agi
