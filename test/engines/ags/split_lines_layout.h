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

#include "common/str.h"
#include "common/ustr.h"

// unicode.h declares an enum { LC_CTYPE } for the engine; the runner has
// already seen <locale.h>'s macro of that name.
#pragma push_macro("LC_CTYPE")
#undef LC_CTYPE
#include "ags/lib/allegro/unicode.h"
#pragma pop_macro("LC_CTYPE")
#include "ags/shared/font/ags_text_layout.h"

namespace {

/** 10 px per character, whatever the format: counts units of the decoder. */
class AgsFakeLayoutMetrics : public AGS3::AgsLayoutMetrics {
public:
	explicit AgsFakeLayoutMetrics(int uformat) : calls(0), _dec(uformat) {}
	int calls;

protected:
	int measureBytes(const char *s, uint32 len) override {
		calls++;
		Graphics::TextRun run;
		run.decode((const byte *)s, len, _dec);
		return 10 * (int)run.size();
	}

private:
	AGS3::AgsTextDecoder _dec;
};

struct AgsLaidOut {
	Common::Array<Common::String> lines;
	bool ok;
};

AgsLaidOut agsLayout(const char *text, int uformat, int maxWidth) {
	AgsFakeLayoutMetrics m(uformat);
	m.setText(text);
	Common::Array<AGS3::AgsLineSpan> spans;
	AgsLaidOut r;
	r.ok = AGS3::ags_layout_lines(text, strlen(text), uformat, maxWidth, m, Graphics::BreakRules(), spans);
	for (uint i = 0; i < spans.size(); i++)
		r.lines.push_back(Common::String(text + spans[i].start, spans[i].end - spans[i].start));
	return r;
}

} // End of anonymous namespace

/**
 * AGS split_lines() through the shared layout stage for every text format
 * but U_ASCII (I18N_TEXT_DESIGN.md section 4.3, AGS row).
 */
class AgsSplitLinesLayoutTestSuite : public CxxTest::TestSuite {
public:
	void test_only_ascii_keeps_the_old_path() {
		TS_ASSERT(AGS3::split_lines_uses_layout(U_UTF8));
		TS_ASSERT(AGS3::split_lines_uses_layout(U_EUCKR));
		TS_ASSERT(!AGS3::split_lines_uses_layout(U_ASCII));
	}

	void test_japanese_line_never_starts_with_a_full_stop() {
		// Ten characters fit; the eleventh is "。", which may not begin a line,
		// so the tenth goes down with it.
		const char *text = "あいうえおかきくけこ。さしす";
		const AgsLaidOut r = agsLayout(text, U_UTF8, 100);
		TS_ASSERT(r.ok);
		TS_ASSERT_EQUALS(r.lines.size(), 2u);
		Common::String joined;
		for (uint i = 0; i < r.lines.size(); i++) {
			TS_ASSERT(!r.lines[i].hasPrefix("。"));
			joined += r.lines[i];
		}
		TS_ASSERT_EQUALS(joined, Common::String(text));
		TS_ASSERT_EQUALS(r.lines[1], Common::String("こ。さしす"));
	}

	void test_utf8_words_wrap_at_spaces() {
		const AgsLaidOut r = agsLayout("hello world", U_UTF8, 60);
		TS_ASSERT_EQUALS(r.lines.size(), 2u);
		TS_ASSERT_EQUALS(r.lines[0], "hello");
		TS_ASSERT_EQUALS(r.lines[1], "world");
	}

	void test_thai_marks_stay_on_their_base() {
		// ที่นี่ไม่มี: every line must start on a base, never on U+0E35/U+0E48/U+0E31.
		const char *text = "ที่นี่ไม่มีใคร";
		const AgsLaidOut r = agsLayout(text, U_UTF8, 30);
		TS_ASSERT(r.ok);
		TS_ASSERT(r.lines.size() > 1);
		Common::String joined;
		for (uint i = 0; i < r.lines.size(); i++) {
			const Common::U32String u = r.lines[i].decode(Common::kUtf8);
			TS_ASSERT(!u.empty());
			if (!u.empty()) {
				TS_ASSERT(u[0] != 0x0E35 && u[0] != 0x0E48 && u[0] != 0x0E31 && u[0] != 0x0E34);
			}
			joined += r.lines[i];
		}
		TS_ASSERT_EQUALS(joined, Common::String(text));
	}

	void test_newlines_as_split_lines_cuts_them() {
		AgsLaidOut r = agsLayout("ab\ncd", U_UTF8, 100);
		TS_ASSERT_EQUALS(r.lines.size(), 2u);
		TS_ASSERT_EQUALS(r.lines[0], "ab");
		TS_ASSERT_EQUALS(r.lines[1], "cd");
		r = agsLayout("ab\n", U_UTF8, 100);
		TS_ASSERT_EQUALS(r.lines.size(), 1u);
		r = agsLayout("\n\nx", U_UTF8, 100);
		TS_ASSERT_EQUALS(r.lines.size(), 3u);
		TS_ASSERT_EQUALS(r.lines[0], "");
		TS_ASSERT_EQUALS(r.lines[1], "");
		TS_ASSERT_EQUALS(r.lines[2], "x");
		// The last line keeps its trailing spaces, as before.
		r = agsLayout("ab  ", U_UTF8, 100);
		TS_ASSERT_EQUALS(r.lines.size(), 1u);
		TS_ASSERT_EQUALS(r.lines[0], "ab  ");
	}

	void test_nothing_fits() {
		const AgsLaidOut r = agsLayout("abc", U_UTF8, 5);
		TS_ASSERT(!r.ok);
		TS_ASSERT_EQUALS(r.lines.size(), 0u);
	}

	void test_euckr_pairs_are_one_unit() {
		// 가나 다라 (EUC-KR): Hangul breaks at spaces (hangul=word).
		const char *text = "\xb0\xa1\xb3\xaa \xb4\xd9\xb6\xf3";
		const AgsLaidOut r = agsLayout(text, U_EUCKR, 30);
		TS_ASSERT_EQUALS(r.lines.size(), 2u);
		TS_ASSERT_EQUALS(r.lines[0], "\xb0\xa1\xb3\xaa");
		TS_ASSERT_EQUALS(r.lines[1], "\xb4\xd9\xb6\xf3");
		AGS3::AgsTextDecoder dec(U_EUCKR);
		uint32 cp = 0;
		byte flags = 0;
		TS_ASSERT_EQUALS(dec.decode((const byte *)text, (const byte *)text + 2, cp, flags), 2);
		TS_ASSERT_EQUALS(cp, 0xAC00u);
	}

	void test_widths_are_measured_not_added() {
		// extend() re-measures the whole prefix (outline and kerning make
		// AGS widths non-additive): one measurement per unit tried.
		AgsFakeLayoutMetrics m(U_UTF8);
		const char *text = "abcd";
		m.setText(text);
		Graphics::TextRun run;
		AGS3::AgsTextDecoder dec(U_UTF8);
		run.decode((const byte *)text, 4, dec);
		TS_ASSERT_EQUALS(m.extend(run, 1, 2, 999), 20);
		TS_ASSERT_EQUALS(m.width(run, 0, 4), 40);
	}
};
