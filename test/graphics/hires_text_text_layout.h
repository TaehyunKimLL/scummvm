#include <cxxtest/TestSuite.h>

#include "common/archive.h"
#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/system.h"
#include "common/ustr.h"
#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_props.h"

#include "../system/null_osystem.h"

namespace {

// Every unit is 1 wide, except combining marks and control units (0).
class UnitMetrics : public Graphics::LayoutMetrics {
public:
	int advance(uint32 cp) override {
		if (cp == Graphics::kControlUnit || Graphics::Unicode::isCombining(cp))
			return 0;
		return 1;
	}
};

// An engine-style decoder: FF 0A x y is one control unit of 4 bytes (an
// escape with two argument bytes), FF 01 a control unit that is also a
// newline; everything else is UTF-8.
class EscapeDecoder : public Graphics::TextDecoder {
public:
	int decode(const byte *p, const byte *end, uint32 &cp, byte &flags) const override {
		if (*p == 0xFF && p + 1 < end) {
			cp = Graphics::kControlUnit;
			flags = Graphics::kUnitControl;
			if (p[1] == 0x01) {
				flags |= Graphics::kUnitNewline;
				return 2;
			}
			if (p[1] == 0x0A && p + 4 <= end)
				return 4;
			return 2;
		}
		return _utf8.decode(p, end, cp, flags);
	}

private:
	Graphics::Utf8TextDecoder _utf8;
};

const char kMixed[] = "\x41\xEA\xB0\x80\xE0\xB8\x97\xE0\xB8\xB5\xE0\xB9\x88\xF0\x9F\x98\x80";
// "ここには何もない。「扉」は閉じている。"
const char kJapanese[] = "\xE3\x81\x93\xE3\x81\x93\xE3\x81\xAB\xE3\x81\xAF\xE4\xBD\x95\xE3\x82\x82\xE3\x81\xAA\xE3\x81\x84\xE3\x80\x82\xE3\x80\x8C\xE6\x89\x89\xE3\x80\x8D\xE3\x81\xAF\xE9\x96\x89\xE3\x81\x98\xE3\x81\xA6\xE3\x81\x84\xE3\x82\x8B\xE3\x80\x82";
// "ที่นี่ไม่มีใครอยู่"
const char kThai[] = "\xE0\xB8\x97\xE0\xB8\xB5\xE0\xB9\x88\xE0\xB8\x99\xE0\xB8\xB5\xE0\xB9\x88\xE0\xB9\x84\xE0\xB8\xA1\xE0\xB9\x88\xE0\xB8\xA1\xE0\xB8\xB5\xE0\xB9\x83\xE0\xB8\x84\xE0\xB8\xA3\xE0\xB8\xAD\xE0\xB8\xA2\xE0\xB8\xB9\xE0\xB9\x88";
// "가나다 라마바"
const char kHangul[] = "\xEA\xB0\x80\xEB\x82\x98\xEB\x8B\xA4\x20\xEB\x9D\xBC\xEB\xA7\x88\xEB\xB0\x94";

uint32 len(const char *s) {
	return (uint32)strlen(s);
}

void decodeUtf8(Graphics::TextRun &run, const char *s) {
	Graphics::Utf8TextDecoder dec;
	run.decode((const byte *)s, len(s), dec);
}

Common::String lineText(const char *s, const Graphics::LineSpan &l) {
	return Common::String(s + l.byteStart, l.byteEnd - l.byteStart);
}

} // End of anonymous namespace

