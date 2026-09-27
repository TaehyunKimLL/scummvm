#include <cxxtest/TestSuite.h>

#include "common/endian.h"
#include "common/hash-str.h"
#include "common/str.h"
#include "common/ustr.h"
#include "graphics/hires_text/text_layout.h"

#include "engines/grim/localize_text.h"

namespace {

// A game directory holding the named files.
class GrimFakeDir : public Grim::TabFileProbe {
public:
	explicit GrimFakeDir(const char *const *names) {
		for (; *names; names++)
			_files.push_back(*names);
	}
	bool exists(const Common::String &name) const override {
		for (uint i = 0; i < _files.size(); i++)
			if (_files[i].equalsIgnoreCase(name))
				return true;
		return false;
	}

private:
	Common::StringArray _files;
};

// Parse text as Grim does after the magic: data holds a NUL after size.
void grimParse(const Common::String &text, int32 start, Common::StringMap &entries,
		   Common::StringArray *notes = nullptr) {
	entries.clear();
	Grim::parseTabLines(text.c_str(), (int32)text.size(), start, true, false, entries, notes);
}

// Unit widths for the dash rule.
class GrimUnitMetrics : public Graphics::LayoutMetrics {
public:
	int advance(uint32) override { return 1; }
};

// Every character 1 wide; a CP949 pair 2.
class GrimUnitWrapWidths : public Grim::LegacyWrapWidths {
public:
	int32 charWidth(uint32) const override { return 1; }
	int32 wcharWidth(byte, byte) const override { return 2; }
	bool isKoreanChar(byte hi, byte lo) const override { return hi >= 0xB0 && hi <= 0xC8 && lo >= 0xA1 && lo <= 0xFE; }
};

// Text as the engine gets it from an official UTF-16LE table.
Common::U32String fromUtf16(const char *ascii) {
	Common::Array<uint16> u;
	for (const char *p = ascii; *p; p++)
		u.push_back(TO_LE_16((uint16)(byte)*p));
	return Common::U32String::decodeUTF16LE(u.data(), u.size());
}

Common::String utf8(const Common::U32String &s) {
	return s.encode(Common::kUtf8);
}
} // End of anonymous namespace

class GrimLocalizeTestSuite : public CxxTest::TestSuite {
public:
	void test_strip_utf8_bom() {
		const char bom[] = "\xEF\xBB\xBFINTTX38\tx\r\n";
		const char *p = bom;
		int32 size = (int32)strlen(bom);
		TS_ASSERT(Grim::stripUtf8Bom(p, size));
		TS_ASSERT_EQUALS(p, bom + 3);
		TS_ASSERT_EQUALS(size, (int32)strlen(bom) - 3);

		// No mark, a partial mark, too short: untouched.
		const char *cases[] = { "RCNE\x94", "\xEF\xBB", "\xEF\xBB\xBE\x41", "" };
		for (uint i = 0; i < ARRAYSIZE(cases); i++) {
			const char *q = cases[i];
			int32 n = (int32)strlen(q);
			TS_ASSERT(!Grim::stripUtf8Bom(q, n));
			TS_ASSERT_EQUALS(q, cases[i]);
			TS_ASSERT_EQUALS(n, (int32)strlen(cases[i]));
		}
	}

	void test_bom_table_keeps_the_first_key() {
		// What harness/i18n/c11/mkgrimtab.py writes: the mark, then lines.
		const Common::String file("\xEF\xBB\xBFINTTX38\t\xE9\x8E\x8C\r\nmoma069\t\xE3\x81\x93\r\n\r\nafter\tgone\r\n");
		const char *p = file.c_str();
		int32 size = (int32)file.size();
		TS_ASSERT(Grim::stripUtf8Bom(p, size));
		Common::StringMap e;
		grimParse(file, (int32)(p - file.c_str()), e);
		TS_ASSERT(e.contains("INTTX38"));
		TS_ASSERT(!e.contains("NTTX38"));
		TS_ASSERT(!e.contains("TTX38"));
		TS_ASSERT_EQUALS(e["INTTX38"], Common::String("\xE9\x8E\x8C"));
		TS_ASSERT_EQUALS(e["moma069"], Common::String("\xE3\x81\x93"));
		// Grim stops at the first empty line.
		TS_ASSERT(!e.contains("after"));
		TS_ASSERT_EQUALS(e.size(), 2u);
	}

