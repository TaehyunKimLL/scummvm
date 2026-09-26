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

#include "ags/lib/allegro/unicode_euckr.h"

/**
 * The EUC-KR text format AGS uses for the Korean fan patches' legacy .tra
 * files: a KS X 1001 Hangul pair (both bytes 0xA1..0xFE) is one character,
 * its Unicode code point; every other byte is itself, as under U_ASCII.
 */
class AgsEucKrTestSuite : public CxxTest::TestSuite {
public:
	void test_getx_mixed_stream() {
		// U+AC00, 'A', a Windows-1252 byte followed by ASCII (not a pair),
		// 'b', the AGS escape '[', end.
		const char text[] = "\xb0\xa1" "A" "\xe9" "b" "[";
		char *p = const_cast<char *>(text);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0xAC00);
		TS_ASSERT_EQUALS(p - text, 2);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0x41);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0xE9);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0x62);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), '[');
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0);
		TS_ASSERT_EQUALS(p - text, 7);
	}

	void test_getc_does_not_advance() {
		const char text[] = "\xc8\xfe" "x";
		TS_ASSERT_EQUALS(AGS3::euckr_getc(text), 0xD79D); // last KS X 1001 syllable
		TS_ASSERT_EQUALS(AGS3::euckr_getc(text + 2), 'x');
		TS_ASSERT_EQUALS(AGS3::euckr_width(text), 2);
		TS_ASSERT_EQUALS(AGS3::euckr_width(text + 2), 1);
	}

	void test_lone_lead_byte_at_end() {
		const char text[] = "\xb0";
		char *p = const_cast<char *>(text);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0xB0);
		TS_ASSERT_EQUALS(p - text, 1);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0);
		TS_ASSERT_EQUALS(AGS3::euckr_width(text), 1);
	}

	void test_non_hangul_pairs_stay_bytes() {
		// A UHC-only syllable (trail below 0xA1) and a KS X 1001 symbol
		// (not in the Hangul table) are read byte by byte.
		const char uhc[] = "\x8b\x41";
		char *p = const_cast<char *>(uhc);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0x8B);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0x41);
		const char sym[] = "\xa1\xa4";
		p = const_cast<char *>(sym);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0xA1);
		TS_ASSERT_EQUALS(AGS3::euckr_getx(&p), 0xA4);
	}

	void test_setc() {
		char buf[4] = { 0, 0, 0, 0 };
		TS_ASSERT_EQUALS(AGS3::euckr_setc(buf, 0xAC00), 2);
		TS_ASSERT_EQUALS((byte)buf[0], 0xB0);
		TS_ASSERT_EQUALS((byte)buf[1], 0xA1);
		// Not in KS X 1001 (only CP949's extension has it).
		TS_ASSERT_EQUALS(AGS3::euckr_setc(buf, 0xD7A3), 1);
		TS_ASSERT_EQUALS(buf[0], '?');
		TS_ASSERT_EQUALS(AGS3::euckr_setc(buf, 0xE9), 1);
		TS_ASSERT_EQUALS((byte)buf[0], 0xE9);
		TS_ASSERT_EQUALS(AGS3::euckr_setc(buf, '['), 1);
		TS_ASSERT_EQUALS(buf[0], '[');
	}

	void test_cwidth_isok() {
		TS_ASSERT_EQUALS(AGS3::euckr_cwidth('A'), 1);
		TS_ASSERT_EQUALS(AGS3::euckr_cwidth(0xE9), 1);
		TS_ASSERT_EQUALS(AGS3::euckr_cwidth(0xAC00), 2);
		TS_ASSERT(AGS3::euckr_isok(0xAC00));
		TS_ASSERT(AGS3::euckr_isok(0xFF));
		TS_ASSERT(!AGS3::euckr_isok(0xD7A3));
		TS_ASSERT(!AGS3::euckr_isok(-1));
	}

	void test_length() {
		// ustrlen() under U_EUCKR is this loop over the format's getx.
		TS_ASSERT_EQUALS(length("\xb0\xa1\xb0\xa2"), 2);
		TS_ASSERT_EQUALS(length("\xb0\xa1" "A" "\xe9" "b"), 4);
		TS_ASSERT_EQUALS(length("\xb0"), 1);
	}

	void test_length_agrees_with_offsets() {
		// String.Length/GetChars and Substring/Truncate/ReplaceCharAt must
		// count the same units: characters, not bytes, under EUC-KR.
		const char text[] = "\xb0\xa1" "AB";
		TS_ASSERT_EQUALS(length(text), 3);
		TS_ASSERT_EQUALS((int)strlen(text), 4);
		// Character n for every n < length() is reachable by getx.
		TS_ASSERT_EQUALS(charAt(text, 0), 0xAC00);
		TS_ASSERT_EQUALS(charAt(text, 1), 'A');
		TS_ASSERT_EQUALS(charAt(text, 2), 'B');
		// The last character by (length - 1), as s.Substring(s.Length-1, 1).
		TS_ASSERT_EQUALS(charAt(text, length(text) - 1), 'B');
	}

private:
	static int charAt(const char *s, int index) {
		char *p = const_cast<char *>(s);
		int c = 0;
		for (int i = 0; i <= index; i++)
			c = AGS3::euckr_getx(&p);
		return c;
	}


	static int length(const char *s) {
		char *p = const_cast<char *>(s);
		int n = 0;
		while (AGS3::euckr_getx(&p))
			n++;
		return n;
	}
};
