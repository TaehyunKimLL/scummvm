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

#ifndef SCUMM_KOREAN_SENTENCE_H
#define SCUMM_KOREAN_SENTENCE_H

#include "common/language.h"
#include "common/platform.h"
#include "common/str.h"

namespace Scumm {

/**
 * The SCUMM v2 sentence line (Maniac Mansion, Zak McKracken) in Korean.
 *
 * ScummEngine_v2::drawSentence() builds the line itself: verb, object A, a hard-coded preposition, object B, in
 * English order ("Give ticket to King"). A Korean bundle translates the parts but cannot reorder them, so for a
 * Korean target with a bundle the parts go through composeKoreanSentence(): verb last, the preposition turned into a
 * particle on B ("왕에게 티켓 주기"). Text is UTF-8, the engine's Korean text
 * encoding (korean_text.h). Pure functions: nothing here needs a running engine.
 */

enum KoreanFinal {
	kKoFinalNone,       ///< vowel-final syllable; also Latin letters, digits, symbols
	kKoFinalConsonant,  ///< final consonant other than ㄹ
	kKoFinalRieul,      ///< final ㄹ (takes 로, not 으로)
	kKoFinalUnknown     ///< nothing to judge by (the "..." placeholder): both forms
};

enum KoreanJosa {
	kJosaNone,
	kJosaObject,   ///< 을/를
	kJosaTopic,    ///< 은/는
	kJosaSubject,  ///< 이/가
	kJosaAnd,      ///< 과/와
	kJosaMeans,    ///< (으)로
	kJosaAt,       ///< 에
	kJosaTo        ///< 에게
};

/** Bytes of the UTF-8 character at @p p (never past a NUL). */
int koreanCharLength(const char *p);

/** The final of the last character of @p noun ('@' padding and end spaces skipped). */
KoreanFinal koreanFinalOf(const Common::String &noun);

/** The particle @p josa in the form @p final takes (UTF-8). */
Common::String koreanJosa(KoreanFinal final, KoreanJosa josa);

/** One v2 sentence, each part already translated. */
struct KoreanSentenceParts {
	Common::String verb;
	Common::String objA;
	Common::String objB;
	bool hasA = false;
	bool hasB = false;
	int prep = 0;              ///< v2 preposition: 0 none, 1 in, 2 with, 3 on, 4 to
	bool bIsActor = false;     ///< B is a person (to: 에게, else 에)
	bool instrumentB = false;  ///< with: B is the tool (Unlock/Fix ... with key)
};

/** The sentence in Korean order ("<B>에게 <A> <verb>"; an unchosen B is "..." with both particle forms). */
Common::String composeKoreanSentence(const KoreanSentenceParts &parts);

typedef int (*KoreanWidthFunc)(const Common::String &text, void *ctx);

/**
 * composeKoreanSentence() fitted to @p maxWidth as @p width measures it: the wider object name loses its last
 * character until the line fits (the particle stays the full name's); if the names cannot shrink further, the line
 * is cut after its last whole character that fits. Never cuts inside a character.
 */
Common::String fitKoreanSentence(const KoreanSentenceParts &parts, int maxWidth, KoreanWidthFunc width, void *ctx);

/** A v2 game (not v1, not NES) in Korean with a bundle loaded. Everything else keeps the English-order line. */
bool koreanSentenceApplies(int version, Common::Platform platform, Common::Language language, bool hasBundle);

/** Whether @p verb has a non-ASCII byte: a bundle that left verbs English keeps the English order. */
bool koreanVerbTranslated(const Common::String &verb);

} // End of namespace Scumm

#endif
