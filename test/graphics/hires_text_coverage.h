#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/str.h"
#include "common/ustr.h"
#include "graphics/hires_text/coverage.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "common/fs.h"
#endif

/**
 * Face fallback chains and the coverage check against the translation's own
 * code points (I18N_TEXT_DESIGN.md sections 4.4 and 4.5).
 */

namespace {

/** A source with a fixed repertoire: cp -> advance (0 = zero-width). */
class RepertoireSource : public Graphics::UnicodeGlyphSource {
public:
	explicit RepertoireSource(byte cell = 16, int bpp = 8, byte fill = 0)
		: _cell(cell), _bpp(bpp), _row(cell * 2, fill), destroyed(nullptr) {}
	~RepertoireSource() override {
		if (destroyed)
			(*destroyed)++;
	}

	Common::HashMap<uint32, int> glyphs;	///< cp -> the face's own advance
	int *destroyed;

	byte cellWidth() const override { return _cell; }
	byte cellHeight() const override { return _cell; }
	byte advanceNarrow() const override { return _cell / 2; }
	byte advanceWide() const override { return _cell; }
	int bitsPerPixel() const override { return _bpp; }
	int cells(uint32 cp) override {
		if (!glyphs.contains(cp))
			return 0;
		return Graphics::Unicode::isWide(cp) ? 2 : 1;
	}
	const byte *row(uint32 cp, int y) override {
		return glyphs.contains(cp) ? &_row[0] : nullptr;
	}
	int advance(uint32 cp) override {
		return glyphs.contains(cp) ? glyphs[cp] : 0;
	}
	uint32 glyphCount() const override { return glyphs.size(); }

private:
	byte _cell;
	int _bpp;
	Common::Array<byte> _row;
};

/** Reports its own advance even for a combining mark (a spacing mark face). */
class SpacingMarkSource : public RepertoireSource {
public:
	bool metrics(uint32 cp, Graphics::GlyphMetrics &m) override {
		if (!RepertoireSource::metrics(cp, m))
			return false;
		m.advance = (int16)advance(cp);
		return true;
	}
};

} // End of anonymous namespace

class HiResCoverageTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	// --- FallbackGlyphSource ---------------------------------------------

	void test_fallback_asks_in_order() {
		RepertoireSource *a = new RepertoireSource(16, 8, 0xAA);
		RepertoireSource *b = new RepertoireSource(16, 8, 0xBB);
		a->glyphs[0xAC00] = 16;
		a->glyphs['A'] = 7;
		b->glyphs[0x0E01] = 9;
		b->glyphs['A'] = 11;	// shadowed by a

		Common::Array<Graphics::UnicodeGlyphSource *> chain;
		chain.push_back(a);
		chain.push_back(b);
		Graphics::FallbackGlyphSource fb(chain, DisposeAfterUse::YES);

		TS_ASSERT_EQUALS(fb.cellWidth(), 16);
		TS_ASSERT_EQUALS(fb.cellHeight(), 16);
		TS_ASSERT_EQUALS(fb.bitsPerPixel(), 8);

		TS_ASSERT_EQUALS(fb.cells(0xAC00), 2);
		TS_ASSERT_EQUALS(fb.row(0xAC00, 0)[0], 0xAA);	// from a
		TS_ASSERT_EQUALS(fb.cells(0x0E01), 1);
		TS_ASSERT_EQUALS(fb.row(0x0E01, 3)[0], 0xBB);	// from b
		TS_ASSERT_EQUALS(fb.advance(0x0E01), 9);
		TS_ASSERT_EQUALS(fb.advance('A'), 7);			// first answer wins
		TS_ASSERT_EQUALS(fb.cells(0x3042), 0);
		TS_ASSERT_EQUALS(fb.advance(0x3042), 0);

		Graphics::GlyphMetrics m;
		TS_ASSERT(fb.metrics(0x0E01, m));
		TS_ASSERT_EQUALS(m.advance, 9);
		TS_ASSERT(!fb.metrics(0x3042, m));

		TS_ASSERT_EQUALS(fb.glyphCount(), 4u);	// 2 + 2
	}

	void test_fallback_dispose() {
		int destroyed = 0;
		{
			RepertoireSource *a = new RepertoireSource();
			RepertoireSource *b = new RepertoireSource();
			a->destroyed = &destroyed;
			b->destroyed = &destroyed;
			Common::Array<Graphics::UnicodeGlyphSource *> chain;
			chain.push_back(a);
			chain.push_back(b);
			Graphics::FallbackGlyphSource fb(chain, DisposeAfterUse::YES);
		}
		TS_ASSERT_EQUALS(destroyed, 2);

		destroyed = 0;
		RepertoireSource a, b;
		a.destroyed = &destroyed;
		{
			Common::Array<Graphics::UnicodeGlyphSource *> chain;
			chain.push_back(&a);
			chain.push_back(&b);
			Graphics::FallbackGlyphSource fb(chain, DisposeAfterUse::NO);
		}
		TS_ASSERT_EQUALS(destroyed, 0);
		a.destroyed = nullptr;
	}

	// Rows are read with the first source's stride and depth: a later source
	// with another cell or depth cannot answer (it is skipped, still owned).
	void test_fallback_skips_mismatched_cells() {
		int destroyed = 0;
		{
			RepertoireSource *a = new RepertoireSource(16, 8);
			RepertoireSource *b = new RepertoireSource(24, 8);
			RepertoireSource *c = new RepertoireSource(16, 1);
			a->glyphs['A'] = 8;
			b->glyphs[0x0E01] = 12;
			c->glyphs[0x3042] = 16;
			b->destroyed = &destroyed;
			c->destroyed = &destroyed;
			Common::Array<Graphics::UnicodeGlyphSource *> chain;
			chain.push_back(a);
			chain.push_back(b);
			chain.push_back(c);
			Graphics::FallbackGlyphSource fb(chain, DisposeAfterUse::YES);
			TS_ASSERT_EQUALS(fb.cells('A'), 1);
			TS_ASSERT_EQUALS(fb.cells(0x0E01), 0);
			TS_ASSERT_EQUALS(fb.cells(0x3042), 0);
		}
		TS_ASSERT_EQUALS(destroyed, 2);
	}

	// --- CodePointSet ----------------------------------------------------

	void test_code_point_set_utf8() {
		Graphics::CodePointSet set;
		const char *s = "\xea\xb0\x80\xea\xb0\x80" "A";	// "가가A"
		set.addUtf8(s, strlen(s));
		TS_ASSERT_EQUALS(set.size(), 2u);
		TS_ASSERT(set.contains(0xAC00));
		TS_ASSERT(set.contains('A'));
		TS_ASSERT(!set.contains('B'));

		// Invalid bytes and control codes are no characters of the text.
		const char *bad = "\xff\x0a\x80" "B\x0d";
		set.addUtf8(bad, strlen(bad));
		TS_ASSERT_EQUALS(set.size(), 3u);
		TS_ASSERT(set.contains('B'));
		TS_ASSERT(!set.contains(0xFFFD));

		// Thai with a mark, as UTF-32.
		Common::U32String th;
		th += (Common::u32char_type_t)0x0E17;
		th += (Common::u32char_type_t)0x0E35;
		th += (Common::u32char_type_t)0x0E48;
		th += (Common::u32char_type_t)0x0E17;
		set.addU32(th);
		TS_ASSERT_EQUALS(set.size(), 6u);
		TS_ASSERT(set.contains(0x0E48));
	}

	void buildThousand(Graphics::CodePointSet &set) {
		for (uint32 cp = 0x20; cp <= 0x7E; cp++)	// 95
			set.add(cp);
		for (uint32 cp = 0x0E01; cp <= 0x0E5B; cp++)	// 91
			set.add(cp);
		for (uint32 cp = 0x3041; cp <= 0x3096; cp++)	// 86
			set.add(cp);
		for (uint32 cp = 0xAC00; cp < 0xAC00 + 728; cp++)	// 728
			set.add(cp);
	}

	void test_code_point_set_sample() {
		Graphics::CodePointSet set;
		buildThousand(set);
		set.add(0xAC00);	// already there
		TS_ASSERT_EQUALS(set.size(), 1000u);

		Common::Array<uint32> s1, s2;
		set.sample(64, s1);
		set.sample(64, s2);
		TS_ASSERT_EQUALS(s1.size(), 64u);
		TS_ASSERT(s1 == s2);

		// Non-ASCII first: no ASCII entry before a non-ASCII one.
		TS_ASSERT(s1.size() > 0 && s1[0] >= 0x80);
		bool seenAscii = false, order = true;
		for (uint i = 0; i < s1.size(); i++) {
			if (s1[i] < 0x80)
				seenAscii = true;
			else if (seenAscii)
				order = false;
			TS_ASSERT(set.contains(s1[i]));
			for (uint j = 0; j < i; j++)
				TS_ASSERT_DIFFERS(s1[i], s1[j]);
		}
		TS_ASSERT(order);

		// The first code point of every 128-block present.
		const uint32 firsts[] = { 0x20, 0x0E01, 0x3041, 0x3080,
		                          0xAC00, 0xAC80, 0xAD00, 0xAD80, 0xAE00, 0xAE80 };
		for (uint f = 0; f < ARRAYSIZE(firsts); f++) {
			bool found = false;
			for (uint i = 0; i < s1.size(); i++)
				found = found || s1[i] == firsts[f];
			TS_ASSERT(found);
		}

		// A set smaller than n: all of it.
		Graphics::CodePointSet small;
		const char *s = "\xea\xb0\x80" "ab";
		small.addUtf8(s, strlen(s));
		Common::Array<uint32> all;
		small.sample(64, all);
		TS_ASSERT_EQUALS(all.size(), 3u);
		if (all.size() == 3) {
			TS_ASSERT_EQUALS(all[0], 0xAC00u);
			TS_ASSERT_EQUALS(all[1], (uint32)'a');
			TS_ASSERT_EQUALS(all[2], (uint32)'b');
		}
		Graphics::CodePointSet empty;
		empty.sample(64, all);
		TS_ASSERT(all.empty());
	}

	// A translation over more than n blocks (Japanese kanji span ~160):
	// the block pass is strided across all of them, so the first, a middle
	// and the last block are sampled, and ASCII keeps a place.
	void test_code_point_set_sample_many_blocks() {
		Graphics::CodePointSet set;
		const uint32 base = 0x4E00;	// block 0x9C
		for (uint32 b = 0; b < 200; b++) {
			set.add(base + b * 128);
			set.add(base + b * 128 + 5);
		}
		for (uint32 cp = 'a'; cp <= 'z'; cp++)
			set.add(cp);
		TS_ASSERT_EQUALS(set.size(), 426u);

		Common::Array<uint32> s1, s2;
		set.sample(64, s1);
		set.sample(64, s2);
		TS_ASSERT_EQUALS(s1.size(), 64u);
		TS_ASSERT(s1 == s2);

		bool first = false, middle = false, last = false;
		uint ascii = 0;
		for (uint i = 0; i < s1.size(); i++) {
			if (s1[i] < 0x80) {
				ascii++;
				continue;
			}
			const uint32 b = (s1[i] - base) / 128;
			first = first || b == 0;
			middle = middle || (b >= 95 && b <= 105);
			last = last || b == 199;
		}
		TS_ASSERT(first);
		TS_ASSERT(middle);
		TS_ASSERT(last);
		TS_ASSERT(ascii >= 1);
		TS_ASSERT(s1[0] >= 0x80);	// still non-ASCII first
		TS_ASSERT(s1.back() < 0x80);
	}

	// --- checkCoverage / coverageWarning ------------------------------------

	void test_check_coverage_missing() {
		RepertoireSource src;
		Common::Array<uint32> sample;
		const uint32 cps[10] = { 0x0E01, 0x0E17, 0x0E35, 0x0E48, 0x0E49, 0x0E32, 'a', 'b', 'c', ' ' };
		for (int i = 0; i < 10; i++) {
			sample.push_back(cps[i]);
			if (cps[i] != 0x0E48 && cps[i] != 0x0E49)
				src.glyphs[cps[i]] = cps[i] == 0x0E35 ? 0 : 10;
		}
		Graphics::CoverageReport r = Graphics::checkCoverage(&src, sample);
		TS_ASSERT_EQUALS(r.sampled, 10u);
		TS_ASSERT_EQUALS(r.missing, 2u);
		TS_ASSERT_EQUALS(r.firstMissing.size(), 2u);
		if (r.firstMissing.size() == 2) {
			TS_ASSERT_EQUALS(r.firstMissing[0], 0x0E48u);
			TS_ASSERT_EQUALS(r.firstMissing[1], 0x0E49u);
		}
		TS_ASSERT_EQUALS(r.spacingMarks, 0u);

		TS_ASSERT_EQUALS(Graphics::coverageWarning("Thonburi", r, "the game's font"),
			"hires text: Thonburi lacks 2 of 10 sampled characters of the translation "
			"(U+0E48 U+0E49); they fall back to the game's font");
	}

	void test_check_coverage_first_missing_is_five_ascending() {
		RepertoireSource src;
		Common::Array<uint32> sample;
		// 17 missing, sampled in descending order.
		for (uint32 cp = 0x0E5B; cp > 0x0E5B - 17; cp--)
			sample.push_back(cp);
		for (uint32 cp = 'a'; cp < 'a' + 47; cp++) {
			sample.push_back(cp);
			src.glyphs[cp] = 8;
		}
		Graphics::CoverageReport r = Graphics::checkCoverage(&src, sample);
		TS_ASSERT_EQUALS(r.sampled, 64u);
		TS_ASSERT_EQUALS(r.missing, 17u);
		TS_ASSERT_EQUALS(r.firstMissing.size(), 5u);
		if (r.firstMissing.size() == 5) {
			TS_ASSERT_EQUALS(r.firstMissing[0], 0x0E4Bu);	// 0x0E5B - 16
			TS_ASSERT_EQUALS(r.firstMissing[4], 0x0E4Fu);
		}
		TS_ASSERT_EQUALS(Graphics::coverageWarning("ko", r, "th"),
			"hires text: ko lacks 17 of 64 sampled characters of the translation "
			"(U+0E4B U+0E4C U+0E4D U+0E4E U+0E4F ...); they fall back to th");
	}

	void test_check_coverage_spacing_marks() {
		SpacingMarkSource src;
		src.glyphs[0x0E17] = 12;
		src.glyphs[0x0E48] = 12;	// a mark that advances: the Thonburi case
		src.glyphs[0x0E35] = 0;		// a zero-width mark
		Common::Array<uint32> sample;
		sample.push_back(0x0E17);
		sample.push_back(0x0E48);
		sample.push_back(0x0E35);
		Graphics::CoverageReport r = Graphics::checkCoverage(&src, sample);
		TS_ASSERT_EQUALS(r.missing, 0u);
		TS_ASSERT_EQUALS(r.spacingMarks, 1u);
		TS_ASSERT_EQUALS(Graphics::coverageWarning("Thonburi", r, "the game's font"),
			"hires text: Thonburi draws combining marks as spacing glyphs (it needs "
			"shaping); choose a face with zero-width marks, e.g. Sukhumvit Set");

		// A source whose metrics() zeroes a mark but whose face advances it
		// (TtfGlyphSource: the default metrics() plus the FreeType advance).
		RepertoireSource ttfLike;
		ttfLike.glyphs[0x0E48] = 12;
		r = Graphics::checkCoverage(&ttfLike, sample);
		TS_ASSERT_EQUALS(r.missing, 2u);
		TS_ASSERT_EQUALS(r.spacingMarks, 1u);
		// Both at once: the two lines, missing first.
		TS_ASSERT_EQUALS(Graphics::coverageWarning("F", r, "G"),
			"hires text: F lacks 2 of 3 sampled characters of the translation "
			"(U+0E17 U+0E35); they fall back to G\n"
			"hires text: F draws combining marks as spacing glyphs (it needs "
			"shaping); choose a face with zero-width marks, e.g. Sukhumvit Set");
	}

	void test_coverage_warning_empty_when_complete() {
		RepertoireSource src;
		src.glyphs['a'] = 8;
		Common::Array<uint32> sample;
		sample.push_back('a');
		Graphics::CoverageReport r = Graphics::checkCoverage(&src, sample);
		TS_ASSERT_EQUALS(r.missing, 0u);
		TS_ASSERT(Graphics::coverageWarning("F", r, "G").empty());
		// No translation: an empty sample, no warning.
		r = Graphics::checkCoverage(&src, Common::Array<uint32>());
		TS_ASSERT_EQUALS(r.sampled, 0u);
		TS_ASSERT(Graphics::coverageWarning("F", r, "G").empty());
	}

	// --- TtfGlyphSource::create() fit probes ---------------------------------

	void test_legacy_require_hangul_unchanged() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode latin("/System/Library/Fonts/Supplemental/Arial.ttf");
		if (!latin.exists())
			return;
		Common::String error;
		Graphics::TtfGlyphSource *src = Graphics::TtfGlyphSource::create(
			latin.createReadStream(), DisposeAfterUse::YES, 16, error, true);
		TS_ASSERT(src == nullptr);
		TS_ASSERT_EQUALS(error, Common::String("face has no Hangul glyphs"));
		delete src;
