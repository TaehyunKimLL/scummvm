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

#ifndef COMMON_KOREAN_VOCAB_H
#define COMMON_KOREAN_VOCAB_H

#include "common/path.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/translation_file.h"
#include "common/ustr.h"

namespace Common {

/**
 * Korean words for a parser game (SCI0, AGI), held outside the game's own vocabulary.
 *
 * A Korean word cannot be put into the game's vocabulary: SCI's vocab.000 loader masks every byte with 0x7F and AGI's
 * WORDS.TOK is 7-bit, so the mapping lives beside it. It maps a word to a vocabulary GROUP and the class to insert it
 * with: said() matches by group, and parseGNF() also tests the class.
 *
 * The file (sci-ko.dat / SCIKOR.DAT, agi-ko.dat / AGIKOR.DAT; tools/korean/k6bake.py writes it) holds code points,
 * sorted by code unit, so a lookup of the decoded typed word, or of a stemmer reading of it, is a binary search.
 *
 * Layout, little-endian. Version 2 (the packs made before fingerprint headers):
 *
 *     0   8  magic "SCIKOR\0\0" or "AGIKOR\0\0"
 *     8   4  version 2
 *     12  4  count
 *     16     count * { u16 offset, u16 length, u16 group, u16 class }
 *            then the UTF-16LE code units the offsets index
 *
 * Version 3 (DESIGN.md section 4, decision 4):
 *
 *     0   8  magic
 *     8   4  version 3
 *     12  4  count
 *     16  4  header size H, a multiple of 4
 *     20  H  the fingerprint header line (TranslationFileHeader, with vocab=), NUL-terminated, NUL-padded
 *     20+H   count * { u32 offset, u16 length, u16 group, u16 class, u16 reserved (0) }
 *            then the UTF-16LE code units
 *
 * Offsets and lengths count code units. Entries are sorted by their code units (unsigned, shorter first on a common
 * prefix); the entries of one word are adjacent, in the order written. A class with kSequenceFlag continues into the
 * next entry of the same word.
 */
class KoreanVocabulary {
public:
	explicit KoreanVocabulary(uint32 debugChannel = 0);
	~KoreanVocabulary() { unload(); }

	/** The engine's debug channel for the [korvocab] lines; 0 logs nothing. */
	void setDebugChannel(uint32 channel) { _debugChannel = channel; }

	/**
	 * Load a word table through SearchMan. @p magic is the file's 6-letter tag ("SCIKOR", "AGIKOR"). @p expected
	 * holds what the game knows about itself (any field empty: not compared); a version 3 file whose header differs
	 * is refused, a version 2 file loads with a warning (checkTranslationHeader()).
	 *
	 * @return false when the file is absent (no warning: no Korean words installed), malformed or refused.
	 */
	bool load(const Path &filename, const char *magic, const TranslationFileHeader &expected = TranslationFileHeader());

	/** load() once the file is open; @p name is for the warnings. */
	bool loadFromStream(SeekableReadStream &in, const char *magic, const String &name,
	                    const TranslationFileHeader &expected = TranslationFileHeader());

	void unload();

	bool isLoaded() const { return _data != nullptr; }

	/** Number of entries. */
	uint32 size() const { return _count; }

	/** 2 or 3; 0 when nothing is loaded. */
	uint32 getVersion() const { return _version; }

	/** The header of a version 3 table; not present() for version 2. */
	const TranslationFileHeader &getHeader() const { return _header; }

	/**
	 * Look up one typed word, decoded to code points: the exact word first, then the readings
	 * koreanStemCandidates() gives, best first; the first the table holds wins.
	 */
	bool lookup(const U32String &word, uint16 &group, uint16 &wclass) const;

	/**
	 * Every (group, class) pair the table holds for the word, or else for its best reading (several when the word
	 * stands for an English word in several groups, or is a sequence). @p role receives the KoreanRole of the reading
	 * that answered. Returns how many pairs were written, at most @p max.
	 */
	uint lookupAll(const U32String &word, uint16 *groups, uint16 *classes, uint max, uint8 &role) const;

	/** The pairs stored for exactly @p word, no stemmer reading. */
	uint lookupExact(const U32String &word, uint16 *groups, uint16 *classes, uint max) const;

	/**
	 * Whether two adjacent typed words are one stored word: the table holds @p first + @p second as typed, or
	 * @p first joined to a reading of @p second (물을 내린다 -> 물을내리다), or the reading of the joined string that
	 * lookupAll() takes reaches past the first word (사 줘 -> 사다). A reading that ends inside the first word dropped
	 * the second as a particle and does not count (나무 보다 is tree, look).
	 */
	bool lookupJoined(const U32String &first, const U32String &second) const;

	/** Class flag of an entry that continues into the next entry of the same word (들여다봐 is "look in"). */
	enum { kSequenceFlag = 0x8000 };

private:
	struct Entry {
		uint32 offset;
		uint16 length;
		uint16 group;
		uint16 wclass;
	};

	Entry entry(uint32 index) const;
	/** -1, 0, 1: entry @p index against @p word. */
	int compare(uint32 index, const uint16 *word, uint len) const;
	/** -1, 0, 1: entry @p a against entry @p b. */
	int compareEntries(uint32 a, uint32 b) const;
	int findIndex(const uint16 *word, uint len) const;
	uint collect(int index, uint16 *groups, uint16 *classes, uint max) const;
	bool sameWord(uint32 a, uint32 b) const;
	bool refuse(const String &name, const String &why);

	byte *_data;
	uint32 _size;
	uint32 _count;
	uint32 _version;
	uint32 _entrySize;
	const byte *_entries;
	const byte *_blob;
	uint32 _blobUnits;
	TranslationFileHeader _header;
	uint32 _debugChannel;
};

} // End of namespace Common

#endif // COMMON_KOREAN_VOCAB_H
