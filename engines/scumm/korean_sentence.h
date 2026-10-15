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
 * The v2 engine builds the line itself: verb, object A, a hard-coded
 * preposition, object B, in English order ("Give ticket to King"). A Korean
 * translation can translate each part but not the order, so for a Korean
 * target drawSentence() hands the translated parts to the composer here,
 * which puts the verb last and turns the preposition into a particle on B
 * ("왕에게 티켓 주기"). Nothing here needs a running engine.
 *
 * Text is the engine's: CP949 bytes (the fan patches' korean.trs), or UTF-8
 * when the bundle is UTF-8. docs/superpowers/specs/2026-10-15-v2-korean-
 * sentence-design.md has the order and particle table.
 */

/** What a noun ends with, for choosing a particle. */
enum KoreanFinal {
	kKoFinalNone,       ///< a vowel-final syllable; also Latin letters, digits and symbols
	kKoFinalConsonant,  ///< a syllable with a final consonant other than ㄹ
	kKoFinalRieul,      ///< a syllable ending in ㄹ (takes 로, not 으로)
	kKoFinalUnknown     ///< nothing to judge by (the "..." placeholder): write both forms
};

enum KoreanJosa {
	kJosaNone,
	kJosaObject,   ///< 을/를
	kJosaTopic,    ///< 은/는
	kJosaSubject,  ///< 이/가
	kJosaAnd,      ///< 과/와
	kJosaMeans,    ///< (으)로 - ㄹ takes 로
	kJosaAt,       ///< 에 (invariant)
	kJosaTo        ///< 에게 (invariant)
};

/** Bytes of the character at @p p (1 for ASCII, 2 for a CP949 lead byte, 1..4 in UTF-8). */
int koreanCharLength(const char *p, bool utf8);

/** The final of the last character of @p noun (spaces and '@' at the end skipped). */
KoreanFinal koreanFinalOf(const Common::String &noun, bool utf8);

/** The particle @p josa in the form @p final takes, encoded like the text. */
Common::String koreanJosa(KoreanFinal final, KoreanJosa josa, bool utf8);

/** @p noun followed by @p josa in the form its last syllable takes. */
Common::String koreanWithJosa(const Common::String &noun, KoreanJosa josa, bool utf8);

/** One v2 sentence, every part already translated. */
struct KoreanSentenceParts {
	Common::String verb;
	Common::String objA;
	Common::String objB;
	bool hasA = false;
	bool hasB = false;
	int prep = 0;              ///< the v2 preposition: 0 none, 1 in, 2 with, 3 on, 4 to
	bool bIsActor = false;     ///< B is a person (to: 에게, else 에)
	bool instrumentB = false;  ///< with: B is the tool (Unlock/Fix door with key)
};

/**
 * The sentence in Korean order: "<A> <verb>", "<B>에게 <A> <verb>", ...;
 * B not chosen yet shows as "..." with its particle ("...에게 티켓 주기").
 * A verb ending in '?' ("이게 뭐지?") stays first: "이게 뭐지? 램프".
 */
Common::String composeKoreanSentence(const KoreanSentenceParts &parts, bool utf8);

typedef int (*KoreanWidthFunc)(const Common::String &text, void *ctx);

/**
 * composeKoreanSentence(), made to fit @p maxWidth as @p width measures it:
 * the longer object name loses its last character until the line fits (the
 * particle stays the one the full name takes, the verb is kept whole); if
 * the names cannot shrink further, the line is cut after its last whole
 * character that fits. Never cuts inside a character.
 */
Common::String fitKoreanSentence(const KoreanSentenceParts &parts, bool utf8, int maxWidth,
								 KoreanWidthFunc width, void *ctx);

/**
 * Whether drawSentence() composes the line in Korean: a v2 game (MM, Zak;
 * not v1, not the NES version, which draws its own prepositions) running in
 * Korean with a translation bundle loaded. Everything else keeps the
 * engine's English-order line, byte for byte.
 */
bool koreanSentenceApplies(int version, Common::Platform platform, Common::Language language, bool hasBundle);

/**
 * The v2 sentence line's length cut, as drawSentence() has always done it:
 * at most 40 printable characters (60 on the NES, with a line break after
 * 30), '@' not counted. Writes at most @p outSize bytes, NUL included.
 */
void cutV2Sentence(const char *sentence, bool nes, byte *out, int outSize);

} // End of namespace Scumm

#endif
