#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"

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
