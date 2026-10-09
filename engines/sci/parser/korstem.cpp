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
static const uint32 kJongCount = 28;
static const uint32 kJongNieun = 4;      // ㄴ in the jongseong table

static bool isSyllable(uint32 c) {
	return c >= kSyllableBase && c < kSyllableBase + kSyllableCount;
}

// Endings tried longest-first, so 어라 is removed before 라 would be.
// Each is stripped and 다 re-attached, which is how an inflected form
// reaches its citation form.
static const char *const kEndings[] = {
	"\xec\x96\xb4\xeb\x9d\xbc",   // 어라
	"\xec\x95\x84\xeb\x9d\xbc",   // 아라
	"\xec\x97\xac\xeb\x9d\xbc",   // 여라
	"\xea\xb1\xb0\xeb\x9d\xbc",   // 거라
	"\xeb\x8a\x94\xeb\x8b\xa4",   // 는다
	"\xec\x96\xb4\xec\x9a\x94",   // 어요
	"\xec\x95\x84\xec\x9a\x94",   // 아요
	"\xec\x84\xb8\xec\x9a\x94",   // 세요
	"\xec\x96\xb4",               // 어
	"\xec\x95\x84",               // 아
	"\xec\x97\xac",               // 여
	"\xeb\x9d\xbc",               // 라
	"\xec\x9e\x90",               // 자
	"\xec\xa7\x80",               // 지
	"\xea\xb3\xa0",               // 고
	nullptr
};

static const char kDa[] = "\xeb\x8b\xa4";     // 다

// Case markers a player attaches to a noun: 문을, 문이, 문에서. The map
// stores bare nouns - a parser token is whatever sits between two spaces,
// so 문을 and 문 are different strings and only the bare one can match.
//
// Longest first, so 에서 is removed before 서 would be.
//
// This list is SAFE ONLY because a stripped candidate has to hit an entry
// that already exists. 사나이, 국가 and 종이 all end in a syllable that is
// also a particle, and stripping them yields 사나/국/종 - words the map
// does not contain, so the candidate is simply discarded. Attempting the
// strip costs a failed lookup; skipping it costs the player their sentence.
static const char *const kParticles[] = {
	"\xec\x97\x90\xec\x84\x9c",   // 에서
	"\xec\x9c\xbc\xeb\xa1\x9c",   // 으로
	"\xed\x95\x9c\xed\x85\x8c",   // 한테
	"\xea\xbb\x98\xec\x84\x9c",   // 께서
	"\xec\x9d\x84",               // 을
	"\xeb\xa5\xbc",               // 를
	"\xec\x9d\xb4",               // 이
	"\xea\xb0\x80",               // 가
	"\xec\x9d\x80",               // 은
	"\xeb\x8a\x94",               // 는
	"\xec\x9d\x98",               // 의
	"\xec\x97\x90",               // 에
	"\xeb\xa1\x9c",               // 로
	"\xec\x99\x80",               // 와
	"\xea\xb3\xbc",               // 과
	"\xeb\x8f\x84",               // 도
	"\xeb\xa7\x8c",               // 만
	nullptr
};

/** Append a candidate if it is new and there is room. */
static void addCandidate(Common::String *out, uint &n,
                         const Common::U32String &u32) {
	if (n >= kMaxKoreanStems)
		return;
	// Back to EUC-KR, because that is what the table is sorted in and what
	// the caller will search with.
	Common::String enc = Common::convertFromU32String(u32, Common::kWindows949);
	if (enc.empty())
		return;
	for (uint i = 0; i < n; ++i) {
		if (out[i] == enc)
			return;
	}
	out[n++] = enc;
}

uint koreanStemCandidates(const char *word, uint len,
                          Common::String *outCands) {
	uint n = 0;
	if (!word || !len || !outCands)
		return 0;

	// Work in code points. The rules are about syllables, and a syllable is
	// two bytes in EUC-KR but the arithmetic that splits off a final
	// consonant only holds for the Unicode form.
	Common::String raw(word, len);
	Common::U32String u = Common::convertToU32String(raw.c_str(),
	                                                 Common::kWindows949);
	if (u.empty())
		return 0;

	const Common::U32String da = Common::convertToU32String(kDa,
	                                                        Common::kUtf8);
	const bool endsWithDa = u.size() >= da.size() &&
	    Common::U32String(u.c_str() + u.size() - da.size(), da.size()) == da;

	for (uint e = 0; kEndings[e]; ++e) {
		const Common::U32String end =
		    Common::convertToU32String(kEndings[e], Common::kUtf8);
		if (end.empty() || u.size() <= end.size())
			continue;
		const Common::U32String tail(u.c_str() + u.size() - end.size(),
		                             end.size());
		if (tail != end)
			continue;
		const Common::U32String base(u.c_str(), u.size() - end.size());
		addCandidate(outCands, n, base + da);
		addCandidate(outCands, n, base);
	}

	// 준다 -> 주다, 탄다 -> 타다. Here the ㄴ is fused into the stem's last
	// syllable rather than standing as its own ending, so no amount of
	// suffix matching finds it; it comes off by syllable arithmetic.
	if (endsWithDa && u.size() >= 2) {
		const uint32 prev = u[u.size() - 2];
		if (isSyllable(prev) &&
		    (prev - kSyllableBase) % kJongCount == kJongNieun) {
			Common::U32String cand(u.c_str(), u.size() - 2);
			cand += (uint32)(prev - kJongNieun);
			cand += da;
			addCandidate(outCands, n, cand);
		}
	}

	// A bare stem typed on its own: 읽 -> 읽다.
	if (!endsWithDa)
		addCandidate(outCands, n, u + da);

	// Nouns: 문을 -> 문. Tried last so a verb form never loses to a
	// particle that happens to look like its ending.
	for (uint p = 0; kParticles[p]; ++p) {
		const Common::U32String par =
		    Common::convertToU32String(kParticles[p], Common::kUtf8);
		if (par.empty() || u.size() <= par.size())
			continue;
		const Common::U32String tail(u.c_str() + u.size() - par.size(),
		                             par.size());
		if (tail == par)
			addCandidate(outCands, n,
			             Common::U32String(u.c_str(), u.size() - par.size()));
	}

	return n;
}

} // End of namespace Sci