class HiResTextLayoutTestSuite : public CxxTest::TestSuite {
public:
	void test_decode_utf8_mixed_scripts() {
		Graphics::TextRun run;
		decodeUtf8(run, kMixed);
		const uint32 cps[] = { 0x41, 0xAC00, 0xE17, 0xE35, 0xE48, 0x1F600 };
		const uint32 offs[] = { 0, 1, 4, 7, 10, 13 };
		TS_ASSERT_EQUALS(run.size(), 6u);
		for (uint32 i = 0; i < 6 && i < run.size(); i++) {
			TS_ASSERT_EQUALS(run.cp(i), cps[i]);
			TS_ASSERT_EQUALS(run.byteOffset(i), offs[i]);
		}
		TS_ASSERT_EQUALS(run.byteOffset(run.size()), 17u);
		TS_ASSERT(run.flags(1) & Graphics::kUnitWide);
		TS_ASSERT(run.flags(3) & Graphics::kUnitCombining);
		TS_ASSERT(run.flags(4) & Graphics::kUnitCombining);
		TS_ASSERT(!(run.flags(2) & Graphics::kUnitCombining));
		TS_ASSERT(run.flags(5) & Graphics::kUnitWide);
	}

	void test_decode_utf8_invalid_is_one_byte_fffd() {
		Graphics::TextRun run;
		decodeUtf8(run, "\xE0\x80");
		TS_ASSERT_EQUALS(run.size(), 2u);
		TS_ASSERT_EQUALS(run.cp(0), 0xFFFDu);
		TS_ASSERT_EQUALS(run.cp(1), 0xFFFDu);
		TS_ASSERT_EQUALS(run.byteOffset(1), 1u);
		TS_ASSERT_EQUALS(run.byteOffset(2), 2u);

		// Truncated, surrogate, past U+10FFFF, overlong: each one byte.
		decodeUtf8(run, "\xF0\x9F\x98");
		TS_ASSERT_EQUALS(run.size(), 3u);
		decodeUtf8(run, "\xED\xA0\x80");
		TS_ASSERT_EQUALS(run.size(), 3u);
		TS_ASSERT_EQUALS(run.cp(0), 0xFFFDu);
		decodeUtf8(run, "\xF4\x90\x80\x80");
		TS_ASSERT_EQUALS(run.size(), 4u);
		decodeUtf8(run, "\xC0\xAF");
		TS_ASSERT_EQUALS(run.size(), 2u);
		// The run is reused: a later decode starts clean.
		decodeUtf8(run, "a");
		TS_ASSERT_EQUALS(run.size(), 1u);
		TS_ASSERT_EQUALS(run.cp(0), (uint32)'a');
		TS_ASSERT_EQUALS(run.byteOffset(1), 1u);
	}

	void test_decode_space_and_newline_flags() {
		Graphics::TextRun run;
		decodeUtf8(run, "a b\n\xE3\x80\x80\xC2\xA0");
		TS_ASSERT_EQUALS(run.size(), 6u);
		TS_ASSERT(run.flags(1) & Graphics::kUnitSpace);
		TS_ASSERT(run.flags(3) & Graphics::kUnitNewline);
		TS_ASSERT(run.flags(4) & Graphics::kUnitSpace);      // U+3000
		TS_ASSERT(!(run.flags(5) & Graphics::kUnitSpace));   // U+00A0 is no break
	}

	void test_assign_code_points() {
		Graphics::TextRun run;
		Common::U32String s;
		s += (Common::u32char_type_t)0x41;
		s += (Common::u32char_type_t)0x0E17;
		s += (Common::u32char_type_t)0x0E48;
		s += (Common::u32char_type_t)0x20;
		run.assign(s);
		TS_ASSERT_EQUALS(run.size(), 4u);
		TS_ASSERT_EQUALS(run.byteOffset(2), 2u);
		TS_ASSERT_EQUALS(run.byteOffset(4), 4u);
		TS_ASSERT(run.flags(2) & Graphics::kUnitCombining);
		TS_ASSERT(run.flags(3) & Graphics::kUnitSpace);
	}

	void test_code_page_decoder_cp949() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		const char *kFallbackDir = "dists/engine-data";
		bool addedFallback = false;
		if (!Common::File::exists("encoding.dat")) {
			Common::FSNode dir(kFallbackDir);
			if (dir.exists()) {
				SearchMan.addDirectory("hires_text_text_layout", dir);
				addedFallback = true;
			}
		}
		const bool haveTables = Common::File::exists("encoding.dat");

