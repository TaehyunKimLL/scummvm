#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_value.h"

#include "../system/null_osystem.h"

class HiResFontValueTestSuite : public CxxTest::TestSuite {
	Graphics::HiResFaceNames names() {
		Graphics::HiResFaceNames n;
		n["ko"] = "fonts/KO.SVF";
		n["sym"] = "SYMBOLS.SVF";
		return n;
	}

	bool value(const char *text, Graphics::HiResFontValue &out, Common::Array<Common::String> &w) {
		return Graphics::parseFontValue(text, names(), Common::Path("/maps", '/'), Common::Path("/games/mi2", '/'), out, w);
	}

	bool glyph(const char *text, Graphics::HiResGlyphRule &out) {
		Common::String error;
		return Graphics::parseGlyphRule(text, names(), Common::Path("/maps", '/'), out, error);
	}

public:
	void test_names_paths_and_sentinels() {
		Graphics::HiResFontValue v;
		Common::Array<Common::String> w;
		TS_ASSERT(value("ko, LATIN.TTF#1, same", v, w));
		TS_ASSERT_EQUALS(v.entries.size(), 3u);
		TS_ASSERT_EQUALS(v.entries[0].kind, Graphics::kHiResFaceFile);
		TS_ASSERT_EQUALS(v.entries[0].path.toString('/'), "/maps/fonts/KO.SVF");   // a name: map folder
		TS_ASSERT_EQUALS(v.entries[1].path.toString('/'), "/games/mi2/LATIN.TTF#1"); // a literal: pathBaseDir
		TS_ASSERT_EQUALS(v.entries[2].kind, Graphics::kHiResFaceSame);
		TS_ASSERT(v.hasSame());
		TS_ASSERT(!v.endsInOriginal());
		TS_ASSERT(w.empty());
	}

	void test_original_ends_the_chain() {
		Graphics::HiResFontValue v;
		Common::Array<Common::String> w;
		TS_ASSERT(value("sym, original, ko", v, w));
		TS_ASSERT_EQUALS(v.entries.size(), 2u);
		TS_ASSERT(v.endsInOriginal());
		TS_ASSERT_EQUALS(w.size(), 1u);
		TS_ASSERT_EQUALS(w[0], "HIRESTXT.MAP: entries after 'original' are ignored: 'ko'");
	}

	void test_unknown_name_dropped() {
		Graphics::HiResFontValue v;
		Common::Array<Common::String> w;
		TS_ASSERT(!value("nosuchface", v, w));
		TS_ASSERT(v.empty());
		TS_ASSERT_EQUALS(w[0], "HIRESTXT.MAP: unknown face name 'nosuchface'");
	}

	void test_face_name_grammar() {
		TS_ASSERT(Graphics::isValidFaceName("dlg_2-b"));
		TS_ASSERT(!Graphics::isValidFaceName("same"));
		TS_ASSERT(!Graphics::isValidFaceName("Original"));
		TS_ASSERT(!Graphics::isValidFaceName("data"));
		TS_ASSERT(!Graphics::isValidFaceName("a.b"));
	}

	void test_glyph_values() {
		Graphics::HiResGlyphRule r;
		TS_ASSERT(glyph("original", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphOriginal);
		TS_ASSERT(glyph("u+2026", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphCodePoint);
		TS_ASSERT_EQUALS(r.value, 0x2026u);
		TS_ASSERT(glyph("+0xFEE0", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphOffset);
		TS_ASSERT_EQUALS(r.value, 0xFEE0u);
		TS_ASSERT(!glyph("keep", r));   // removed: the one name is 'original'
	}

	void test_targeted_glyphs() {
		Graphics::HiResGlyphRule r;
		TS_ASSERT(glyph("sym:u+2620", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphTarget);
		TS_ASSERT_EQUALS(r.value, 0x2620u);
		TS_ASSERT_EQUALS(r.face.path.toString('/'), "/maps/SYMBOLS.SVF");
		TS_ASSERT(glyph("ICONS.SVF:u+e001", r));                      // PUA in a custom SVF
		TS_ASSERT_EQUALS(r.value, 0xE001u);
		TS_ASSERT_EQUALS(r.face.path.toString('/'), "/maps/ICONS.SVF");

		// "data:" is resolved through HiResFontMap::resolvePath(), which -
		// for a value it cannot find - falls back to the real file system
		// (SearchMan/dataRoots()); that needs a real g_system, as in
		// hires_text_font_map.h's own data: tests. No test root has
		// hires_text/x.svf, so this also prints one harmless "not in the
		// extrapath or any ScummVM data folder" warning every run - the
		// same miss-path behaviour font_map.cpp already has.
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
		TS_ASSERT(glyph("data:hires_text/x.svf:u+2620", r));          // split at the last colon
		TS_ASSERT_EQUALS(r.face.written, "data:hires_text/x.svf");
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif

		TS_ASSERT(glyph("same:u+2620", r));
		TS_ASSERT_EQUALS(r.face.kind, Graphics::kHiResFaceSame);
		TS_ASSERT(glyph("sym:+0xE000", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphTargetOffset);
		TS_ASSERT(!glyph("sym, ko:u+2620", r));    // a chain is refused
		TS_ASSERT(!glyph("original:u+2620", r));   // original is not a face
		TS_ASSERT(!glyph("sym:2620", r));          // the part after the colon must be u+ or +0x
	}

	void test_surrogates_rejected() {
		uint32 cp;
		TS_ASSERT(Graphics::parseCodePointValue("u+d7ff", cp));   // just below the surrogate range: valid
		TS_ASSERT_EQUALS(cp, 0xD7FFu);
		TS_ASSERT(!Graphics::parseCodePointValue("u+d800", cp));  // low surrogate: not a scalar value
		TS_ASSERT(!Graphics::parseCodePointValue("u+dfff", cp));  // high surrogate: not a scalar value

		Graphics::HiResGlyphRule r;
		TS_ASSERT(glyph("u+d7ff", r));
		TS_ASSERT(!glyph("u+d800", r));
		TS_ASSERT(!glyph("u+dfff", r));
		TS_ASSERT(!glyph("sym:u+d800", r));        // targeted form: same rejection
	}

	void test_same_and_original_are_case_insensitive() {
		Graphics::HiResFontValue v;
		Common::Array<Common::String> w;
		TS_ASSERT(value("SAME", v, w));
		TS_ASSERT_EQUALS(v.entries.size(), 1u);
		TS_ASSERT_EQUALS(v.entries[0].kind, Graphics::kHiResFaceSame);
		TS_ASSERT(w.empty());

		Graphics::HiResFontValue v2;
		Common::Array<Common::String> w2;
		TS_ASSERT(value("ORIGINAL", v2, w2));
		TS_ASSERT(v2.endsInOriginal());
		TS_ASSERT(w2.empty());

		Graphics::HiResGlyphRule r;
		TS_ASSERT(glyph("ORIGINAL", r));
		TS_ASSERT_EQUALS(r.kind, Graphics::kHiResGlyphOriginal);
		TS_ASSERT(glyph("SAME:u+2620", r));
		TS_ASSERT_EQUALS(r.face.kind, Graphics::kHiResFaceSame);
	}
};
