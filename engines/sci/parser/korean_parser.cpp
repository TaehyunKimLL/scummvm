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

#include "common/algorithm.h"
#include "common/debug.h"
#include "common/ustr.h"

#include "sci/parser/korean_parser.h"

namespace Sci {

KoreanParser::KoreanParser(const WordMap &words, const Common::KoreanVocabulary &vocab, uint32 debugChannel) :
	_words(words), _vocab(vocab), _debugChannel(debugChannel) {
}

uint KoreanParser::lookup(const Common::String &word, ResultWordList &retval, ResultWordListList &extra, uint8 &role) const {
	uint16 groups[kMaxMeanings], classes[kMaxMeanings];
	const Common::U32String kword(word, Common::kUtf8);
	role = Common::kKoreanRoleNone;
	const uint n = _vocab.lookupAll(kword, groups, classes, kMaxMeanings, role);
	// A sequence word (들여다봐: look, in) is its first pair here and one
	// extra token per further pair.
	const bool sequence = n > 1 && (classes[0] & Common::KoreanVocabulary::kSequenceFlag);
	for (uint i = 0; i < n; ++i) {
		ResultWord tmp = { (uint16)(classes[i] & ~Common::KoreanVocabulary::kSequenceFlag), groups[i] };
		if (sequence && i > 0) {
			ResultWordList one;
			one.push_back(tmp);
			extra.push_back(one);
		} else {
			retval.push_back(tmp);
		}
	}
	return n;
}

Common::String KoreanParser::joinPairs(const char *sentence) const {
	Common::Array<Common::String> words;
	Common::String cur;
	for (const char *p = sentence; ; ++p) {
		if (*p == ' ' || *p == '\0') {
			if (!cur.empty())
				words.push_back(cur);
			cur.clear();
			if (!*p)
				break;
		} else {
			cur += *p;
		}
	}
	Common::String out;
	for (uint i = 0; i < words.size(); ++i) {
		if (i + 1 < words.size() && (byte)words[i][0] >= 0x80 && (byte)words[i + 1][0] >= 0x80) {
			const Common::String joined = words[i] + words[i + 1];
			const Common::U32String first(words[i], Common::kUtf8), second(words[i + 1], Common::kUtf8);
			if (_vocab.lookupJoined(first, second)) {
				debugC(1, _debugChannel, "[parse] joined '%s' + '%s'", words[i].c_str(), words[i + 1].c_str());
				words[i] = joined;
				words.remove_at(i + 1);
			}
		}
		if (!out.empty())
			out += ' ';
		out += words[i];
	}
	return out;
}

static bool hasClass(const ResultWordList &w, uint16 mask) {
	for (ResultWordList::const_iterator r = w.begin(); r != w.end(); ++r)
		if (r->_class & mask)
			return true;
	return false;
}

// Every pair of the token has the class bit.
static bool onlyClass(const ResultWordList &w, uint16 mask) {
	if (w.empty())
		return false;
	for (ResultWordList::const_iterator r = w.begin(); r != w.end(); ++r)
		if (!(r->_class & mask))
			return false;
	return true;
}

// The preposition classes: 0x010 is VOCAB_CLASS_PREPOSITION; "about" and
// "with" carry 0x008 in every SCI0 vocabulary seen, their own class, and
// the grammar places them like prepositions.
static const uint16 kPrepositionClasses = (VOCAB_CLASS_PREPOSITION << 4) | 0x008;

// A token that is a preposition and nothing else: 안 (in), 위 (on), 대해
// (about). Korean puts it after its noun, English before.
static bool isPrepositionOnly(const ResultWordList &w) {
	return onlyClass(w, kPrepositionClasses);
}

void KoreanParser::preposition(uint8 role, ResultWordList &out) const {
	static const char *const kTo[] = { "to", nullptr };
	static const char *const kAt[] = { "in", "on", "at", "into", nullptr };
	static const char *const kFrom[] = { "from", "in", nullptr };
	static const char *const kWith[] = { "with", "into", "to", nullptr };
	const char *const *names;
	switch (role) {
	case Common::kKoreanRoleTo: names = kTo; break;
	case Common::kKoreanRoleAt: names = kAt; break;
	case Common::kKoreanRoleFrom: names = kFrom; break;
	case Common::kKoreanRoleWith: names = kWith; break;
	default: return;
	}
	for (; *names; ++names) {
		WordMap::const_iterator it = _words.find(*names);
		if (it == _words.end())
			continue;
		for (ResultWordList::const_iterator r = it->_value.begin(); r != it->_value.end(); ++r) {
			if (!(r->_class & kPrepositionClasses))
				continue;
			bool dup = false;
			for (ResultWordList::const_iterator o = out.begin(); o != out.end() && !dup; ++o)
				dup = o->_group == r->_group;
			if (!dup)
				out.push_back(*r);
		}
	}
}

void KoreanParser::arrange(ResultWordListList &words, const Common::Array<uint8> &roles, ResultWordListList &fallback) const {
	Common::Array<ResultWordList> tok;
	Common::Array<uint8> role;
	uint i = 0;
	for (ResultWordListList::const_iterator it = words.begin(); it != words.end(); ++it, ++i) {
		tok.push_back(*it);
		role.push_back(i < roles.size() ? roles[i] : (uint8)Common::kKoreanRoleNone);
	}

	// Korean puts the verb last ("옷을 가져와"), the said() specs expect it
	// first ("get clothes"): move a trailing verb to the front.
	// Either verb class: Laura Bow 1's "use" is an indicative verb only,
	// and its said() specs take it as the command all the same.
	const uint16 verbClass = (VOCAB_CLASS_IMPERATIVE_VERB | VOCAB_CLASS_INDICATIVE_VERB) << 4;
	const uint16 nounClass = VOCAB_CLASS_NOUN << 4;
	// A word that is noun and verb at once (KQ1's fiddle) stays the
	// object: the last token is the verb unless the first is a verb and
	// nothing else while the last could be a noun ("look 나무를").
	// A sequence verb's tail (들여다봐: look, in) follows the verb: the
	// trailing prepositions and adverbs are stepped over to find it, and
	// go out right after it.
	uint last = tok.size();
	while (last > 1 && (isPrepositionOnly(tok[last - 1]) || onlyClass(tok[last - 1], VOCAB_CLASS_ADVERB << 4)) &&
	       hasClass(tok[last - 2], verbClass))
		--last;
	int verb = -1;
	if (last > 1 && hasClass(tok[last - 1], verbClass) &&
	    !(hasClass(tok.front(), verbClass) && !hasClass(tok.front(), nounClass) && hasClass(tok[last - 1], nounClass)))
		verb = (int)last - 1;
	else if (!tok.empty() && hasClass(tok.front(), verbClass))
		verb = 0;
	const uint verbTail = (verb >= 0 && (uint)verb == last - 1) ? tok.size() - last : 0;

	// A noun unit is the noun with the bare or 의-marked nouns before it
	// that modify it: 나무 구멍 안을 is "in tree hole", as the game's own
	// "look in tree hole" parses. @p unitStart[k] is the first token of
	// the unit token k ends.
	// An article (그 as "the") belongs to its noun too.
	const uint16 articleClass = VOCAB_CLASS_ARTICLE << 4;
	Common::Array<uint> unitStart;
	for (uint k = 0; k < tok.size(); ++k) {
		uint start = k;
		const bool head = hasClass(tok[k], nounClass) && !isPrepositionOnly(tok[k]);
		while (head && start > 0 && (int)(start - 1) != verb &&
		       ((hasClass(tok[start - 1], nounClass) && !isPrepositionOnly(tok[start - 1]) &&
		         (role[start - 1] == Common::kKoreanRoleNone || role[start - 1] == Common::kKoreanRoleOf)) ||
		        (onlyClass(tok[start - 1], articleClass) &&
		         (role[start - 1] == Common::kKoreanRoleNone || role[start - 1] == Common::kKoreanRoleOf))))
			--start;
		unitStart.push_back(start);
	}

	// The primary arrangement, and the fallback for said() when a case
	// particle became a preposition: the same without those, so a spec
	// that wants the bare noun ("climb [<up] / beanstalk" against 콩나무에
	// 올라가) gets it. Postpositions (안, 대해) are kept in both.
	const bool rolePhrases = arrangeTokens(tok, role, verb, verbTail, unitStart, true, words);
	fallback.clear();
	if (rolePhrases)
		arrangeTokens(tok, role, verb, verbTail, unitStart, false, fallback);
	askAbout(words);
	askAbout(fallback);
}

void KoreanParser::askAbout(ResultWordListList &words) const {
	// "펜러스 물어봐" means "ask about fenrus", not "ask fenrus": Korean
	// often drops 에 대해. A conversation game's specs want the topic after
	// "about" (QFG2: ask , tell / / *), so a bare topic of ask/tell gets it.
	if (words.size() < 2)
		return;
	WordMap::const_iterator about = _words.find("about");
	if (about == _words.end())
		return;
	ResultWordList aboutWord;
	for (ResultWordList::const_iterator r = about->_value.begin(); r != about->_value.end(); ++r)
		if (r->_class & kPrepositionClasses)
			aboutWord.push_back(*r);
	if (aboutWord.empty())
		return;
	bool isAskOrTell = false;
	static const char *const kVerbs[] = { "ask", "tell" };
	for (uint v = 0; v < ARRAYSIZE(kVerbs) && !isAskOrTell; v++) {
		WordMap::const_iterator it = _words.find(kVerbs[v]);
		if (it == _words.end())
			continue;
		for (ResultWordList::const_iterator r = it->_value.begin(); r != it->_value.end() && !isAskOrTell; ++r)
			for (ResultWordList::const_iterator f = words.front().begin(); f != words.front().end() && !isAskOrTell; ++f)
				isAskOrTell = f->_group == r->_group;
	}
	if (!isAskOrTell)
		return;
	const uint16 nounClass = VOCAB_CLASS_NOUN << 4;
	ResultWordListList::iterator second = words.begin();
	++second;
	// "샤민에게 말해" is "talk to shameen": tell with nothing to tell but
	// someone to tell it to becomes the game's talk.
	if (words.size() == 3 && isPrepositionOnly(*second) && second->front()._group != aboutWord.front()._group) {
		WordMap::const_iterator tell = _words.find("tell");
		WordMap::const_iterator talk = _words.find("talk");
		bool isTell = false;
		if (tell != _words.end())
			for (ResultWordList::const_iterator r = tell->_value.begin(); r != tell->_value.end(); ++r)
				isTell = isTell || words.front().front()._group == r->_group;
		if (isTell && talk != _words.end()) {
			words.front() = talk->_value;
			return;
		}
	}
	// Only a topic straight after the verb: "to shameen" (에게) or an
	// "about" already there leave the line as it is.
	if (isPrepositionOnly(*second) || !hasClass(*second, nounClass))
		return;
	for (ResultWordListList::const_iterator t = words.begin(); t != words.end(); ++t)
		for (ResultWordList::const_iterator r = t->begin(); r != t->end(); ++r)
			if (r->_group == aboutWord.front()._group)
				return;
	words.insert(second, aboutWord);
}

bool KoreanParser::arrangeTokens(const Common::Array<ResultWordList> &tok, const Common::Array<uint8> &role, int verb, uint verbTail,
                               const Common::Array<uint> &unitStart, bool useRoles, ResultWordListList &out) const {
	const uint16 nounClass = VOCAB_CLASS_NOUN << 4;
	Common::Array<ResultWordList> plain, phrases;
	uint plainUnits = 0; // tokens of plain that belong to the last unit
	bool rolePhrases = false;
	for (uint k = 0; k < tok.size(); ++k) {
		if ((int)k == verb || (verb >= 0 && k > (uint)verb && k <= (uint)verb + verbTail))
			continue;
		if (isPrepositionOnly(tok[k]) && plainUnits && hasClass(plain.back(), nounClass)) {
			// 구멍 안을 봐: the postposition and its noun unit become "in hole".
			phrases.push_back(tok[k]);
			const uint units = MIN<uint>(plainUnits, plain.size());
			for (uint u = plain.size() - units; u < plain.size(); ++u)
				phrases.push_back(plain[u]);
			for (uint u = 0; u < units; ++u)
				plain.pop_back();
			plainUnits = 0;
			continue;
		}
		// 성배에 대해: the postposition after the noun takes it, not the 에.
		const bool nextIsPostposition = k + 1 < tok.size() && (int)(k + 1) != verb && isPrepositionOnly(tok[k + 1]);
		ResultWordList prep;
		if (useRoles && !isPrepositionOnly(tok[k]) && !nextIsPostposition)
			preposition(role[k], prep);
		if (!prep.empty()) {
			// 나무꾼에게: "to woodcutter", after the plain object.
			rolePhrases = true;
			phrases.push_back(prep);
			// The unit's earlier tokens come from plain, and only as many as
			// plain holds: a token that went elsewhere is not emitted twice.
			const uint popCount = MIN<uint>(k - unitStart[k], plain.size());
			for (uint u = plain.size() - popCount; u < plain.size(); ++u)
				phrases.push_back(plain[u]);
			phrases.push_back(tok[k]);
			for (uint u = 0; u < popCount; ++u)
				plain.pop_back();
			plainUnits = 0;
			continue;
		}
		plain.push_back(tok[k]);
		plainUnits = MIN<uint>(k + 1 - unitStart[k], plain.size());
	}

	out.clear();
	if (verb >= 0)
		for (uint k = (uint)verb; k <= (uint)verb + verbTail; ++k)
			out.push_back(tok[k]);
	for (uint k = 0; k < plain.size(); ++k)
		out.push_back(plain[k]);
	for (uint k = 0; k < phrases.size(); ++k)
		out.push_back(phrases[k]);
	return rolePhrases;
}

Common::String KoreanParser::englishLine(const ResultWordListList &words) const {
	// Every English word of every group a token may be: a script that looks
	// for one word ("stefan") must find it whichever synonym ("baron") or
	// reading the Korean word maps to.
	Common::String line;
	for (ResultWordListList::const_iterator t = words.begin(); t != words.end(); ++t) {
		Common::Array<Common::String> found;
		for (WordMap::const_iterator w = _words.begin(); w != _words.end(); ++w) {
			bool match = false;
			for (ResultWordList::const_iterator r = w->_value.begin(); r != w->_value.end() && !match; ++r)
				for (ResultWordList::const_iterator a = t->begin(); a != t->end() && !match; ++a)
					match = a->_group == r->_group;
			if (match)
				found.push_back(w->_key);
		}
		if (found.empty())
			return Common::String();
		Common::sort(found.begin(), found.end());
		for (uint i = 0; i < found.size(); ++i) {
			if (!line.empty())
				line += ' ';
			line += found[i];
		}
	}
	return line;
}

static const KoreanAnswerRoom s_answerRooms[] = {
	// QFG1 EGA: script 802's search for the hut's rhyme and the gargoyle's answers (jamo e5295f1b411)
	{ GID_QFG1, 22, "QFG1 Baba Yaga's hut" },
	{ GID_QFG1, 29, "QFG1 the gargoyle at Erasmus's gate" },
	// QFG2: script 2's search for Aziza's questions (jamo 40e65ff93ac)
	{ GID_QFG2, 199, "QFG2 Aziza's door" }
};

const KoreanAnswerRoom *koreanAnswerRoom(SciGameId game, int room) {
	for (uint i = 0; i < ARRAYSIZE(s_answerRooms); ++i)
		if (s_answerRooms[i].game == game && s_answerRooms[i].room == room)
			return &s_answerRooms[i];
	return nullptr;
}

} // End of namespace Sci
