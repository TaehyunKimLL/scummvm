#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * advance= / advance.<spec>= (design sections 6.2, 6.3): how far the pen
 * moves after a replacement glyph, per code-point range.
 *
 * `game` keeps the game's own width (advanceGamePx()); `font` uses the
 * face's own advance; `cell` is the id's narrow/wide cell; with none of
 * these set (kHiResAdvanceEngine) the C31 legacy grid rule applies - a
 * bitmap face steps on the game's grid, a wide TrueType glyph by the face.
 * A remapped code point goes through its own advance/range rules too
 * (design 6.5 step 2), so MI2's 0x5c/0x5e/0x60 are not excluded.
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
	/// table and the ink's column reach - what cellRuleAdvance()'s
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

	/// One glyph's metrics and ink box, for svfnMulti().
	struct G {
		uint32 cp;
		int advance, bearing, inkWidth;
		int x0, x1, y0, y1;
	};

	/// As svfn(), but for several glyphs in one file (a base and a
	/// combining mark sharing a font, the way a real translation's face
	/// would).
	static Common::Array<byte> svfnMulti(const G *g, int n, int cell = 16, int ascent = 13) {
		const uint32 stride = (uint32)cell * cell;
		const uint32 metricsOff = 36, dataOff = metricsOff + n * 4;
		const uint32 cmapOff = dataOff + stride * n;
		Common::Array<byte> b;
		b.resize(cmapOff + n * 8, 0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, 1); // proportional
		b[8] = 8;       // 8bpp
		put16(b, 12, n);
		b[14] = cell;
		b[15] = cell;
		b[16] = ascent;
		put32(b, 20, metricsOff);
		put32(b, 24, dataOff);
		put32(b, 28, stride * n);
		put32(b, 32, cmapOff);
		for (int i = 0; i < n; ++i) {
			b[metricsOff + i * 4 + 0] = g[i].advance;
			b[metricsOff + i * 4 + 1] = (byte)(int8)g[i].bearing;
			b[metricsOff + i * 4 + 2] = g[i].inkWidth;
			for (int y = g[i].y0; y < g[i].y1; ++y)
				for (int x = g[i].x0; x < g[i].x1; ++x)
					b[dataOff + (uint32)i * stride + y * cell + x] = 255;
			put32(b, cmapOff + i * 8, g[i].cp);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	/// The leftmost inked (non-zero) column of a surface, or -1.
	static int inkLeft(const Graphics::Surface &s) {
		for (int x = 0; x < s.w; ++x)
			for (int y = 0; y < s.h; ++y)
				if (*(const byte *)s.getBasePtr(x, y))
					return x;
		return -1;
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

	/// advance=game|font on a wide glyph reproduces
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
			// test_bitmap_wide_glyph_keeps_the_game_grid unable to fail:
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

	/// Under the engine default (no advance= key) a wide SVF
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

	/// A narrow non-ASCII, non-wide glyph (the "other"
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

	/// noteTranslatedString() (C11-T3c): the translation's code points are
	/// read past every escape by the shared rule (escapeArgBytes()) - codes
	/// 4-7 and 9 take two argument bytes too, and an argument of 0 does not
	/// end the string.
	void test_note_translated_string_skips_every_escape_argument() {
		Scumm::ScummHiResText hr;
		hr.useUtf8Text();
		static const byte kText[] = {
			0xFF, 0x04, 0x00, 0x00, 0xEA, 0xB0, 0x80, 'a',	// FF 04 00 00, U+AC00 a
			0xFF, 0x05, 'Q', 'R', 'b',			// FF 05 Q R, b
			0xFF, 0x06, 'S', 'T', 0xFF, 0x07, 'U', 'V', 'c',	// FF 06 S T, FF 07 U V, c
			0xFF, 0x09, 'W', 0x00, 'd',			// FF 09 W 00, d
			0xFF, 0x01, 'e',				// FF 01 (no arguments), e
			0xFF, 0x0E, 'X', 'Y', '@', 'f', 0x00, 'Z'
		};
		hr.noteTranslatedString(kText, sizeof(kText));
		const Graphics::CodePointSet &cps = hr.translationCodePoints();
		const uint32 want[] = { 0xAC00, 'a', 'b', 'c', 'd', 'e', 'f' };
		for (uint i = 0; i < ARRAYSIZE(want); i++)
			TS_ASSERT(cps.contains(want[i]));
		TS_ASSERT_EQUALS(cps.size(), (uint32)ARRAYSIZE(want));
	}

	/// The anchor a combining mark attaches to is the base just drawn by
	/// this layer on this line and in this string - not an older one, a
	/// declined one, or one on another line (the drawGlyphPlaced()/
	/// beginString() anchor bookkeeping).
	///
	/// This custom font's mark (U+0300) has no marksAtOrigin flag, so its
	/// originX is 0 and its own ink starts at its cell's column 0: with no
	/// valid anchor to fall back to, drawGlyphPlaced() draws it at the
	/// caller's own pen exactly, which this test pins directly.
	void test_mark_anchor_is_reset() {
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		hr.useUtf8Text();
		// A base (0x1101, wide so it is not routed through the ascii/"other"
		// combining-mark special case) and a zero-advance combining mark
		// (U+0300), both in one file (addFace() is a per-path cache, so a
		// second call under the same path would just reuse the first).
		const G both[] = {
			{ 0x1101, 12, 1, 10, 1, 11, 2, 14 },
			{ 0x0300, 0, -4, 4, 0, 4, 0, 4 },
		};
		const Common::Array<byte> bytes = svfnMulti(both, 2);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/OWN.SVF", ms));

		Graphics::Surface scratch, mark;
		scratch.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		mark.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(scratch.getPixels(), 0, scratch.pitch * scratch.h);

		// A declined base ('Z', no glyph): the mark stays wherever the
		// caller's own pen says, not on the base drawn before that one.
		TS_ASSERT(hr.drawChar(scratch, 0x1101, kCs, 4, 4, 15, 0, 1));
		TS_ASSERT(!hr.drawChar(scratch, 'Z', kCs, 30, 4, 15, 0, 1));
		memset(mark.getPixels(), 0, mark.pitch * mark.h);
		TS_ASSERT(hr.drawChar(mark, 0x0300, kCs, 60, 4, 15, 0, 1));
		int l = inkLeft(mark);
		TS_ASSERT_EQUALS(l, 60);   // no valid anchor: drawn at the caller's own pen

		// A new string on the same line starts with no base either.
		TS_ASSERT(hr.drawChar(scratch, 0x1101, kCs, 4, 4, 15, 0, 1));
		hr.beginString();
		memset(mark.getPixels(), 0, mark.pitch * mark.h);
		TS_ASSERT(hr.drawChar(mark, 0x0300, kCs, 60, 4, 15, 0, 1));
		l = inkLeft(mark);
		TS_ASSERT_EQUALS(l, 60);

		// A base on another line is not this mark's base either.
		TS_ASSERT(hr.drawChar(scratch, 0x1101, kCs, 4, 4, 15, 0, 1));
		memset(mark.getPixels(), 0, mark.pitch * mark.h);
		TS_ASSERT(hr.drawChar(mark, 0x0300, kCs, 60, 20, 15, 0, 1));
		l = inkLeft(mark);
		TS_ASSERT_EQUALS(l, 60);

		scratch.free();
		mark.free();
	}

	/// The mark-placement half of the old test_combining_zero_advance: a
	/// combining mark's ink is drawn against the previous base's hi-res
	/// anchor, not the caller's own (rounded, game-px) pen - the anchor
	/// carries the sub-game-pixel position a run of marks needs to line up
	/// on. The zero-advance half is covered by
	/// test_combining_mark_advances_zero.
	void test_combining_mark_is_drawn_against_the_base_anchor_not_the_pen() {
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		hr.useUtf8Text();
		const G both[] = {
			{ 0x1101, 12, 1, 10, 1, 11, 2, 14 },
			{ 0x0300, 0, -4, 4, 0, 4, 0, 4 },
		};
		const Common::Array<byte> bytes = svfnMulti(both, 2);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/OWN.SVF", ms));

		Graphics::Surface base, mark;
		base.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		mark.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(base.getPixels(), 0, base.pitch * base.h);
		memset(mark.getPixels(), 0, mark.pitch * mark.h);

		// The base's own advance (12) at scale 2 is 6 game px; drawn with a
		// caller pen far from that (34, not 20+6=26), the mark still lands
		// against the base's real hi-res anchor (20+12=32), not the caller's.
		TS_ASSERT(hr.drawChar(base, 0x1101, kCs, 20, 4, 15, 0, 1));
		TS_ASSERT(hr.drawChar(mark, 0x0300, kCs, 34, 4, 15, 0, 1));
		TS_ASSERT_EQUALS(inkLeft(mark), 32);
		TS_ASSERT(inkLeft(mark) != 34);

		base.free();
		mark.free();
	}

	/// advance=game centres any glyph narrower than the game cell it is
	/// given - not only a wide one under the engine default (the wide-glyph
	/// test): explicit `advance=game` on the "other"/ASCII-like path
	/// (kHiResAdvanceGame is not wide-gated) centres too, for every family.
	void test_metrics_game_centres_narrow_glyph() {
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\nadvance=game\n"));
		hr.useUtf8Text();
		// A narrow (non-wide, non-ASCII so it takes the "other" path, not
		// the ascii one - both are gated by rule==Game the same way) glyph:
		// advance 10, ink columns 0..9 (10 px).
		const Common::Array<byte> bytes = svfn(0x00E9, /*advance*/ 10, /*bearing*/ 0, /*inkWidth*/ 10,
											   /*x0*/ 0, /*x1*/ 10, /*y0*/ 2, /*y1*/ 14, /*cell*/ 16, /*ascent*/ 13);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/OWN.SVF", ms));

		Graphics::Surface dest;
		dest.create(96, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, dest.pitch * dest.h);
		const int x = 40, gameAdvance = 8;
		TS_ASSERT(hr.drawChar(dest, 0x00E9, kCs, x, 0, 15, 0, 1, nullptr, true, gameAdvance));
		// slack = gameAdvance*scale - own = 8*2-10 = 6, slack/2 = 3.
		TS_ASSERT_EQUALS(inkLeft(dest), x + 6 / 2);
		dest.free();
	}

	/// A stream over a copy of some bytes whose reads can be made to fail.
	class FlakyStream : public Common::MemoryReadStream {
	public:
		FlakyStream(const Common::Array<byte> &bytes, bool &fail)
			: Common::MemoryReadStream(copyOf(bytes), bytes.size(), DisposeAfterUse::YES), _fail(fail) {}
		uint32 read(void *dataPtr, uint32 dataSize) override {
			return _fail ? 0 : Common::MemoryReadStream::read(dataPtr, dataSize);
		}
		static byte *copyOf(const Common::Array<byte> &bytes) {
			byte *p = (byte *)malloc(bytes.size());
			memcpy(p, bytes.begin(), bytes.size());
			return p;
		}

	private:
		bool &_fail;
	};

	/// An SVF face read from its file whose glyph read fails draws nothing
	/// (and does not crash), also later: its block is not read again. A face
	/// that reads draws as before.
	void test_a_face_whose_glyph_read_fails_draws_once_it_can() {
		Graphics::HiResBitmapFont::setStreamThreshold(0);
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		hr.useUtf8Text();
		const Common::Array<byte> bytes = svfn(0xAC00, 9, 0, 6, 4, 10, 2, 14, 16, 13);
		bool fail = false;
		TS_ASSERT(hr.addFace("/tmp/t/OWN.SVF", new FlakyStream(bytes, fail), DisposeAfterUse::YES));
		Graphics::Surface dest;
		dest.create(64, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(dest.getPixels(), 0, dest.pitch * dest.h);
		fail = true;
		hr.drawChar(dest, 0xAC00, kCs, 10, 5, 15, 0, 1, nullptr, true, 12);
		fail = false;
		hr.drawChar(dest, 0xAC00, kCs, 10, 5, 15, 0, 1, nullptr, true, 12);
		bool ink = false;
		for (int y = 0; y < dest.h && !ink; ++y)
			for (int x = 0; x < dest.w && !ink; ++x)
				ink = *(const byte *)dest.getBasePtr(x, y) != 0;
		TS_ASSERT(!ink);
		// The same face read whole draws it.
		Scumm::ScummHiResText good;
		TS_ASSERT(open(good, overlay, "[font.4]\nface=OWN.SVF\n"));
		good.useUtf8Text();
		TS_ASSERT(good.addFace("/tmp/t/OWN.SVF", new FlakyStream(bytes, fail), DisposeAfterUse::YES));
		TS_ASSERT(good.drawChar(dest, 0xAC00, kCs, 10, 5, 15, 0, 1, nullptr, true, 12));
		for (int y = 0; y < dest.h && !ink; ++y)
			for (int x = 0; x < dest.w && !ink; ++x)
				ink = *(const byte *)dest.getBasePtr(x, y) != 0;
		TS_ASSERT(ink);
		dest.free();
		Graphics::HiResBitmapFont::setStreamThreshold(HIRES_SVF_STREAM_MIN);
	}
};
