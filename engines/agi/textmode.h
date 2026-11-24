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


#ifndef AGI_TEXTMODE_H
#define AGI_TEXTMODE_H

#include "common/language.h"
#include "common/str.h"

namespace Agi {

/**
 * How an AGI game's text is held and drawn, decided once at engine start
 * (DESIGN.md section 3 policy 1). Upstream behaviour is kept whenever
 * encoding is kSingle: every new branch of the engine tests utf8().
 */
struct TextMode {
	enum Encoding {
		kSingle, ///< the game's own single-byte text (upstream; Russian, Hebrew, French fan fonts included)
		kUtf8    ///< a fork translation: agi-<code>.str, UTF-8 strings, cells = code points, hi-res text, Korean input
	};

	TextMode() : encoding(kSingle), hires(false), language(Common::UNK_LANG) {}

	Encoding encoding;
	bool hires;                 ///< 16x16 face stencils in the 640x400 display
	Common::Language language;  ///< what AgiBase::getLanguage() answers

	bool utf8() const { return encoding == kUtf8; }
};

/** What decideTextMode() looks at; filled from the descriptor, ConfMan and the game folder. */
struct TextModeInputs {
	TextModeInputs() : detected(Common::UNK_LANG), requested(Common::UNK_LANG), koreanText(true),
		translationFolder(false), stringTable(false), hiresText(true) {}

	Common::Language detected;   ///< the detection entry's language
	Common::Language requested;  ///< the target's language= (UNK_LANG: not set or unknown)
	bool koreanText;             ///< korean_text ([scummvm] off switch)
	Common::String textEncoding; ///< text_encoding: auto (or empty), utf8; cp949 is not an AGI encoding
	bool translationFolder;      ///< the requested language's folder (ko/) holds a translation
	bool stringTable;            ///< that folder (or the game folder) holds agi-<code>.str
	bool hiresText;              ///< hires_text
};

/** The text mode for @p in; @p notes gets one line per decision a user may wonder about. */
TextMode decideTextMode(const TextModeInputs &in, Common::String &notes);

} // End of namespace Agi

#endif
