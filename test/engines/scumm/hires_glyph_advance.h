#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * advance= / advance.<spec>= (design sections 6.2, 6.3, replacing metrics=
 * and [latin] mode=/space=): how far the pen moves after a replacement
 * glyph, per code-point range.
 *
 * `game` keeps the game's own width (advanceGamePx()); `font` uses the
 * face's own advance; `cell` is the id's narrow/wide cell (old
 * `[latin] mode=half`); with none of these set (kHiResAdvanceEngine) the
 * C31 legacy grid rule applies - a bitmap face steps on the game's grid, a
 * wide TrueType glyph by the face. Since Task 7 a remapped code point goes
 * through its own advance/range rules too (design 6.5 step 2: "a deliberate
 * change from SCUMM's old exclusion of remapped codes from the Latin
 * rules") - the old MI2 0x5c/0x5e/0x60 exclusion is gone.
 */
class ScummHiResGlyphAdvanceTestSuite : public CxxTest::TestSuite {
	static const int kCs = ScummHiResFixture::kCs;
	static const int kOtherCs = ScummHiResFixture::kOtherCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

	bool add(Scumm::ScummHiResText &hr, const char *path, const Common::Array<uint32> &cps) {
		return ScummHiResFixture::addFace(hr, path, cps);
	}

	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		for (int i = 0; i < 4; i++)
			b[pos + i] = (v >> (8 * i)) & 0xff;
	}

	/// A one-glyph 8bpp proportional SVFN with exact control of the metrics
	/// table and the ink's column reach - what M6's restored cellRuleAdvance()
	/// ink floor/widening needs pinned to a precise number, unlike
	/// ScummHiResFixture::makeFont()'s fixed 9 px advance and 6 px ink.
	static Common::Array<byte> svfn(uint32 cp, int advance, int bearing, int inkWidth,
									 int x0, int x1, int y0, int y1, int cell, int ascent) {
		const uint32 stride = cell * cell;
		const uint32 metricsOff = 36, dataOff = metricsOff + 4;
		const uint32 cmapOff = dataOff + stride;
		Common::Array<byte> b;
		b.resize(cmapOff + 8, 0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, 1); // proportional
		b[8] = 8;       // 8bpp
		put16(b, 12, 1);
		b[14] = cell;
		b[15] = cell;
		b[16] = ascent;
		put32(b, 20, metricsOff);
		put32(b, 24, dataOff);
		put32(b, 28, stride);
		put32(b, 32, cmapOff);
		b[metricsOff + 0] = advance;
		b[metricsOff + 1] = (byte)(int8)bearing;
		b[metricsOff + 2] = inkWidth;
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++)
				b[dataOff + y * cell + x] = 255;
		put32(b, cmapOff, cp);
		put32(b, cmapOff + 4, 0);
		return b;
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	/// advance.basic-latin=font: the face's own advance (9 hi-res px at
	/// makeFont()'s ink), scaled down by the map's scale (2).
	void test_advance_font_uses_the_faces_own_advance() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\nadvance.basic-latin=font\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		// makeFont(): advance 9 hi-res px, scale 2 -> 5 game px (round half up).
		TS_ASSERT_EQUALS(hr.advanceFor('A', kCs, 3), 5);
	}

	/// advance.basic-latin=game (the engine default): the game's own width
	/// stands, whatever the face's own advance says.
	void test_advance_game_is_the_default() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT_EQUALS(hr.advanceFor('A', kCs, 3), 3);
	}

	/// advance.<spec>=cell: the narrow cell (half of kCell/2 == 8, at scale 2
	/// that is 4 game px) for a narrow code point.
	void test_advance_cell_uses_the_narrow_cell() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\nadvance.basic-latin=cell\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT_EQUALS(hr.advanceFor('A', kCs, 3), ScummHiResFixture::kCell / 2 / 2);
	}

	/// A combining mark advances 0 and does not disturb the carry.
	void test_combining_mark_advances_zero() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\nadvance.basic-latin=font\n"));
		hr.useUtf8Text(); // chr is the code point directly, as combining marks need
		Common::Array<uint32> own;
		own.push_back(0x0300); // a combining grave accent (Graphics::Unicode::isCombining())
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		int carry = 7;
		TS_ASSERT_EQUALS(hr.advanceFor(0x0300, kCs, 3, &carry), 0);
		TS_ASSERT_EQUALS(carry, 7);
	}

	/// A declined code (the game's font draws it) is laid out by the game.
	void test_declined_code_keeps_the_game_width() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x42 = original\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		own.push_back('B');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT_EQUALS(hr.advanceFor('B', kCs, 3), 3);
	}

	/// A remapped code point now follows its own advance rule (design 6.5
	/// step 2), the inverse of the old ASCII-exclusion: MI2's ellipsis
	/// (0x5e -> u+2026) can be told to keep the game's width even though
	/// general-punctuation is otherwise drawn from the face.
	void test_remapped_code_follows_its_own_advance_rule() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nface=OWN.SVF\nadvance.general-punctuation=font\n"
					   "advance.U+2026=game\n[glyphs]\n0x5e = u+2026\n"));
		Common::Array<uint32> own;
		own.push_back(0x2026);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT_EQUALS(hr.advanceFor(0x5e, kCs, 3), 3); // advance.U+2026=game wins (narrower span)
	}

	/// M6 (controller ruling): advance=game|font on a wide glyph reproduces
	/// the old cellRuleAdvance() exactly - the ink floor MAX(fit, gameWidth)
	/// for game, and the carry-widened face advance for font. A 9 px-advance,
	/// 12 px-wide-ink glyph (ink reaches column 13) is ceil(13/2) = 7 hi-res
	/// scale-2 px either way; only `game` also floors it at the game's own
	/// width when that is wider.
	void test_wide_keeps_cell() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		const Common::Array<byte> bytes = svfn(0xAC00, /*advance*/ 9, /*bearing*/ 1, /*inkWidth*/ 12,
											   /*x0*/ 1, /*x1*/ 13, /*y0*/ 2, /*y1*/ 14, /*cell*/ 16, /*ascent*/ 13);
		for (int metricsFont = 0; metricsFont < 2; ++metricsFont) {
			Scumm::ScummHiResText hr;
			const Common::String body = Common::String::format(
				"[font.4]\nface=OWN.SVF\nadvance=%s\n", metricsFont ? "font" : "game");
			TS_ASSERT(open(hr, overlay, body.c_str()));
			// codePointFor() only takes chr as a literal Unicode code point in
			// UTF-8 mode; otherwise 0xAC00 is read as a two-byte game code
			// (0x00, 0xAC) that decodes to nothing, and advanceFor()/drawChar()
			// fall back to gameWidth before ever reaching cellRuleAdvance() -
			// exactly the "found (4 != 7)" a first draft of this test got, and
			// the same trap that leaves the neighbouring
			// test_bitmap_wide_glyph_keeps_the_game_grid unable to fail (M10):
			// its gameWidth argument (12) happens to equal the real ink floor.
			hr.useUtf8Text();
			Common::MemoryReadStream ms(bytes.begin(), bytes.size());
			TS_ASSERT(hr.addFace("/tmp/t/OWN.SVF", ms));
			// Today's value, pinned: the ink reach 13 at scale 2 is 7.
			TS_ASSERT_EQUALS(hr.advanceFor(0xAC00, kCs, 4), 7);
			TS_ASSERT_EQUALS(hr.advanceFor(0xAC00, kCs, 8), metricsFont ? 7 : 8);
			int carry = 1;
			TS_ASSERT_EQUALS(hr.advanceFor(0xAC00, kCs, 4, &carry), 7);
			if (metricsFont)
				TS_ASSERT_EQUALS(carry, 0); // (9+13 -> 13+1=14)/2=7, remainder 0
		}
	}

	/// H1 regression: under the engine default (no advance= key) a wide SVF
	/// glyph is still centred in the game cell, exactly as the old
	/// `metrics == Game` default did - not drawn flush against the pen the
	/// way a TrueType face stepping by its own advance is (that face-steps
	/// exception, C31, is the only case the engine default does NOT centre).
	void test_wide_glyph_is_centred_under_the_engine_default() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		// No advance= key at all: kHiResAdvanceEngine.
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		hr.useUtf8Text();
		// Ink narrower than the cell (columns 4..9, 6 px) so centring moves
		// it visibly; advance 9 (own), matching test_wide_keeps_cell's glyph
		// shape otherwise.
		const Common::Array<byte> bytes = svfn(0xAC00, /*advance*/ 9, /*bearing*/ 0, /*inkWidth*/ 6,
											   /*x0*/ 4, /*x1*/ 10, /*y0*/ 2, /*y1*/ 14, /*cell*/ 16, /*ascent*/ 13);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/OWN.SVF", ms));

		Graphics::Surface dest;
		dest.create(64, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, dest.pitch * dest.h);
		const int x = 10, gameAdvance = 12, scale = 2;
		TS_ASSERT(hr.drawChar(dest, 0xAC00, kCs, x, 5, 15, 0, 1, nullptr, true, gameAdvance));
		int firstCol = -1;
		for (int col = 0; col < dest.w && firstCol < 0; ++col)
			for (int row = 0; row < dest.h; ++row)
				if (*(const byte *)dest.getBasePtr(col, row)) { firstCol = col; break; }
		TS_ASSERT(firstCol >= 0);
		// drawX = x + slack/2, slack = gameAdvance*scale - own = 24-9 = 15,
		// slack/2 = 7 (integer); the glyph's own ink starts at its column 4.
		const int slack = gameAdvance * scale - 9;
		const int expectedDrawX = x + slack / 2;
		TS_ASSERT_EQUALS(firstCol, expectedDrawX + 4);
		dest.free();
	}

	/// H2 regression: a narrow non-ASCII, non-wide glyph (the "other"
	/// category - Thai base letters, narrow punctuation, a UTF-8
	/// translation's own scripts) steps by the face's own advance under the
	/// engine default, not the wide-glyph grid rule (which would give it
	/// max(ink, game) instead). U+2026 (horizontal ellipsis, general
	/// punctuation) at face advance 13 hi-res px, scale 2, on an 8 px game
	/// cell: the dropped test_metrics_font_advance pinned 7 here.
	void test_narrow_non_ascii_glyph_steps_by_the_face_by_default() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		hr.useUtf8Text();
		// advance=13, narrow ink so it is not classified wide; U+2026 is not
		// Unicode::isWide().
		const Common::Array<byte> bytes = svfn(0x2026, /*advance*/ 13, /*bearing*/ 0, /*inkWidth*/ 10,
											   /*x0*/ 1, /*x1*/ 11, /*y0*/ 12, /*y1*/ 14, /*cell*/ 16, /*ascent*/ 13);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/OWN.SVF", ms));
		TS_ASSERT_EQUALS(hr.advanceFor(0x2026, kCs, 8), 7);
	}

	/// Without missing=, a code point no face has (and no box configured)
	/// is laid out by the game - a blank glyph falls through, it is not
	/// measured by the face.
	void test_blank_glyph_falls_through_to_the_game_width() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT_EQUALS(hr.advanceFor('Z', kCs, 3), 3);
	}
};