	void test_legacy_start_at_4() {
		// A decoded RCNE table: the magic occupies bytes 0-3.
		Common::StringMap e;
		grimParse("RCNEINTTX38\tscythe\r\nmoma127\tOur relationship\r\n\r\n", 4, e);
		TS_ASSERT_EQUALS(e["INTTX38"], Common::String("scythe"));
		TS_ASSERT_EQUALS(e["moma127"], Common::String("Our relationship"));
		TS_ASSERT_EQUALS(e.size(), 2u);
	}

	void test_continuation_lines() {
		// A line without a TAB is appended to the previous entry
		// (engines/grim/localize.cpp, upstream behaviour), CR dropped.
		Common::StringMap e;
		Common::StringArray notes;
		grimParse("a\tfirst\r\n second part\r\nb\tnext\r\n\r\n", 0, e, &notes);
		TS_ASSERT_EQUALS(e["a"], Common::String("first second part"));
		TS_ASSERT_EQUALS(e["b"], Common::String("next"));
		TS_ASSERT_EQUALS(notes.size(), 1u);

		// Malformed: a continuation before any entry, LF-only lines, a
		// last line without a newline: no crash, nothing invented.
		grimParse(" orphan\r\nk\tv\r\n\nz\t\n", 0, e, &notes);
		TS_ASSERT(!e.contains(""));
		TS_ASSERT_EQUALS(e["k"], Common::String("v"));
		TS_ASSERT(e.contains("z"));
		TS_ASSERT_EQUALS(e["z"], Common::String());
	}

	void test_language_selection() {
		const char *const all[] = { "grim.ja.tab", "grim.ko.tab", "GRIM.TAB", nullptr };
		const char *const korean[] = { "grim.ko.tab", "GRIM.TAB", nullptr };
		GrimFakeDir dirAll(all), dirKorean(korean);
		Common::String warn;

		// The forced language's own table first.
		TS_ASSERT_EQUALS(Grim::selectGrimTab("ja", false, true, dirAll, warn), Common::String("grim.ja.tab"));
		TS_ASSERT(warn.empty());
		// English, or no language: grim.tab, as today.
		TS_ASSERT_EQUALS(Grim::selectGrimTab("en", false, false, dirAll, warn), Common::String("grim.tab"));
		TS_ASSERT(warn.empty());
		TS_ASSERT_EQUALS(Grim::selectGrimTab("", false, false, dirAll, warn), Common::String("grim.tab"));
		TS_ASSERT(warn.empty());
		// The Korean fan patch (detected ko): grim.ko.tab, as today.
		TS_ASSERT_EQUALS(Grim::selectGrimTab("ko", true, false, dirKorean, warn), Common::String("grim.ko.tab"));
		TS_ASSERT(warn.empty());
		TS_ASSERT_EQUALS(Grim::selectGrimTab("", true, false, dirKorean, warn), Common::String("grim.ko.tab"));
		// Forced ja over the Korean patch: ja wins when present.
		TS_ASSERT_EQUALS(Grim::selectGrimTab("ja", true, true, dirAll, warn), Common::String("grim.ja.tab"));
		// th forced over the Korean patch without grim.th.tab: the patch.
		TS_ASSERT_EQUALS(Grim::selectGrimTab("th", true, true, dirKorean, warn), Common::String("grim.ko.tab"));
		TS_ASSERT(!warn.empty());
	}

	void test_missing_table_falls_back_to_english_with_one_warning() {
		const char *const english[] = { "GRIM.TAB", nullptr };
		GrimFakeDir dir(english);
		Common::String warn;
		TS_ASSERT_EQUALS(Grim::selectGrimTab("ja", false, true, dir, warn), Common::String("grim.tab"));
		TS_ASSERT(warn.contains("grim.ja.tab"));
		TS_ASSERT(warn.contains("grim.tab"));
		TS_ASSERT_EQUALS(warn.find('\n'), -1); // one message
		// A language the game was detected as needs no table of its own:
		// no warning (a German release with language=de).
		TS_ASSERT_EQUALS(Grim::selectGrimTab("de", false, false, dir, warn), Common::String("grim.tab"));
		TS_ASSERT(warn.empty());
	}

