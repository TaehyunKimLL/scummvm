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

#include "sci/textencoding.h"
#include "sci/utf8.h"

/**
 * C38: Korean text of the SCI fan patches without encoding.dat, and the
 * per-lead-byte font banks of Conquests of Camelot's Korean beta.
 *
 * The test runner has no encoding.dat, which is exactly the situation that
 * turned every Hangul pair of Camelot into U+FFFD: these must still decode.
 */
class SciKoreanTextTestSuite : public CxxTest::TestSuite {
public:
	void test_decode_hangul_without_encoding_dat() {
		// EUC-KR B0 A1 = U+AC00, C8 FE = U+D79D (the last KS X 1001 syllable).
		TS_ASSERT_EQUALS(Sci::decodeCodePagePair(0xB0, 0xA1, Common::kWindows949), 0xAC00u);
		TS_ASSERT_EQUALS(Sci::decodeCodePagePair(0xC8, 0xFE, Common::kWindows949), 0xD79Du);
		// "Camelot" in the patch: C4 AB = U+CE74.
		TS_ASSERT_EQUALS(Sci::decodeCodePagePair(0xC4, 0xAB, Common::kWindows949), 0xCE74u);
	}

	void test_encode_hangul_without_encoding_dat() {
		// Packed as the legacy faces index: lead byte low, trail byte high.
		TS_ASSERT_EQUALS(Sci::encodeCodePagePair(0xAC00, Common::kWindows949), 0xA1B0u);
		TS_ASSERT_EQUALS(Sci::encodeCodePagePair(0xD79D, Common::kWindows949), 0xFEC8u);
		TS_ASSERT_EQUALS(Sci::encodeCodePagePair(0xCE74, Common::kWindows949), 0xABC4u);
		// ASCII is not a pair.
		TS_ASSERT_EQUALS(Sci::encodeCodePagePair('A', Common::kWindows949), 0u);
	}

	void test_round_trip_every_ksx1001_syllable() {
		int bad = 0;
		for (int lead = 0xB0; lead <= 0xC8; lead++) {
			for (int trail = 0xA1; trail <= 0xFE; trail++) {
				const uint32 cp = Sci::decodeCodePagePair(lead, trail, Common::kWindows949);
				if (!cp || Sci::encodeCodePagePair(cp, Common::kWindows949) != ((uint32)lead | ((uint32)trail << 8)))
					bad++;
			}
		}
		TS_ASSERT_EQUALS(bad, 0);
	}

	void test_bank_and_slot() {
		int bank = 0;
		byte slot = 0;
		// Camelot: font 500 slot 0xA1 is 'ga' (B0 A1).
		TS_ASSERT(Sci::koreanBankAndSlot(0xA1B0, 500, bank, slot));
		TS_ASSERT_EQUALS(bank, 500);
		TS_ASSERT_EQUALS(slot, 0xA1);
		// The last row, and the outline set for font 104.
		TS_ASSERT(Sci::koreanBankAndSlot(0xFEC8, 525, bank, slot));
		TS_ASSERT_EQUALS(bank, 549);
		TS_ASSERT_EQUALS(slot, 0xFE);
		// Outside the Hangul rows: symbols (A1..AF), Hanja (CA..FD), ASCII, bad trail.
		TS_ASSERT(!Sci::koreanBankAndSlot(0xA1A1, 500, bank, slot));
		TS_ASSERT(!Sci::koreanBankAndSlot(0xA1CA, 500, bank, slot));
		TS_ASSERT(!Sci::koreanBankAndSlot('A', 500, bank, slot));
		TS_ASSERT(!Sci::koreanBankAndSlot(0x41B0, 500, bank, slot));
		// No banks at all.
		TS_ASSERT(!Sci::koreanBankAndSlot(0xA1B0, -1, bank, slot));
	}

	// --- text_encoding= ---------------------------------------------------

	void test_parse_text_encoding() {
		bool known = false;
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("", known), Sci::kTextEncodingAuto);
		TS_ASSERT(known);
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("auto", known), Sci::kTextEncodingAuto);
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("euc-kr", known), Sci::kTextEncodingEucKr);
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("CP949", known), Sci::kTextEncodingEucKr);
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("utf8", known), Sci::kTextEncodingUtf8);
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("UTF-8", known), Sci::kTextEncodingUtf8);
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("ascii", known), Sci::kTextEncodingAscii);
		TS_ASSERT(known);
		TS_ASSERT_EQUALS(Sci::parseTextEncoding("klingon", known), Sci::kTextEncodingAuto);
		TS_ASSERT(!known);
	}

	void test_auto_is_todays_behaviour() {
		// The code page is the language's, the heap question is detection's,
		// the Korean path is KO_KOR's - each untouched.
		TS_ASSERT_EQUALS(Sci::textEncodingCodePage(Sci::kTextEncodingAuto, Common::kWindows949), Common::kWindows949);
		TS_ASSERT_EQUALS(Sci::textEncodingCodePage(Sci::kTextEncodingAuto, Common::kLatin1), Common::kLatin1);
		TS_ASSERT(Sci::textEncodingHeapIsUtf8(Sci::kTextEncodingAuto, true));
		TS_ASSERT(!Sci::textEncodingHeapIsUtf8(Sci::kTextEncodingAuto, false));
		TS_ASSERT(Sci::textEncodingKorean(Sci::kTextEncodingAuto, Common::KO_KOR));
		TS_ASSERT(!Sci::textEncodingKorean(Sci::kTextEncodingAuto, Common::EN_ANY));
	}

	void test_explicit_key_beats_language_and_detection() {
		// euc-kr alone: an English-detected game gets the Korean path and CP949.
		TS_ASSERT_EQUALS(Sci::textEncodingCodePage(Sci::kTextEncodingEucKr, Common::kLatin1), Common::kWindows949);
		TS_ASSERT(Sci::textEncodingKorean(Sci::kTextEncodingEucKr, Common::EN_ANY));
		// ... and bytes, even where detection or a manifest said UTF-8.
		TS_ASSERT(!Sci::textEncodingHeapIsUtf8(Sci::kTextEncodingEucKr, true));

		// utf8: the heap is UTF-8 even with no manifest; the language keeps its code page and Korean path.
		TS_ASSERT(Sci::textEncodingHeapIsUtf8(Sci::kTextEncodingUtf8, false));
		TS_ASSERT_EQUALS(Sci::textEncodingCodePage(Sci::kTextEncodingUtf8, Common::kWindows949), Common::kWindows949);
		TS_ASSERT(Sci::textEncodingKorean(Sci::kTextEncodingUtf8, Common::KO_KOR));
		TS_ASSERT(!Sci::textEncodingKorean(Sci::kTextEncodingUtf8, Common::EN_ANY));

		// ascii: no double-byte path at all, even with language=ko.
		TS_ASSERT_EQUALS(Sci::textEncodingCodePage(Sci::kTextEncodingAscii, Common::kWindows949), Common::kLatin1);
		TS_ASSERT(!Sci::textEncodingKorean(Sci::kTextEncodingAscii, Common::KO_KOR));
		TS_ASSERT(!Sci::textEncodingHeapIsUtf8(Sci::kTextEncodingAscii, true));
	}
};
