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

#ifndef COMMON_HANGUL_H
#define COMMON_HANGUL_H

#include "common/str.h"
#include "common/ustr.h"

namespace Common {

/**
 * Two-beolsik Hangul input composer.
 *
 * An operating system input method delivers finished syllables as
 * Common::EVENT_KEYDOWN with KEYCODE_INVALID and the code point in ascii, but
 * no composition (pre-edit) events, and a backend without an input method
 * delivers only keys. This class does the composition itself: it maps QWERTY
 * keys to jamo and runs the choseong/jungseong/jongseong automaton, emitting
 * precomposed U+AC00 syllables.
 *
 * The composer keeps the syllable currently being assembled separate from the
 * committed text so the caller can draw it as a live pre-edit character.
 *
 * It is deliberately free of any encoding knowledge: it emits code points, and
 * a caller that needs bytes converts them into the encoding its text is kept
 * in (an engine's text mode: UTF-8, or CP949 for a CP949 pack).
 */
class HangulComposer {
public:
	HangulComposer() { reset(); }

	/** Drop all state, committed and pending. */
	void reset();

	/** True if @p ascii is a key that maps to a jamo. */
	static bool isJamoKey(char ascii);

	/**
	 * Feed one ASCII key.
	 * Non-jamo keys flush the pending syllable and are appended verbatim.
	 * @return true if the key was consumed as Hangul.
	 */
	bool feed(char ascii);

	/** Delete one jamo, then one syllable, mirroring native IME behaviour. */
	void backspace();

	/** Commit the pending syllable, if any. */
	void flush();

	/** Committed text plus the syllable in progress. */
	U32String text() const;

	/** The syllable currently being assembled, or 0 when there is none. */
	uint32 preedit() const { return composeSyllable(); }

	/** True when nothing at all has been typed. */
	bool empty() const { return _out.empty() && !_cho && !_jung && !_jong; }

private:
	uint32 composeSyllable() const;

	U32String _out;
	// indices into the standard jamo tables, 0 means "not set"
	int _cho;   // 1..19
	int _jung;  // 1..21
	int _jong;  // 1..27
};

/**
 * Pick the Korean particles a translation leaves open after an inserted
 * word: "%s을(를) 열 수 없다." becomes "문을 열 수 없다." or "상자를 열 수
 * 없다." by the last syllable's final consonant. The markers are 을(를)
 * 이(가) 은(는) 과(와) with either form first, (이) and (으); a digit is read
 * as its Korean numeral and a Latin letter by l, m, n ending in a consonant.
 * Quotes and brackets between the word and the marker are looked through.
 * UTF-8 in and out; the result is never longer. (Was
 * Sci::ScriptStrings::resolveParticles.)
 */
void resolveKoreanParticles(String &s);

/**
 * Bring typed Korean text to the form a word table holds (DESIGN.md section 6):
 * conjoining jamo (U+1100..U+1112, U+1161..U+1175, U+11A8..U+11C2), as an input
 * method that sends decomposed text gives them, are composed into precomposed
 * syllables by the Unicode Hangul composition (a syllable without a final also
 * takes a following final jamo); full-width ASCII (U+FF01..U+FF5E) becomes ASCII
 * and the ideographic space (U+3000) a space. Everything else, compatibility
 * jamo (U+3131..) included, is kept. Case is not folded (the engine's parser
 * does that).
 */
void normalizeKoreanInput(U32String &s);

} // End of namespace Common

#endif
