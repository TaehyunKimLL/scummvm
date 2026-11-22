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

#ifndef SCI_TEXTMODE_H
#define SCI_TEXTMODE_H

#include "common/language.h"
#include "common/str.h"

#include "sci/detection.h"

namespace Sci {

/**
 * How a game's text is held and drawn, decided once at engine start (DESIGN.md
 * section 3 policy 1). Upstream behaviour is kept whenever encoding is not kUtf8.
 */
struct TextMode {
	enum Encoding {
		kSingle, ///< the game's own single-byte text (upstream)
		kCp949,  ///< an upstream CP949 fan pack: font 1001, nothing of the fork
		kUtf8    ///< a fork translation: UTF-8 heap, ScriptStrings, hi-res text, Korean input
	};

	TextMode() : encoding(kSingle), hires(false), language(Common::UNK_LANG) {}

	Encoding encoding;
	bool hires;                 ///< text drawn into the 2x text plane
	Common::Language language;  ///< what SciEngine::getLanguage() answers

	bool utf8() const { return encoding == kUtf8; }
};

/** What decideTextMode() looks at; filled from the descriptor, ConfMan and the game folder. */
struct TextModeInputs {
	TextModeInputs() : detected(Common::UNK_LANG), requested(Common::UNK_LANG), koreanText(true),
		translationFolder(false), stringTable(false), hiresText(true), version(SCI_VERSION_NONE) {}

	Common::Language detected;   ///< the detection entry's language
	Common::Language requested;  ///< the target's language= (UNK_LANG: not set or unknown)
	bool koreanText;             ///< korean_text ([scummvm] off switch)
	Common::String textEncoding; ///< text_encoding: auto (or empty), utf8, cp949
	bool translationFolder;      ///< the requested language's folder (ko/) holds a translation
	bool stringTable;            ///< that folder (or the game folder) holds sci-<code>.str
	bool hiresText;              ///< hires_text
	SciVersion version;
};

/** The text mode for @p in; @p notes gets one line per decision a user may wonder about. */
TextMode decideTextMode(const TextModeInputs &in, Common::String &notes);

} // End of namespace Sci

#endif
