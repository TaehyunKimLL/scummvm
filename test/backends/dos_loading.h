#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/loading-progress.h"
#include "backends/platform/dos/loading-screen.h"

class DosLoadingTestSuite : public CxxTest::TestSuite {
public:
	void test_phases_start_in_order_and_end_at_1000() {
		for (int p = 1; p < DOS::kLoadPhaseCount; ++p)
			TS_ASSERT(DOS::kLoadPhases[p].from > DOS::kLoadPhases[p - 1].from);
		TS_ASSERT_EQUALS(DOS::kLoadPhases[DOS::kLoadDone].from, 1000);
	}

	void test_permille_starts_at_the_phase_start() {
		for (int p = 0; p < DOS::kLoadDone; ++p)
			TS_ASSERT_EQUALS(DOS::loadPermille((DOS::LoadPhase)p, 0, 0), DOS::kLoadPhases[p].from);
	}

	void test_permille_never_reaches_the_next_phase() {
		// Even hours into a phase, the bar stays short of the next one's start.
		for (int p = 0; p < DOS::kLoadDone; ++p) {
			const uint16 v = DOS::loadPermille((DOS::LoadPhase)p, 10000000, 0xFFFFFFFF);
			TS_ASSERT(v < DOS::kLoadPhases[p + 1].from);
			TS_ASSERT(v <= DOS::kLoadMaxBeforeDone);
		}
	}

	void test_permille_is_three_quarters_through_at_the_usual_length() {
		const DOS::LoadPhaseSpec &s = DOS::kLoadPhases[DOS::kLoadInit];
		const uint span = DOS::kLoadPhases[DOS::kLoadInit + 1].from - s.from;
		const uint16 v = DOS::loadPermille(DOS::kLoadInit, s.expectMs, 0);
		TS_ASSERT_EQUALS(v, s.from + span * 3 / 4);
	}

	void test_permille_grows_with_time_and_bytes() {
		uint16 last = 0;
		for (uint32 t = 0; t < 5000; t += 50) {
			const uint16 v = DOS::loadPermille(DOS::kLoadFirstFrame, t, 0);
			TS_ASSERT(v >= last);
			last = v;
		}
		TS_ASSERT(DOS::loadPermille(DOS::kLoadInit, 100, 4 * 1024 * 1024) > DOS::loadPermille(DOS::kLoadInit, 100, 0));
	}

	void test_done_is_1000() {
		TS_ASSERT_EQUALS(DOS::loadPermille(DOS::kLoadDone, 0, 0), 1000);
	}

	void test_progress_milestones_only_move_forward() {
		DOS::LoadProgress p;
		p.reset(0);
		p.enter(DOS::kLoadData, 100);
		p.enter(DOS::kLoadDetect, 200);	// late, out of order: ignored
		TS_ASSERT_EQUALS(p.phase(), DOS::kLoadData);
		TS_ASSERT_EQUALS(p.since(), 100u);
	}

	void test_progress_value_never_goes_back() {
		// Far into a long phase, then the next phase starts: its start is
		// below where the bar already is, and the bar stays put.
		DOS::LoadProgress p;
		p.reset(0);
		p.enter(DOS::kLoadDetect, 0);
		const uint16 before = p.value(100000);
		TS_ASSERT(before > DOS::kLoadPhases[DOS::kLoadEngine].from - 5);
		p.enter(DOS::kLoadEngine, 100000);
		TS_ASSERT(p.value(100000) >= before);
		p.enter(DOS::kLoadDone, 100001);
		TS_ASSERT_EQUALS(p.value(100001), 1000);
	}

	void test_ascii_title_replaces_utf8_characters_once() {
		// "Hangul: " + U+D55C U+AE00 (3 bytes each in UTF-8)
		const Common::String s("Hangul: \xED\x95\x9C\xEA\xB8\x80!");
		TS_ASSERT_EQUALS(DOS::asciiTitle(s, 80), Common::String("Hangul: ??" "!"));
	}

	void test_ascii_title_cuts_with_an_ellipsis() {
		TS_ASSERT_EQUALS(DOS::asciiTitle("King's Quest I (English)", 10), Common::String("King's ..."));
		TS_ASSERT_EQUALS(DOS::asciiTitle("abc\tdef", 80), Common::String("abc def"));
	}

