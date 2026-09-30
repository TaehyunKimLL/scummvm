#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * `range.basic-latin=` (design section 6, replacing the old [latin] font=):
 * `same` (SCUMM's engine scope default, spec 8) prefers the charset's own
 * face over a range-named Latin face elsewhere; a named face wins without
 * it; `same` on a charset with no font of its own borrows the nearest
 * charset's, as it always has; the `missing=` box is drawn for any code
 * point no face of the chain has - a Hangul syllable as much as ASCII.
 *
 * perGlyphSourceFor() is the same test-only accessor faceForCodePoint()'s
 * callers (drawChar, advanceFor, latinBaselineByFace, latinStepsByFace,
 * drawsCode) see it through: the resolved Graphics::UnicodeGlyphSource*, and
 * the code point as it was updated (a range rule's substitution, or the
 * missing= mark).
 */
class ScummHiResRangeRulesTestSuite : public CxxTest::TestSuite {
	static const int kCs = ScummHiResFixture::kCs;
	static const int kOtherCs = ScummHiResFixture::kOtherCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

	bool add(Scumm::ScummHiResText &hr, const char *path, const Common::Array<uint32> &cps) {
		return ScummHiResFixture::addFace(hr, path, cps);
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	/// range.basic-latin=same (explicit, matching the engine default):
	/// ASCII comes from the charset's own face, not a range-named one
	/// elsewhere that also has it.
	void test_range_same_prefers_own_font_over_a_named_face() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[fonts]\nlat=LAT.SVF\n[font.4]\nface=OWN.SVF\nrange.basic-latin=same\n"));
		Common::Array<uint32> own, lat;
		own.push_back('A');
		lat.push_back('A');
		Graphics::UnicodeGlyphSource *ownSrc, *latSrc;
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/LAT.SVF", lat));
		ownSrc = hr.sourceForFace("/tmp/t/OWN.SVF");
		latSrc = hr.sourceForFace("/tmp/t/LAT.SVF");
		TS_ASSERT(ownSrc && latSrc && ownSrc != latSrc);

		uint32 cp = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), ownSrc);
	}

	/// Without same (a range naming a face outright), the named face wins;
	/// the id chain still answers behind it (design 6.5 step 4's "appended
	/// after"), which is what test_range_falls_back_to_the_id_chain checks.
	void test_named_range_face_wins_without_same() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[fonts]\nlat=LAT.SVF\n[font.4]\nface=OWN.SVF\nrange.basic-latin=lat\n"));
		Common::Array<uint32> own, lat;
		own.push_back('A');
		lat.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/LAT.SVF", lat));

		uint32 cp = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/LAT.SVF"));
	}

	/// A named range face without the glyph: the id chain answers behind it.
	void test_range_falls_back_to_the_id_chain() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[fonts]\nlat=LAT.SVF\n[font.4]\nface=OWN.SVF\nrange.basic-latin=lat\n"));
		Common::Array<uint32> own, lat;
		own.push_back('A');   // LAT lacks it; OWN (the id chain) answers
		lat.push_back('B');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/LAT.SVF", lat));

		uint32 cp = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/OWN.SVF"));
	}

	/// A charset with no font of its own (kOtherCs) still gets ASCII from
	/// the nearest charset's face under range.basic-latin=same - SCUMM's
	/// long-standing nearestFont() borrowing (design section 5.3), which
	/// pickGlyph() alone cannot reach for a wholly empty id chain (B6).
	void test_same_borrows_the_nearest_charset_when_this_one_has_none() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));

		Graphics::UnicodeGlyphSource *ownSrc = hr.sourceForFace("/tmp/t/OWN.SVF");
		uint32 cp = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kOtherCs, cp), ownSrc);
	}

	/// missing=u+25a1 (open()'s header) draws the box for a Hangul syllable
	/// the SVF lacks - not only ASCII, the old ASCII-only rule.
	void test_missing_box_for_a_hangul_syllable_the_svf_lacks() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back(0x25A1); // no Hangul: the missing mark only
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));

		Graphics::UnicodeGlyphSource *ownSrc = hr.sourceForFace("/tmp/t/OWN.SVF");
		uint32 cp = 0xAC00;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), ownSrc);
		TS_ASSERT_EQUALS(cp, (uint32)0x25A1);
	}

	/// missing= also draws the box for ASCII the SVF lacks (unchanged
	/// behaviour, still pinned so the rule is not accidentally narrowed).
	void test_missing_box_for_ascii() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back(0x25A1);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));

		Graphics::UnicodeGlyphSource *ownSrc = hr.sourceForFace("/tmp/t/OWN.SVF");
		uint32 cp = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), ownSrc);
		TS_ASSERT_EQUALS(cp, (uint32)0x25A1);
	}

	/// [font.4] face=original: the whole charset, Hangul included, is the
	/// game's own to draw - faceForCodePoint() declines outright, and does
	/// not borrow a neighbour's font into it either.
	void test_face_original_declines_the_whole_charset() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.0]\nface=OWN0.SVF\n[font.4]\nface=original\n"));
		Common::Array<uint32> own0;
		own0.push_back('A');
		own0.push_back(0xAC00);
		TS_ASSERT(add(hr, "/tmp/t/OWN0.SVF", own0));

		uint32 cpA = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpA));
		uint32 cpHangul = 0xAC00;
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpHangul));
	}

	/// [font] face=original (design 6.5 step 3, map-wide - not a per-id
	/// [font.N]): every charset is the game's own to draw, not only the one
	/// under test. Uses openMap() directly (a single, self-contained [font]
	/// section) rather than open()'s body-appended-after-a-default-[font]
	/// form, which the id-section tests below need for the same reason.
	void test_map_wide_face_original_declines_every_charset() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(ScummHiResFixture::openMap(hr, overlay,
			"[map]\nversion=2\n[render]\nblend=off\n[font]\nface=original\n"));
		uint32 cpCs = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpCs));
		uint32 cpOther = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kOtherCs, cpOther));
	}

	/// A [font.N] face=<path> always wins over a map-wide face=original for
	/// that one id - the per-id value overrides the map-wide default, same
	/// as any other key; a sibling id with no [font.N] section of its own
	/// still inherits the map-wide original.
	void test_font_n_face_path_overrides_map_wide_original() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(ScummHiResFixture::openMap(hr, overlay,
			"[map]\nversion=2\n[render]\nblend=off\n[font]\nface=original\n[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));

		uint32 cpCs = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cpCs), hr.sourceForFace("/tmp/t/OWN.SVF"));
		// kOtherCs names no [font.N] section: the map-wide original applies.
		uint32 cpOther = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kOtherCs, cpOther));
	}

	/// face=original declines the id outright, even for a code point a
	/// [glyphs] rule remaps to: the remap does not get a second chance to
	/// override the id-wide decline.
	void test_original_beats_glyph_remap() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=original\n[glyphs.4]\n0x41=u+0042\n"));
		// Even asked for directly by the remapped code point, it is declined.
		uint32 cp = 'B';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
		uint32 cpA = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpA));
	}

	/// face=original never borrows a neighbouring id's chain, even when a
	/// donor is actually present and would otherwise be found - unlike a
	/// truly empty id chain (test_same_borrows_the_nearest_charset_when_this_one_has_none),
	/// `original` is a deliberate "always the game's own" and B6's
	/// borrow-retry is guarded against it explicitly.
	void test_original_never_borrows_even_with_a_donor_present() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		// kOtherCs (0) is the donor with a real face; kCs (4) explicitly
		// declines everything.
		TS_ASSERT(open(hr, overlay, "[font.0]\nface=OWN0.SVF\n[font.4]\nface=original\n"));
		Common::Array<uint32> own0;
		own0.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN0.SVF", own0));

		uint32 cpDonor = 'A';
		TS_ASSERT(hr.perGlyphSourceFor(kOtherCs, cpDonor));   // the donor itself still draws

		uint32 cpOriginal = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpOriginal));    // original never borrows it
	}

	/// SVF through face= opens as a bitmap font (was bitmap=).
	void test_svf_through_face_opens_as_a_bitmap_font() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=CARD.SVF\n"));
		Common::Array<uint32> cps;
		cps.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/CARD.SVF", cps));
		uint32 cp = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/CARD.SVF"));
	}

	/// An SVF of a different cell height on the same charset is refused.
	void test_different_cell_height_svf_is_refused() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF,TALL.SVF\n"));
		Common::Array<uint32> own, tall;
		own.push_back('A');
		tall.push_back('B');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		const Common::Array<byte> bytes = ScummHiResFixture::makeFont(tall, ScummHiResFixture::kCell * 2);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/TALL.SVF", ms));

		uint32 cp = 'B';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}

	/// Task 6b's render-target qualifiers reach the faces: a map loaded with
	/// target clut8 routes charset 4 to the :clut8 preset's face; rgb565
	/// (no fallback between targets, spec 3.4) uses the bare preset.
	void test_render_target_qualifier_selects_the_fonts_preset() {
		const Common::String text =
			"[map]\nversion=2\n[render]\nblend=off\n[font]\nmissing=u+25a1\n"
			"[fonts]\ndlg=OWN.SVF\n[fonts:clut8]\ndlg=L.SVF\n[font.4]\nface=dlg\n";

		for (int pass = 0; pass < 2; ++pass) {
			const Graphics::HiResRenderTarget target = pass == 0 ? Graphics::kHiResTargetClut8
																  : Graphics::kHiResTargetRgb565;
			Graphics::HiResMap m;
			Common::Array<Common::String> q;
			Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
			Graphics::HiResMapLoadOptions options;
			options.target = target;
			TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/tmp/t", '/'), q,
													  Graphics::kHiResKeysScumm, m, options));

			Scumm::HiResOverlay overlay;
			overlay.create(64, 40, false);
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptMap(m);
			hr.noteGameCharset(kCs, 8, 8);
			hr.setCharsetGrid(kCs, 8, 8);

			Common::Array<uint32> own, l;
			own.push_back('A');
			l.push_back('A');
			TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
			TS_ASSERT(add(hr, "/tmp/t/L.SVF", l));

			uint32 cp = 'A';
			Graphics::UnicodeGlyphSource *resolved = hr.perGlyphSourceFor(kCs, cp);
			if (target == Graphics::kHiResTargetClut8) {
				TS_ASSERT_EQUALS(resolved, hr.sourceForFace("/tmp/t/L.SVF"));
			} else {
				TS_ASSERT_EQUALS(resolved, hr.sourceForFace("/tmp/t/OWN.SVF"));
			}
		}
	}
};
