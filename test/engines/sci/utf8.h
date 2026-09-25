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

#include <cxxtest/TestSuite.h>

#include "sci/utf8.h"

/**
 * The single UTF-8 decoder every SCI string path shares once the heap
 * holds UTF-8. kStrLen counts with it, kStrAt indexes with it, GfxText16
 * walks with it - so its answers to "how many bytes" and "which code
 * point" are what keeps those three in agreement.
 */
class SciUtf8TestSuite : public CxxTest::TestSuite {
public:
	static uint32 decode(const char *s, int &bytes) {
		return Sci::decodeUtf8Char((const byte *)s, bytes);
	}

	void test_ascii_is_one_byte() {
		int n = 0;
		TS_ASSERT_EQUALS(decode("A", n), 0x41u);
		TS_ASSERT_EQUALS(n, 1);
		TS_ASSERT_EQUALS(decode("\x7F", n), 0x7Fu);
		TS_ASSERT_EQUALS(n, 1);
	}

	void test_two_three_four_byte_sequences() {
		int n = 0;
		// U+00E9 é
		TS_ASSERT_EQUALS(decode("\xC3\xA9", n), 0xE9u);
		TS_ASSERT_EQUALS(n, 2);
		// U+D504 프 - the character the failed experiment decoded first
		TS_ASSERT_EQUALS(decode("\xED\x94\x84", n), 0xD504u);
		TS_ASSERT_EQUALS(n, 3);
		// U+1F600 😀 - outside the BMP, the case uint16 truncated
		TS_ASSERT_EQUALS(decode("\xF0\x9F\x98\x80", n), 0x1F600u);
		TS_ASSERT_EQUALS(n, 4);
	}

	void test_malformed_input_passes_the_byte_through_and_advances_one() {
		// Every one of these must consume exactly one byte, otherwise a walk
		// over corrupt text could stall or leap over the terminator.
		int n = 0;
		// Stray continuation byte
		TS_ASSERT_EQUALS(decode("\x80", n), 0x80u);
		TS_ASSERT_EQUALS(n, 1);
		// Lead byte, truncated by NUL
		TS_ASSERT_EQUALS(decode("\xED\x94", n), 0xEDu);
		TS_ASSERT_EQUALS(n, 1);
		// Lead byte followed by a non-continuation
		TS_ASSERT_EQUALS(decode("\xC3" "A", n), 0xC3u);
		TS_ASSERT_EQUALS(n, 1);
		// Invalid lead bytes
		TS_ASSERT_EQUALS(decode("\xC0\x80", n), 0xC0u);	// over-long NUL
		TS_ASSERT_EQUALS(n, 1);
		TS_ASSERT_EQUALS(decode("\xFF", n), 0xFFu);
		TS_ASSERT_EQUALS(n, 1);
	}

	void test_overlong_and_surrogate_forms_are_rejected() {
		int n = 0;
		// U+0041 encoded in three bytes: must not decode to 'A', because
		// two byte strings would then mean the same text.
		TS_ASSERT_EQUALS(decode("\xE0\x81\x81", n), 0xE0u);
		TS_ASSERT_EQUALS(n, 1);
		// A UTF-16 surrogate, U+D800, encoded directly
		TS_ASSERT_EQUALS(decode("\xED\xA0\x80", n), 0xEDu);
		TS_ASSERT_EQUALS(n, 1);
		// Above U+10FFFF
		TS_ASSERT_EQUALS(decode("\xF4\x90\x80\x80", n), 0xF4u);
		TS_ASSERT_EQUALS(n, 1);
	}

	void test_legacy_cp949_bytes_do_not_look_like_utf8() {
		// The bytes a cp949 Korean game holds. The gate keeps these away
		// from the decoder. Most, reaching it anyway, come back as
		// themselves one byte at a time: 0xB0..0xC1 is not a UTF-8 lead.
		int n = 0;
		// '가' in cp949 is B0 A1
		TS_ASSERT_EQUALS(decode("\xB0\xA1", n), 0xB0u);
		TS_ASSERT_EQUALS(n, 1);
		// '다' is B4 D9
		TS_ASSERT_EQUALS(decode("\xB4\xD9", n), 0xB4u);
		TS_ASSERT_EQUALS(n, 1);
	}

	void test_some_cp949_hangul_is_valid_utf8_so_the_gate_cannot_be_the_language() {
		// But not all. A cp949 lead of 0xC2..0xC8 followed by a trail of
		// 0xA1..0xBF is a well-formed 2-byte UTF-8 sequence - 217 hangul
		// syllables - and decodes to a different character. So whether a
		// heap is UTF-8 must come from the detection entry (ADGF_UTF8I18N),
		// never from KO_KOR: upstream's Korean translations are cp949.
		int n = 0;
		// '징' in cp949 is C2 A1; as UTF-8 that is U+00A1.
		TS_ASSERT_EQUALS(decode("\xC2\xA1", n), 0xA1u);
		TS_ASSERT_EQUALS(n, 2);
	}

	void test_length_counts_code_points() {
		// "프롤로그" is 4 characters, 12 bytes
		TS_ASSERT_EQUALS(Sci::utf8Length((const byte *)"\xED\x94\x84\xEB\xA1\xA4\xEB\xA1\x9C\xEA\xB7\xB8"), 4u);
		TS_ASSERT_EQUALS(Sci::utf8Length((const byte *)"abc"), 3u);
		TS_ASSERT_EQUALS(Sci::utf8Length((const byte *)""), 0u);
		// Mixed, with a malformed byte in the middle counting as one
		TS_ASSERT_EQUALS(Sci::utf8Length((const byte *)"a\x80" "b"), 3u);
	}

	void test_offset_of_maps_index_to_byte() {
		const byte *s = (const byte *)"a\xED\x94\x84" "b";	// a 프 b
		TS_ASSERT_EQUALS(Sci::utf8OffsetOf(s, 0), 0u);
		TS_ASSERT_EQUALS(Sci::utf8OffsetOf(s, 1), 1u);
		TS_ASSERT_EQUALS(Sci::utf8OffsetOf(s, 2), 4u);
		// Past the end lands on the terminator, never beyond it
		TS_ASSERT_EQUALS(Sci::utf8OffsetOf(s, 3), 5u);
		TS_ASSERT_EQUALS(Sci::utf8OffsetOf(s, 99), 5u);
	}
};