	void test_layout_fits_320x200_and_640x480() {
		const int sizes[][2] = { { 320, 200 }, { 640, 400 }, { 640, 480 } };
		for (int i = 0; i < 3; ++i) {
			const int w = sizes[i][0], h = sizes[i][1];
			const DOS::LoadScreenLayout l = DOS::loadScreenLayout(w, h, 38, 108, 13, 26);
			const Common::Rect screen(w, h);
			TS_ASSERT(screen.contains(l.title));
			TS_ASSERT(screen.contains(l.caption));
			TS_ASSERT(screen.contains(l.bar));
			TS_ASSERT(screen.contains(l.label));
			TS_ASSERT(l.title.bottom <= l.caption.top);
			TS_ASSERT(l.caption.bottom <= l.bar.top);
			TS_ASSERT(l.bar.bottom <= l.label.top);
		}
	}

	void test_bar_fill_inside_the_outline() {
		const Common::Rect bar(100, 50, 300, 66);
		TS_ASSERT_EQUALS(DOS::loadBarFill(bar, 0).width(), 0);
		const Common::Rect full = DOS::loadBarFill(bar, 1000);
		TS_ASSERT_EQUALS(full, Common::Rect(102, 52, 298, 64));
		TS_ASSERT_EQUALS(DOS::loadBarFill(bar, 500).width(), 98);
	}

	void test_canvas_fill_and_bitmap_clip() {
		uint32 px[4 * 3];
		memset(px, 0, sizeof(px));
		DOS::LoadCanvas c((byte *)px, 16, 4, 3, 4);
		c.fill(Common::Rect(-2, 1, 10, 2), 0x123456);
		for (int x = 0; x < 4; ++x) {
			TS_ASSERT_EQUALS(px[0 * 4 + x], 0u);
			TS_ASSERT_EQUALS(px[1 * 4 + x], 0x123456u);
			TS_ASSERT_EQUALS(px[2 * 4 + x], 0u);
		}
		const byte bits[1] = { 0xA0 };	// x = 0 and 2
		c.bitmap(1, 2, bits, 1, 8, 1, 7);
		TS_ASSERT_EQUALS(px[2 * 4 + 1], 7u);
		TS_ASSERT_EQUALS(px[2 * 4 + 2], 0u);
		TS_ASSERT_EQUALS(px[2 * 4 + 3], 7u);
	}

	void test_used_colors_lit_follows_the_palette() {
		byte screen[8 * 2];
		memset(screen, 5, sizeof(screen));
		bool used[256];
		memset(used, 0, sizeof(used));
		DOS::markUsedColors(screen, 8, Common::Rect(8, 2), used);
		byte pal[256 * 3];
		memset(pal, 0, sizeof(pal));
		TS_ASSERT(!DOS::anyUsedColorLit(used, pal));
		pal[5 * 3 + 1] = 15;	// below the threshold: still black on a DAC
		TS_ASSERT(!DOS::anyUsedColorLit(used, pal));
		pal[5 * 3 + 1] = 16;
		TS_ASSERT(DOS::anyUsedColorLit(used, pal));
		pal[5 * 3 + 1] = 0;
		pal[9 * 3] = 200;	// lit, but not drawn with
		TS_ASSERT(!DOS::anyUsedColorLit(used, pal));
	}

	void test_true_colour_lit_and_count() {
		const Graphics::PixelFormat f(4, 8, 8, 8, 0, 16, 8, 0, 0);
		uint32 px[20 * 20];
		memset(px, 0, sizeof(px));
		const Common::Rect all(20, 20);
		TS_ASSERT(!DOS::anyPixelLit((const byte *)px, 80, f, all));
		px[4 * 20 + 8] = 0x000F0F0F;	// every channel 15: not lit
		TS_ASSERT(!DOS::anyPixelLit((const byte *)px, 80, f, all));
		px[4 * 20 + 8] = 0x00100000;	// red 16, on a pixel both looks sample
		TS_ASSERT(DOS::anyPixelLit((const byte *)px, 80, f, all));
		TS_ASSERT(!DOS::anyPixelLit((const byte *)px, 80, f, Common::Rect(9, 0, 20, 20)));
		// A strip one pixel wide is still looked at.
		TS_ASSERT(DOS::anyPixelLit((const byte *)px, 80, f, Common::Rect(8, 1, 9, 20)) == false);
		TS_ASSERT(DOS::anyPixelLit((const byte *)px, 80, f, Common::Rect(8, 4, 9, 5)));
		TS_ASSERT_EQUALS(DOS::countLitSamples((const byte *)px, 80, f, nullptr, 20, 20, 100), 1u);
		px[3 * 20 + 5] = 0x00FFFFFF;	// an odd row and column: not sampled
		TS_ASSERT_EQUALS(DOS::countLitSamples((const byte *)px, 80, f, nullptr, 20, 20, 100), 1u);
	}

