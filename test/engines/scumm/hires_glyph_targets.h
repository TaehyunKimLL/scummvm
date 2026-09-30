#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * [glyphs] targeted glyphs (design section 6.7): `0xNN = <face>:u+XXXX` draws
 * exactly that code point from exactly that face, bypassing the id's range
 * rules, its coverage fallback and `missing=` alike - the face answers only
 * its own listed codes.
 */
class ScummHiResGlyphTargetsTestSuite : public CxxTest::TestSuite {
	static const int kCs = ScummHiResFixture::kCs;

	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *body) {
		return ScummHiResFixture::open(hr, overlay, body);
	}

	bool add(Scumm::ScummHiResText &hr, const char *path, const Common::Array<uint32> &cps) {
		return ScummHiResFixture::addFace(hr, path, cps);
	}

	static bool hasWarning(const Scumm::ScummHiResText &hr, const Common::String &text) {
		for (uint i = 0; i < hr.map().warnings.size(); ++i) {
			if (hr.map().warnings[i] == text)
				return true;
		}
		return false;
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	void test_targeted_glyph_draws_that_face_and_code_point() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs.4]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back(0x2620);          // the id chain has it too: must not be used
		sym.push_back(0x2620);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		uint32 cp = 0x07;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/SYM.SVF"));
		TS_ASSERT_EQUALS(cp, 0x2620u);
	}

	void test_targeted_pua_glyph_in_a_custom_svf() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x07 = ICONS.SVF:u+e001\n"));
		Common::Array<uint32> own, icons;
		own.push_back('A');
		icons.push_back(0xE001);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/ICONS.SVF", icons));
		uint32 cp = 0x07;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/ICONS.SVF"));
		TS_ASSERT_EQUALS(cp, 0xE001u);
	}

	void test_target_lacking_the_glyph_falls_back_to_the_game_not_the_box() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back(0x25A1);          // the box exists, and is still not used
		sym.push_back('x');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		uint32 cp = 0x07;
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}

	void test_same_target_uses_the_id_chain_and_bypasses_ranges() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\nrange.misc-symbols=original\n[glyphs]\n0x07 = same:u+2620\n"));
		Common::Array<uint32> own;
		own.push_back(0x2620);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		uint32 cp = 0x07;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp), hr.sourceForFace("/tmp/t/OWN.SVF"));
		uint32 direct = 0x2620;         // the same code point through the rules: original
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, direct));
	}

	void test_target_face_is_not_a_fallback_for_other_codes() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back('A');
		sym.push_back(0xAC00);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		uint32 cp = 0xAC00;
		Graphics::UnicodeGlyphSource *src = hr.perGlyphSourceFor(kCs, cp);
		TS_ASSERT(src == nullptr || src == hr.sourceForFace("/tmp/t/OWN.SVF"));   // the box from OWN, or nothing - never SYM
	}

	void test_original_in_glyphs_declines() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x5f = original\n"));
		Common::Array<uint32> own;
		own.push_back('_');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		uint32 cp = 0x5f;
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}

	// ---- S3 / spec 10.4: load-time warnings, kept in HiResMap::warnings ----

	/// An SVF whose cell height differs from the first SVF of the id's own
	/// chain is refused and dropped: 'B' (only in TALL.SVF) is never drawn.
	void test_cell_height_mismatch_is_warned_and_dropped() {
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
		TS_ASSERT(hasWarning(hr, "HIRESTXT.MAP: /tmp/t/TALL.SVF: cell height 32 differs from /tmp/t/OWN.SVF's 16 on 4; not used"));
		uint32 cp = 'B';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}

	/// A [glyphs] target whose face lacks the code point: one load-time
	/// warning, naming the face as written and the code point.
	void test_target_lacking_glyph_is_warned_once() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n[glyphs]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back(0x25A1);
		sym.push_back('x');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		TS_ASSERT(hasWarning(hr, "HIRESTXT.MAP: [glyphs] 0x07 -> SYM.SVF:U+2620: the face has no such glyph; the game's font draws it"));
	}

	/// missing= set, but no face of the id's chain has that code point.
	void test_missing_without_a_glyph_is_warned_once() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back('Z');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(hasWarning(hr, "HIRESTXT.MAP: missing=U+25A1 has no effect: OWN.SVF has no glyph for it"));
	}
};