	void test_dash_only_between_latin_letters() {
		GrimUnitMetrics m;
		Graphics::BreakRules rules;
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextRun run;

		run.assign(Common::U32String("abcdefgh"));
		Graphics::TextLayout::breakLines(run, 5, m, rules, lines);
		TS_ASSERT(Grim::splitWantsDash(run, lines[0]));
		TS_ASSERT(!Grim::splitWantsDash(run, lines[1])); // the last line

		// Accented Latin counts; digits do not.
		run.assign(Common::U32String("caf\xC3\xA9s\xC3\xA9s", Common::kUtf8));
		Graphics::TextLayout::breakLines(run, 4, m, rules, lines);
		TS_ASSERT(Grim::splitWantsDash(run, lines[0]));
		run.assign(Common::U32String("12345678"));
		Graphics::TextLayout::breakLines(run, 5, m, rules, lines);
		TS_ASSERT(lines[0].emergency);
		TS_ASSERT(!Grim::splitWantsDash(run, lines[0]));

		// A break at a space is no emergency.
		run.assign(Common::U32String("ab cd"));
		Graphics::TextLayout::breakLines(run, 3, m, rules, lines);
		TS_ASSERT(!Grim::splitWantsDash(run, lines[0]));

		// Japanese: never a dash (kana and kanji, no emergency; forced
		// emergency at width 0 between kana is still not Latin).
		run.assign(Common::U32String("\xE3\x81\x93\xE3\x81\x93\xE3\x81\xAB", Common::kUtf8));
		const Graphics::LineSpan l = Graphics::TextLayout::fitLine(run, 0, 0, m, rules);
		TS_ASSERT(!Grim::splitWantsDash(run, l));
	}

	// Official UTF-16 releases keep Grim's own breaker (TextObject takes the
	// shared stage only for a UTF-8 table): the space stays at the line end,
	// a long word is cut with '-' reserved, Chinese without it.
	void test_utf16_text_wraps_as_the_old_loop() {
		GrimUnitWrapWidths w;
		Common::U32String msg = fromUtf16("hello world foo"), out;
		TS_ASSERT_EQUALS(Grim::wrapLegacy(msg, 7, out, w, false, true), 3);
		TS_ASSERT_EQUALS(utf8(out), Common::String("hello \nworld \nfoo"));

		msg = fromUtf16("abcdefghij");
		out.clear();
		TS_ASSERT_EQUALS(Grim::wrapLegacy(msg, 5, out, w, false, true), 3);
		TS_ASSERT_EQUALS(utf8(out), Common::String("abcd-\nefgh-\nij"));
		out.clear();
		TS_ASSERT_EQUALS(Grim::wrapLegacy(msg, 5, out, w, false, false), 2);
		TS_ASSERT_EQUALS(utf8(out), Common::String("abcde\nfghij"));

		// The shared stage would give "hello" / "world" / "foo": different,
		// which is why UTF-16 text stays here.
		Graphics::TextRun run;
		run.assign(fromUtf16("hello world foo"));
		GrimUnitMetrics m;
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 7, m, Graphics::BreakRules(), lines);
		TS_ASSERT_EQUALS(lines.size(), 3u);
		TS_ASSERT_EQUALS(lines[0].end, 5u);
	}

	void test_legacy_keeps_cp949_pairs_whole() {
		GrimUnitWrapWidths w;
		// Three Hangul pairs (2 wide each) at width 5: two per line.
		const Common::String msg("\xB0\xA1\xB3\xAA\xB4\xD9");
		Common::String out;
		TS_ASSERT_EQUALS(Grim::wrapLegacy(msg, 5, out, w, true, true), 2);
		TS_ASSERT_EQUALS(out, Common::String("\xB0\xA1\xB3\xAA\n\xB4\xD9"));
	}
};