#endif
	}

	// Without extra probes the new overload fits exactly as the legacy one;
	// with them the load stays within the fixed set + extras + 6 retries.
	void test_create_extra_fit_probes() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode node("/System/Library/Fonts/Supplemental/SukhumvitSet.ttc");
		if (!node.exists())
			node = Common::FSNode("/Users/juami/work/scummvm/runs/c11/data/fonts/sukhumvit-text.ttf");
		if (!node.exists())
			return;

		Common::String e1, e2, e3;
		Graphics::TtfGlyphSource *legacy = Graphics::TtfGlyphSource::create(
			node.createReadStream(), DisposeAfterUse::YES, 24, e1);
		Graphics::TtfGlyphSource *none = Graphics::TtfGlyphSource::create(
			node.createReadStream(), DisposeAfterUse::YES, 24, e2, nullptr, 0);
		const uint32 thai[] = { 0x0E17, 0x0E35, 0x0E48, 0x0E4B, 0x0E38, 0x0E39, 0x0E3A, 0x0E0E, 0x0E0F };
		Graphics::TtfGlyphSource *extra = Graphics::TtfGlyphSource::create(
			node.createReadStream(), DisposeAfterUse::YES, 24, e3, thai, ARRAYSIZE(thai));
		TS_ASSERT(legacy && none && extra);
		if (legacy && none && extra) {
			TS_ASSERT_EQUALS(legacy->rasterCount(), none->rasterCount());
			const uint32 cps[] = { 'A', 'g', 0x0E01, 0x0E48 };
			for (uint i = 0; i < ARRAYSIZE(cps); i++) {
				TS_ASSERT_EQUALS(legacy->cells(cps[i]), none->cells(cps[i]));
				for (int y = 0; y < legacy->cellHeight() && legacy->cells(cps[i]); y++)
					TS_ASSERT_SAME_DATA(legacy->row(cps[i], y), none->row(cps[i], y), legacy->cellWidth() * 2);
			}
			TS_ASSERT(extra->rasterCount() <= 26 + ARRAYSIZE(thai) + 6);
			TS_ASSERT(extra->rasterCount() >= 26 + ARRAYSIZE(thai));
			TS_ASSERT(extra->cells(0x0E48) == 1);
		}
		delete legacy;
		delete none;
		delete extra;
