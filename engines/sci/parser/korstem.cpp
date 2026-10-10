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

#include "common/scummsys.h"
#include "common/str-enc.h"
#include "common/ustr.h"

#include "sci/parser/korstem.h"

namespace Sci {

// Precomposed Hangul: U+AC00 + (cho * 21 + jung) * 28 + jong.
static const uint32 kSyllableBase = 0xAC00;
static const uint32 kSyllableCount = 11172;
static const uint32 kJungCount = 21;
static const uint32 kJongCount = 28;

enum Cho { kChoRieul = 5, kChoIeung = 11, kChoHieut = 18 };
enum Jung {
	kJungA = 0, kJungAe = 1, kJungYae = 3, kJungEo = 4, kJungE = 5, kJungYeo = 6, kJungYe = 7, kJungO = 8,
	kJungWa = 9, kJungWae = 10, kJungOe = 11, kJungU = 13, kJungWo = 14, kJungEu = 18,
	kJungI = 20
};
enum Jong {
	kJongNone = 0, kJongNieun = 4, kJongDigeut = 7, kJongRieul = 8, kJongBieup = 17,
	kJongSiot = 19, kJongSsang = 20, kJongHieut = 27
};

static bool isSyllable(uint32 c) {
	return c >= kSyllableBase && c < kSyllableBase + kSyllableCount;
}

static uint32 cho(uint32 c) { return (c - kSyllableBase) / (kJungCount * kJongCount); }
static uint32 jung(uint32 c) { return ((c - kSyllableBase) / kJongCount) % kJungCount; }
static uint32 jong(uint32 c) { return (c - kSyllableBase) % kJongCount; }

static uint32 compose(uint32 ch, uint32 ju, uint32 jo) {
	return kSyllableBase + (ch * kJungCount + ju) * kJongCount + jo;
}

typedef Common::U32String U32;

static U32 utf8(const char *s) {
	return Common::convertToU32String(s, Common::kUtf8);
}

static bool endsWith(const U32 &s, const U32 &tail) {
	return s.size() > tail.size() &&
	       U32(s.c_str() + s.size() - tail.size(), tail.size()) == tail;
}

static U32 head(const U32 &s, uint drop) {
	return U32(s.c_str(), s.size() - drop);
}

static uint32 last(const U32 &s) { return s[s.size() - 1]; }

static U32 withLast(const U32 &s, uint32 c) {
	U32 r = head(s, 1);
	r += c;
	return r;
}

// Everything the rules know about the ending they just removed.
enum StemFlags {
	kStemPlain = 0,
	// A ㄹ-final stem drops its ㄹ before this ending (ㄴ, ㅂ, ㅅ, 오):
	// 연다, 엽니다, 여세요 are all 열다.
	kStemRieul = 1 << 0,
	// The ending was a separate 어/아/여 syllable, so the stem is bare and
	// may be a ㄷ or ㅅ irregular (들어 -> 듣다, 지어 -> 짓다).
	kStemExplicitVowel = 1 << 1,
	// The stem's last syllable still carries a fused 어/아 (봐, 줘, 해, 써,
	// 잠가, 불러, 도와) that has to be undone.
	kStemFused = 1 << 2,
	// The remainder is in its 어/아 form, explicit or fused: 라, 서, 도
	// and 야 attach to that form (봐라, 해서, 열어도).
	kStemVowelForm = 1 << 3,
	// The ending's own consonant sits in the stem's last syllable: ㅂ니다
	// leaves 합, ㄴ다 leaves 간.
	kStemJongBieup = 1 << 4,
	kStemJongNieun = 1 << 5,
	// 여, 여요, 여라, 여서: an ㅣ stem's 어 (붙여 is 붙이 + 어) or 하's own
	// 여 (하여). Never a bare consonant stem's.
	kStemYeo = 1 << 6,
	// An 으 ending (으세요, 으십시오): only a consonant stem takes it, and
	// a ㄹ or vowel final before it is the ㄷ or ㅅ irregular (물으세요 is
	// 묻다, 지으세요 is 짓다).
	kStemAfterEu = 1 << 7
};

struct Ending {
	const char *text;
	uint flags;
};

// The endings a player types after a verb at a parser prompt: plain and
// polite statements, requests, imperatives, and the 고/서 that join two
// commands. Questions, modifiers and nominal endings are left out on
// purpose: 집을, 안은, 사도 and 하나 would otherwise read as 집다, 안다, 사다
// and 하다 before the particle strip gets its turn, and a wrong verb is
// worse than a missing one. Longest first within a shared tail, so 어라
// is removed before 라 would be; the loop relies on the table order.
static const Ending kEndings[] = {
	{ "\xec\x9c\xbc\xec\x8b\xad\xec\x8b\x9c\xec\x98\xa4", kStemAfterEu },          // 으십시오
	{ "\xec\x8b\xad\xec\x8b\x9c\xec\x98\xa4", kStemRieul },                        // 십시오
	{ "\xec\x8a\xb5\xeb\x8b\x88\xeb\x8b\xa4", kStemPlain },                        // 습니다
	{ "\xec\x8a\xb5\xeb\x8b\x88\xea\xb9\x8c", kStemPlain },                        // 습니까
	{ "\xec\x9d\x8d\xec\x8b\x9c\xeb\x8b\xa4", kStemPlain },                        // 읍시다
	{ "\xeb\x8b\x88\xeb\x8b\xa4", kStemJongBieup },                                // ㅂ니다
	{ "\xeb\x8b\x88\xea\xb9\x8c", kStemJongBieup },                                // ㅂ니까
	{ "\xec\x8b\x9c\xeb\x8b\xa4", kStemJongBieup },                                // ㅂ시다
	{ "\xec\x9c\xbc\xec\x84\xb8\xec\x9a\x94", kStemAfterEu },                      // 으세요
	{ "\xec\x84\xb8\xec\x9a\x94", kStemRieul },                                    // 세요
	{ "\xec\x9c\xbc\xec\x8b\x9c\xec\x98\xa4", kStemAfterEu },                      // 으시오
	{ "\xec\x8b\x9c\xec\x98\xa4", kStemRieul },                                    // 시오
	{ "\xec\x96\xb4\xeb\x9d\xbc", kStemExplicitVowel },                            // 어라
	{ "\xec\x95\x84\xeb\x9d\xbc", kStemExplicitVowel },                            // 아라
	{ "\xec\x97\xac\xeb\x9d\xbc", kStemYeo },                                     // 여라
	{ "\xec\x96\xb4\xec\x9a\x94", kStemExplicitVowel },                            // 어요
	{ "\xec\x95\x84\xec\x9a\x94", kStemExplicitVowel },                            // 아요
	{ "\xec\x97\xac\xec\x9a\x94", kStemYeo },                                     // 여요
	{ "\xec\x96\xb4\xec\x84\x9c", kStemExplicitVowel },                            // 어서
	{ "\xec\x95\x84\xec\x84\x9c", kStemExplicitVowel },                            // 아서
	{ "\xec\x97\xac\xec\x84\x9c", kStemYeo },                                     // 여서
	{ "\xea\xb1\xb0\xeb\x9d\xbc", kStemPlain },                                    // 거라
	{ "\xeb\x84\x88\xeb\x9d\xbc", kStemPlain },                                    // 너라
	{ "\xeb\x8a\x94\xeb\x8b\xa4", kStemPlain },                                    // 는다
	{ "\xeb\x8b\xa4", kStemJongNieun },                                            // 다, ㄴ다
	{ "\xec\x96\xb4", kStemExplicitVowel },                                        // 어
	{ "\xec\x95\x84", kStemExplicitVowel },                                        // 아
	{ "\xec\x97\xac", kStemYeo },                                                 // 여
	{ "\xeb\x9d\xbc", kStemVowelForm },                                            // 라
	{ "\xec\x84\x9c", kStemVowelForm },                                            // 서
	{ "\xec\x9e\x90", kStemPlain },                                                // 자
	{ "\xea\xb3\xa0", kStemPlain },                                                // 고
	{ nullptr, 0 }
};

// Auxiliary verbs that follow the 어/아 form inside one word: 열어봐,
// 열어줘, 열어보세요, 가져와. The aux is removed and the head is stemmed.
struct Aux {
	const char *text;
	// 가, 오 and 내 also make compounds and nouns (나가, 새가): they only
	// count after a separate 어/아/여, never after a fused head.
	bool fusedHead;
};

static const Aux kAuxAfterVowel[] = {
	{ "\xeb\x93\x9c\xeb\xa6\xac", true },    // 드리
	{ "\xeb\xb2\x84\xeb\xa6\xac", true },    // 버리
	{ "\xeb\xb3\xb4", true },                // 보
	{ "\xec\xa3\xbc", true },                // 주
	{ "\xeb\x86\x93", true },                // 놓
	{ "\xeb\x91\x90", true },                // 두
	{ "\xea\xb0\x80", false },               // 가
	{ "\xec\x98\xa4", false },               // 오
	{ "\xeb\x82\xb4", false },               // 내
	{ "\xec\x9e\x88", true },                // 있
	{ nullptr, false }
};

// 고 있다, 고 싶다.
static const char *const kAuxAfterGo[] = {
	"\xec\x9e\x88",               // 있
	"\xec\x8b\xb6",               // 싶
	nullptr
};

// Case markers and auxiliary particles a player attaches to a noun. A
// parser token is whatever sits between two spaces, so 문을 and 문 are
// different strings and only the bare one can match the map.
//
// The allomorph constraint (을 after a final consonant, 를 after a vowel)
// is what keeps 사나이 from losing its 이: the strip would only have
// produced a candidate the map does not hold, but a false hit on a word
// that IS held is a wrong answer rather than a missing one.
enum ParticleAfter { kAfterAny, kAfterConsonant, kAfterVowel, kAfterVowelOrRieul };

struct Particle {
	const char *text;
	ParticleAfter after;
};

static const Particle kParticles[] = {
	{ "\xec\x97\x90\xea\xb2\x8c\xec\x84\x9c", kAfterAny },        // 에게서
	{ "\xed\x95\x9c\xed\x85\x8c\xec\x84\x9c", kAfterAny },        // 한테서
	{ "\xec\x9c\xbc\xeb\xa1\x9c\xec\x8d\xa8", kAfterConsonant },  // 으로써
	{ "\xec\x9c\xbc\xeb\xa1\x9c\xec\x84\x9c", kAfterConsonant },  // 으로서
	{ "\xec\x97\x90\xec\x84\x9c", kAfterAny },                    // 에서
	{ "\xec\x97\x90\xea\xb2\x8c", kAfterAny },                    // 에게
	{ "\xed\x95\x9c\xed\x85\x8c", kAfterAny },                    // 한테
	{ "\xea\xbb\x98\xec\x84\x9c", kAfterAny },                    // 께서
	{ "\xec\x9c\xbc\xeb\xa1\x9c", kAfterConsonant },              // 으로
	{ "\xeb\xa1\x9c\xec\x8d\xa8", kAfterVowelOrRieul },           // 로써
	{ "\xeb\xa1\x9c\xec\x84\x9c", kAfterVowelOrRieul },           // 로서
	{ "\xeb\xb6\x80\xed\x84\xb0", kAfterAny },                    // 부터
	{ "\xea\xb9\x8c\xec\xa7\x80", kAfterAny },                    // 까지
	{ "\xec\xb2\x98\xeb\x9f\xbc", kAfterAny },                    // 처럼
	{ "\xeb\xb3\xb4\xeb\x8b\xa4", kAfterAny },                    // 보다
	{ "\xed\x95\x98\xea\xb3\xa0", kAfterAny },                    // 하고
	{ "\xec\x9d\xb4\xeb\x9e\x91", kAfterConsonant },              // 이랑
	{ "\xec\x9d\xb4\xeb\x82\x98", kAfterConsonant },              // 이나
	{ "\xec\x9d\xb4\xec\x95\xbc", kAfterConsonant },              // 이야
	{ "\xec\x9d\xb4\xeb\x8b\xa4", kAfterConsonant },              // 이다
	{ "\xec\xa1\xb0\xec\xb0\xa8", kAfterAny },                    // 조차
	{ "\xeb\xa7\x88\xec\xa0\x80", kAfterAny },                    // 마저
	{ "\xeb\xb0\x96\xec\x97\x90", kAfterAny },                    // 밖에
	{ "\xec\x9d\x84", kAfterConsonant },                          // 을
	{ "\xeb\xa5\xbc", kAfterVowel },                              // 를
	{ "\xec\x9d\xb4", kAfterConsonant },                          // 이
	{ "\xea\xb0\x80", kAfterVowel },                              // 가
	{ "\xec\x9d\x80", kAfterConsonant },                          // 은
	{ "\xeb\x8a\x94", kAfterVowel },                              // 는
	{ "\xec\x9d\x98", kAfterAny },                                // 의
	{ "\xec\x97\x90", kAfterAny },                                // 에
	{ "\xeb\xa1\x9c", kAfterVowelOrRieul },                       // 로
	{ "\xec\x99\x80", kAfterVowel },                              // 와
	{ "\xea\xb3\xbc", kAfterConsonant },                          // 과
	{ "\xeb\x9e\x91", kAfterVowel },                              // 랑
	{ "\xeb\x8f\x84", kAfterAny },                                // 도
	{ "\xeb\xa7\x8c", kAfterAny },                                // 만
	{ "\xea\xbb\x98", kAfterAny },                                // 께
	{ "\xec\x95\xbc", kAfterVowel },                              // 야
	{ "\xeb\x93\xa4", kAfterAny },                                // 들
	{ nullptr, kAfterAny }
};

class Collector {
public:
	Collector(Common::String *out, const U32 &typed) : _out(out), _n(0), _typed(typed), _da(utf8("\xeb\x8b\xa4")) {}

