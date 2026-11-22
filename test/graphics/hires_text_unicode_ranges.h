#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/str.h"
#include "graphics/hires_text/unicode_ranges.h"

class HiResUnicodeRangesTestSuite : public CxxTest::TestSuite {
	static Graphics::HiResRangeSpec spec(const char *text) {
		Graphics::HiResRangeSpec s;
		Common::String error;
		TS_ASSERT(Graphics::parseRangeSpec(text, s, error));
		return s;
	}

	static Graphics::HiResRangeScope scope(const char *a, int va, const char *b = nullptr, int vb = 0,
										   const char *c = nullptr, int vc = 0) {
		Graphics::HiResRangeScope s;
		s.specs.push_back(spec(a));
		s.values.push_back(va);
		if (b) {
			s.specs.push_back(spec(b));
			s.values.push_back(vb);
		}
		if (c) {
			s.specs.push_back(spec(c));
			s.values.push_back(vc);
		}
		return s;
	}

public:
	void test_block_names() {
		Graphics::HiResSpan s;
		TS_ASSERT(Graphics::lookupUnicodeBlock("basic-latin", s));
		TS_ASSERT_EQUALS(s.lo, 0x20u);
		TS_ASSERT_EQUALS(s.hi, 0x7Eu);
		TS_ASSERT(Graphics::lookupUnicodeBlock("Hangul-Syllables", s));
		TS_ASSERT_EQUALS(s.lo, 0xAC00u);
		TS_ASSERT_EQUALS(s.hi, 0xD7A3u);
		TS_ASSERT(Graphics::lookupUnicodeBlock("general-punctuation", s));
		TS_ASSERT_EQUALS(s.lo, 0x2000u);
		TS_ASSERT(Graphics::lookupUnicodeBlock("pua", s));
		TS_ASSERT_EQUALS(s.lo, 0xE000u);
		TS_ASSERT_EQUALS(s.hi, 0xF8FFu);
		TS_ASSERT(Graphics::lookupUnicodeBlock("misc-symbols", s));
		TS_ASSERT_EQUALS(s.lo, 0x2600u);
		TS_ASSERT(!Graphics::lookupUnicodeBlock("latin", s));
	}

	void test_explicit_spans() {
		Graphics::HiResRangeSpec s = spec("U+2026");
		TS_ASSERT_EQUALS(s.kind, Graphics::kHiResSpecSpan);
		TS_ASSERT_EQUALS(s.span.lo, 0x2026u);
		TS_ASSERT_EQUALS(s.span.hi, 0x2026u);
		s = spec("u+0020-007e");
		TS_ASSERT_EQUALS(s.span.lo, 0x20u);
		TS_ASSERT_EQUALS(s.span.hi, 0x7Eu);
		s = spec("U+E000-U+E0FF");
		TS_ASSERT_EQUALS(s.span.hi, 0xE0FFu);
		s = spec("wide");
		TS_ASSERT_EQUALS(s.kind, Graphics::kHiResSpecWide);
	}

	void test_bad_specs() {
		Graphics::HiResRangeSpec s;
		Common::String error;
		TS_ASSERT(!Graphics::parseRangeSpec("U+007E-0020", s, error));   // reversed
		TS_ASSERT(!Graphics::parseRangeSpec("U+110000", s, error));      // beyond Unicode
		TS_ASSERT(!Graphics::parseRangeSpec("0x20-0x7e", s, error));     // hex form is for [glyphs] keys only
		TS_ASSERT(!Graphics::parseRangeSpec("latin", s, error));
		TS_ASSERT(!error.empty());
	}

	void test_narrowest_wins_inside_a_scope() {
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("basic-latin", 1, "U+0041-005A", 2, "U+0051", 3));
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup('a'), 1);
		TS_ASSERT_EQUALS(t.lookup('B'), 2);
		TS_ASSERT_EQUALS(t.lookup('Q'), 3);
		TS_ASSERT_EQUALS(t.lookup(0x7F), -1);
		TS_ASSERT_EQUALS(t.lookup(0xAC00), -1);
	}

	void test_a_more_specific_scope_beats_any_width() {
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("basic-latin", 7));            // [font.N]
		scopes.push_back(scope("U+0041", 1));                 // [font]
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup('A'), 7);
	}

	void test_wide_is_least_specific_in_its_scope() {
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("wide", 4, "U+AC00", 5));
		scopes.push_back(scope("hangul-syllables", 9));
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup(0xAC00), 5);   // explicit span in scope 0
		TS_ASSERT_EQUALS(t.lookup(0xAC01), 4);   // wide in scope 0 beats scope 1
		TS_ASSERT_EQUALS(t.lookup('A'), -1);     // not wide, no span
	}

	void test_outside_the_bmp_and_empty() {
		Graphics::HiResRangeTable empty;
		TS_ASSERT(empty.empty());
		TS_ASSERT_EQUALS(empty.lookup('A'), -1);
		Common::Array<Graphics::HiResRangeScope> scopes;
		scopes.push_back(scope("U+1F600-1F64F", 2));
		Graphics::HiResRangeTable t;
		t.compile(scopes);
		TS_ASSERT_EQUALS(t.lookup(0x1F602), 2);
		TS_ASSERT_EQUALS(t.lookup(0x1F650), -1);
	}

	void test_hash_is_stable_and_discriminates() {
		Common::Array<Graphics::HiResRangeScope> a, b;
		a.push_back(scope("basic-latin", 1));
		b.push_back(scope("basic-latin", 2));
		Graphics::HiResRangeTable ta, ta2, tb;
		ta.compile(a);
		ta2.compile(a);
		tb.compile(b);
		TS_ASSERT_EQUALS(ta.hash(), ta2.hash());
		TS_ASSERT_DIFFERS(ta.hash(), tb.hash());
	}
};