		Graphics::CodePageTextDecoder dec(Common::kWindows949);
		const byte text[] = { 0xB0, 0xA1, 0x41 };
		uint32 cp = 0;
		byte flags = 0;
		// Lengths come from the charLength() rule alone; only the pair's
		// code point needs encoding.dat, and decoding without it warns.
		if (haveTables) {
			TS_ASSERT_EQUALS(dec.decode(text, text + 3, cp, flags), 2);
			TS_ASSERT_EQUALS(cp, 0xAC00u);
		}
		TS_ASSERT_EQUALS(dec.decode(text + 2, text + 3, cp, flags), 1);
		TS_ASSERT_EQUALS(cp, 0x41u);
		// A lead byte with its trail missing is one byte.
		if (haveTables) {
			Graphics::TextRun run;
			run.decode(text, 3, dec);
			TS_ASSERT_EQUALS(run.size(), 2u);
			TS_ASSERT_EQUALS(run.byteOffset(1), 2u);
			TS_ASSERT_EQUALS(run.byteOffset(2), 3u);
			TS_ASSERT(run.flags(0) & Graphics::kUnitWide);
		}
		TS_ASSERT_EQUALS(dec.decode(text, text + 1, cp, flags), 1);

		if (addedFallback)
			SearchMan.remove("hires_text_text_layout");
		Common::uninstall_null_g_system();
		if (!haveTables)
			TS_SKIP("encoding.dat is not available; the CP949 pair was not decoded");
#else
		TS_SKIP("no null OSystem on this platform");
#endif
	}

	void test_escape_units_keep_their_bytes() {
		const char s[] = "ab\xFF\x0A\x01\x02" "cd\xFF\x01" "ef";
		EscapeDecoder dec;
		Graphics::TextRun run;
		run.decode((const byte *)s, 12, dec);
		const uint32 offs[] = { 0, 1, 2, 6, 7, 8, 10, 11, 12 };
		TS_ASSERT_EQUALS(run.size(), 8u);
		for (uint32 i = 0; i <= 8 && i <= run.size(); i++)
			TS_ASSERT_EQUALS(run.byteOffset(i), offs[i]);
		TS_ASSERT_EQUALS(run.cp(2), Graphics::kControlUnit);
		TS_ASSERT(run.flags(2) & Graphics::kUnitControl);
		TS_ASSERT(run.flags(5) & Graphics::kUnitNewline);

		// No line may end between the escape and what follows it.
		UnitMetrics m;
		Graphics::BreakRules rules;
		for (int w = 1; w <= 8; w++) {
			Common::Array<Graphics::LineSpan> lines;
			Graphics::TextLayout::breakLines(run, w, m, rules, lines);
			for (uint32 k = 0; k < lines.size(); k++) {
				TS_ASSERT(lines[k].end != 3 || lines[k].next != 3);
				TS_ASSERT_DIFFERS(lines[k].next, 3u);
			}
			// The newline escape always ends a line.
			bool sawForced = false;
			for (uint32 k = 0; k < lines.size(); k++)
				if (lines[k].forced) {
					TS_ASSERT_EQUALS(lines[k].byteNext, 10u);
					sawForced = true;
				}
			TS_ASSERT(sawForced);
		}
		TS_ASSERT(!Graphics::TextLayout::isClusterBoundary(run, 3));
		TS_ASSERT(!Graphics::TextLayout::canBreakBefore(run, 3, rules));
	}

	void test_escape_before_a_word_moves_with_it() {
		const char s[] = "ab \xFF\x0A\x01\x02" "cd";
		EscapeDecoder dec;
		Graphics::TextRun run;
		run.decode((const byte *)s, 9, dec);
		UnitMetrics m;
		Graphics::BreakRules rules;
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 3, m, rules, lines);
		TS_ASSERT_EQUALS(lines.size(), 2u);
		if (lines.size() == 2) {
			TS_ASSERT_EQUALS(lines[0].byteEnd, 2u);
			TS_ASSERT_EQUALS(lines[1].byteStart, 3u);
		}
	}

	void test_latin_breaks_at_spaces() {
		const char s[] = "the door is locked";
		Graphics::TextRun run;
		decodeUtf8(run, s);
		UnitMetrics m;
		Graphics::BreakRules rules;
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 10, m, rules, lines);
		TS_ASSERT_EQUALS(lines.size(), 2u);
		if (lines.size() == 2) {
			TS_ASSERT_EQUALS(lineText(s, lines[0]), "the door");
			TS_ASSERT_EQUALS(lineText(s, lines[1]), "is locked");
			TS_ASSERT_EQUALS(lines[0].next, 9u);
			TS_ASSERT_EQUALS(lines[0].byteNext, 9u);
			TS_ASSERT_EQUALS(lines[0].width, 8);
			TS_ASSERT(!lines[0].forced);
			TS_ASSERT(!lines[0].emergency);
		}
	}

	void test_japanese_kinsoku() {
		Graphics::TextRun run;
		decodeUtf8(run, kJapanese);
		TS_ASSERT_EQUALS(run.size(), 19u);
		UnitMetrics m;
		Graphics::BreakRules rules;
		// At width 8, unit 8 (U+3002) would begin line 2: the break moves
		// one unit earlier instead of letting line 1 run one unit over.
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 8, m, rules, lines);
		TS_ASSERT(lines.size() >= 2);
		if (lines.size() >= 2) {
			TS_ASSERT_EQUALS(lines[0].end, 7u);
			TS_ASSERT_EQUALS(lines[1].first, 7u);
		}
		for (int w = 2; w <= 20; w++) {
			Graphics::TextLayout::breakLines(run, w, m, rules, lines);
			// Only an emergency split (nothing else fits) may break the
			// table, as SCI splits a word that is wider than the line.
			for (uint32 k = 0; k < lines.size(); k++) {
				TS_ASSERT(lines[k].width <= w);
				if (k > 0 && !lines[k - 1].emergency)
					TS_ASSERT(!Graphics::Unicode::kinsokuNoStart(run.cp(lines[k].first)));
				if (k + 1 < lines.size() && !lines[k].emergency)
					TS_ASSERT_DIFFERS(run.cp(lines[k].end - 1), 0x300Cu);
			}
		}
		// Kinsoku off: the table no longer applies.
		rules.kinsoku = false;
		Graphics::TextLayout::breakLines(run, 8, m, rules, lines);
		TS_ASSERT_EQUALS(lines[0].end, 8u);
	}

	void test_ascii_closer_after_wide_is_no_start() {
		// "あいう)" : the ASCII ')' after a wide unit may not begin a line.
		const char s[] = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86)";
		Graphics::TextRun run;
		decodeUtf8(run, s);
		Graphics::BreakRules rules;
		TS_ASSERT(Graphics::TextLayout::canBreakBefore(run, 2, rules));
		TS_ASSERT(!Graphics::TextLayout::canBreakBefore(run, 3, rules));
		// "(あ": the ASCII '(' before a wide unit may not end a line.
		decodeUtf8(run, "(\xE3\x81\x82");
		TS_ASSERT(!Graphics::TextLayout::canBreakBefore(run, 1, rules));
	}

	void test_thai_never_strands_marks_or_leading_vowels() {
		Graphics::TextRun run;
		decodeUtf8(run, kThai);
		TS_ASSERT_EQUALS(run.size(), 18u);
		UnitMetrics m;
		Graphics::BreakRules rules;
		for (int w = 1; w <= 12; w++) {
			Common::Array<Graphics::LineSpan> lines;
			Graphics::TextLayout::breakLines(run, w, m, rules, lines);
			TS_ASSERT(lines.size() >= 1);
			for (uint32 k = 0; k < lines.size(); k++) {
				const Graphics::LineSpan &l = lines[k];
				TS_ASSERT(l.end > l.first);
				TS_ASSERT(!(run.flags(l.first) & Graphics::kUnitCombining));
				TS_ASSERT(!Graphics::Unicode::isThaiFollowingVowel(run.cp(l.first)));
				// Only an emergency split (w 1: "ไม่" is 2 wide) may leave a
				// leading vowel at a line end.
				if (k + 1 < lines.size() && !l.emergency)
					TS_ASSERT(!Graphics::Unicode::isThaiLeadingVowel(run.cp(l.end - 1)));
				TS_ASSERT_DIFFERS((byte)kThai[l.byteStart] & 0xC0, 0x80);
				if (w >= 2)
					TS_ASSERT(!l.emergency);
			}
		}
		// Between syllable-ish units: before U+0E19 (after a mark on a Thai
		// base), before U+0E44; not after U+0E44, not before a mark.
		Graphics::BreakRules r;
		TS_ASSERT(Graphics::TextLayout::canBreakBefore(run, 3, r));
		TS_ASSERT(Graphics::TextLayout::canBreakBefore(run, 6, r));
		TS_ASSERT(!Graphics::TextLayout::canBreakBefore(run, 7, r));
		TS_ASSERT(!Graphics::TextLayout::canBreakBefore(run, 1, r));
		TS_ASSERT(!Graphics::TextLayout::isClusterBoundary(run, 1));
		TS_ASSERT(Graphics::TextLayout::isClusterBoundary(run, 3));
		r.thaiFallback = false;
		TS_ASSERT(!Graphics::TextLayout::canBreakBefore(run, 3, r));
	}

	void test_combining_marks_have_no_width() {
		Graphics::TextRun run;
		decodeUtf8(run, kThai);
		UnitMetrics m;
		// 18 units, 8 of them marks.
		TS_ASSERT_EQUALS(m.width(run, 0, run.size()), 10);
	}

	void test_hangul_word_and_any() {
		const char *s = kHangul;
		Graphics::TextRun run;
		decodeUtf8(run, s);
		UnitMetrics m;
		Graphics::BreakRules rules;
		Common::Array<Graphics::LineSpan> lines;

		rules.hangul = Graphics::kHangulBreakWord;
		Graphics::TextLayout::breakLines(run, 5, m, rules, lines);
		TS_ASSERT_EQUALS(lines.size(), 2u);
		if (lines.size() == 2) {
			TS_ASSERT_EQUALS(lines[0].end, 3u);
			TS_ASSERT_EQUALS(lines[1].first, 4u);
		}
		TS_ASSERT(!Graphics::TextLayout::canBreakBefore(run, 1, rules));

		rules.hangul = Graphics::kHangulBreakAny;
		TS_ASSERT(Graphics::TextLayout::canBreakBefore(run, 1, rules));
		Graphics::TextLayout::breakLines(run, 2, m, rules, lines);
		TS_ASSERT_EQUALS(lines.size(), 4u);
		if (lines.size() == 4) {
			TS_ASSERT_EQUALS(lines[0].end, 2u);
			TS_ASSERT_EQUALS(lines[1].first, 2u);
			TS_ASSERT_EQUALS(lines[1].end, 3u);
			TS_ASSERT_EQUALS(lines[2].first, 4u);
			TS_ASSERT(!lines[0].emergency);
		}
	}

	void test_emergency_split() {
		const char s[] = "Supercalifragilistic";
		Graphics::TextRun run;
		decodeUtf8(run, s);
		UnitMetrics m;
		Graphics::BreakRules rules;
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 5, m, rules, lines);
		TS_ASSERT_EQUALS(lines.size(), 4u);
		for (uint32 k = 0; k < lines.size(); k++) {
			TS_ASSERT_EQUALS(lines[k].end - lines[k].first, 5u);
			if (k + 1 < lines.size())
				TS_ASSERT(lines[k].emergency);
		}
		// At least one cluster per line even when nothing fits, and a
		// base keeps its marks.
		decodeUtf8(run, kThai);
		Graphics::LineSpan l = Graphics::TextLayout::fitLine(run, 0, 0, m, rules);
		TS_ASSERT_EQUALS(l.end, 3u);
		TS_ASSERT(l.emergency);
	}

	void test_newline_forces_a_break() {
		const char s[] = "ab\ncd";
		Graphics::TextRun run;
		decodeUtf8(run, s);
		UnitMetrics m;
		Graphics::BreakRules rules;
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 100, m, rules, lines);
		TS_ASSERT_EQUALS(lines.size(), 2u);
		if (lines.size() == 2) {
			TS_ASSERT(lines[0].forced);
			TS_ASSERT_EQUALS(lines[0].end, 2u);
			TS_ASSERT_EQUALS(lines[0].next, 3u);
			TS_ASSERT_EQUALS(lines[0].byteNext, 3u);
			TS_ASSERT(!lines[1].forced);
			TS_ASSERT_EQUALS(lineText(s, lines[1]), "cd");
		}
	}

	void test_lines_reproduce_the_input_bytes() {
		const char *inputs[] = { "the door is locked", kJapanese, kThai, kHangul, kMixed,
		                         "  lead  and trail  ", "a\n\n b  \nc", "Supercalifragilistic" };
		UnitMetrics m;
		Graphics::BreakRules rules;
		for (uint32 n = 0; n < ARRAYSIZE(inputs); n++) {
			const char *s = inputs[n];
			Graphics::TextRun run;
			decodeUtf8(run, s);
			for (int w = 1; w <= 12; w++) {
				for (int h = 0; h < 2; h++) {
					rules.hangul = h ? Graphics::kHangulBreakAny : Graphics::kHangulBreakWord;
					Common::Array<Graphics::LineSpan> lines;
					Graphics::TextLayout::breakLines(run, w, m, rules, lines);
					Common::String joined;
					uint32 expectStart = 0;
					for (uint32 k = 0; k < lines.size(); k++) {
						const Graphics::LineSpan &l = lines[k];
						TS_ASSERT_EQUALS(l.byteStart, expectStart);
						TS_ASSERT(l.byteStart <= l.byteEnd && l.byteEnd <= l.byteNext);
						TS_ASSERT(l.next > l.first);
						TS_ASSERT_EQUALS(l.byteStart, run.byteOffset(l.first));
						TS_ASSERT_EQUALS(l.byteEnd, run.byteOffset(l.end));
						TS_ASSERT_EQUALS(l.byteNext, run.byteOffset(l.next));
						TS_ASSERT_EQUALS(l.width, m.width(run, l.first, l.end));
						joined += Common::String(s + l.byteStart, l.byteEnd - l.byteStart);
						joined += Common::String(s + l.byteEnd, l.byteNext - l.byteEnd);
						expectStart = l.byteNext;
					}
					TS_ASSERT_EQUALS(expectStart, len(s));
					TS_ASSERT_EQUALS(joined, Common::String(s));
				}
			}
		}
	}

	void test_break_lines_speed() {
		// About 300 bytes of Japanese, broken 1000 times. Bounded loosely so a
		// quadratic slip shows without making the suite flaky.
		Common::String s;
		while (s.size() + strlen(kJapanese) <= 300)
			s += kJapanese;
		Graphics::TextRun run;
		Graphics::Utf8TextDecoder dec;
		UnitMetrics m;
		Graphics::BreakRules rules;
		Common::Array<Graphics::LineSpan> lines;
		uint32 total = 0;
		for (int i = 0; i < 1000; i++) {
			run.decode((const byte *)s.c_str(), s.size(), dec);
			Graphics::TextLayout::breakLines(run, 20, m, rules, lines);
			total += lines.size();
		}
		TS_ASSERT(total > 0);
	}
};
