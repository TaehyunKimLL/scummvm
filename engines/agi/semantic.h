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

#ifndef AGI_SEMANTIC_H
#define AGI_SEMANTIC_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/path.h"
#include "common/str.h"

namespace Common {
class SeekableReadStream;
}

namespace Agi {

/**
 * Semantic ("fuzzy") lookup for the AGI parser.
 *
 * The player may type Korean, or word things differently from WORDS.TOK. This
 * maps a phrase onto dictionary word groups by combining a lexical match with
 * the cosine similarity of precomputed static embeddings.
 *
 * No model runs at runtime. agisem.dat holds one quantised vector per
 * tokenizer entry, built offline; input is embedded with a Unigram Viterbi
 * pass and a mean, which reproduces the source model's encode() exactly.
 * Lookup is a scan over a few thousand short vectors - microseconds.
 *
 * Optionally the search is restricted to the word groups the current room
 * actually tests with said(), which resolves most remaining ambiguity.
 */
class SemanticParser {
public:
	SemanticParser();
	~SemanticParser();

	/** Load agisem.dat. Returns false and stays disabled on any error. */
	bool load(const Common::Path &path);

	bool isLoaded() const { return _loaded; }

	struct Match {
		uint16 gid;
		float score;
		bool verb;
	};

	/**
	 * Rank word groups for a single input token.
	 *
	 * @param token     one UTF-8 word from the player
	 * @param wantVerb  when true only verb groups are considered, when false
	 *                  only noun groups
	 * @param allowed   optional whitelist of group ids (the room's said()
	 *                  set); empty means "search everything"
	 * @param out       receives the ranked matches, best first
	 * @param maxOut    how many matches to return
	 */
	void rankToken(const Common::String &token, bool wantVerb,
	               const Common::Array<uint16> &allowed,
	               Common::Array<Match> &out, uint maxOut = 3) const;

	/** Display name of a group (its first English synonym). */
	Common::String groupName(uint16 gid) const;

	/** True when the group was classified as a verb group. */
	bool isVerbGroup(uint16 gid) const;

	/** Exposed for the unit test: tokenize and embed a string. */
	bool embed(const Common::String &text, float *outVec) const;

	uint16 dim() const { return _dim; }

private:
	struct Group {
		uint16 gid;
		bool verb;
		Common::String name;
	};

	struct Entry {
		uint16 gid;
		float weight;           // idf, 0..1
		Common::String text;
	};

	struct Token {
		Common::String text;
		float logp;
		uint32 vecOffset;       // index into _vectors, in rows
	};

	int findToken(const Common::String &s) const;
	void viterbi(const Common::String &text, Common::Array<int> &outIds) const;
	static void stemCandidates(const Common::String &word,
	                           Common::Array<Common::String> &out);
	float lexicalScore(const Common::Array<Common::String> &forms,
	                   const Common::String &entry) const;

	bool _loaded;
	uint16 _dim;
	float _scale;

	Common::Array<Group> _groups;
	Common::Array<Entry> _entries;
	Common::Array<Token> _tokens;   // sorted by text, binary searchable
	Common::Array<int8> _vectors;

	Common::HashMap<uint16, uint> _groupIndex;
	// unit-normalised embedding of every dictionary entry, _dim floats each
	Common::Array<float> _entryVectors;
};

} // End of namespace Agi

#endif