	void test_count_lit_samples_clut8_and_limit() {
		const Graphics::PixelFormat f = Graphics::PixelFormat::createFormatCLUT8();
		byte px[10 * 10];
		memset(px, 3, sizeof(px));
		byte pal[256 * 3];
		memset(pal, 0, sizeof(pal));
		TS_ASSERT_EQUALS(DOS::countLitSamples(px, 10, f, pal, 10, 10, 1000), 0u);
		pal[3 * 3 + 2] = 255;
		TS_ASSERT_EQUALS(DOS::countLitSamples(px, 10, f, pal, 10, 10, 1000), 25u);
		TS_ASSERT_EQUALS(DOS::countLitSamples(px, 10, f, pal, 10, 10, 7), 7u);
	}

	void test_first_frame_needs_half_a_percent_of_samples() {
		TS_ASSERT_EQUALS(DOS::firstFrameLitSamples(640, 400), 320u);
		TS_ASSERT_EQUALS(DOS::firstFrameLitSamples(320, 200), 80u);
		TS_ASSERT_EQUALS(DOS::firstFrameLitSamples(2, 2), 1u);
	}

	void test_double_byte_code_pages() {
		TS_ASSERT(DOS::isDbcsCodePage(949));
		TS_ASSERT(DOS::isDbcsCodePage(932));
		TS_ASSERT(DOS::isDbcsCodePage(936));
		TS_ASSERT(DOS::isDbcsCodePage(950));
		TS_ASSERT(DOS::isDbcsCodePage(1361));
		TS_ASSERT(!DOS::isDbcsCodePage(437));
		TS_ASSERT(!DOS::isDbcsCodePage(850));
		TS_ASSERT(!DOS::isDbcsCodePage(0));
	}

	void test_dbcs_table_with_a_lead_byte_range() {
		const byte korean[] = { 0x81, 0xFE, 0, 0 };
		const byte sjis[] = { 0x81, 0x9F, 0xE0, 0xFC, 0, 0 };
		const byte none[] = { 0, 0, 0, 0 };
		const byte stray[] = { 0x41, 0x5A, 0, 0 };
		const byte reversed[] = { 0x81, 0x80, 0, 0 };
		TS_ASSERT(DOS::dbcsTableHasLeadBytes(korean));
		TS_ASSERT(DOS::dbcsTableHasLeadBytes(sjis));
		TS_ASSERT(!DOS::dbcsTableHasLeadBytes(none));
		TS_ASSERT(!DOS::dbcsTableHasLeadBytes(stray));
		TS_ASSERT(!DOS::dbcsTableHasLeadBytes(reversed));
	}

	void test_text_bar_cells_are_cp437_blocks_or_ascii() {
		static const byte cp437[3] = { 0xB0, 0xDD, 0xDB };
		static const byte ascii[3] = { '.', '+', '#' };
		for (int h = 0; h < 3; ++h) {
			byte ch = 0, attr = 0;
			DOS::textBarCell(h, false, ch, attr);
			TS_ASSERT_EQUALS(ch, cp437[h]);
			TS_ASSERT_EQUALS(attr, h ? 0x0B : 0x08);
			DOS::textBarCell(h, true, ch, attr);
			TS_ASSERT_EQUALS(ch, ascii[h]);
			TS_ASSERT(ch < 0x80);
			TS_ASSERT_EQUALS(attr, h ? 0x0B : 0x08);
		}
	}
};
