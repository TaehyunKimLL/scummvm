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

#ifndef SCI_PARSER_KOREAN_PARSER_H
#define SCI_PARSER_KOREAN_PARSER_H

#include "common/array.h"
#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/str.h"

#include "sci/detection.h"
#include "sci/parser/vocabulary.h"

namespace Sci {

/**
 * Korean sentences for the game's English parser (DESIGN.md section 6): the
 * words come from the pack's word table (Common::KoreanVocabulary), and
 * the sentence is put into the order the said() specs were written for.
 * Holds no state of its own between calls; the game's words and the table
 * belong to Vocabulary. Text is UTF-8 (a TextMode::kUtf8 game).
 */
class KoreanParser {
public:
	KoreanParser(const WordMap &words, const Common::KoreanVocabulary &vocab, uint32 debugChannel = 0);

	enum { kMaxMeanings = 8 };

	/**
	 * Whether a tokenized sentence is put into English order: only with a
	 * Korean word table, and only when a Korean word was typed. Any other
	 * parser game (German umlauts, Russian CP866) keeps its typed order.
	 */
	static bool shouldArrange(bool koreanTableLoaded, bool sawKoreanWord) { return koreanTableLoaded && sawKoreanWord; }

	/**
	 * The table's (class, group) pairs for one typed word: into @p retval,
	 * and for a sequence word (들여다봐: look, in) its further pairs as one
	 * token each into @p extra. @p role: the particle the word lost.
	 * Returns the pairs found; 0 leaves both lists as they were.
	 */
	uint lookup(const Common::String &word, ResultWordList &retval, ResultWordListList &extra, uint8 &role) const;

	/** @p sentence with adjacent Korean words the table holds as one word joined (사 줘 -> 사줘). */
	Common::String joinPairs(const char *sentence) const;

	/**
	 * Puts the verb first, the plain objects next and each particle's noun
	 * as an English preposition phrase last (나무꾼에게 그릇을 줘 -> give
	 * bowl to woodcutter). @p roles: one Common::KoreanRole per token.
	 * @p fallback: the same without the particle phrases, when there were
	 * any (said() is retried on it), else empty.
	 */
	void arrange(ResultWordListList &words, const Common::Array<uint8> &roles, ResultWordListList &fallback) const;

	/** The English prepositions of @p role that the game's vocabulary has. */
	void preposition(uint8 role, ResultWordList &out) const;

	/** ask/tell + a bare topic: "about" is put before it; tell + to + someone is the game's talk. */
	void askAbout(ResultWordListList &words) const;

	/**
	 * The parsed line as the words an answer room's script searches for:
	 * for each token, those of @p searchWords (the room's list, a NULL
	 * terminated array) that are WHOLE English words of one of the token's
	 * groups, in the list's order, space separated. A token with none is
	 * left out (an injected preposition, an unrelated word); a line with no
	 * match at all is empty, and the typed text then stays as it is. Never
	 * a substring or a synonym that merely contains a search word.
	 */
	Common::String englishLine(const ResultWordListList &words, const char *const *searchWords) const;

private:
	bool arrangeTokens(const Common::Array<ResultWordList> &tok, const Common::Array<uint8> &role, int verb, uint verbTail,
	                   const Common::Array<uint> &unitStart, bool useRoles, ResultWordListList &out) const;

	const WordMap &_words;
	const Common::KoreanVocabulary &_vocab;
	uint32 _debugChannel;
};

/**
 * The per-game table (DESIGN.md section 6): a room whose script checks a
 * typed answer by searching the text itself (StrLen and StrAt over the edit
 * box) for English words, with no said(). There, to those two calls only,
 * and only when they are made by @c script, a Korean answer reads as the
 * words of @c words that its tokens are (KoreanParser::englishLine()).
 */
struct KoreanAnswerRoom {
	SciGameId game;
	uint16 room;
	uint16 script;                   ///< the script whose search it is: other callers (save lists, edit controls) read the text as typed
	const char *const *words;        ///< what that script searches for, lower case, NULL terminated
	const char *what;                ///< for the debug log

	bool appliesTo(int callingScript) const { return callingScript == script; }
};

/** The table's row for @p room of @p game; nullptr when there is none. */
const KoreanAnswerRoom *koreanAnswerRoom(SciGameId game, int room);

} // End of namespace Sci

#endif
