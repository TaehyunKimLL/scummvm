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
#include "common/ustr.h"

#include "sci/parser/korstem.h"

namespace Sci {

// Precomposed Hangul: U+AC00 + (cho * 21 + jung) * 28 + jong. Every rule
// below is arithmetic on that, which is what makes a precomposed code
// point as good as a jamo-composed one: the three parts are three
// divisions away.
static const uint32 kSyllableBase = 0xAC00;
static const uint32 kSyllableCount = 11172;
static const uint32 kJungCount = 21;
static const uint32 kJongCount = 28;

enum Cho { kChoRieul = 5, kChoIeung = 11, kChoPieup = 17, kChoHieut = 18 };
enum Jung {
	kJungA = 0, kJungAe = 1, kJungYae = 3, kJungEo = 4, kJungE = 5, kJungYeo = 6, kJungYe = 7, kJungO = 8,
	kJungWa = 9, kJungWae = 10, kJungOe = 11, kJungU = 13, kJungWo = 14, kJungEu = 18,
	kJungI = 20
};
enum Jong {
	kJongNone = 0, kJongNieun = 4, kJongDigeut = 7, kJongRieul = 8, kJongBieup = 17,
	kJongSiot = 19, kJongSsang = 20, kJongHieut = 27
};

// The syllables the rules name on their own.
enum Syl {
	kSylDa = 0xB2E4,      // 다
	kSylEo = 0xC5B4,      // 어
	kSylA = 0xC544,       // 아
	kSylYeo = 0xC5EC,     // 여
	kSylYo = 0xC694,      // 요
	kSylGo = 0xACE0,      // 고
	kSylSi = 0xC2DC,      // 시
	kSylSyeoss = 0xC168,  // 셨
	kSylSyeo = 0xC154,    // 셔
	kSylEu = 0xC73C,      // 으
	kSylAss = 0xC558,     // 았
	kSylEoss = 0xC5C8,    // 었
	kSylYeoss = 0xC600,   // 였
	kSylGess = 0xACA0,    // 겠
	kSylI = 0xC774,       // 이
	kSylHa = 0xD558,      // 하
	kSylU = 0xC6B0,       // 우
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

// Rules build their readings as U32String temporaries. Those stay in the
// string's inline storage (29 code points) for any word this is given,
// so nothing here touches the heap: the input is capped at
// KoreanStems::kMaxLen and no rule adds more than two syllables.
typedef Common::U32String U32;

static bool endsWithCps(const U32 &s, const uint16 *cp, uint n) {
	if (s.size() <= n)
		return false;
	const uint base = s.size() - n;
	for (uint i = 0; i < n; ++i)
		if (s[base + i] != cp[i])
			return false;
	return true;
}

static bool endsWithSyl(const U32 &s, uint32 c) {
	return s.size() > 1 && s[s.size() - 1] == c;
}

static bool endsWithSyl2(const U32 &s, uint32 a, uint32 b) {
	return s.size() > 2 && s[s.size() - 2] == a && s[s.size() - 1] == b;
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

static U32 plusSyl(const U32 &s, uint32 c) {
	U32 r = s;
	r += c;
	return r;
}

// Everything the rules know about the ending they just removed.
enum StemFlags {
	kStemPlain = 0,
	// A ㄹ-final stem drops its ㄹ before this ending (ㄴ, ㅂ, ㅅ, 오):
	// 연다, 엽니다, 여세요 are all 열다.
	kStemRieul = 1 << 0,
	// The ending was a separate 어/아 syllable, so the stem is bare and
	// may be a ㄷ or ㅅ irregular (들어 -> 듣다, 지어 -> 짓다).
	kStemExplicitVowel = 1 << 1,
	// The stem's last syllable still carries a fused 어/아 (봐, 줘, 해, 써,
	// 잠가, 불러, 도와) that has to be undone.
	kStemFused = 1 << 2,
	// The remainder is in its 어/아 form, explicit or fused: 라 and 서
	// attach to that form (봐라, 해서, 열어도).
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
	kStemAfterEu = 1 << 7,
	// ㄹ까, ㄹ래: the ㄹ is the ending's after a vowel stem (볼까 is 보다)
	// and the stem's own after a ㄹ stem (열까 is 열다), so both are read.
	kStemJongRieul = 1 << 8
};

struct Ending {
	uint8 len;
	uint16 cp[4];
	uint16 flags;
};

// The endings a player types after a verb at a parser prompt: plain and
// polite statements, requests (열어줄래, 열어볼까), imperatives, and the
// 고/서 that join two commands. Other questions, modifiers and nominal
// endings are left out on purpose: 집을, 안은, 사도 and 하나 would otherwise
// read as 집다, 안다, 사다 and 하다 before the particle strip gets its turn,
// and a wrong verb is worse than a missing one. Longest first within a
// shared tail, so 어라 is removed before 라 would be; the loop relies on
// the table order. Stored as code points so a match is a compare, never
// a decode.
static const Ending kEndings[] = {
	{ 4, { 0xC73C, 0xC2ED, 0xC2DC, 0xC624 }, kStemAfterEu },  // 으십시오
	{ 3, { 0xC2ED, 0xC2DC, 0xC624 }, kStemRieul },            // 십시오
	{ 3, { 0xC2B5, 0xB2C8, 0xB2E4 }, kStemPlain },            // 습니다
	{ 3, { 0xC2B5, 0xB2C8, 0xAE4C }, kStemPlain },            // 습니까
	{ 3, { 0xC74D, 0xC2DC, 0xB2E4 }, kStemPlain },            // 읍시다
	{ 2, { 0xB2C8, 0xB2E4 }, kStemJongBieup },                // 니다
	{ 2, { 0xB2C8, 0xAE4C }, kStemJongBieup },                // 니까
	{ 2, { 0xC2DC, 0xB2E4 }, kStemJongBieup },                // 시다
	{ 3, { 0xC73C, 0xC138, 0xC694 }, kStemAfterEu },          // 으세요
	{ 2, { 0xC138, 0xC694 }, kStemRieul },                    // 세요
	{ 3, { 0xC73C, 0xC2DC, 0xC624 }, kStemAfterEu },          // 으시오
	{ 2, { 0xC2DC, 0xC624 }, kStemRieul },                    // 시오
	{ 2, { 0xC5B4, 0xB77C }, kStemExplicitVowel },            // 어라
	{ 2, { 0xC544, 0xB77C }, kStemExplicitVowel },            // 아라
	{ 2, { 0xC5EC, 0xB77C }, kStemYeo },                      // 여라
	{ 2, { 0xC5B4, 0xC694 }, kStemExplicitVowel },            // 어요
	{ 2, { 0xC544, 0xC694 }, kStemExplicitVowel },            // 아요
	{ 2, { 0xC5EC, 0xC694 }, kStemYeo },                      // 여요
	{ 2, { 0xC5B4, 0xC11C }, kStemExplicitVowel },            // 어서
	{ 2, { 0xC544, 0xC11C }, kStemExplicitVowel },            // 아서
	{ 2, { 0xC5EC, 0xC11C }, kStemYeo },                      // 여서
	{ 2, { 0xAC70, 0xB77C }, kStemPlain },                    // 거라
	{ 2, { 0xB108, 0xB77C }, kStemPlain },                    // 너라
	{ 2, { 0xB294, 0xB2E4 }, kStemPlain },                    // 는다
	{ 2, { 0xC744, 0xAE4C }, kStemPlain },                    // 을까
	{ 2, { 0xC744, 0xB798 }, kStemPlain },                    // 을래
	{ 1, { 0xAE4C }, kStemJongRieul },                        // 까
	{ 1, { 0xB798 }, kStemJongRieul },                        // 래
	{ 1, { 0xB2E4 }, kStemJongNieun },                        // 다
	{ 1, { 0xC5B4 }, kStemExplicitVowel },                    // 어
	{ 1, { 0xC544 }, kStemExplicitVowel },                    // 아
	{ 1, { 0xC5EC }, kStemYeo },                              // 여
	{ 1, { 0xB77C }, kStemVowelForm },                        // 라
	{ 1, { 0xC11C }, kStemVowelForm },                        // 서
	{ 1, { 0xC790 }, kStemPlain },                            // 자
	{ 1, { 0xACE0 }, kStemPlain },                            // 고
};

struct Aux {
	uint8 len;
	uint16 cp[2];
	// 가, 오 and 내 also make compounds and nouns (나가, 새가): they only
	// count after a separate 어/아/여, never after a fused head.
	bool fusedHead;
};

// Auxiliary verbs that follow the 어/아 form inside one word: 열어봐,
// 열어줘, 열어보세요, 가져와. The aux is removed and the head is stemmed.
static const Aux kAuxAfterVowel[] = {
	{ 2, { 0xB4DC, 0xB9AC }, true },                // 드리
	{ 2, { 0xBC84, 0xB9AC }, true },                // 버리
	{ 1, { 0xBCF4 }, true },                        // 보
	{ 1, { 0xC8FC }, true },                        // 주
	{ 1, { 0xB193 }, true },                        // 놓
	{ 1, { 0xB450 }, true },                        // 두
	{ 1, { 0xAC00 }, false },                        // 가
	{ 1, { 0xC624 }, false },                        // 오
	{ 1, { 0xB0B4 }, false },                        // 내
	{ 1, { 0xC788 }, true },                        // 있
};

struct AuxGo {
	uint8 len;
	uint16 cp[1];
};

// 고 있다, 고 싶다.
static const AuxGo kAuxAfterGo[] = {
	{ 1, { 0xC788 } },   // 있
	{ 1, { 0xC2F6 } },   // 싶
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

// A case marker sits next to the noun, an auxiliary particle after it, and
// the plural 들 before either (방에서는, 친구에게도, 사람들에게도) - never the
// other way round. So a further strip is tried only in that order: 단도를
// is 단도 + 를, and a second round must not make it 단 + 도.
enum ParticleKind { kCase, kAux, kPlural, kNoParticle };

struct Particle {
	uint8 len;
	uint16 cp[3];
	uint8 after;
	uint8 kind;
	uint8 role; ///< KoreanRole a case particle gives the noun
};

static const Particle kParticles[] = {
	{ 3, { 0xC5D0, 0xAC8C, 0xC11C }, kAfterAny, kCase, kKoreanRoleFrom },             // 에게서
	{ 3, { 0xD55C, 0xD14C, 0xC11C }, kAfterAny, kCase, kKoreanRoleFrom },             // 한테서
	{ 3, { 0xC73C, 0xB85C, 0xC368 }, kAfterConsonant, kCase, kKoreanRoleWith },       // 으로써
	{ 3, { 0xC73C, 0xB85C, 0xC11C }, kAfterConsonant, kCase, kKoreanRoleWith },       // 으로서
	{ 2, { 0xC5D0, 0xC11C }, kAfterAny, kCase, kKoreanRoleFrom },                     // 에서
	{ 2, { 0xC5D0, 0xAC8C }, kAfterAny, kCase, kKoreanRoleTo },                     // 에게
	{ 2, { 0xD55C, 0xD14C }, kAfterAny, kCase, kKoreanRoleTo },                     // 한테
	{ 2, { 0xAED8, 0xC11C }, kAfterAny, kCase, kKoreanRoleNone },                     // 께서
	{ 2, { 0xC73C, 0xB85C }, kAfterConsonant, kCase, kKoreanRoleWith },               // 으로
	{ 2, { 0xB85C, 0xC368 }, kAfterVowelOrRieul, kCase, kKoreanRoleWith },            // 로써
	{ 2, { 0xB85C, 0xC11C }, kAfterVowelOrRieul, kCase, kKoreanRoleWith },            // 로서
	{ 3, { 0xC774, 0xB77C, 0xACE0 }, kAfterConsonant, kAux, kKoreanRoleNone },  // 이라고 (say X: 피터라고 해)
	{ 3, { 0xC774, 0xB0D0, 0xACE0 }, kAfterConsonant, kAux, kKoreanRoleNone },  // 이냐고 (ask who: 누구냐고 물어)
	{ 2, { 0xB77C, 0xACE0 }, kAfterVowel, kAux, kKoreanRoleNone },             // 라고
	{ 2, { 0xB0D0, 0xACE0 }, kAfterAny, kAux, kKoreanRoleNone },               // 냐고
	{ 2, { 0xBD80, 0xD130 }, kAfterAny, kAux, kKoreanRoleNone },                     // 부터
	{ 2, { 0xAE4C, 0xC9C0 }, kAfterAny, kAux, kKoreanRoleNone },                     // 까지
	{ 2, { 0xCC98, 0xB7FC }, kAfterAny, kAux, kKoreanRoleNone },                     // 처럼
	{ 2, { 0xBCF4, 0xB2E4 }, kAfterAny, kAux, kKoreanRoleNone },                     // 보다
	{ 2, { 0xD558, 0xACE0 }, kAfterAny, kCase, kKoreanRoleWith },                     // 하고
	{ 2, { 0xC774, 0xB791 }, kAfterConsonant, kCase, kKoreanRoleWith },               // 이랑
	{ 2, { 0xC774, 0xB098 }, kAfterConsonant, kCase, kKoreanRoleNone },               // 이나
	{ 2, { 0xC774, 0xC57C }, kAfterConsonant, kAux, kKoreanRoleNone },               // 이야
	{ 2, { 0xC774, 0xB2E4 }, kAfterConsonant, kAux, kKoreanRoleNone },               // 이다
	{ 2, { 0xC870, 0xCC28 }, kAfterAny, kAux, kKoreanRoleNone },                     // 조차
	{ 2, { 0xB9C8, 0xC800 }, kAfterAny, kAux, kKoreanRoleNone },                     // 마저
	{ 2, { 0xBC16, 0xC5D0 }, kAfterAny, kAux, kKoreanRoleNone },                     // 밖에
	{ 1, { 0xC744 }, kAfterConsonant, kCase, kKoreanRoleObject },                       // 을
	{ 1, { 0xB97C }, kAfterVowel, kCase, kKoreanRoleObject },                           // 를
	{ 1, { 0xC774 }, kAfterConsonant, kCase, kKoreanRoleObject },                       // 이
	{ 1, { 0xAC00 }, kAfterVowel, kCase, kKoreanRoleObject },                           // 가
	{ 1, { 0xC740 }, kAfterConsonant, kAux, kKoreanRoleObject },                       // 은
	{ 1, { 0xB294 }, kAfterVowel, kAux, kKoreanRoleObject },                           // 는
	{ 1, { 0xC758 }, kAfterAny, kAux, kKoreanRoleOf },                             // 의
	{ 1, { 0xC5D0 }, kAfterAny, kCase, kKoreanRoleAt },                             // 에
	{ 1, { 0xB85C }, kAfterVowelOrRieul, kCase, kKoreanRoleWith },                    // 로
	{ 1, { 0xC640 }, kAfterVowel, kCase, kKoreanRoleWith },                           // 와
	{ 1, { 0xACFC }, kAfterConsonant, kCase, kKoreanRoleWith },                       // 과
	{ 1, { 0xB791 }, kAfterVowel, kCase, kKoreanRoleWith },                           // 랑
	{ 1, { 0xB3C4 }, kAfterAny, kAux, kKoreanRoleNone },                             // 도
	{ 1, { 0xB9CC }, kAfterAny, kAux, kKoreanRoleNone },                             // 만
	{ 1, { 0xAED8 }, kAfterAny, kCase, kKoreanRoleTo },                             // 께
	{ 1, { 0xC57C }, kAfterVowel, kAux, kKoreanRoleNone },                           // 야
	{ 1, { 0xB4E4 }, kAfterAny, kPlural, kKoreanRoleNone },                             // 들
	{ 1, { 0xB2E4 }, kAfterVowel, kAux, kKoreanRoleNone },                           // 다 (copula: 방패다)
};

Common::U32String KoreanStems::get(uint i) const {
	U32 r;
	if (i >= count)
		return r;
	for (uint k = 0; k < len[i]; ++k)
		r += (uint32)cp[i][k];
	return r;
}

class Collector {
public:
	Collector(KoreanStems &out, const U32 &typed) : _out(out), _typed(typed) {
		_out.count = 0;
	}

	uint count() const { return _out.count; }

	/** Append a candidate if it is new, fits, and there is room. @p role
	 *  is the KoreanRole of the particle the reading lost, if any. */
	void add(const U32 &u32, uint8 role = kKoreanRoleNone) {
		// The typed word itself was looked up before any of this ran.
		if (_out.count >= KoreanStems::kMaxCount || u32.empty() ||
		    u32.size() > KoreanStems::kMaxLen || u32 == _typed)
			return;
		for (uint i = 0; i < u32.size(); ++i)
			if (u32[i] > 0xFFFF)
				return;
		for (uint i = 0; i < _out.count; ++i) {
			if (_out.len[i] != u32.size())
				continue;
			uint k = 0;
			while (k < u32.size() && _out.cp[i][k] == u32[k])
				++k;
			if (k == u32.size())
				return;
		}
		uint16 *dst = _out.cp[_out.count];
		for (uint i = 0; i < u32.size(); ++i)
			dst[i] = (uint16)u32[i];
		_out.len[_out.count] = (uint8)u32.size();
		_out.role[_out.count] = role;
		_out.count++;
	}

	/** A stem as a dictionary entry: stem + 다, unless it is one already. */
	void addLemma(const U32 &stem) {
		if (stem.empty() || last(stem) == kSylDa)
			return;
		add(plusSyl(stem, kSylDa));
	}

private:
	KoreanStems &_out;
	U32 _typed;
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
		c.addLemma(plusSyl(s, kSylEu));

	if ((flags & kStemRieul) && jong(L) == kJongNone) {
		c.addLemma(withLast(s, L + kJongRieul));
		// ㅂ irregular before an 으 ending: 도우세요 -> 돕다.
		if (hasPrev && L == kSylU && jong(P) == kJongNone)
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
		// 퍼 -> 푸다, the one ㅜ irregular.
		if (jung(L) == kJungEo && ch == kChoPieup)
			c.addLemma(withLast(s, compose(ch, kJungU, kJongNone)));
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

static void processStem(Collector &c, U32 r, uint flags);

// The stem that an auxiliary verb followed: 열어보 -> 열, 도와주 -> 돕,
// 사용해보 -> 사용하, 가지고있 -> 가지.
static void addAuxHead(Collector &c, const U32 &r) {
	for (uint i = 0; i < ARRAYSIZE(kAuxAfterGo); ++i) {
		if (!endsWithCps(r, kAuxAfterGo[i].cp, kAuxAfterGo[i].len))
			continue;
		const U32 h = head(r, kAuxAfterGo[i].len);
		if (endsWithSyl(h, kSylGo))
			addStem(c, head(h, 1), kStemPlain);
	}
	for (uint i = 0; i < ARRAYSIZE(kAuxAfterVowel); ++i) {
		if (!endsWithCps(r, kAuxAfterVowel[i].cp, kAuxAfterVowel[i].len))
			continue;
		const U32 h = head(r, kAuxAfterVowel[i].len);
		if (endsWithSyl(h, kSylEo) || endsWithSyl(h, kSylA))
			addStem(c, head(h, 1), kStemExplicitVowel);
		else if (endsWithSyl(h, kSylYeo))
			processStem(c, head(h, 1), kStemYeo);
		else if (kAuxAfterVowel[i].fusedHead && lastIsFusedVowel(h))
			addStem(c, h, kStemFused);
	}
}

// The ㄷ/ㅅ irregular readings of a stem an 으 ending followed.
static void addAfterEu(Collector &c, const U32 &r) {
	const uint32 L = last(r);
	if (jong(L) == kJongRieul)
		c.addLemma(withLast(r, L - kJongRieul + kJongDigeut));
	else if (jong(L) == kJongNone)
		c.addLemma(withLast(r, L + kJongSiot));
}

// The stem left after an ending came off: strip the prefinal layer
// (tense, honorific), resolve its 어/아 form, look for an auxiliary verb,
// then add every reading.
static void processStem(Collector &c, U32 r, uint flags) {
	if (r.empty() || !isSyllable(last(r)))
		return;

	if (flags & kStemYeo) {
		if (last(r) == kSylHa) {
			flags = kStemPlain;
		} else {
			addStem(c, plusSyl(r, kSylI), kStemPlain);
			flags = kStemExplicitVowel;
		}
	}
	if (flags & kStemAfterEu) {
		addAfterEu(c, r);
		flags = kStemPlain;
	}
	if (flags & kStemJongBieup) {
		if (jong(last(r)) != kJongBieup)
			return;
		r = withLast(r, last(r) - kJongBieup);
		flags = kStemRieul;
	}
	if (flags & kStemJongRieul) {
		if (jong(last(r)) != kJongRieul)
			return;
		// 열까 keeps its ㄹ; 볼까 and 열어줄래 lose the ending's.
		processStem(c, r, kStemPlain);
		r = withLast(r, last(r) - kJongRieul);
		flags = kStemPlain;
	}
	if (flags & kStemJongNieun) {
		flags = kStemPlain;
		// 간다 -> 가다, 연다 -> 열다: the ㄴ of ㄴ다 fused into the stem.
		if (jong(last(r)) == kJongNieun)
			processStem(c, withLast(r, last(r) - kJongNieun), kStemRieul);
	}

	// Prefinal endings, innermost last: 하셨다 is 하 + 시 + 었 + 다.
	for (int guard = 0; guard < 3; ++guard) {
		if (endsWithSyl2(r, kSylEu, kSylSyeoss) || endsWithSyl2(r, kSylEu, kSylSyeo) ||
		    endsWithSyl2(r, kSylEu, kSylSi)) {                                     // 으셨 으셔 으시
			// The 으 may be the stem's own: 모으셔요 is 모으다.
			addStem(c, head(r, 1), kStemPlain);
			r = head(r, 2);
			flags = kStemAfterEu;
			continue;
		}
		if (endsWithSyl(r, kSylSyeoss) || endsWithSyl(r, kSylSyeo)) {               // 셨 셔
			// 마셨다 is 마시 + 었 + 다 as well as an honorific 마 + 시 + 었.
			addStem(c, withLast(r, kSylSi), kStemPlain);
			r = head(r, 1);
			flags = kStemRieul;
			continue;
		}
		if (endsWithSyl(r, kSylSi)) {                                                // 시
			// 꼬신다 is 꼬시다 before it is an honorific 꼬다.
			addStem(c, r, flags & ~kStemVowelForm);
			r = head(r, 1);
			flags = kStemRieul;
			continue;
		}
		if (endsWithSyl(r, kSylYeoss)) {                                             // 였
			// 기울였다 is 기울이 + 었, 하였다 is 하 + 였.
			addStem(c, withLast(r, kSylI), kStemPlain);
			r = head(r, 1);
			flags = kStemExplicitVowel;
			continue;
		}
		if (endsWithSyl(r, kSylAss) || endsWithSyl(r, kSylEoss)) {                   // 았 었
			r = head(r, 1);
			flags = kStemExplicitVowel;
			continue;
		}
		if (endsWithSyl(r, kSylGess)) {                                              // 겠
			r = head(r, 1);
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
		addAfterEu(c, r);
		flags = kStemPlain;
	}

	U32 auxBase = r;
	if (flags & kStemVowelForm) {
		if (endsWithSyl(r, kSylYeo)) {                                               // 여
			// 보여라 is 보이 + 어 + 라; 하여라 is 하 + 여 + 라.
			const U32 h = head(r, 1);
			if (last(h) != kSylHa)
				addStem(c, plusSyl(h, kSylI), kStemPlain);
			r = h;
			flags = kStemExplicitVowel;
			auxBase = r;
		} else if (endsWithSyl(r, kSylEo) || endsWithSyl(r, kSylA)) {                // 어 아
			r = head(r, 1);
			flags = kStemExplicitVowel;
			auxBase = r;
		} else if (lastIsFusedVowel(r)) {
			flags = kStemFused;
			auxBase = regularUnfuse(r);
		} else {
			// 집도 is 집 + 도, not a verb: 라 and 서 only ever follow an
			// 어/아 form.
			return;
		}
	}

	addStem(c, r, flags);
	addAuxHead(c, auxBase);
}

static void deinflect(Collector &c, const U32 &w) {
	for (uint e = 0; e < ARRAYSIZE(kEndings); ++e) {
		if (endsWithCps(w, kEndings[e].cp, kEndings[e].len))
			processStem(c, head(w, kEndings[e].len), kEndings[e].flags);
	}

	// A fused 어/아 form with nothing after it: 봐, 해, 써, 잠가, 열어봐. A
	// word ending in 다 is a citation form, not an 어/아 form.
	if (lastIsFusedVowel(w) && last(w) != kSylDa)
		processStem(c, w, kStemVowelForm);

	// A bare stem typed on its own: 읽 -> 읽다. 가 and 켜 reach their own
	// dictionary forms here as well.
	addStem(c, w, kStemPlain);
}

static bool particleFits(const U32 &stem, uint8 after) {
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

// Nouns: 문을 -> 문, 방에서는 -> 방에서 -> 방, 사람들을 -> 사람들 -> 사람. A
// further round only in the order the particles stack.
static bool particleMayFollow(uint8 inner, uint8 outer) {
	switch (outer) {
	case kNoParticle: return true;
	case kAux: return inner == kCase || inner == kPlural;
	case kCase: return inner == kPlural;
	default: return false;
	}
}

// @p role is the KoreanRole of the case particle already stripped from
// outside (나무꾼에게는 loses 는 first, then 에게); a reading keeps the role
// of its case particle through the plural stripped after it (친구들에게 ->
// 친구들 -> 친구, both "to").
static void stripParticles(Collector &c, const U32 &w, uint8 outer, uint8 role) {
	for (uint p = 0; p < ARRAYSIZE(kParticles); ++p) {
		if (!particleMayFollow(kParticles[p].kind, outer))
			continue;
		if (!endsWithCps(w, kParticles[p].cp, kParticles[p].len))
			continue;
		const U32 stem = head(w, kParticles[p].len);
		if (!particleFits(stem, kParticles[p].after))
			continue;
		// The innermost particle that places the noun wins: 나무꾼에게는 is
		// "to", 왕의 is "of"; a bare 도 or 만 keeps what is inside it.
		const uint8 r = kParticles[p].role != kKoreanRoleNone ? kParticles[p].role : role;
		c.add(stem, r);
		stripParticles(c, stem, kParticles[p].kind, r);
	}
}

uint koreanStemCandidates(const Common::U32String &word, KoreanStems &out) {
	out.count = 0;
	if (word.empty() || word.size() > KoreanStems::kMaxLen || !isSyllable(last(word)))
		return 0;

	Collector c(out, word);

	deinflect(c, word);

	// The polite 요 sits outside everything else: 열어봐요, 하세요 (as 하세 +
	// 요 would be wrong, 세요 is in the table in its own right).
	if (endsWithSyl(word, kSylYo) && isSyllable(word[word.size() - 2]))
		deinflect(c, head(word, 1));

	// Tried last so a verb form never loses to a particle that happens to
	// look like its ending; what keeps a noun from losing to a verb is the
	// ending table above, which holds none of the particle shapes.
	stripParticles(c, word, kNoParticle, kKoreanRoleNone);

	return c.count();
}

} // End of namespace Sci
