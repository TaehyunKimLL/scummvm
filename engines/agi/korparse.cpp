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

#include "agi/korparse.h"

#include "common/korean_stem.h"
#include "common/korean_vocab.h"
#include "common/ustr.h"

namespace Agi {

bool saidMatches(const AgiSentence &words, const uint16 *spec, int nwords) {
	int n = (int)words.size();
	int z = 0;
	int s = 0;
	for (int c = 0; nwords && n; c++, nwords--, n--) {
		z = spec[s++];
		switch (z) {
		case 9999: // rest of line (empty string counts to...)
			nwords = 1;
			break;
		case 1: // any word
			break;
		default:
			if (words[c].id != z)
				return false;
			break;
		}
	}
	// The entry string should be entirely parsed, or last word = 9999
	if (n && z != 9999)
		return false;
	// The interpreter string shouldn't be entirely parsed, but next word must be 9999.
	if (nwords != 0 && spec[s] != 9999)
		return false;
	return true;
}

bool lookupKoreanToken(const Common::KoreanVocabulary &voc, const Common::String &utf8, AgiSentence &out,
                       bool exactOnly) {
	const Common::U32String word = utf8.decode(Common::kUtf8);
	uint16 groups[4], classes[4];
	uint8 role = Common::kKoreanRoleNone;
	const uint n = exactOnly ? voc.lookupExact(word, groups, classes, 4)
	                         : voc.lookupAll(word, groups, classes, 4, role);
	if (!n)
		return false;
	const bool sequence = n > 1 && (classes[0] & Common::KoreanVocabulary::kSequenceFlag);
	const uint rows = sequence ? n : 1;
	bool pushed = false;
	for (uint i = 0; i < rows; i++) {
		if (groups[i] == 0)
			continue; // a word the game ignores
		AgiWord a;
		a.word = utf8;
		a.id = groups[i];
		a.korean = true;
		a.verb = (classes[i] & kAgiKoreanVerbClass) != 0;
		a.role = (i + 1 == rows) ? role : (uint8)Common::kKoreanRoleNone;
		a.cont = pushed; // a sequence's tail rides on its first word
		out.push_back(a);
		pushed = true;
	}
	return true;
}

static bool roleMarked(uint8 role) {
	return role == Common::kKoreanRoleTo || role == Common::kKoreanRoleAt || role == Common::kKoreanRoleFrom ||
	       role == Common::kKoreanRoleWith || role == Common::kKoreanRoleOf;
}

static void append(AgiSentence &to, const AgiSentence &from) {
	for (uint i = 0; i < from.size(); i++)
		to.push_back(from[i]);
}

static void addUnique(Common::Array<AgiSentence> &out, AgiSentence s, uint maxWords) {
	if (s.size() > maxWords)
		s.resize(maxWords);
	for (uint k = 0; k < out.size(); k++) {
		if (out[k].size() != s.size())
			continue;
		bool same = true;
		for (uint i = 0; i < s.size() && same; i++)
			same = out[k][i].id == s[i].id;
		if (same)
			return;
	}
	out.push_back(s);
}

typedef Common::Array<AgiSentence> AgiUnits;

/** The typed words as units: a word and the cont words after it. */
static void splitUnits(const AgiSentence &typed, AgiUnits &units) {
	for (uint i = 0; i < typed.size(); i++) {
		if (typed[i].cont && !units.empty())
			units.back().push_back(typed[i]);
		else
			units.push_back(AgiSentence(1, typed[i]));
	}
}

static void appendUnits(AgiSentence &to, const AgiUnits &from) {
	for (uint i = 0; i < from.size(); i++)
		append(to, from[i]);
}

void buildKoreanCandidates(const AgiSentence &typed, const AgiPrepositions &preps, uint maxWords,
                           Common::Array<AgiSentence> &out) {
	out.clear();
	AgiUnits units, verbs, plain, marked, rest;
	splitUnits(typed, units);
	// A unit is a verb by its first word; its role is on its last.
	for (uint i = 0; i < units.size(); i++) {
		if (units[i][0].verb)
			verbs.push_back(units[i]);
		else if (roleMarked(units[i].back().role))
			marked.push_back(units[i]);
		else
			plain.push_back(units[i]);
		if (!units[i][0].verb)
			rest.push_back(units[i]);
	}
	// (a) verbs, plain objects in typed order, role-marked nouns
	AgiSentence a;
	appendUnits(a, verbs);
	appendUnits(a, plain);
	appendUnits(a, marked);
	addUnique(out, a, maxWords);
	// (b) the game's own preposition before each role-marked noun
	AgiSentence b;
	appendUnits(b, verbs);
	appendUnits(b, plain);
	bool inserted = false;
	for (uint i = 0; i < marked.size(); i++) {
		const uint8 r = marked[i].back().role;
		if (r < 8 && preps.group[r]) {
			AgiWord p;
			p.word = preps.word[r];
			p.id = preps.group[r];
			b.push_back(p);
			inserted = true;
		}
		append(b, marked[i]);
	}
	if (inserted)
		addUnique(out, b, maxWords);
	// (c) as typed
	addUnique(out, typed, maxWords);
	// (d) X의 Y -> Y X, verbs first
	bool swapped = false;
	for (uint i = 0; i + 1 < rest.size(); i++) {
		if (rest[i].back().role == Common::kKoreanRoleOf) {
			SWAP(rest[i], rest[i + 1]);
			swapped = true;
			i++;
		}
	}
	if (swapped) {
		AgiSentence d;
		appendUnits(d, verbs);
		appendUnits(d, rest);
		addUnique(out, d, maxWords);
	}
	if (out.size() > 4)
		out.resize(4);
}

} // End of namespace Agi