#endif
	}

	// The design's two Thai faces: Sukhumvit Set draws marks with zero
	// advance, Thonburi as spacing glyphs.
	void test_real_faces_spacing_marks() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::FSNode sukhumvit("/System/Library/Fonts/Supplemental/SukhumvitSet.ttc");
		Common::FSNode thonburi("/System/Library/Fonts/Supplemental/Thonburi.ttc");
		if (!sukhumvit.exists() || !thonburi.exists())
			return;
		Common::Array<uint32> sample;
		const uint32 cps[] = { 0x0E17, 0x0E35, 0x0E48, 0x0E48, 0x0E34, 0x0E01 };
		for (uint i = 0; i < ARRAYSIZE(cps); i++)
			sample.push_back(cps[i]);
		Common::String error;
		Graphics::TtfGlyphSource *s = Graphics::TtfGlyphSource::create(
			sukhumvit.createReadStream(), DisposeAfterUse::YES, 24, error, sample.data(), sample.size());
		Graphics::TtfGlyphSource *t = Graphics::TtfGlyphSource::create(
			thonburi.createReadStream(), DisposeAfterUse::YES, 24, error, sample.data(), sample.size());
		TS_ASSERT(s && t);
		if (s && t) {
			Graphics::CoverageReport rs = Graphics::checkCoverage(s, sample);
			Graphics::CoverageReport rt = Graphics::checkCoverage(t, sample);
			TS_ASSERT_EQUALS(rs.missing, 0u);
			TS_ASSERT_EQUALS(rs.spacingMarks, 0u);
			TS_ASSERT_EQUALS(rt.missing, 0u);
			TS_ASSERT(rt.spacingMarks > 0);
		}
		delete s;
		delete t;
#endif
	}
};
