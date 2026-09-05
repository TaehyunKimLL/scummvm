#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

/**
 * Tests for the hi-res text font map reader.
 *
 * The map file is the only user facing part of the hi-res text layer, so the
 * cases below double as a description of what a translation is allowed to
 * write in one.
 */
class HiResFontMapTestSuite : public CxxTest::TestSuite {
private:
	/// Parse a map held in a string, as if it sat in "/games/demo/font.map".
	bool parse(const char *text, Graphics::HiResTextConfig &out,
			   const char *q0 = nullptr, const char *q1 = nullptr) {
		Common::Array<Common::String> qualifiers;
		if (q0)
			qualifiers.push_back(q0);
		if (q1)
			qualifiers.push_back(q1);

		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		return Graphics::HiResFontMap::loadFromStream(
			stream, Common::Path("/games/demo"), qualifiers, out);
	}

public:
	void test_documented_example() {
		const char *map =
			"[hires]\n"
			"scale=3\n"
			"alpha=true\n"
			"\n"
			"[encoding]\n"
			"codepage=utf8\n"
			"\n"
			"[bitmap]\n"
			"single=subtitle.fnt\n"
			"\n"
			"[render]\n"
			"metrics=font\n"
			"\n"
			"[fonts]\n"
			"default=fonts/subtitle.ttf\n"
			"\n"
			"[sizes]\n"
			"default=12pt\n"
			"\n"
			"[map]\n"
			"height_12=default\n";
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg));
		TS_ASSERT_EQUALS(cfg.scale, 3);
		TS_ASSERT(cfg.alpha);
		TS_ASSERT_EQUALS(cfg.encoding, Common::kUtf8);
		TS_ASSERT_EQUALS(cfg.metricsSource, Graphics::kHiResMetricsFont);
		TS_ASSERT_EQUALS(cfg.ttfSize[0], 12);
		TS_ASSERT(cfg.ttfSizeRelative[0]);
		TS_ASSERT_EQUALS(cfg.ttfPath[0].toString('/'), "/games/demo/fonts/subtitle.ttf");
	}

	void test_defaults() {
		Graphics::HiResTextConfig cfg;

		// An untouched config must describe "hi-res text is off", so that a
		// game with no map behaves exactly as it did before.
		TS_ASSERT_EQUALS(cfg.scale, 1);
		TS_ASSERT(!cfg.alpha);
		TS_ASSERT(!cfg.legacy.latinEnabled);
		TS_ASSERT_EQUALS(cfg.encoding, Common::kCodePageInvalid);
		TS_ASSERT_EQUALS(cfg.shadowMode, Graphics::kHiResShadowGame);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsGame);
		TS_ASSERT_EQUALS(cfg.bitmapGlyphs, 0);

		for (int r = 0; r < Graphics::kHiResRoleCount; ++r) {
			TS_ASSERT_EQUALS(cfg.ttfSize[r], 0);
			TS_ASSERT_EQUALS(cfg.ttfSupersample[r], 1);
		}
	}

	void test_empty_map_is_valid() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("", cfg));
		TS_ASSERT_EQUALS(cfg.scale, 1);
	}

	void test_hires_section() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[hires]\nscale=3\nalpha=true\n", cfg));

		TS_ASSERT_EQUALS(cfg.scale, 3);
		TS_ASSERT(cfg.alpha);

		// The caller has to be able to tell a value the map chose from one the
		// user chose, because an explicit user setting outranks the map.
		TS_ASSERT(cfg.scaleFromMap);
		TS_ASSERT(cfg.alphaFromMap);
	}

	void test_scale_out_of_range_is_rejected() {
		Graphics::HiResTextConfig cfg;

		// Parsing still succeeds - one bad key must not throw the whole map
		// away - but the value is not taken.
		TS_ASSERT(parse("[hires]\nscale=9\n", cfg));
		TS_ASSERT_EQUALS(cfg.scale, 1);
		TS_ASSERT(!cfg.scaleFromMap);
	}

	void test_codepage_names() {
		typedef Graphics::HiResFontMap M;

		TS_ASSERT_EQUALS(M::parseCodePage("cp949"), Common::kWindows949);
		TS_ASSERT_EQUALS(M::parseCodePage("uhc"), Common::kWindows949);
		TS_ASSERT_EQUALS(M::parseCodePage("sjis"), Common::kWindows932);
		TS_ASSERT_EQUALS(M::parseCodePage("cp932"), Common::kWindows932);
		TS_ASSERT_EQUALS(M::parseCodePage("gbk"), Common::kWindows936);
		TS_ASSERT_EQUALS(M::parseCodePage("big5"), Common::kWindows950);
		TS_ASSERT_EQUALS(M::parseCodePage("johab"), Common::kJohab);

		// Unicode input is the reason this layer works on code points.
		TS_ASSERT_EQUALS(M::parseCodePage("utf8"), Common::kUtf8);
		TS_ASSERT_EQUALS(M::parseCodePage("utf-8"), Common::kUtf8);

		// Names are matched without regard to case.
		TS_ASSERT_EQUALS(M::parseCodePage("UTF-8"), Common::kUtf8);
		TS_ASSERT_EQUALS(M::parseCodePage("SJIS"), Common::kWindows932);

		TS_ASSERT_EQUALS(M::parseCodePage("klingon"), Common::kCodePageInvalid);
	}

	void test_non_dbcs_encoding_selection() {
		typedef Graphics::HiResFontMap M;
		TS_ASSERT_EQUALS(M::parseCodePage("CP1252"), Common::kWindows1252);
		TS_ASSERT_EQUALS(M::parseCodePage("cp1251"), Common::kWindows1251);
		TS_ASSERT_EQUALS(M::parseCodePage("latin1"), Common::kISO8859_1);
		TS_ASSERT_EQUALS(M::parseCodePage("macroman"), Common::kMacRoman);
		TS_ASSERT_EQUALS(M::parseCodePage("cp850"), Common::kDos850);
		TS_ASSERT_EQUALS(M::parseCodePage("ascii"), Common::kASCII);
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[encoding]\ncodepage=cp1252\n", cfg));
		TS_ASSERT_EQUALS(cfg.encoding, Common::kWindows1252);
		TS_ASSERT(cfg.encodingFromMap);
		TS_ASSERT(!cfg.legacy.latinEnabled);
	}

	void test_encoding_section() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[encoding]\ncodepage=utf8\n", cfg));
		TS_ASSERT_EQUALS(cfg.encoding, Common::kUtf8);
		TS_ASSERT(cfg.encodingFromMap);
	}

	void test_unknown_codepage_keeps_default() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[encoding]\ncodepage=nonesuch\n", cfg));
		TS_ASSERT_EQUALS(cfg.encoding, Common::kCodePageInvalid);
		TS_ASSERT(!cfg.encodingFromMap);
	}

	void test_bitmap_section() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[bitmap]\nmulti=korean%02d.fnt\nsingle=korean.fnt\nglyphs=2350\n", cfg));

		TS_ASSERT_EQUALS(cfg.bitmapPattern, "korean%02d.fnt");
		TS_ASSERT_EQUALS(cfg.bitmapSingle, "korean.fnt");
		TS_ASSERT_EQUALS(cfg.bitmapGlyphs, 2350);
	}

	void test_bad_glyph_count_keeps_default() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[bitmap]\nglyphs=0\n", cfg));
		TS_ASSERT_EQUALS(cfg.bitmapGlyphs, 0);
	}

	void test_latin_bitmap_implies_enabled() {
		Graphics::HiResTextConfig cfg;

		// Naming a Latin font is itself the request to use it: a map that
		// ships one but forgets "enabled=true" should still work.
		TS_ASSERT(parse("[latin]\nbitmap=latin24.fnt\n", cfg));
		TS_ASSERT(cfg.legacy.latinEnabled);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapName, "latin24.fnt");
	}

	void test_latin_metrics() {
		Graphics::HiResTextConfig cfg;

		// "game" keeps the original layout, so text still fits the boxes the
		// game script measured with its own font.
		TS_ASSERT(parse("[latin]\nenabled=true\nmetrics=game\n", cfg));
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsGame);

		cfg.clear();
		TS_ASSERT(parse("[latin]\nenabled=true\nmetrics=font\n", cfg));
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsFont);

		// The older spellings select only their corresponding renderer.
		cfg.clear();
		TS_ASSERT(parse("[latin]\nenabled=true\nmetrics=bitmap\n", cfg));
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsFont);

		cfg.clear();
		TS_ASSERT(parse("[latin]\nenabled=true\nmetrics=ttf\n", cfg));
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsGame);
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfMetrics, Graphics::kHiResMetricsFont);
	}

	void test_shadow_modes() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[shadow]\nmode=outline\noffset=2\ncolor=7\n", cfg));

		TS_ASSERT_EQUALS(cfg.shadowMode, Graphics::kHiResShadowOutline);
		TS_ASSERT_EQUALS(cfg.shadowOffset, 2);
		TS_ASSERT_EQUALS((int)cfg.shadowColor, 7);
		TS_ASSERT(cfg.shadowColorSet);

		cfg.clear();
		TS_ASSERT(parse("[shadow]\nmode=none\n", cfg));
		TS_ASSERT_EQUALS(cfg.shadowMode, Graphics::kHiResShadowNone);

		// An unknown mode falls back to following the game rather than
		// silently turning the shadow off.
		cfg.clear();
		TS_ASSERT(parse("[shadow]\nmode=sparkle\n", cfg));
		TS_ASSERT_EQUALS(cfg.shadowMode, Graphics::kHiResShadowGame);

		// Without an explicit colour the caller keeps the game's.
		TS_ASSERT(!cfg.shadowColorSet);
	}

	void test_sizes_plain_and_supersampled() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[sizes]\ntitle=32\nbold=16x2\n", cfg));

		TS_ASSERT_EQUALS(cfg.ttfSize[Graphics::kHiResRoleTitle], 32);
		TS_ASSERT_EQUALS(cfg.ttfSupersample[Graphics::kHiResRoleTitle], 1);

		// "16x2" renders at 32 and filters back down to 16, which keeps a
		// pixel font on its native grid.
		TS_ASSERT_EQUALS(cfg.ttfSize[Graphics::kHiResRoleBold], 16);
		TS_ASSERT_EQUALS(cfg.ttfSupersample[Graphics::kHiResRoleBold], 2);
	}

	void test_sizes_in_points_follow_the_scale() {
		Graphics::HiResTextConfig cfg;

		// "pt" is resolution independent: the same map has to look the same
		// at 2x and at 3x. The adapter resolves it after user overrides.
		TS_ASSERT(parse("[hires]\nscale=3\n\n[sizes]\ndefault=12pt\n", cfg));
		TS_ASSERT_EQUALS(cfg.ttfSize[Graphics::kHiResRoleDefault], 12);

		cfg.clear();
		TS_ASSERT(parse("[hires]\nscale=2\n\n[sizes]\ndefault=12pt\n", cfg));
		TS_ASSERT_EQUALS(cfg.ttfSize[Graphics::kHiResRoleDefault], 12);
	}

	void test_qualified_sections_win() {
		const char *map =
			"[bitmap]\n"
			"single=common.fnt\n"
			"[bitmap:v2]\n"
			"single=v2.fnt\n"
			"[bitmap:maniac]\n"
			"single=maniac.fnt\n";

		// The most specific qualifier the caller offers is tried first.
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg, "maniac", "v2"));
		TS_ASSERT_EQUALS(cfg.bitmapSingle, "maniac.fnt");

		// A caller that does not know the game still gets the version.
		cfg.clear();
		TS_ASSERT(parse(map, cfg, "zak", "v2"));
		TS_ASSERT_EQUALS(cfg.bitmapSingle, "v2.fnt");

		// And with no qualifier at all, the bare section.
		cfg.clear();
		TS_ASSERT(parse(map, cfg));
		TS_ASSERT_EQUALS(cfg.bitmapSingle, "common.fnt");
	}

	void test_height_roles_are_merged_not_replaced() {
		const char *map =
			"[map]\n"
			"height_8=default\n"
			"height_12=bold\n"
			"[map:v5]\n"
			"height_12=title\n";

		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg, "v5"));

		// The qualified section refines one height...
		TS_ASSERT_EQUALS(cfg.roleForHeight(12), Graphics::kHiResRoleTitle);
		// ...without dropping the ones only the bare section mentions.
		TS_ASSERT_EQUALS(cfg.roleForHeight(8), Graphics::kHiResRoleDefault);

		// An unmapped height falls back to the default role.
		TS_ASSERT_EQUALS(cfg.roleForHeight(99), Graphics::kHiResRoleDefault);
	}

	void test_relative_paths_resolve_against_the_map() {
		typedef Graphics::HiResFontMap M;
		const Common::Path base("/games/demo");

		// A translation ships its fonts next to the map and stays movable.
		TS_ASSERT_EQUALS(M::resolvePath("fonts/nanum.ttf", base).toString('/'),
						 "/games/demo/fonts/nanum.ttf");

		// An absolute path is used as it stands.
		TS_ASSERT_EQUALS(M::resolvePath("/usr/share/fonts/x.ttf", base).toString('/'),
						 "/usr/share/fonts/x.ttf");

		// So is a Windows drive letter.
		TS_ASSERT_EQUALS(M::resolvePath("C:/fonts/x.ttf", base).toString('/'),
						 "C:/fonts/x.ttf");

		TS_ASSERT(M::resolvePath("", base).empty());
	}

	void test_role_names() {
		typedef Graphics::HiResFontMap M;

		TS_ASSERT_EQUALS(M::parseRole("title"), Graphics::kHiResRoleTitle);
		TS_ASSERT_EQUALS(M::parseRole("bold"), Graphics::kHiResRoleBold);
		TS_ASSERT_EQUALS(M::parseRole("default"), Graphics::kHiResRoleDefault);

		// Anything unrecognised is the default role, not an error: a map that
		// names a role a future version adds must still load today.
		TS_ASSERT_EQUALS(M::parseRole("handwriting"), Graphics::kHiResRoleDefault);
	}

	void test_realistic_map() {
		// The shape a shipped translation actually uses.
		const char *map =
			"# MI2: bitmap Hangul, 8bpp alpha, 3x\n"
			"[hires]\n"
			"scale=3\n"
			"alpha=true\n"
			"\n"
			"[bitmap]\n"
			"multi=svfn%02d.fnt\n"
			"\n"
			"[latin]\n"
			"enabled=true\n"
			"bitmap=svfnlat.fnt\n"
			"\n"
			"[shadow]\n"
			"mode=game\n"
			"\n"
			"[map]\n"
			"height_24=default\n"
			"height_27=default\n"
			"height_36=title\n";

		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg, "monkey2", "v5"));

		TS_ASSERT_EQUALS(cfg.scale, 3);
		TS_ASSERT(cfg.alpha);
		TS_ASSERT_EQUALS(cfg.bitmapPattern, "svfn%02d.fnt");
		TS_ASSERT(cfg.legacy.latinEnabled);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapName, "svfnlat.fnt");
		TS_ASSERT_EQUALS(cfg.shadowMode, Graphics::kHiResShadowGame);
		TS_ASSERT_EQUALS(cfg.roleForHeight(36), Graphics::kHiResRoleTitle);
		TS_ASSERT_EQUALS(cfg.roleForHeight(24), Graphics::kHiResRoleDefault);

		// No TrueType font is named, so this map works in a build without
		// FreeType - which is the point of the bitmap path.
		for (int r = 0; r < Graphics::kHiResRoleCount; ++r)
			TS_ASSERT(cfg.ttfPath[r].empty());
	}

	void test_global_metrics_are_not_latin_only() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[render]\nmetrics=font\n", cfg));
		TS_ASSERT_EQUALS(cfg.metricsSource, Graphics::kHiResMetricsFont);
		TS_ASSERT(!cfg.legacy.latinEnabled);
	}

	void test_legacy_ttf_path_does_not_enable_latin() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[latin]\nenabled=false\nfont=sample.ttf\nmetrics=bitmap\n", cfg));
		TS_ASSERT(!cfg.legacy.latinEnabled);
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfMetrics, Graphics::kHiResMetricsGame);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsFont);
	}

	void test_bad_values_do_not_replace_existing_settings() {
		Graphics::HiResTextConfig cfg;
		cfg.scale = 2;
		cfg.alpha = true;
		cfg.metricsSource = Graphics::kHiResMetricsGame;
		TS_ASSERT(parse("[hires]\nscale=3junk\nalpha=typo\n[shadow]\ncolor=256\noffset=-5\n"
			"[sizes]\ndefault=999999999999999999999999pt\nbold=16x0\n[render]\nmetrics=typo\n", cfg));
		TS_ASSERT_EQUALS(cfg.scale, 2);
		TS_ASSERT(cfg.alpha);
		TS_ASSERT(!cfg.shadowColorSet);
		TS_ASSERT_EQUALS(cfg.shadowOffset, -1);
		TS_ASSERT_EQUALS(cfg.ttfSize[0], 0);
		TS_ASSERT_EQUALS(cfg.ttfSize[1], 0);
		TS_ASSERT_EQUALS(cfg.metricsSource, Graphics::kHiResMetricsGame);
	}

	void test_reparse_sizes_replaces_units_and_supersampling() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[sizes]\ndefault=16x2\n", cfg));
		TS_ASSERT(parse("[sizes]\ndefault=12pt\n", cfg));
		TS_ASSERT_EQUALS(cfg.ttfSupersample[0], 1);
		TS_ASSERT_EQUALS(cfg.ttfSize[0], 12);
		TS_ASSERT(cfg.ttfSizeRelative[0]);
		cfg.scale = 2;
		TS_ASSERT_EQUALS(cfg.ttfSize[0], 12);
		TS_ASSERT(parse("[sizes]\ndefault=18\n", cfg));
		TS_ASSERT(!cfg.ttfSizeRelative[0]);
	}

	void test_invalid_syntax_is_atomic() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(!parse("[hires]\nscale=3\n[broken\n", cfg));
		TS_ASSERT_EQUALS(cfg.scale, 1);
	}

	void test_clear_resets_everything() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[hires]\nscale=3\nalpha=true\n\n[latin]\nenabled=true\n", cfg));
		TS_ASSERT_EQUALS(cfg.scale, 3);

		cfg.clear();

		TS_ASSERT_EQUALS(cfg.scale, 1);
		TS_ASSERT(!cfg.alpha);
		TS_ASSERT(!cfg.legacy.latinEnabled);
		TS_ASSERT(!cfg.scaleFromMap);
		TS_ASSERT_EQUALS(cfg.heightRoles.size(), 0u);
	}
};
