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

#ifndef AGI_WORDS_H
#define AGI_WORDS_H

#include "common/korean_vocab.h"
#include "agi/korparse.h"

namespace Agi {

#define DICTIONARY_RESULT_UNKNOWN -1
#define DICTIONARY_RESULT_IGNORE   0

#define EXTENDED_DICTIONARY_FILENAME "words.tok.extended"

struct WordEntry {
	uint16 id;
	Common::String word;
};

class Words {
public:
	Words(AgiEngine *vm);
	~Words();

private:
	AgiEngine *_vm;

	// Dictionary of words in WORDS.TOK or WORDS.TOK.EXTENDED.
	// key:   first character of the word
	// value: words in the order they appear
	Common::HashMap<byte, Common::Array<WordEntry>> _dictionary;

	WordEntry _egoWords[MAX_WORDS];
	uint16  _egoWordCount;

	bool _hasExtendedCharacters; // true on WORDS.TOK.EXTENDED
public:
	uint16 getEgoWordCount() const;
	const char *getEgoWord(int16 wordNr) const;
	uint16 getEgoWordId(int16 wordNr) const;

	int  loadDictionary_v1(Common::SeekableReadStream &stream);
	int  loadDictionary(const char *fname);
	int  loadDictionary(Common::SeekableReadStream &stream);
	// used for fan made translations requiring extended char set
	int  loadExtendedDictionary(const char *fname);
	void unloadDictionary();

	void clearEgoWords();
	void parseUsingDictionary(const char *rawUserInput);

	/** A parse without side effects: the console's parse/said use it, and so does a Korean line. */
	struct ParseResult {
		Common::Array<AgiSentence> candidates; ///< [0] becomes the ego words
		uint16 unknownPos;                     ///< 1-based typed position of the unknown word, 0 = none
		ParseResult() : unknownPos(0) {}
	};
	void analyze(const char *rawUserInput, ParseResult &out) const;

	/** UTF-8 text mode: AGIKOR.DAT and the game's groups for to/in/on/at/into/from/with/of. */
	void loadKoreanTable();
	bool hasCandidates() const { return _candidates.size() > 1; }
	uint candidateCount() const { return _candidates.size(); }
	const AgiSentence &candidate(uint i) const { return _candidates[i]; }
	/** said() matched candidate @p i: it becomes the ego words (%w, word.to.string). */
	void commitCandidate(uint i);

private:
	Common::KoreanVocabulary _korean;
	AgiPrepositions _preps;
	Common::Array<AgiSentence> _candidates;
	void applyParse(const ParseResult &r);
	/** A Korean pair typed as two words that the table stores as one (켄이 보냈다). */
	bool lookupJoinedPair(const Common::String &first, const Common::String &second, AgiSentence &out) const;

	void  cleanUpInput(const char *userInput, Common::String &cleanInput) const;
	int16 findWordInDictionary(const Common::String &userInputLowercase, uint16 userInputLen, uint16 userInputPos, uint16 &foundWordLen) const;

	bool handleSpeedCommands(const Common::String &userInputLowercase);
	static void convertRussianUserInput(Common::String &userInputLowercase);
};

} // End of namespace Agi

#endif /* AGI_WORDS_H */
