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

#ifndef SCI_PARSER_KORVOCAB_H
#define SCI_PARSER_KORVOCAB_H

#include "common/scummsys.h"
#include "common/str.h"

namespace Sci {

/**
 * Korean words for the SCI0 parser, held outside the game's own vocabulary.
 *
 * WHY THIS EXISTS AT ALL, because the obvious alternative is impossible
 * rather than merely worse: `vocab.000`'s loader masks every stored byte
 * with 0x7F (vocabulary.cpp, `currentWord[currentWordPos++] = c & 0x7f`),
 * so a dictionary word is 7-bit ASCII by construction. Measured on
 * Cascade Quest: 1600 words, zero bytes above 0x7F. A Korean word cannot
 * be put into the resource - not "should not", cannot - so the mapping
 * has to live beside it.
 *
 * What it maps to is a GROUP ID, not an English word. The parser matches
 * `said()` specs by group (said.cpp), and a group is exactly a set of
 * synonyms, so naming the group in Korean is the whole job. Translating
 * Korean to English text and re-looking-it-up would add a second lookup
 * that can fail for its own reasons.
 *
 * The class travels with the group because parseGNF() tests both: a word
 * inserted with the right group and the wrong class still fails to parse.
 *
 * The file is produced offline by harness/k6bake.py and holds EUC-KR,
 * which is what the edit control leaves in the game's heap string - so
 * there is no conversion at lookup time and none to get wrong.
 */
class KoreanVocabulary {
public:
	KoreanVocabulary() : _data(nullptr), _size(0), _count(0),
	                     _entries(nullptr), _blob(nullptr) {}
	~KoreanVocabulary() { unload(); }

	/**
	 * Load `scikor.dat` through SearchMan.
	 *
	 * @return false when the file is absent or malformed. Absent is not an
	 *         error: without it Korean simply resolves nothing, exactly as
	 *         before this feature.
	 */
	bool load(const Common::String &filename = "scikor.dat");

	void unload();

	bool isLoaded() const { return _data != nullptr; }

	uint32 size() const { return _count; }

	/**
	 * Look up one EUC-KR word.
	 *
	 * Exact match first, then the stemming rules in korstem.cpp - a typed
	 * 먹는다 has to reach the stored 먹다. Stemming only ever STRIPS: it
	 * never manufactures a form and then looks for it, because a generated
	 * conjugation that is not a real word is a false match waiting to
	 * happen, and the AGI chain measured that generating lowers accuracy.
	 *
	 * @param word    EUC-KR bytes, no terminator required
	 * @param len     length in bytes
	 * @param group   out: vocab group id
	 * @param wclass  out: the class mask to insert it with
	 * @return true when resolved
	 */
	bool lookup(const char *word, uint len, uint16 &group, uint16 &wclass) const;

private:
	/** Exact bsearch over the sorted EUC-KR blob. */
	bool find(const char *word, uint len, uint16 &group, uint16 &wclass) const;

	byte *_data;
	uint32 _size;
	uint32 _count;
	const byte *_entries;
	const byte *_blob;
};

} // End of namespace Sci

#endif // SCI_PARSER_KORVOCAB_H
