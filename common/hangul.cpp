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

#include "common/hangul.h"
#include "common/util.h"

namespace Common {

// Jamo index tables. Index 0 is unused/empty so that 0 can mean "not set".
// Choseong, 19 entries.
static const uint16 kCho[] = {
	0,
	0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143,
	0x3145, 0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D,
	0x314E
};

// Jungseong, 21 entries.
static const uint16 kJung[] = {
	0,
	0x314F, 0x3150, 0x3151, 0x3152, 0x3153, 0x3154, 0x3155, 0x3156, 0x3157,
	0x3158, 0x3159, 0x315A, 0x315B, 0x315C, 0x315D, 0x315E, 0x315F, 0x3160,
	0x3161, 0x3162, 0x3163
};

// Jongseong, 27 entries.
static const uint16 kJong[] = {
	0,
	0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313A,
	0x313B, 0x313C, 0x313D, 0x313E, 0x313F, 0x3140, 0x3141, 0x3142, 0x3144,
	0x3145, 0x3146, 0x3147, 0x3148, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E
};

struct KeyJamo {
	char key;
	uint16 jamo;
};

// Two-beolsik layout. Shifted keys produce the tense consonants and the
// wide vowels.
static const KeyJamo kKeyMap[] = {
	{ 'q', 0x3142 }, { 'w', 0x3148 }, { 'e', 0x3137 }, { 'r', 0x3131 },
	{ 't', 0x3145 }, { 'y', 0x315B }, { 'u', 0x3155 }, { 'i', 0x3151 },
	{ 'o', 0x3150 }, { 'p', 0x3154 }, { 'a', 0x3141 }, { 's', 0x3134 },
	{ 'd', 0x3147 }, { 'f', 0x3139 }, { 'g', 0x314E }, { 'h', 0x3157 },
	{ 'j', 0x3153 }, { 'k', 0x314F }, { 'l', 0x3163 }, { 'z', 0x314B },
	{ 'x', 0x314C }, { 'c', 0x314A }, { 'v', 0x314D }, { 'b', 0x3160 },
	{ 'n', 0x315C }, { 'm', 0x3161 },
	{ 'Q', 0x3143 }, { 'W', 0x3149 }, { 'E', 0x3138 }, { 'R', 0x3132 },
	{ 'T', 0x3146 }, { 'O', 0x3152 }, { 'P', 0x3156 }
};

struct Combine {
	uint8 a, b, result;
};

// Compound vowels, by jungseong index.
static const Combine kVowelCombine[] = {
	{  9,  1, 10 }, //  ㅗ + ㅏ = ㅘ
	{  9,  2, 11 }, //  ㅗ + ㅐ = ㅙ
	{  9, 21, 12 }, //  ㅗ + ㅣ = ㅚ
	{ 14,  5, 15 }, //  ㅜ + ㅓ = ㅝ
	{ 14,  6, 16 }, //  ㅜ + ㅔ = ㅞ
	{ 14, 21, 17 }, //  ㅜ + ㅣ = ㅟ
	{ 19, 21, 20 }  //  ㅡ + ㅣ = ㅢ
};

// Compound finals, by jongseong index.
static const Combine kFinalCombine[] = {
	{  1, 19,  3 }, //  ㄱ + ㅅ = ㄳ
	{  4, 22,  5 }, //  ㄴ + ㅈ = ㄵ
	{  4, 27,  6 }, //  ㄴ + ㅎ = ㄶ
	{  8,  1,  9 }, //  ㄹ + ㄱ = ㄺ
	{  8, 16, 10 }, //  ㄹ + ㅁ = ㄻ
	{  8, 17, 11 }, //  ㄹ + ㅂ = ㄼ
	{  8, 19, 12 }, //  ㄹ + ㅅ = ㄽ
	{  8, 25, 13 }, //  ㄹ + ㅌ = ㄾ
	{  8, 26, 14 }, //  ㄹ + ㅍ = ㄿ
	{  8, 27, 15 }, //  ㄹ + ㅎ = ㅀ
	{ 17, 19, 18 }  //  ㅂ + ㅅ = ㅄ
};

static uint16 jamoForKey(char key) {
	for (uint i = 0; i < ARRAYSIZE(kKeyMap); ++i) {
		if (kKeyMap[i].key == key)
			return kKeyMap[i].jamo;
	}
	// A shifted key with no shifted jamo of its own types the unshifted one,
	// as every two-beolsik IME does (Shift+A is still ㅁ). Caps Lock and an
	// on-screen keyboard left in its shift layout then still type Hangul.
	if (key >= 'A' && key <= 'Z')
		return jamoForKey(key - 'A' + 'a');
	return 0;
}

static int indexIn(const uint16 *table, uint count, uint16 jamo) {
	for (uint i = 1; i < count; ++i) {
		if (table[i] == jamo)
			return i;
	}
	return 0;
}

/** Split a compound final back into (kept, moved-to-next-initial). */
static bool splitFinal(int jong, int &keep, int &moved) {
	for (uint i = 0; i < ARRAYSIZE(kFinalCombine); ++i) {
		if (kFinalCombine[i].result == jong) {
			keep = kFinalCombine[i].a;
			moved = kFinalCombine[i].b;
			return true;
		}
	}
	return false;
}

/** Split a compound vowel back into its first component. */
static int splitVowel(int jung) {
	for (uint i = 0; i < ARRAYSIZE(kVowelCombine); ++i) {
		if (kVowelCombine[i].result == jung)
			return kVowelCombine[i].a;
	}
	return 0;
}

void HangulComposer::reset() {
	_out.clear();
	_cho = _jung = _jong = 0;
}

bool HangulComposer::isJamoKey(char ascii) {
	return jamoForKey(ascii) != 0;
}

uint32 HangulComposer::composeSyllable() const {
	if (_cho && _jung)
		return 0xAC00 + (_cho - 1) * 588 + (_jung - 1) * 28 + _jong;

	// An isolated jamo is shown as the compatibility jamo itself.
	if (_cho)
		return kCho[_cho];
	if (_jung)
		return kJung[_jung];
	if (_jong)
		return kJong[_jong];
	return 0;
}

void HangulComposer::flush() {
	uint32 c = composeSyllable();
	if (c)
		_out += c;
	_cho = _jung = _jong = 0;
}

bool HangulComposer::feed(char ascii) {
	uint16 jamo = jamoForKey(ascii);
	if (!jamo) {
		flush();
		_out += (uint32)(byte)ascii;
		return false;
	}

	const int choIdx = indexIn(kCho, ARRAYSIZE(kCho), jamo);
	const int jungIdx = indexIn(kJung, ARRAYSIZE(kJung), jamo);
	const int jongIdx = indexIn(kJong, ARRAYSIZE(kJong), jamo);

	if (jungIdx) {
		// A vowel steals the pending final to start the next syllable.
		if (_jong) {
			int keep = 0, moved = 0;
			if (!splitFinal(_jong, keep, moved)) {
				keep = 0;
				moved = _jong;
			}
			_jong = keep;
			// The moved jamo becomes the next initial, so translate the
			// jongseong index into a choseong index via the jamo itself.
			const int nextCho = indexIn(kCho, ARRAYSIZE(kCho), kJong[moved]);
			flush();
			_cho = nextCho;
			_jung = jungIdx;
			return true;
		}

		if (_jung) {
			for (uint i = 0; i < ARRAYSIZE(kVowelCombine); ++i) {
				if (kVowelCombine[i].a == _jung && kVowelCombine[i].b == jungIdx) {
					_jung = kVowelCombine[i].result;
					return true;
				}
			}
			flush();
			_jung = jungIdx;
			return true;
		}

		_jung = jungIdx;
		return true;
	}

	// Consonant.
	if (!_cho && !_jung) {
		_cho = choIdx;
		if (!_cho) {
			// A consonant that cannot be an initial (only ㄳ, ㄵ ... which
			// the keyboard never produces directly) - keep it as a final.
			_jong = jongIdx;
		}
		return true;
	}

	if (_cho && !_jung) {
		flush();
		_cho = choIdx;
		return true;
	}

	if (!_cho) {
		// A lone vowel has no syllable to take a final: the consonant after
		// it starts the next one. Setting _jong here would be dropped by
		// composeSyllable().
		flush();
		_cho = choIdx;
		return true;
	}

	if (!_jong) {
		if (jongIdx) {
			_jong = jongIdx;
			return true;
		}
		flush();
		_cho = choIdx;
		return true;
	}

	for (uint i = 0; i < ARRAYSIZE(kFinalCombine); ++i) {
		if (kFinalCombine[i].a == _jong && kFinalCombine[i].b == jongIdx) {
			_jong = kFinalCombine[i].result;
			return true;
		}
	}

	flush();
	_cho = choIdx;
	return true;
}

void HangulComposer::backspace() {
	if (_jong) {
		int keep = 0, moved = 0;
		_jong = splitFinal(_jong, keep, moved) ? keep : 0;
	} else if (_jung) {
		_jung = splitVowel(_jung);
	} else if (_cho) {
		_cho = 0;
	} else if (!_out.empty()) {
		_out.deleteLastChar();
	}
}

Common::U32String HangulComposer::text() const {
	Common::U32String s(_out);
	uint32 c = composeSyllable();
	if (c)
		s += c;
	return s;
}

// The final consonant of the code point before @p end in @p s, looking
// back through quotes and brackets: 0 none, 1 a consonant, 2 ㄹ (which
// takes 로, not 으로); -1 when there is nothing to read it from.
static int finalConsonant(const String &s, uint end) {
	while (end > 0) {
		const byte c = s[end - 1];
		if (c == '"' || c == '\'' || c == ')' || c == ']') {
			end--;
			continue;
		}
		if (c < 0x80) {
			if (isDigit(c))
				return c == '1' || c == '7' || c == '8' ? 2 : (c == '0' || c == '3' || c == '6') ? 1 : 0;
			if (!isAlpha(c))
				return -1;
			const byte l = c | 0x20;
			return l == 'l' ? 2 : (l == 'm' || l == 'n') ? 1 : 0;
		}
		// A three-byte UTF-8 Hangul syllable, U+AC00..U+D7A3.
		if (end >= 3 && (byte)s[end - 3] >= 0xEA && (byte)s[end - 3] <= 0xED) {
			const uint32 cp = (((byte)s[end - 3] & 0x0F) << 12) | (((byte)s[end - 2] & 0x3F) << 6) |
			                  ((byte)s[end - 1] & 0x3F);
			if (cp >= 0xAC00 && cp <= 0xD7A3) {
				const uint jong = (cp - 0xAC00) % 28;
				return jong == 0 ? 0 : jong == 8 ? 2 : 1;
			}
		}
		return -1;
	}
	return -1;
}

void resolveKoreanParticles(String &s) {
	// marker, form after a consonant, form after a vowel (or ㄹ for (으))
	static const char *const kPairs[][3] = {
		{ "\xEC\x9D\x84(\xEB\xA5\xBC)", "\xEC\x9D\x84", "\xEB\xA5\xBC" }, // 을(를)
		{ "\xEB\xA5\xBC(\xEC\x9D\x84)", "\xEC\x9D\x84", "\xEB\xA5\xBC" }, // 를(을)
		{ "\xEC\x9D\xB4(\xEA\xB0\x80)", "\xEC\x9D\xB4", "\xEA\xB0\x80" }, // 이(가)
		{ "\xEA\xB0\x80(\xEC\x9D\xB4)", "\xEC\x9D\xB4", "\xEA\xB0\x80" }, // 가(이)
		{ "\xEC\x9D\x80(\xEB\x8A\x94)", "\xEC\x9D\x80", "\xEB\x8A\x94" }, // 은(는)
		{ "\xEB\x8A\x94(\xEC\x9D\x80)", "\xEC\x9D\x80", "\xEB\x8A\x94" }, // 는(은)
		{ "\xEA\xB3\xBC(\xEC\x99\x80)", "\xEA\xB3\xBC", "\xEC\x99\x80" }, // 과(와)
		{ "\xEC\x99\x80(\xEA\xB3\xBC)", "\xEA\xB3\xBC", "\xEC\x99\x80" }, // 와(과)
		{ "(\xEC\x9D\xB4)", "\xEC\x9D\xB4", "" },                        // (이)
		{ "(\xEC\x9C\xBC)", "\xEC\x9C\xBC", "" },                        // (으)
	};
	if (!s.contains('('))
		return;
	String out;
	uint i = 0;
	while (i < s.size()) {
		bool done = false;
		if ((byte)s[i] >= 0x80 || s[i] == '(') {
			for (uint k = 0; k < ARRAYSIZE(kPairs) && !done; k++) {
				const uint len = strlen(kPairs[k][0]);
				if (s.size() - i < len || memcmp(s.c_str() + i, kPairs[k][0], len))
					continue;
				const int fc = finalConsonant(out, out.size());
				const bool isEu = k == ARRAYSIZE(kPairs) - 1;
				// Nothing to read: keep both forms, as written.
				if (fc < 0)
					out += String(s.c_str() + i, len);
				else
					out += kPairs[k][(fc == 1 || (fc == 2 && !isEu)) ? 1 : 2];
				i += len;
				done = true;
			}
		}
		if (!done)
			out += s[i++];
	}
	s = out;
}

} // End of namespace Common
