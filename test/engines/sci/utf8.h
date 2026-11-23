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

	void test_encode_round_trips_through_decode() {
		const uint32 cps[] = { 0x41, 0xE9, 0xAC00, 0xD7A3, 0x0E01, 0x1F600 };
		for (uint i = 0; i < ARRAYSIZE(cps); i++) {
			byte buf[5] = { 0, 0, 0, 0, 0 };
			const int n = Sci::encodeUtf8Char(cps[i], buf);
			int bytes = 0;
			TS_ASSERT_EQUALS(Sci::decodeUtf8Char(buf, bytes), cps[i]);
			TS_ASSERT_EQUALS(bytes, n);
		}
		byte buf[4];
		TS_ASSERT_EQUALS(Sci::encodeUtf8Char(0x110000, buf), 3);	// U+FFFD
		TS_ASSERT_EQUALS(buf[0], 0xEF);
	}

	void test_write_offset_follows_a_left_to_right_copy() {
		// LSL1's age quiz: (StrAt dst i (StrAt src (+ i 1))). After "가"
		// (3 bytes) went to index 0 over an old ASCII buffer, index 1 is
		// byte 3, whatever the old bytes after it are.
		const byte *built = (const byte *)"\xEA\xB0\x80" "Johnny";
		TS_ASSERT_EQUALS(Sci::utf8WriteOffset(built, 0), 0u);
		TS_ASSERT_EQUALS(Sci::utf8WriteOffset(built, 1), 3u);
		// Past the terminator: one byte per index, as the byte op had it
		const byte *shortStr = (const byte *)"ab";
		TS_ASSERT_EQUALS(Sci::utf8WriteOffset(shortStr, 2), 2u);
		TS_ASSERT_EQUALS(Sci::utf8WriteOffset(shortStr, 5), 5u);
		TS_ASSERT_EQUALS(Sci::utf8WriteOffset((const byte *)"", 0), 0u);
	}

	void test_index_past_the_terminator_is_a_byte_offset() {
		// The save list is one buffer of fixed-size records, and the
		// dialog asks (StrAt text 36) whether a second record exists.
		// The first record ends long before byte 36, so that index is the
		// record layout's, not a count of characters.
		const byte *rec = (const byte *)"Camelot 1";
		TS_ASSERT(Sci::utf8IndexIsPastEnd(rec, 36));
		// Within the string it is a code point index, the terminator
		// included: a 프 b is 3 characters
		const byte *s = (const byte *)"a\xED\x94\x84" "b";
		TS_ASSERT(!Sci::utf8IndexIsPastEnd(s, 0));
		TS_ASSERT(!Sci::utf8IndexIsPastEnd(s, 2));
		TS_ASSERT(!Sci::utf8IndexIsPastEnd(s, 3));
		TS_ASSERT(!Sci::utf8IndexIsPastEnd(s, 5));
		TS_ASSERT(Sci::utf8IndexIsPastEnd(s, 6));
		// 6 bytes, 2 characters: indexes 3..6 are not a record offset
		// (a script reading one past the end must still see the end)
		const byte *k = (const byte *)"\xEA\xB0\x80\xEA\xB0\x80";
		TS_ASSERT(!Sci::utf8IndexIsPastEnd(k, 3));
		TS_ASSERT(!Sci::utf8IndexIsPastEnd(k, 6));
		TS_ASSERT(Sci::utf8IndexIsPastEnd(k, 7));
		TS_ASSERT(!Sci::utf8IndexIsPastEnd((const byte *)"", 0));
	}

	void test_edit_steps_are_whole_code_points() {
		// "a 가 b" : a(0) 가(1..3) b(4), 5 bytes
		const byte *s = (const byte *)"a\xEA\xB0\x80" "b";
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(s, 5, 5), 4u);
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(s, 5, 4), 1u);
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(s, 5, 1), 0u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(s, 5, 0), 1u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(s, 5, 1), 4u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(s, 5, 4), 5u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(s, 5, 5), 5u);
		// ASCII stays one byte per step
		const byte *e = (const byte *)"abc";
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(e, 3, 3), 2u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(e, 3, 1), 2u);
		// Malformed bytes are one step each: a stray continuation byte, a
		// lead byte whose tail was cut off
		const byte *bad = (const byte *)"\x80" "a\xEA\xB0";
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(bad, 4, 1), 0u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(bad, 4, 0), 1u);
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(bad, 4, 4), 3u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(bad, 4, 2), 3u);
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(bad, 4, 3), 4u);
	}

	void test_code_point_index_and_byte_offset_round_trip() {
		// a(0) 가(1..3) 나(4..6) b(7), 8 bytes
		const byte *s = (const byte *)"a\xEA\xB0\x80\xEB\x82\x98" "b";
		for (uint32 i = 0; i <= 4; i++)
			TS_ASSERT_EQUALS(Sci::utf8IndexOfOffset(s, Sci::utf8OffsetOf(s, i)), i);
		TS_ASSERT_EQUALS(Sci::utf8IndexOfOffset(s, 8), 4u);
		TS_ASSERT_EQUALS(Sci::utf8IndexOfOffset(s, 99), 4u);
		TS_ASSERT_EQUALS(Sci::utf8IndexOfOffset((const byte *)"abc", 2), 2u);
		TS_ASSERT_EQUALS(Sci::utf8IndexOfOffset((const byte *)"", 0), 0u);
		// inside 가 (bytes 1..3): that character counts as before the offset
		TS_ASSERT_EQUALS(Sci::utf8IndexOfOffset(s, 2), 2u);
	}

	void test_edit_steps_over_four_byte_sequences() {
		// a(0) U+1F600 (1..4) b(5), 6 bytes
		const byte *s = (const byte *)"a\xF0\x9F\x98\x80" "b";
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(s, 6, 1), 5u);
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(s, 6, 5), 1u);
		// a valid sequence that ends exactly at len
		TS_ASSERT_EQUALS(Sci::utf8NextBoundary(s, 5, 1), 5u);
		TS_ASSERT_EQUALS(Sci::utf8PrevBoundary(s, 5, 5), 1u);
	}

	void test_edit_control_cursor_index_is_not_a_byte_offset() {
		// kDrawControl hands the edit control the script's cursor selector,
		// a code point index. "파일 봐" reopened with the cursor at its end
		// has cursor 4; read as a byte offset that is the middle byte 0x9D
		// of 일, which the cursor-width measure drew as a box (U+009D).
		const byte *s = (const byte *)"\xED\x8C\x8C\xEC\x9D\xBC \xEB\xB4\x90";
		TS_ASSERT_EQUALS(s[4], 0x9D);
		TS_ASSERT_EQUALS(Sci::utf8OffsetOf(s, 4), 10u);
		TS_ASSERT_EQUALS(Sci::utf8OffsetOf(s, 2), 6u);
	}

	// kStrAt writes (the string is rebuilt, never overwritten byte by byte)
	static Common::String put(const char *text, uint32 index, uint32 cp, uint32 capacity, Sci::Utf8WriteResult &r, uint32 &old) {
		Common::String t(text);
		r = Sci::utf8ReplaceAt(t, index, cp, capacity, old);
		return t;
	}

	void test_replace_ascii_over_hangul() {
		Sci::Utf8WriteResult r;
		uint32 old;
		// "가나" with StrAt(0, 'A'): "A나", valid UTF-8, the old value comes back
		TS_ASSERT_EQUALS(put("\xEA\xB0\x80\xEB\x82\x98", 0, 'A', 0, r, old), "A\xEB\x82\x98");
		TS_ASSERT_EQUALS(r, Sci::kUtf8Replaced);
		TS_ASSERT_EQUALS(old, 0xAC00u);
	}

	void test_replace_hangul_over_ascii() {
		Sci::Utf8WriteResult r;
		uint32 old;
		TS_ASSERT_EQUALS(put("abcd", 0, 0xAC00, 0, r, old), "\xEA\xB0\x80" "bcd");
		TS_ASSERT_EQUALS(old, (uint32)'a');
	}

	void test_replace_hangul_over_hangul() {
		Sci::Utf8WriteResult r;
		uint32 old;
		TS_ASSERT_EQUALS(put("\xEA\xB0\x80\xEB\x82\x98", 1, 0xB2E4, 0, r, old), "\xEA\xB0\x80\xEB\x8B\xA4");
		TS_ASSERT_EQUALS(old, 0xB098u);
	}

	void test_replace_nul_terminates() {
		Sci::Utf8WriteResult r;
		uint32 old;
		TS_ASSERT_EQUALS(put("\xEA\xB0\x80\xEB\x82\x98x", 1, 0, 0, r, old), "\xEA\xB0\x80");
		TS_ASSERT_EQUALS(r, Sci::kUtf8Replaced);
	}

	void test_replace_at_end_appends() {
		Sci::Utf8WriteResult r;
		uint32 old;
		TS_ASSERT_EQUALS(put("\xEA\xB0\x80", 1, 'b', 0, r, old), "\xEA\xB0\x80" "b");
		TS_ASSERT_EQUALS(old, 0u);
		TS_ASSERT_EQUALS(put("ab", 2, 0xAC00, 0, r, old), "ab\xEA\xB0\x80");
		TS_ASSERT_EQUALS(put("ab", 2, 0, 0, r, old), "ab");
	}

	void test_replace_past_end_is_a_byte_offset() {
		Sci::Utf8WriteResult r;
		uint32 old;
		// a fixed-size record: the index is beyond both counts, the caller writes the byte
		put("ab", 7, 'x', 0, r, old);
		TS_ASSERT_EQUALS(r, Sci::kUtf8ByteOffset);
		// between the code point count and the byte length: refused, not a guess
		put("\xEA\xB0\x80\xEB\x82\x98", 4, 'x', 0, r, old);
		TS_ASSERT_EQUALS(r, Sci::kUtf8Refused);
	}

	void test_replace_respects_the_buffer() {
		Sci::Utf8WriteResult r;
		uint32 old;
		// capacity 6 (5 bytes + NUL): 가 + 나 is 6 bytes, so after hangul over 'a' in "abc" the tail is cut
		// on a code point boundary: "가" (3) + "bc" (2) = 5 fits
		TS_ASSERT_EQUALS(put("abc", 0, 0xAC00, 6, r, old), "\xEA\xB0\x80" "bc");
		// capacity 5: 3 + 1 + NUL: "bc" loses its last byte
		TS_ASSERT_EQUALS(put("abc", 0, 0xAC00, 5, r, old), "\xEA\xB0\x80" "b");
		// a tail that would be cut inside a syllable loses the whole syllable
		TS_ASSERT_EQUALS(put("a\xEB\x82\x98", 0, 0xAC00, 6, r, old), "\xEA\xB0\x80");
		// the new code point itself does not fit
		put("abc", 0, 0xAC00, 3, r, old);
		TS_ASSERT_EQUALS(r, Sci::kUtf8Refused);
	}

	void test_truncate_on_a_boundary() {
		TS_ASSERT_EQUALS(Sci::utf8Truncate("abc", 0), "abc");
		TS_ASSERT_EQUALS(Sci::utf8Truncate("abc", 4), "abc");
		TS_ASSERT_EQUALS(Sci::utf8Truncate("abc", 3), "ab");
		TS_ASSERT_EQUALS(Sci::utf8Truncate("\xEA\xB0\x80\xEB\x82\x98", 6), "\xEA\xB0\x80");
		TS_ASSERT_EQUALS(Sci::utf8Truncate("\xEA\xB0\x80\xEB\x82\x98", 7), "\xEA\xB0\x80\xEB\x82\x98");
		TS_ASSERT_EQUALS(Sci::utf8Truncate("\xEA\xB0\x80", 1), "");
		// a 36-byte save name field (kMaxSaveNameLength) holds 11 syllables, not a cut twelfth
		const Common::String twelve("\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80"
		                            "\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80");
		TS_ASSERT_EQUALS(twelve.size(), 36u);
		TS_ASSERT_EQUALS(Sci::utf8Truncate(twelve, 36).size(), 33u);
	}

	// The copy plan of SegManager::strncpy(reg_t, const char *, n): no case may write past the buffer.
	void test_copyPlan_growsOnlyTheUnboundedCopy() {
		const Common::String ko("\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4"); // 9 bytes
		Sci::Utf8CopyPlan a = Sci::utf8CopyPlan(ko, 0xFFFFFFFFU, 4, true);
		TS_ASSERT_EQUALS(a.growTo, 10u);
		TS_ASSERT_EQUALS(a.text, ko);
		TS_ASSERT_EQUALS(a.limit, 10u);
		TS_ASSERT(!a.cut);
		// the same text with a finite limit never grows: it is cut to the buffer
		Sci::Utf8CopyPlan b = Sci::utf8CopyPlan(ko, 100, 4, true);
		TS_ASSERT_EQUALS(b.growTo, 0u);
		TS_ASSERT_EQUALS(b.text, "\xEA\xB0\x80");
		TS_ASSERT(b.cut);
		TS_ASSERT(b.limit <= 4u);
		// refusal: the unbounded copy of a buffer that cannot grow is cut too
		Sci::Utf8CopyPlan c = Sci::utf8CopyPlan(ko, 0xFFFFFFFFU, 7, false);
		TS_ASSERT_EQUALS(c.growTo, 0u);
		TS_ASSERT_EQUALS(c.text, "\xEA\xB0\x80\xEB\x82\x98");
		TS_ASSERT_EQUALS(c.limit, 7u);
	}

	void test_copyPlan_finiteLimitIsClampedToTheBuffer() {
		// n far larger than strlen + 1: the zero padding of a limited copy stays inside a 16 byte buffer
		Sci::Utf8CopyPlan a = Sci::utf8CopyPlan("ab", 1000, 16, true);
		TS_ASSERT_EQUALS(a.text, "ab");
		TS_ASSERT_EQUALS(a.limit, 16u);
		TS_ASSERT_EQUALS(a.growTo, 0u);
		// size unknown: the limit is the caller's
		TS_ASSERT_EQUALS(Sci::utf8CopyPlan("ab", 1000, 0, false).limit, 1000u);
		// a limit that ends inside a syllable rounds down to the one before
		Sci::Utf8CopyPlan b = Sci::utf8CopyPlan(Common::String("\xEA\xB0\x80\xEB\x82", 5), 5, 100, false);
		TS_ASSERT_EQUALS(b.text, "\xEA\xB0\x80");
		TS_ASSERT(b.cut);
		// a limit that ends on a boundary keeps the text
		Sci::Utf8CopyPlan c = Sci::utf8CopyPlan("\xEA\xB0\x80", 3, 100, false);
		TS_ASSERT_EQUALS(c.text, "\xEA\xB0\x80");
		TS_ASSERT(!c.cut);
	}

	void test_copyPlan_textThatFitsIsUntouched() {
		Sci::Utf8CopyPlan a = Sci::utf8CopyPlan("abc", 0xFFFFFFFFU, 4, true);
		TS_ASSERT_EQUALS(a.growTo, 0u);
		TS_ASSERT_EQUALS(a.text, "abc");
		TS_ASSERT_EQUALS(a.limit, 4u);
	}

	// agy (round 2): the non-raw store must not write a NUL one byte past the terminator.
	// The fake array is 2 bytes per slot like reg_t's low/high; the cells after the string are a sentinel.
	struct FakeSink : public Sci::CharSink {
		byte mem[16];
		FakeSink() { memset(mem, 0xAA, sizeof(mem)); }
		void set(uint i, char c) override { mem[i] = (byte)c; }
	};

	void test_storeString_keepsTheSentinelAfterTheTerminator() {
		FakeSink s;
		Sci::storeString(s, "abc", 0xFFFFFFFFU, 16, true);
		TS_ASSERT_EQUALS(s.mem[0], 'a');
		TS_ASSERT_EQUALS(s.mem[3], 0);
		TS_ASSERT_EQUALS(s.mem[4], 0xAA); // untouched
		FakeSink t; // the plan's finite limit text.size()+1 does the same
		Sci::storeString(t, "abc", 4, 16, true);
		TS_ASSERT_EQUALS(t.mem[3], 0);
		TS_ASSERT_EQUALS(t.mem[4], 0xAA);
	}

	void test_storeString_exactFitEmptyAndFiniteLimits() {
		FakeSink a; // 3 characters and the NUL in a 4 byte buffer
		Sci::storeString(a, "abc", 0xFFFFFFFFU, 4, true);
		TS_ASSERT_EQUALS(a.mem[3], 0);
		TS_ASSERT_EQUALS(a.mem[4], 0xAA);
		FakeSink e;
		Sci::storeString(e, "", 0xFFFFFFFFU, 16, true);
		TS_ASSERT_EQUALS(e.mem[0], 0);
		TS_ASSERT_EQUALS(e.mem[1], 0xAA);
		FakeSink shorter; // n shorter than the string: no terminator in the loop, the NUL goes at n
		Sci::storeString(shorter, "abcdef", 3, 16, true);
		TS_ASSERT_EQUALS(shorter.mem[2], 'c');
		TS_ASSERT_EQUALS(shorter.mem[3], 0);
		TS_ASSERT_EQUALS(shorter.mem[4], 0xAA);
		FakeSink longer; // n longer than the string: stops at the terminator
		Sci::storeString(longer, "ab", 10, 16, true);
		TS_ASSERT_EQUALS(longer.mem[2], 0);
		TS_ASSERT_EQUALS(longer.mem[3], 0xAA);
		FakeSink bounded; // never past the array
		Sci::storeString(bounded, "abcdef", 0xFFFFFFFFU, 3, true);
		TS_ASSERT_EQUALS(bounded.mem[2], 'c');
		TS_ASSERT_EQUALS(bounded.mem[3], 0xAA);
	}

	void test_storeString_upstreamBehaviourForOtherGames() {
		FakeSink s; // upstream also writes the NUL at offset n after a terminated copy
		Sci::storeString(s, "ab", 5, 16, false);
		TS_ASSERT_EQUALS(s.mem[2], 0);
		TS_ASSERT_EQUALS(s.mem[5], 0);
	}
};
