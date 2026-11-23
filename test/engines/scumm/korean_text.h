#include <cxxtest/TestSuite.h>

#include "engines/scumm/korean_text.h"

// Korean engine text: UTF-8 inside, CP949 converted at the boundary (engines/scumm/korean_text.h).
class ScummKoreanTextTestSuite : public CxxTest::TestSuite {
	static Common::String engineText(const char *src, int dstSize = 256, uint *unmapped = nullptr) {
		byte dst[256];
		const int n = Scumm::toEngineText((const byte *)src, strlen(src) + 1, dst, dstSize, unmapped);
		return Common::String((const char *)dst, n);
	}

public:
	void test_ksx1001_table() {
		TS_ASSERT_EQUALS(Scumm::kKsx1001Hangul[0], 0xAC00);	// 가
		TS_ASSERT_EQUALS(Scumm::kKsx1001Hangul[2349], 0xD79D);	// 힝
		for (int i = 1; i < 2350; i++)
			TS_ASSERT_LESS_THAN(Scumm::kKsx1001Hangul[i - 1], Scumm::kKsx1001Hangul[i]);
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xAC00), 0);
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xD55C), (0xC7 - 0xB0) * 94 + (0xD1 - 0xA1));	// 한 = C7 D1
		TS_ASSERT_EQUALS(Scumm::ksx1001Index(0xB620), -1);	// 똠: not in KS X 1001
		TS_ASSERT_EQUALS(Scumm::ksx1001CodePoint(0xC7, 0xD1), 0xD55Cu);
		TS_ASSERT_EQUALS(Scumm::ksx1001CodePoint(0xA1, 0xA6), 0u);
	}

	void test_cp949_to_utf8_keeps_escapes() {
		uint unmapped = 0;
		// 한 FF 04 xx xx 글: an escape's arguments are not text.
		TS_ASSERT_EQUALS(engineText("\xc7\xd1\xff\x04\xb0\xa1\xb1\xdb", 256, &unmapped), "\xed\x95\x9c\xff\x04\xb0\xa1\xea\xb8\x80");
		// A trail byte 0xFE is part of its syllable (BC FE), FE 01 is an escape.
		TS_ASSERT_EQUALS(engineText("\xbc\xfe\xfe\x01" "a", 256, &unmapped), "\xec\x88\xad\xfe\x01" "a");
		TS_ASSERT_EQUALS(unmapped, 0u);
	}

	void test_boundary_is_idempotent() {
		const Common::String once = engineText("\xc7\xd1\xb1\xdb ok");	// 한글 ok
		TS_ASSERT_EQUALS(once, "\xed\x95\x9c\xea\xb8\x80 ok");
		TS_ASSERT_EQUALS(engineText(once.c_str()), once);
		TS_ASSERT_EQUALS(engineText("Give"), "Give");
	}

	void test_cut_on_a_character() {
		// 한글 is 6 bytes of UTF-8; a 6-byte buffer holds 5 + NUL: only 한.
		TS_ASSERT_EQUALS(engineText("\xed\x95\x9c\xea\xb8\x80", 6), "\xed\x95\x9c");
		// An escape is never cut either: "a" FF 04 xx xx in 5 bytes keeps "a".
		TS_ASSERT_EQUALS(engineText("a\xff\x04\x01\x02", 5), "a");
	}

	void test_final_bits() {
		const byte king[] = "\xec\x99\x95";	// 왕
		const byte table[] = "\xed\x85\x8c\xec\x9d\xb4\xeb\xb8\x94";	// 테이블
		const byte key[] = "\xec\x97\xb4\xec\x87\xa0 ab";	// 열쇠 ab
		TS_ASSERT_EQUALS(Scumm::koreanFinalBits(king, king + sizeof(king)), 1);
		TS_ASSERT_EQUALS(Scumm::koreanFinalBits(table, table + sizeof(table)), 3);
		TS_ASSERT_EQUALS(Scumm::koreanFinalBits(key, key + sizeof(key)), 0);
	}

	void test_utf8_judgement() {
		const byte utf8[] = "\xea\xb0\x80\xff\x04\xb0\xa1";
		const byte cp949[] = "\xc7\xd1";
		TS_ASSERT(Scumm::textIsUtf8(utf8, utf8 + sizeof(utf8)));
		TS_ASSERT(!Scumm::textIsUtf8(cp949, cp949 + sizeof(cp949)));
	}

	void test_cp949_syllable_that_reads_as_utf8() {
		// 카 is C4 AB in CP949, which is also the UTF-8 of U+012B: a 2-byte character above U+00FF cannot come from the
		// conversion (it only makes U+0080..U+00FF) and korean.fnt has no glyph for it, so such a text is CP949.
		uint unmapped = 0;
		TS_ASSERT_EQUALS(engineText("\xc4\xab", 256, &unmapped), "\xec\xb9\xb4");
		TS_ASSERT_EQUALS(engineText("\xc7\xd1\xc4\xab"), "\xed\x95\x9c\xec\xb9\xb4");
		TS_ASSERT_EQUALS(unmapped, 0u);
		// The charset glyphs stay UTF-8 (U+00E9 = C3 A9), and a converted text passes again unchanged.
		TS_ASSERT_EQUALS(engineText("\xc3\xa9"), "\xc3\xa9");
		const byte once[] = "\xec\xb9\xb4\xc3\xa9";
		TS_ASSERT(Scumm::textIsUtf8(once, once + sizeof(once)));
		const byte pair[] = "\xc4\xab";
		TS_ASSERT(!Scumm::textIsUtf8(pair, pair + sizeof(pair)));
	}

	void test_dialog_code_page_follows_the_engine_text() {
		// Save names, dialog texts and TTS decode engine text with it: UTF-8 for v1-v6, CP949 for FT/Dig/COMI.
		TS_ASSERT_EQUALS(Scumm::koreanEngineCodePage(true), Common::kUtf8);
		TS_ASSERT_EQUALS(Scumm::koreanEngineCodePage(false), Common::kWindows949);
		// A UTF-8 save name survives the round trip through that page (no encoding.dat needed), the CP949 one does not.
		const Common::U32String name("\xed\x95\x9c\xea\xb8\x80 ok", Common::kUtf8);
		TS_ASSERT_EQUALS(name.encode(Scumm::koreanEngineCodePage(true)), "\xed\x95\x9c\xea\xb8\x80 ok");
	}

	void test_truncated_escape_is_dropped() {
		// A 0xFF at the end, or an escape cut before its arguments, would make the terminating NUL an argument for every
		// later reader (resStrLen() runs past it). It is dropped.
		TS_ASSERT_EQUALS(engineText("34\xff"), "34");
		// The terminator as an escape's last argument (MM's "sound FF 04 C8 00") is a whole escape and stays.
		TS_ASSERT_EQUALS(engineText("a\xff\x04" "b"), Common::String("a\xff\x04" "b\0", 5));
		TS_ASSERT_EQUALS(engineText("\xc7\xd1\xfe"), "\xed\x95\x9c");
		byte dst[16];
		const byte cut[] = { 'a', 0xFF, 0x04, 0x01, 0 };	// FF 04 needs two arguments, one and the terminator
		const int n = Scumm::toEngineText(cut, sizeof(cut), dst, sizeof(dst));
		TS_ASSERT_EQUALS(Scumm::scummTextEnd(dst, dst + n + 1) - dst, n);
	}

	void test_trail_byte_fe_is_no_escape() {
		// 원숭이 is BF F8 BC FE C0 CC: BC FE is one syllable, the text ends at its NUL.
		const byte monkey[] = { 0xBF, 0xF8, 0xBC, 0xFE, 0xC0, 0xCC, 0, 0xAA, 0xBB };
		byte dst[32];
		int n = Scumm::toEngineText(monkey, sizeof(monkey), dst, sizeof(dst));
		TS_ASSERT_EQUALS(Common::String((const char *)dst, n), "\xec\x9b\x90\xec\x88\xad\xec\x9d\xb4");
		const byte plank[] = { 0xB4, 0xC0, 0xBD, 0xBC, 0xC7, 0xD1, 0x20, 0xB3, 0xCE, 0xBA, 0xFE, 0xC1, 0xF6, 0, 1, 2, 3 };
		n = Scumm::toEngineText(plank, sizeof(plank), dst, sizeof(dst));
		TS_ASSERT_EQUALS(Common::String((const char *)dst, n), "\xeb\x8a\x90\xec\x8a\xa8\xed\x95\x9c \xeb\x84\x90\xeb\xb9\xa4\xec\xa7\x80");
		// FE as an escape (a line break, code 1) still passes and still stands in UTF-8 text.
		TS_ASSERT_EQUALS(engineText("\xed\x95\x9c\xfe\x01\xed\x95\x9c"), "\xed\x95\x9c\xfe\x01\xed\x95\x9c");
		TS_ASSERT_EQUALS(engineText("\xc7\xd1\xfe\x01\xc7\xd1"), "\xed\x95\x9c\xfe\x01\xed\x95\x9c");
	}

	void test_one_stray_byte_does_not_make_utf8_text_cp949() {
		// MI1 UTE line 334 of the UTF-8 pack: a raw 0xFA between UTF-8 words. The valid characters stay, 0xFA is the
		// charset glyph U+00FA.
		TS_ASSERT_EQUALS(engineText("\xec\x84\xb8\xec\x9d\xb8\xed\x8a\xb8\xfa\xed\x82\xa4\xec\xb8\xa0(St.\xfa" "Kitts)"),
		                 "\xec\x84\xb8\xec\x9d\xb8\xed\x8a\xb8\xc3\xba\xed\x82\xa4\xec\xb8\xa0(St.\xc3\xba" "Kitts)");
		// A cut final sequence is dropped, the rest stays.
		TS_ASSERT_EQUALS(engineText("\xed\x95\x9c\xea\xb8"), "\xed\x95\x9c");
		// A CP949 text is still CP949.
		TS_ASSERT_EQUALS(engineText("\xc7\xd1\xb1\xdb\xfa"), "\xed\x95\x9c\xea\xb8\x80\xc3\xba");
	}

	void test_boundary_is_idempotent_and_clean_on_hostile_input() {
		// Every output is engine text that ends cleanly (no truncated escape) and passes unchanged a second time.
		static const byte kAlphabet[] = { 0xFF, 0xFE, 0x04, 0x01, 0x08, 0x00, 'a', 0xB0, 0xC7, 0xD1, 0xBC, 0xA1, 0xEA, 0xB0, 0x80,
		                                  0xFA, 0xC3, 0xA9, 0xC4, 0xAB, 0xED, 0x95, 0x9C, 0x20 };
		uint32 seed = 12345;
		for (int iter = 0; iter < 20000; iter++) {
			byte in[14];
			const int len = 1 + (seed = seed * 1103515245u + 12345u) / 65536 % 12;
			for (int i = 0; i < len; i++)
				in[i] = kAlphabet[(seed = seed * 1103515245u + 12345u) / 65536 % sizeof(kAlphabet)];
			in[len] = 0;
			byte a[64], b[64];
			const int na = Scumm::toEngineText(in, len + 1, a, sizeof(a));
			TS_ASSERT_LESS_THAN(na, (int)sizeof(a));
			TS_ASSERT_EQUALS(a[na], 0);
			if (!Scumm::textIsUtf8(a, a + na + 1)) {
				Common::String hex;
				for (int i = 0; i < len; i++)
					hex += Common::String::format("%02x ", in[i]);
				TS_FAIL(hex.c_str());
				break;
			}
			TS_ASSERT_EQUALS(Scumm::scummTextEnd(a, a + na + 1) - a, na);
			const int nb = Scumm::toEngineText(a, na + 1, b, sizeof(b));
			if (nb != na || memcmp(a, b, na)) {
				TS_FAIL("toEngineText is not idempotent");
				break;
			}
		}
	}

	void test_legacy_resource_is_converted_not_guessed() {
		// A save without the UTF-8 trailer holds CP949, so C2/C3 rows are syllables there, never Latin-1: 책 = C3 A5
		// and C3 B2 C3 A2 (챵창) are valid UTF-8 for U+00E5 and U+00F2 U+00E2. (19 real names of Loom, Indy3, Indy4, Zak.)
		Common::Array<byte> out;
		const byte chek[] = { 0xC3, 0xA5, '@', '@', 0 };
		TS_ASSERT(Scumm::legacyCp949Resource(chek, sizeof(chek), out));
		TS_ASSERT_EQUALS(Common::String((const char *)out.begin(), out.size()), Common::String("\xec\xb1\x85@@\0", 6));
		const byte bars[] = { 0xC3, 0xB2, 0xC3, 0xA2, 0 };
		TS_ASSERT(Scumm::legacyCp949Resource(bars, sizeof(bars), out));
		TS_ASSERT_EQUALS(Common::String((const char *)out.begin()), "\xec\xb1\xb5\xec\xb0\xbd");
	}

	void test_legacy_resource_leaves_binary_and_ascii_alone() {
		Common::Array<byte> out;
		// ASCII: nothing to convert.
		const byte ascii[] = { 'G', 'i', 'v', 'e', 0 };
		TS_ASSERT(!Scumm::legacyCp949Resource(ascii, sizeof(ascii), out));
		// A string array of v3-v5 scripts (o5_stringOps): an interior NUL, the data after it is not text.
		const byte bin[] = { 0x0F, 0x90, 0x00, 0x05, 0xC7, 0xD1, 0x00, 0x00 };
		TS_ASSERT(!Scumm::legacyCp949Resource(bin, sizeof(bin), out));
		// Binary without an interior NUL: control bytes and a high byte, no syllable.
		const byte bin2[] = { 0x0F, 0x90, 0x05, 0x01, 0 };
		TS_ASSERT(!Scumm::legacyCp949Resource(bin2, sizeof(bin2), out));
		// Padding after the string (the resource is larger than its text): not one string filling the resource.
		const byte padded[] = { 0xC7, 0xD1, 0, 0, 0, 0 };
		TS_ASSERT(!Scumm::legacyCp949Resource(padded, sizeof(padded), out));
		// No terminator at all.
		const byte open[] = { 0xC7, 0xD1 };
		TS_ASSERT(!Scumm::legacyCp949Resource(open, sizeof(open), out));
		// The trail byte FE does not hide the terminator.
		const byte fe[] = { 0xBC, 0xFE, 0 };
		TS_ASSERT(Scumm::legacyCp949Resource(fe, sizeof(fe), out));
		TS_ASSERT_EQUALS(Common::String((const char *)out.begin()), "\xec\x88\xad");
	}

	void test_legacy_charset_buffer_remaps_its_position() {
		// A save made while a CP949 line was between pages: the read position counts CP949 bytes.
		byte buf[32];
		memset(buf, 0, sizeof(buf));
		const byte line[] = { 0xC7, 0xD1, 0xB1, 0xDB, 0xC7, 0xD1, 0xB1, 0xDB, 'x' };	// 한글한글x
		memcpy(buf, line, sizeof(line));
		int pos = 4;	// after 한글
		TS_ASSERT(Scumm::legacyCp949Buffer(buf, sizeof(buf), pos));
		TS_ASSERT_EQUALS(pos, 6);	// after 한글 in UTF-8
		TS_ASSERT_EQUALS(Common::String((const char *)buf + pos), "\xed\x95\x9c\xea\xb8\x80x");
		TS_ASSERT_EQUALS(buf[sizeof(buf) - 1], 0);
		// A line that does not fit is cut on a character, never inside one.
		byte small[8];
		memset(small, 0, sizeof(small));
		memcpy(small, line, sizeof(line) - 1);
		pos = 6;
		TS_ASSERT(Scumm::legacyCp949Buffer(small, sizeof(small), pos));
		TS_ASSERT_EQUALS(Common::String((const char *)small), "\xed\x95\x9c\xea\xb8\x80");
		TS_ASSERT_EQUALS(pos, 6);
		// Plain ASCII is left alone.
		byte ascii[8] = { 'a', 'b', 0 };
		pos = 1;
		TS_ASSERT(!Scumm::legacyCp949Buffer(ascii, sizeof(ascii), pos));
		TS_ASSERT_EQUALS(pos, 1);
	}
};