	uint count() const { return _n; }

	/** Append a candidate if it is new and there is room. */
	void add(const U32 &u32) {
		// The typed word itself was looked up before any of this ran.
		if (_n >= kMaxKoreanStems || u32.empty() || u32 == _typed)
			return;
		// Back to EUC-KR, because that is what the table is sorted in and
		// what the caller will search with.
		Common::String enc = Common::convertFromU32String(u32, Common::kWindows949);
		if (enc.empty())
			return;
		for (uint i = 0; i < _n; ++i)
			if (_out[i] == enc)
				return;
		_out[_n++] = enc;
	}

	/** A stem as a dictionary entry: stem + 다, unless it is one already. */
	void addLemma(const U32 &stem) {
		if (stem.empty() || endsWith(stem, _da))
			return;
		add(stem + _da);
	}

	const U32 &da() const { return _da; }

private:
	Common::String *_out;
	uint _n;
	U32 _typed;
	U32 _da;
};

static bool lastIsFusedVowel(const U32 &s) {
	if (s.empty() || !isSyllable(last(s)) || jong(last(s)) != kJongNone)
		return false;
	switch (jung(last(s))) {
	case kJungWa: case kJungWo: case kJungYeo: case kJungAe: case kJungYae:
	case kJungWae: case kJungEo: case kJungA: case kJungE: case kJungYe:
		return true;
	default:
		return false;
	}
}

// The regular stem behind a fused syllable: 봐 -> 보, 줘 -> 주, 해 -> 하,
// 버려 -> 버리, 돼 -> 되, 와 -> 오. Used to find an auxiliary verb (열어봐 is
// 열어보 + 아) before the full set of irregular readings is tried.
static U32 regularUnfuse(const U32 &s) {
	if (!lastIsFusedVowel(s))
		return s;
	const uint32 c = last(s);
	switch (jung(c)) {
	case kJungWa: return withLast(s, compose(cho(c), kJungO, kJongNone));
	case kJungWo: return withLast(s, compose(cho(c), kJungU, kJongNone));
	case kJungYeo: return withLast(s, compose(cho(c), kJungI, kJongNone));
	case kJungWae: return withLast(s, compose(cho(c), kJungOe, kJongNone));
	case kJungAe:
		if (cho(c) == kChoHieut)
			return withLast(s, compose(kChoHieut, kJungA, kJongNone));
		return s;
	default:
		return s;
	}
}

// Every reading of a stem, as dictionary forms. @p flags says what the
// removed ending tells us about the stem's shape.
static void addStem(Collector &c, const U32 &s, uint flags) {
	if (s.empty() || !isSyllable(last(s)))
		return;

	const uint32 L = last(s);
	const bool hasPrev = s.size() >= 2 && isSyllable(s[s.size() - 2]);
	const uint32 P = hasPrev ? s[s.size() - 2] : 0;

	if (flags & kStemExplicitVowel) {
		// ㄷ irregular: 들어 -> 듣다. ㅅ irregular: 지어 -> 짓다. These go
		// before the regular reading because at a parser prompt 걸어,
		// 물어 and 지어 are far more often walk, ask and build than hang,
		// bite and lose.
		if (jong(L) == kJongRieul)
			c.addLemma(withLast(s, L - kJongRieul + kJongDigeut));
		else if (jong(L) == kJongNone)
			c.addLemma(withLast(s, L + kJongSiot));
	}

	c.addLemma(s);

	// ㅡ elision across a syllable: 모아 is 모으 + 아, 치러 is 치르 + 어.
	if ((flags & kStemExplicitVowel) && jong(L) == kJongNone)
		c.addLemma(s + compose(kChoIeung, kJungEu, kJongNone));

	if ((flags & kStemRieul) && jong(L) == kJongNone) {
		c.addLemma(withLast(s, L + kJongRieul));
		// ㅂ irregular before an 으 ending: 도우세요 -> 돕다.
		if (hasPrev && L == compose(kChoIeung, kJungU, kJongNone) && jong(P) == kJongNone)
			c.addLemma(withLast(head(s, 1), P + kJongBieup));
	}

	if (!(flags & kStemFused) || jong(L) != kJongNone)
		return;

	const uint32 ch = cho(L);
	switch (jung(L)) {
	case kJungWa:
		if (ch == kChoIeung) {
			// 도와 -> 돕다, 가져와 -> 가져오다, 와 -> 오다.
			if (hasPrev && jong(P) == kJongNone)
				c.addLemma(withLast(head(s, 1), P + kJongBieup));
			c.addLemma(withLast(s, compose(ch, kJungO, kJongNone)));
		} else {
			// 봐 -> 보다, 놔 -> 놓다.
			c.addLemma(withLast(s, compose(ch, kJungO, kJongNone)));
			c.addLemma(withLast(s, compose(ch, kJungO, kJongHieut)));
		}
		break;
	case kJungWo:
		// 누워 -> 눕다, 배워 -> 배우다, 줘 -> 주다.
		if (ch == kChoIeung && hasPrev && jong(P) == kJongNone)
			c.addLemma(withLast(head(s, 1), P + kJongBieup));
		c.addLemma(withLast(s, compose(ch, kJungU, kJongNone)));
		break;
	case kJungYeo:
		// 가져 -> 가지다, 마셔 -> 마시다, 기울여 -> 기울이다. 켜 and 펴 are
		// their own stems and were added above.
		c.addLemma(withLast(s, compose(ch, kJungI, kJongNone)));
		break;
	case kJungAe:
		// 해 -> 하다; 그래 -> 그렇다. 내 and 보내 are their own stems.
		if (ch == kChoHieut)
			c.addLemma(withLast(s, compose(ch, kJungA, kJongNone)));
		c.addLemma(withLast(s, compose(ch, kJungEo, kJongHieut)));
		break;
	case kJungYae:
		// 하얘 -> 하얗다.
		c.addLemma(withLast(s, compose(ch, kJungYeo, kJongHieut)));
		break;
	case kJungWae:
		// 돼 -> 되다.
		c.addLemma(withLast(s, compose(ch, kJungOe, kJongNone)));
		break;
	case kJungEo:
	case kJungA:
		if (ch == kChoIeung)
			break;
		// ㅡ elision: 써 -> 쓰다, 잠가 -> 잠그다, 따라 -> 따르다.
		c.addLemma(withLast(s, compose(ch, kJungEu, kJongNone)));
		// 르 irregular: 불러 -> 부르다, 골라 -> 고르다.
		if (ch == kChoRieul && hasPrev && jong(P) == kJongRieul) {
			U32 r = head(s, 2);
			r += P - kJongRieul;
			r += compose(kChoRieul, kJungEu, kJongNone);
			c.addLemma(r);
		}
		break;
	default:
		break;
	}
}

static bool endsWithUtf8(const U32 &s, const char *tail, U32 &out) {
	const U32 t = utf8(tail);
	if (!endsWith(s, t))
		return false;
	out = head(s, t.size());
	return true;
}

static void processStem(Collector &c, U32 r, uint flags);

// The stem that an auxiliary verb followed: 열어보 -> 열, 도와주 -> 돕,
// 사용해보 -> 사용하, 가지고있 -> 가지.
static void addAuxHead(Collector &c, const U32 &r) {
	U32 h;
	for (uint i = 0; kAuxAfterGo[i]; ++i) {
		if (endsWithUtf8(r, kAuxAfterGo[i], h) && endsWithUtf8(h, "\xea\xb3\xa0", h)) // 고
			addStem(c, h, kStemPlain);
	}
	for (uint i = 0; kAuxAfterVowel[i].text; ++i) {
		if (!endsWithUtf8(r, kAuxAfterVowel[i].text, h) || h.empty())
			continue;
		U32 stem;
		if (endsWithUtf8(h, "\xec\x96\xb4", stem) || endsWithUtf8(h, "\xec\x95\x84", stem)) { // 어 아
			addStem(c, stem, kStemExplicitVowel);
		} else if (endsWithUtf8(h, "\xec\x97\xac", stem)) {                            // 여
			processStem(c, stem, kStemYeo);
		} else if (kAuxAfterVowel[i].fusedHead && lastIsFusedVowel(h)) {
			addStem(c, h, kStemFused);
		}
	}
}

// The stem left after an ending came off: strip the prefinal layer
// (tense, honorific), resolve its 어/아 form, look for an auxiliary verb,
// then add every reading.
static void processStem(Collector &c, U32 r, uint flags) {
	if (r.empty() || !isSyllable(last(r)))
		return;

	if (flags & kStemYeo) {
		if (endsWith(r, utf8("\xed\x95\x98")) || r == utf8("\xed\x95\x98")) {        // 하
			flags = kStemPlain;
		} else {
			addStem(c, r + utf8("\xec\x9d\xb4"), kStemPlain);                       // 이
			flags = kStemExplicitVowel;
		}
	}
	if (flags & kStemAfterEu) {
		const uint32 L = last(r);
		if (jong(L) == kJongRieul)
			c.addLemma(withLast(r, L - kJongRieul + kJongDigeut));
		else if (jong(L) == kJongNone)
			c.addLemma(withLast(r, L + kJongSiot));
		flags = kStemPlain;
	}
	if (flags & kStemJongBieup) {
		if (jong(last(r)) != kJongBieup)
			return;
		r = withLast(r, last(r) - kJongBieup);
		flags = kStemRieul;
	}
	if (flags & kStemJongNieun) {
		flags = kStemPlain;
		// 간다 -> 가다, 연다 -> 열다: the ㄴ of ㄴ다 fused into the stem.
		if (jong(last(r)) == kJongNieun)
			processStem(c, withLast(r, last(r) - kJongNieun), kStemRieul);
	}

	// Prefinal endings, innermost last: 하셨다 is 하 + 시 + 었 + 다.
	for (int guard = 0; guard < 3; ++guard) {
		U32 h;
		if (endsWithUtf8(r, "\xec\x9c\xbc\xec\x85\xa8", h) ||                      // 으셨
		    endsWithUtf8(r, "\xec\x9c\xbc\xec\x85\x94", h) ||                      // 으시
		    endsWithUtf8(r, "\xec\x9c\xbc\xec\x8b\x9c", h)) {                      // 으셔
			// The 으 may be the stem's own: 모으셔요 is 모으다.
			addStem(c, h + utf8("\xec\x9c\xbc"), kStemPlain);
			r = h;
			flags = kStemAfterEu;
			continue;
		}
		if (endsWithUtf8(r, "\xec\x85\xa8", h) || endsWithUtf8(r, "\xec\x85\x94", h)) { // 셨 셔
			// 마셨다 is 마시 + 었 + 다 as well as an honorific 마 + 시 + 었.
			addStem(c, h + utf8("\xec\x8b\x9c"), kStemPlain);
			r = h;
			flags = kStemRieul;
			continue;
		}
		if (endsWithUtf8(r, "\xec\x8b\x9c", h)) {                                   // 시
			// 꼬신다 is 꼬시다 before it is an honorific 꼬다.
			addStem(c, r, flags & ~kStemVowelForm);
			r = h;
			flags = kStemRieul;
			continue;
		}
		if (endsWithUtf8(r, "\xec\x98\x80", h)) {                                   // 였
			// 기울였다 is 기울이 + 었, 하였다 is 하 + 였.
			addStem(c, h + utf8("\xec\x9d\xb4"), kStemPlain);
			r = h;
			flags = kStemExplicitVowel;
			continue;
		}
		if (endsWithUtf8(r, "\xec\x95\x98", h) || endsWithUtf8(r, "\xec\x97\x88", h)) { // 았 었
			r = h;
			flags = kStemExplicitVowel;
			continue;
		}
		if (endsWithUtf8(r, "\xea\xb2\xa0", h)) {                                  // 겠
			r = h;
			flags = kStemPlain;
			continue;
		}
		if (isSyllable(last(r)) && jong(last(r)) == kJongSsang &&
		    lastIsFusedVowel(withLast(r, last(r) - kJongSsang))) {
			// 봤 is 보 + 았 with the ㅆ in the stem's syllable: 봤 -> 봐 -> 보.
			// 있 and 없 keep their ㅆ: 이 is no 어/아 form.
			r = withLast(r, last(r) - kJongSsang);
			flags = kStemVowelForm;
			continue;
		}
		break;
	}
	if (r.empty() || !isSyllable(last(r)))
		return;

	if (flags & kStemAfterEu) {
		const uint32 L = last(r);
		if (jong(L) == kJongRieul)
			c.addLemma(withLast(r, L - kJongRieul + kJongDigeut));
		else if (jong(L) == kJongNone)
			c.addLemma(withLast(r, L + kJongSiot));
		flags = kStemPlain;
	}

	U32 auxBase = r;
	if (flags & kStemVowelForm) {
		U32 h;
		if (endsWithUtf8(r, "\xec\x97\xac", h)) {                                   // 여
			// 보여라 is 보이 + 어 + 라; 하여라 is 하 + 여 + 라.
			if (!endsWith(h, utf8("\xed\x95\x98")) && h != utf8("\xed\x95\x98"))       // 하
				addStem(c, h + utf8("\xec\x9d\xb4"), kStemPlain);                   // 이
			r = h;
			flags = kStemExplicitVowel;
			auxBase = r;
		} else if (endsWithUtf8(r, "\xec\x96\xb4", h) || endsWithUtf8(r, "\xec\x95\x84", h)) { // 어 아
			r = h;
			flags = kStemExplicitVowel;
			auxBase = r;
		} else if (lastIsFusedVowel(r)) {
			flags = kStemFused;
			auxBase = regularUnfuse(r);
		} else {
			// 집도 is 집 + 도, not a verb: 라, 서 and 도 only ever follow an
			// 어/아 form.
			return;
		}
	}

	addStem(c, r, flags);
	addAuxHead(c, auxBase);
}

static void deinflect(Collector &c, const U32 &w) {
	for (uint e = 0; kEndings[e].text; ++e) {
		U32 r;
		if (endsWithUtf8(w, kEndings[e].text, r))
			processStem(c, r, kEndings[e].flags);
	}

	// A fused 어/아 form with nothing after it: 봐, 해, 써, 잠가, 열어봐. A
	// word ending in 다 is a citation form, not an 어/아 form.
	if (lastIsFusedVowel(w) && !endsWith(w, c.da()))
		processStem(c, w, kStemVowelForm);

	// A bare stem typed on its own: 읽 -> 읽다. 가 and 켜 reach their own
	// dictionary forms here as well.
	addStem(c, w, kStemPlain);
}

static bool particleFits(const U32 &stem, ParticleAfter after) {
	const uint32 L = last(stem);
	if (!isSyllable(L))
		return after == kAfterAny;
	switch (after) {
	case kAfterConsonant: return jong(L) != kJongNone;
	case kAfterVowel: return jong(L) == kJongNone;
	case kAfterVowelOrRieul: return jong(L) == kJongNone || jong(L) == kJongRieul;
	default: return true;
	}
}

// Nouns: 문을 -> 문, 방에서는 -> 방에서 -> 방. Two rounds, because a case
// marker can carry an auxiliary particle behind it.
static void stripParticles(Collector &c, const U32 &w, int rounds) {
	for (uint p = 0; kParticles[p].text; ++p) {
		U32 stem;
		if (!endsWithUtf8(w, kParticles[p].text, stem) || stem.empty())
			continue;
		if (!particleFits(stem, kParticles[p].after))
			continue;
		c.add(stem);
		if (rounds > 1)
			stripParticles(c, stem, rounds - 1);
	}
}

uint koreanStemCandidates(const char *word, uint len,
                          Common::String *outCands) {
	if (!word || !len || !outCands)
		return 0;

	// Work in code points. The rules are about syllables, and a syllable is
	// two bytes in EUC-KR but the arithmetic that splits off a final
	// consonant only holds for the Unicode form.
	Common::String raw(word, len);
	U32 u = Common::convertToU32String(raw.c_str(), Common::kWindows949);
	if (u.empty() || !isSyllable(last(u)))
		return 0;

	Collector c(outCands, u);

	deinflect(c, u);

	// The polite 요 sits outside everything else: 열어봐요, 하세요 (as 하세 +
	// 요 would be wrong, 세요 is in the table in its own right).
	U32 h;
	if (endsWithUtf8(u, "\xec\x9a\x94", h) && isSyllable(last(h)))
		deinflect(c, h);

	// Tried last so a verb form never loses to a particle that happens to
	// look like its ending.
	stripParticles(c, u, 2);

	return c.count();
}

} // End of namespace Sci
