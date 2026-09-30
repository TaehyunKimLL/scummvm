#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"
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

	/// M1: how many times a given warning text appears - spec 10 says "once
	/// per cause per load", and a text present in the array does not by
	/// itself say how many times it was pushed.
	static int warningCount(const Scumm::ScummHiResText &hr, const Common::String &text) {
		int n = 0;
		for (uint i = 0; i < hr.map().warnings.size(); ++i)
			if (hr.map().warnings[i] == text)
				++n;
		return n;
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

	/// M3: the cell-height check covers every SVF the plan names, not only
	/// the id chain - a range.*= SVF and a [glyphs] target SVF, each of a
	/// different height than the id chain's own first SVF, are each refused
	/// and dropped, with their own warning.
	void test_cell_height_mismatch_covers_range_and_target_svfs() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nface=OWN.SVF\nrange.basic-latin=RANGE.SVF\n"
					   "[glyphs.4]\n0x07 = TARGET.SVF:u+2620\n"));
		Common::Array<uint32> own;
		own.push_back(0xAC00);
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));

		Common::Array<uint32> rangeCps;
		rangeCps.push_back('A');
		const Common::Array<byte> rangeBytes = ScummHiResFixture::makeFont(rangeCps, ScummHiResFixture::kCell * 2);
		Common::MemoryReadStream rangeMs(rangeBytes.begin(), rangeBytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/RANGE.SVF", rangeMs));

		Common::Array<uint32> targetCps;
		targetCps.push_back(0x2620);
		const Common::Array<byte> targetBytes = ScummHiResFixture::makeFont(targetCps, ScummHiResFixture::kCell * 2);
		Common::MemoryReadStream targetMs(targetBytes.begin(), targetBytes.size());
		TS_ASSERT(hr.addFace("/tmp/t/TARGET.SVF", targetMs));

		uint32 cpLatin = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpLatin));   // RANGE.SVF dropped: falls to the game
		uint32 cpTarget = 0x07;
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpTarget));  // TARGET.SVF dropped likewise

		TS_ASSERT(hasWarning(hr, "HIRESTXT.MAP: /tmp/t/RANGE.SVF: cell height 32 differs from /tmp/t/OWN.SVF's 16 on 4; not used"));
		TS_ASSERT(hasWarning(hr, "HIRESTXT.MAP: /tmp/t/TARGET.SVF: cell height 32 differs from /tmp/t/OWN.SVF's 16 on 4; not used"));
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
		TS_ASSERT_EQUALS(warningCount(hr, "HIRESTXT.MAP: missing=U+25A1 has no effect: OWN.SVF has no glyph for it"), 1);
	}

	/// M1: a [glyphs] target lacking its glyph, touched through several ids
	/// that all name the same target face, is still warned about exactly
	/// once (spec 10: "once per cause per load"), not once per id.
	void test_target_lacking_glyph_is_warned_once_across_ids() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nface=OWN.SVF\n[glyphs.4]\n0x07 = SYM.SVF:u+2620\n"
					   "[font.0]\nface=OWN.SVF\n[glyphs.0]\n0x07 = SYM.SVF:u+2620\n"));
		Common::Array<uint32> own, sym;
		own.push_back(0x25A1);
		sym.push_back('x');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		TS_ASSERT(add(hr, "/tmp/t/SYM.SVF", sym));
		uint32 cp4 = 0x07, cp0 = 0x07;
		hr.perGlyphSourceFor(kCs, cp4);
		hr.perGlyphSourceFor(ScummHiResFixture::kOtherCs, cp0);
		TS_ASSERT_EQUALS(warningCount(hr, "HIRESTXT.MAP: [glyphs] 0x07 -> SYM.SVF:U+2620: the face has no such glyph; the game's font draws it"), 1);
	}

	/// M1: an id with no face of its own at all (a pure borrower of a
	/// neighbouring id's chain, B6/design 5.3) has nothing of its own to
	/// check missing= against - checking it anyway is a false positive the
	/// donor id's own binding already covers once. Here kOtherCs borrows
	/// kCs's chain (ScummHiResFixture::open() gives both the same 8x8 grid).
	void test_missing_false_positive_skipped_for_a_pure_borrower_id() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=OWN.SVF\n"));
		Common::Array<uint32> own;
		own.push_back('Z');
		TS_ASSERT(add(hr, "/tmp/t/OWN.SVF", own));
		uint32 cp4 = 'Z', cp0 = 'Z';
		hr.perGlyphSourceFor(kCs, cp4);
		hr.perGlyphSourceFor(ScummHiResFixture::kOtherCs, cp0);
		// Exactly the donor's (kCs's) own missing= check fired - kOtherCs's
		// empty chain added no second copy.
		TS_ASSERT_EQUALS(warningCount(hr, "HIRESTXT.MAP: missing=U+25A1 has no effect: OWN.SVF has no glyph for it"), 1);
	}

	/// M2: an id naming only a TrueType face still binds (_idBound) and runs
	/// its S3/M1 load-time checks - openPlanFace() used to record an opened
	/// TrueType path only under its size-qualified cache key, never its raw
	/// one, so collectFacePaths()'s "every named path is in _sources or
	/// _failedFaces" gate in checkIdOnceReady() waited forever.
	void test_ttf_only_id_binds_and_runs_its_checks() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		static const char *const kFonts[] = {
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/System/Library/Fonts/AppleSDGothicNeo.ttc",
		};
		const char *ttf = nullptr;
		for (uint i = 0; i < ARRAYSIZE(kFonts) && !ttf; ++i)
			if (Common::FSNode(kFonts[i]).exists())
				ttf = kFonts[i];
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		// DejaVuSans has broad symbol coverage (it has U+25A1, the fixture's
		// default missing= box) but no Hangul: override missing= to U+AC00
		// for this id specifically.
		const Common::String body = Common::String::format("[font.4]\nface=%s\nmissing=u+ac00\n", ttf);
		TS_ASSERT(open(hr, overlay, body.c_str()));
		hr.noteGameCharset(kCs, 16, 16);
		hr.setCharsetGrid(kCs, 16, 16);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		// The id's own missing= check must fire once it is bound - it never
		// did before M2, since a TrueType-only chain's id never bound at all.
		TS_ASSERT_EQUALS(warningCount(hr, "HIRESTXT.MAP: missing=U+AC00 has no effect: " + Common::String(ttf) + " has no glyph for it"), 1);
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// M9: a map chain's translation coverage is checked, not only the
	/// map-less form's (openTtfChain()'s own call) - checkCoverageForId()
	/// runs the same Graphics::checkCoverage() from ensureChainSources()
	/// for every id's own chain. A real file is needed here: loadFonts()
	/// (which alone populates the coverage sample from a noted translation)
	/// resets every face addFace() added in memory.
	void test_coverage_warning_for_a_map_chain() {
		Common::FSNode tmp("/tmp/scummvm-hires-m9-test");
		tmp.createDirectory();
		const Common::Path gameDir = tmp.getPath();
		{
			Common::Array<uint32> cps;
			cps.push_back('A');
			const Common::Array<byte> bytes = ScummHiResFixture::makeFont(cps);
			Common::DumpFile f;
			TS_ASSERT(f.open(gameDir.join(Common::Path("OWN.SVF", '/'))));
			f.write(bytes.begin(), bytes.size());
			f.close();
		}

		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		const Common::String text = "[map]\nversion=2\n[render]\nblend=off\n[font.4]\nface=OWN.SVF\n";
		Graphics::HiResMap m;
		Common::Array<Common::String> q;
		Common::MemoryReadStream s((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, gameDir, q, Graphics::kHiResKeysScumm, m));
		hr.adoptMap(m);
		hr.noteGameCharset(kCs, 8, 8);
		hr.setCharsetGrid(kCs, 8, 8);
		hr.useUtf8Text();

		// A translated string containing U+AC00, which OWN.SVF (only 'A')
		// lacks.
		static const byte kTranslated[] = { 0xEA, 0xB0, 0x80, 0 }; // UTF-8 for U+AC00
		hr.noteTranslatedString(kTranslated, sizeof(kTranslated));
		TS_ASSERT(hr.loadFonts(gameDir));

		// Touch the id so its chain's coverage is actually checked.
		uint32 cp = 'A';
		hr.perGlyphSourceFor(kCs, cp);

		bool found = false;
		for (uint i = 0; i < hr.coverageWarnings().size() && !found; ++i)
			found = hr.coverageWarnings()[i].contains("lacks");
		TS_ASSERT(found);
	}
};
