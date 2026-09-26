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
		TS_ASSERT_EQUALS(cfg.glyphOverrides.size(), 0u);
		TS_ASSERT_EQUALS(cfg.scopedGlyphOverrides.size(), 0u);
	}

	// --- [glyphs] --------------------------------------------------------
	//
	// A game's own font is not a character set: LucasArts titles draw an
	// ellipsis where Latin-1 has '^' and arrows where it has '_' and DEL.

	void test_glyphs_keep_and_remap() {
		const char *map =
			"[glyphs]\n"
			"0x5e = keep\n"
			"0x7f = u+2192\n"
			"100 = keep\n";
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg));

		Graphics::HiResGlyphOverride o;
		TS_ASSERT(cfg.glyphOverride(0x5e, o));
		TS_ASSERT_EQUALS(o.action, Graphics::kHiResGlyphKeep);

		TS_ASSERT(cfg.glyphOverride(0x7f, o));
		TS_ASSERT_EQUALS(o.action, Graphics::kHiResGlyphRemap);
		TS_ASSERT_EQUALS(o.codepoint, 0x2192u);

		// Decimal keys are accepted too.
		TS_ASSERT(cfg.glyphOverride(100, o));

		// Anything not listed is an ordinary character.
		TS_ASSERT(!cfg.glyphOverride(0x41, o));
	}

	void test_glyphs_scope_refines_common_table() {
		// 0x5F is an arrow in the dialogue charset and a real underscore in
		// the others, so a scoped entry must not leak into the common table.
		const char *map =
			"[glyphs]\n"
			"0x5e = keep\n"
			"[glyphs:cs1]\n"
			"0x5f = keep\n";

		Common::Array<Common::String> scopes;
		scopes.push_back("cs0");
		scopes.push_back("cs1");

		Common::MemoryReadStream stream((const byte *)map, strlen(map));
		Common::Array<Common::String> qualifiers;
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(
			stream, Common::Path("/games/demo"), qualifiers, cfg, &scopes));

		Graphics::HiResGlyphOverride o;
		// Common entries apply to every scope, and to no scope at all.
		TS_ASSERT(cfg.glyphOverride(0x5e, o, 0));
		TS_ASSERT(cfg.glyphOverride(0x5e, o, 1));
		TS_ASSERT(cfg.glyphOverride(0x5e, o));

		// The scoped one applies only where it was written.
		TS_ASSERT(cfg.glyphOverride(0x5f, o, 1));
		TS_ASSERT(!cfg.glyphOverride(0x5f, o, 0));
		TS_ASSERT(!cfg.glyphOverride(0x5f, o));
	}

	void test_glyphs_rejects_nonsense() {
		const char *map =
			"[glyphs]\n"
			"0x5e = maybe\n"
			"notacode = keep\n"
			"0xzz = keep\n";
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg));
		// Every line is unusable, so nothing is recorded and nothing crashes.
		TS_ASSERT_EQUALS(cfg.glyphOverrides.size(), 0u);
	}

	void test_glyphs_code_out_of_range_is_rejected() {
		Graphics::HiResTextConfig cfg;
		// Above the Unicode range there is nothing to name. The key is
		// written in hex because the INI reader does not allow '+' in a key
		// name - see test_glyphs_key_may_not_use_u_plus_form().
		TS_ASSERT(parse("[glyphs]\n0x110000 = keep\n", cfg));
		TS_ASSERT_EQUALS(cfg.glyphOverrides.size(), 0u);

		// The same limit applies to a remap target, which is a value and so
		// may use either form.
		TS_ASSERT(parse("[glyphs]\n0x5e = u+110000\n", cfg));
		TS_ASSERT_EQUALS(cfg.glyphOverrides.size(), 0u);
	}

	void test_glyphs_key_may_not_use_u_plus_form() {
		// Common::INIFile only accepts alphanumerics, '-', '_', '.', ' ' and
		// ':' in a key, and rejects the whole file when it sees anything
		// else. A map is therefore unusable in its entirety if a translation
		// writes the left hand side as "u+5e", so the failure is worth
		// pinning down rather than discovering in a game.
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(!parse("[glyphs]\nu+5e = keep\n", cfg));
	}

	// --- SCI additions: [latin] mode/space, [fonts] names, [font.N] -------
	//
	// Everything below is optional and additive: a map that names none of it
	// parses exactly as it did before (test_existing_scumm_maps_unchanged).

	void test_latin_mode_and_space_parse() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(!cfg.latinModeSet);
		TS_ASSERT(!cfg.latinSpaceSet);
		TS_ASSERT(!cfg.latinMetricsSet);

		TS_ASSERT(parse("[latin]\nmode=fullwidth\nspace=fullwidth\nmetrics=font\n", cfg));
		TS_ASSERT(cfg.latinModeSet);
		TS_ASSERT_EQUALS(cfg.latinMode, Graphics::kHiResLatinFullwidth);
		TS_ASSERT(cfg.latinSpaceSet);
		TS_ASSERT(cfg.latinFullwidthSpace);
		TS_ASSERT(cfg.latinMetricsSet);
		TS_ASSERT_EQUALS(cfg.latinMetrics, Graphics::kHiResMetricsFont);

		const struct {
			const char *name;
			Graphics::HiResLatinMode mode;
		} modes[] = {
			{ "off", Graphics::kHiResLatinOff },
			{ "half", Graphics::kHiResLatinHalf },
			{ "fullwidth", Graphics::kHiResLatinFullwidth },
			{ "proportional", Graphics::kHiResLatinProportional }
		};
		for (uint i = 0; i < ARRAYSIZE(modes); ++i) {
			cfg.clear();
			const Common::String map = Common::String::format("[latin]\nmode=%s\nspace=keep\n", modes[i].name);
			TS_ASSERT(parse(map.c_str(), cfg));
			TS_ASSERT(cfg.latinModeSet);
			TS_ASSERT_EQUALS(cfg.latinMode, modes[i].mode);
			TS_ASSERT(cfg.latinSpaceSet);
			TS_ASSERT(!cfg.latinFullwidthSpace);
		}

		// The new keys are not the legacy switch: mode= does not turn on
		// SCUMM's [latin] companion font.
		TS_ASSERT(!cfg.legacy.latinEnabled);

		// A qualified [latin:<platform>] wins over [latin] for its qualifier.
		cfg.clear();
		TS_ASSERT(parse("[latin]\nmode=half\n[latin:pc98]\nmode=fullwidth\n", cfg, "pc98"));
		TS_ASSERT_EQUALS(cfg.latinMode, Graphics::kHiResLatinFullwidth);
		cfg.clear();
		TS_ASSERT(parse("[latin]\nmode=half\n[latin:pc98]\nmode=fullwidth\n", cfg, "dos"));
		TS_ASSERT_EQUALS(cfg.latinMode, Graphics::kHiResLatinHalf);
	}

	void test_hires_face_and_size_parse() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(!cfg.hiresFaceSet);
		TS_ASSERT(!cfg.hiresSizeSet);
		TS_ASSERT(parse("[hires]\nfont=default\nsize=18\n", cfg));
		TS_ASSERT(cfg.hiresFaceSet);
		TS_ASSERT_EQUALS(cfg.hiresFace, "default");
		TS_ASSERT(cfg.hiresSizeSet);
		TS_ASSERT_EQUALS(cfg.hiresSize, 18);
		// Neither touches SCUMM's geometry.
		TS_ASSERT_EQUALS(cfg.scale, 1);
		TS_ASSERT(!cfg.scaleFromMap);
	}

	void test_font_id_sections_parse() {
		const char *map =
			"[font.4]\n"
			"face=default\n"
			"size=16\n"
			"latin=proportional\n"
			"latin_font=latin\n"
			"latin_space=keep\n"
			"metrics=font\n"
			"\n"
			"[font.300]\n"
			"size=24\n";
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg));
		TS_ASSERT_EQUALS(cfg.fontIds.size(), 2u);

		const Graphics::HiResFontIdSettings *f4 = cfg.fontIdSettings(4);
		TS_ASSERT(f4 != nullptr);
		if (f4) {
			TS_ASSERT(f4->faceSet);
			TS_ASSERT_EQUALS(f4->face, "default");
			TS_ASSERT(f4->sizeSet);
			TS_ASSERT_EQUALS(f4->size, 16);
			TS_ASSERT(f4->latinSet);
			TS_ASSERT_EQUALS(f4->latin, Graphics::kHiResLatinProportional);
			TS_ASSERT(f4->latinFontSet);
			TS_ASSERT_EQUALS(f4->latinFont, "latin");
			TS_ASSERT(f4->latinSpaceSet);
			TS_ASSERT(!f4->latinFullwidthSpace);
			TS_ASSERT(f4->metricsSet);
			TS_ASSERT_EQUALS(f4->metrics, Graphics::kHiResMetricsFont);
		}

		// A section sets only what it names.
		const Graphics::HiResFontIdSettings *f300 = cfg.fontIdSettings(300);
		TS_ASSERT(f300 != nullptr);
		if (f300) {
			TS_ASSERT(f300->sizeSet);
			TS_ASSERT_EQUALS(f300->size, 24);
			TS_ASSERT(!f300->faceSet);
			TS_ASSERT(!f300->latinSet);
			TS_ASSERT(!f300->latinFontSet);
			TS_ASSERT(!f300->latinSpaceSet);
			TS_ASSERT(!f300->metricsSet);
		}

		// An id the map does not mention has no entry at all.
		TS_ASSERT(cfg.fontIdSettings(0) == nullptr);

		// latin_face= is the spelling the design doc uses for latin_font=.
		cfg.clear();
		TS_ASSERT(parse("[font.0]\nlatin_face=latin\n", cfg));
		TS_ASSERT(cfg.fontIdSettings(0) && cfg.fontIdSettings(0)->latinFontSet);
		TS_ASSERT(cfg.fontIdSettings(0) && cfg.fontIdSettings(0)->latinFont == "latin");

		// clear() forgets the table.
		cfg.clear();
		TS_ASSERT_EQUALS(cfg.fontIds.size(), 0u);
	}

	void test_font_id_qualified_section_wins_for_its_qualifier() {
		const char *map =
			"[font.0]\n"
			"latin=half\n"
			"size=14\n"
			"[font.0:pc98]\n"
			"latin=fullwidth\n"
			"[font.7:pc98]\n"
			"size=20\n";

		// On its own qualifier the qualified section wins, key by key: the
		// size it does not name still comes from the bare section.
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg, "pc98"));
		const Graphics::HiResFontIdSettings *f0 = cfg.fontIdSettings(0);
		TS_ASSERT(f0 != nullptr);
		if (f0) {
			TS_ASSERT_EQUALS(f0->latin, Graphics::kHiResLatinFullwidth);
			TS_ASSERT_EQUALS(f0->size, 14);
		}
		// A font id named only in a qualified section exists on that qualifier.
		TS_ASSERT(cfg.fontIdSettings(7) != nullptr);
		if (cfg.fontIdSettings(7))
			TS_ASSERT_EQUALS(cfg.fontIdSettings(7)->size, 20);

		// Any other qualifier sees the bare section only...
		cfg.clear();
		TS_ASSERT(parse(map, cfg, "dos"));
		f0 = cfg.fontIdSettings(0);
		TS_ASSERT(f0 != nullptr);
		if (f0) {
			TS_ASSERT_EQUALS(f0->latin, Graphics::kHiResLatinHalf);
			TS_ASSERT_EQUALS(f0->size, 14);
		}
		// ...and no entry for an id only another qualifier names.
		TS_ASSERT(cfg.fontIdSettings(7) == nullptr);

		// As does a caller with no qualifier at all.
		cfg.clear();
		TS_ASSERT(parse(map, cfg));
		TS_ASSERT(cfg.fontIdSettings(0) && cfg.fontIdSettings(0)->latin == Graphics::kHiResLatinHalf);
		TS_ASSERT(cfg.fontIdSettings(7) == nullptr);
	}

	void test_latin_font_accepts_face_name() {
		const char *map =
			"[fonts]\n"
			"default=NanumGothic.ttf\n"
			"latin=/fonts/AppleGothic.ttf\n"
			"[latin]\n"
			"font=latin\n";
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(map, cfg));

		// The raw value is kept, and a [fonts] name resolves to its file.
		TS_ASSERT(cfg.latinFontSet);
		TS_ASSERT_EQUALS(cfg.latinFont, "latin");
		TS_ASSERT_EQUALS(cfg.resolveFace(cfg.latinFont), "/fonts/AppleGothic.ttf");

		// Every [fonts] name is in the table, not only SCUMM's roles, and
		// the lookup ignores case like the rest of the map.
		TS_ASSERT_EQUALS(cfg.fontFaces.size(), 2u);
		TS_ASSERT_EQUALS(cfg.resolveFace("DEFAULT"), "NanumGothic.ttf");

		// Something that is not a face name is taken to be a path.
		TS_ASSERT_EQUALS(cfg.resolveFace("other/Face.ttf"), "other/Face.ttf");

		// SCUMM's role lookup is unchanged by the table.
		TS_ASSERT_EQUALS(cfg.ttfPath[Graphics::kHiResRoleDefault].toString('/'),
						 "/games/demo/NanumGothic.ttf");

		// A path still works in [latin] font=, as it always has.
		cfg.clear();
		TS_ASSERT(parse("[latin]\nfont=fonts/latin.ttf\n", cfg));
		TS_ASSERT_EQUALS(cfg.resolveFace(cfg.latinFont), "fonts/latin.ttf");
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfPath.toString('/'), "/games/demo/fonts/latin.ttf");
		TS_ASSERT(!cfg.legacy.latinEnabled);

		// face= is the design doc's spelling of the same key.
		cfg.clear();
		TS_ASSERT(parse("[latin]\nface=latin\n", cfg));
		TS_ASSERT(cfg.latinFontSet);
		TS_ASSERT_EQUALS(cfg.latinFont, "latin");

		// A qualified [fonts:<q>] entry wins over the bare one for its name.
		cfg.clear();
		TS_ASSERT(parse("[fonts]\nlatin=a.ttf\n[fonts:pc98]\nlatin=b.ttf\n", cfg, "pc98"));
		TS_ASSERT_EQUALS(cfg.resolveFace("latin"), "b.ttf");
	}

	void test_existing_scumm_maps_unchanged() {
		// The full example from engines/scumm/HIRES_TEXT_SETUP.md (hires-text).
		const char *full =
			"[hires]\n"
			"scale=2\n"
			"alpha=true\n"
			"\n"
			"[encoding]\n"
			"codepage=cp949\n"
			"\n"
			"[bitmap]\n"
			"multi=korean%02d.fnt\n"
			"single=korean.fnt\n"
			"glyphs=2350\n"
			"\n"
			"[latin]\n"
			"enabled=true\n"
			"bitmap=hrlat%02d.fnt\n"
			"metrics=font\n"
			"\n"
			"[render]\n"
			"metrics=font\n"
			"\n"
			"[shadow]\n"
			"mode=outline\n"
			"offset=2\n"
			"color=0\n"
			"\n"
			"[glyphs]\n"
			"0x07=keep\n"
			"0x5e=0x2026\n"
			"\n"
			"[translation]\n"
			"file=strings.txt\n";
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse(full, cfg, "monkey2", "v5"));
		TS_ASSERT_EQUALS(cfg.scale, 2);
		TS_ASSERT(cfg.scaleFromMap);
		TS_ASSERT(cfg.alpha);
		TS_ASSERT(cfg.alphaFromMap);
		TS_ASSERT_EQUALS(cfg.encoding, Common::kWindows949);
		TS_ASSERT(cfg.encodingFromMap);
		TS_ASSERT_EQUALS(cfg.bitmapPattern, "korean%02d.fnt");
		TS_ASSERT_EQUALS(cfg.bitmapSingle, "korean.fnt");
		TS_ASSERT_EQUALS(cfg.bitmapGlyphs, 2350);
		TS_ASSERT(cfg.legacy.latinEnabled);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapName, "hrlat%02d.fnt");
		TS_ASSERT(cfg.legacy.latinTtfPath.empty());
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfMetrics, Graphics::kHiResMetricsFont);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsFont);
		TS_ASSERT_EQUALS(cfg.metricsSource, Graphics::kHiResMetricsFont);
		TS_ASSERT_EQUALS(cfg.shadowMode, Graphics::kHiResShadowOutline);
		TS_ASSERT_EQUALS(cfg.shadowOffset, 2);
		TS_ASSERT_EQUALS((int)cfg.shadowColor, 0);
		TS_ASSERT(cfg.shadowColorSet);
		TS_ASSERT_EQUALS(cfg.translationName, "strings.txt");
		TS_ASSERT_EQUALS(cfg.glyphOverrides.size(), 2u);
		Graphics::HiResGlyphOverride o;
		TS_ASSERT(cfg.glyphOverride(0x07, o));
		TS_ASSERT_EQUALS(o.action, Graphics::kHiResGlyphKeep);
		TS_ASSERT(cfg.glyphOverride(0x5e, o));
		TS_ASSERT_EQUALS(o.action, Graphics::kHiResGlyphRemap);
		TS_ASSERT_EQUALS(o.codepoint, 0x2026u);
		for (int r = 0; r < Graphics::kHiResRoleCount; ++r) {
			TS_ASSERT(cfg.ttfPath[r].empty());
			TS_ASSERT_EQUALS(cfg.ttfSize[r], 0);
			TS_ASSERT_EQUALS(cfg.ttfSupersample[r], 1);
		}
		TS_ASSERT(!cfg.ttfStringMode);
		TS_ASSERT_EQUALS(cfg.heightRoles.size(), 0u);
		// Nothing SCI-only appears from a SCUMM map.
		TS_ASSERT(!cfg.latinModeSet);
		TS_ASSERT(!cfg.latinSpaceSet);
		TS_ASSERT(!cfg.hiresFaceSet);
		TS_ASSERT(!cfg.hiresSizeSet);
		TS_ASSERT_EQUALS(cfg.fontIds.size(), 0u);

		// The [fonts]/[sizes] example.
		cfg.clear();
		TS_ASSERT(parse("[fonts]\ndefault=/fonts/NanumGothic.ttf\nbold=/fonts/NanumGothic-Bold.ttf\n"
						"\n[sizes]\ndefault=16\n", cfg));
		TS_ASSERT_EQUALS(cfg.ttfPath[Graphics::kHiResRoleDefault].toString('/'), "/fonts/NanumGothic.ttf");
		TS_ASSERT_EQUALS(cfg.ttfPath[Graphics::kHiResRoleBold].toString('/'), "/fonts/NanumGothic-Bold.ttf");
		TS_ASSERT(cfg.ttfPath[Graphics::kHiResRoleTitle].empty());
		TS_ASSERT_EQUALS(cfg.ttfSize[Graphics::kHiResRoleDefault], 16);
		TS_ASSERT(!cfg.ttfSizeRelative[Graphics::kHiResRoleDefault]);
		TS_ASSERT_EQUALS(cfg.ttfSize[Graphics::kHiResRoleBold], 0);
		TS_ASSERT_EQUALS(cfg.fontIds.size(), 0u);

		// The per-game (qualified) section example, verbatim.
		const char *qualified =
			"[shadow]\n"
			"color=0            ; DOS\n"
			"\n"
			"[shadow:fmtowns]\n"
			"color=8            ; where 0 is transparent\n";
		cfg.clear();
		TS_ASSERT(parse(qualified, cfg, "fmtowns"));
		// INIFile keeps a same-line "; ..." in the value, so neither colour
		// parses as a number - the same result as before this parser grew.
		TS_ASSERT(!cfg.shadowColorSet);

		// The simple [latin] shape keeps meaning what it meant.
		cfg.clear();
		TS_ASSERT(parse("[latin]\nenabled=true\nfont=latin.ttf\nmetrics=game\n", cfg));
		TS_ASSERT(cfg.legacy.latinEnabled);
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfPath.toString('/'), "/games/demo/latin.ttf");
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfMetrics, Graphics::kHiResMetricsGame);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsGame);
		TS_ASSERT(!cfg.latinModeSet);
	}

	void test_bad_values_warn_and_default() {
		const char *map =
			"[hires]\n"
			"size=0\n"
			"[latin]\n"
			"mode=sideways\n"
			"space=narrow\n"
			"metrics=vibes\n"
			"[font.4]\n"
			"size=big\n"
			"latin=maybe\n"
			"latin_space=wide\n"
			"metrics=ttf\n"
			"face=default\n"
			"[font.x]\n"
			"size=16\n"
			"[font.99999999]\n"
			"size=16\n";
		Graphics::HiResTextConfig cfg;
		// One bad key never throws the map away.
		TS_ASSERT(parse(map, cfg));
		TS_ASSERT(!cfg.hiresSizeSet);
		TS_ASSERT(!cfg.latinModeSet);
		TS_ASSERT_EQUALS(cfg.latinMode, Graphics::kHiResLatinOff);
		TS_ASSERT(!cfg.latinSpaceSet);
		TS_ASSERT(!cfg.latinFullwidthSpace);
		TS_ASSERT(!cfg.latinMetricsSet);
		TS_ASSERT_EQUALS(cfg.latinMetrics, Graphics::kHiResMetricsGame);

		// The good key in a section with bad ones is still taken.
		const Graphics::HiResFontIdSettings *f4 = cfg.fontIdSettings(4);
		TS_ASSERT(f4 != nullptr);
		if (f4) {
			TS_ASSERT(f4->faceSet);
			TS_ASSERT(!f4->sizeSet);
			TS_ASSERT(!f4->latinSet);
			TS_ASSERT(!f4->latinSpaceSet);
			TS_ASSERT(!f4->metricsSet);
		}

		// Section names that are not a font id are skipped, not guessed at.
		TS_ASSERT_EQUALS(cfg.fontIds.size(), 1u);

		// The legacy [latin] metrics reader is unaffected by the bad value.
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfMetrics, Graphics::kHiResMetricsGame);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsGame);
	}

	void test_legacy_enabled_literal_is_recorded() {
		// bitmap= still implies latinEnabled (SCUMM), but only an explicit
		// enabled= sets the literal fields an engine without bitmaps reads.
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[latin]\nbitmap=latin24.fnt\n", cfg));
		TS_ASSERT(cfg.legacy.latinEnabled);
		TS_ASSERT(!cfg.legacy.latinEnabledSet);

		cfg.clear();
		TS_ASSERT(parse("[latin]\nenabled=false\nbitmap=latin24.fnt\n", cfg));
		TS_ASSERT(cfg.legacy.latinEnabled);
		TS_ASSERT(cfg.legacy.latinEnabledSet);
		TS_ASSERT(!cfg.legacy.latinEnabledValue);

		cfg.clear();
		TS_ASSERT(parse("[latin]\nenabled=true\n", cfg));
		TS_ASSERT(cfg.legacy.latinEnabledSet);
		TS_ASSERT(cfg.legacy.latinEnabledValue);

		cfg.clear();
		TS_ASSERT(!cfg.legacy.latinEnabledSet);
		TS_ASSERT(!cfg.legacy.latinEnabledValue);
	}

	void test_latin_metrics_ttf_is_font() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[latin]\nmetrics=ttf\n", cfg));
		TS_ASSERT(cfg.latinMetricsSet);
		TS_ASSERT_EQUALS(cfg.latinMetrics, Graphics::kHiResMetricsFont);
		// The legacy reading is unchanged.
		TS_ASSERT_EQUALS(cfg.legacy.latinTtfMetrics, Graphics::kHiResMetricsFont);
		TS_ASSERT_EQUALS(cfg.legacy.latinBitmapMetrics, Graphics::kHiResMetricsGame);

		// bitmap is a legacy-only spelling, not a new-reader value.
		cfg.clear();
		TS_ASSERT(parse("[latin]\nmetrics=bitmap\n", cfg));
		TS_ASSERT(!cfg.latinMetricsSet);
	}

	void test_face_and_font_are_aliases() {
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[hires]\nface=main\n[font.4]\nfont=narrow\n", cfg));
		TS_ASSERT(cfg.hiresFaceSet);
		TS_ASSERT_EQUALS(cfg.hiresFace, "main");
		const Graphics::HiResFontIdSettings *f4 = cfg.fontIdSettings(4);
		TS_ASSERT(f4 && f4->faceSet);
		TS_ASSERT(f4 && f4->face == "narrow");

		// The canonical spelling wins within one section.
		cfg.clear();
		TS_ASSERT(parse("[hires]\nface=b\nfont=a\n[font.4]\nfont=d\nface=c\n", cfg));
		TS_ASSERT_EQUALS(cfg.hiresFace, "a");
		TS_ASSERT(cfg.fontIdSettings(4) && cfg.fontIdSettings(4)->face == "c");

		// A qualified section still wins whichever spelling either uses.
		cfg.clear();
		TS_ASSERT(parse("[font.4]\nface=bare\n[font.4:pc98]\nfont=qualified\n", cfg, "pc98"));
		TS_ASSERT(cfg.fontIdSettings(4) && cfg.fontIdSettings(4)->face == "qualified");
	}

	void test_non_canonical_font_id_sections_are_skipped() {
		// [font.04] and [font.4:] would read back from [font.4] and come out
		// empty; they are skipped with a warning instead.
		Graphics::HiResTextConfig cfg;
		TS_ASSERT(parse("[font.04]\nsize=20\n[font.5:]\nsize=21\n[font.6]\nsize=22\n", cfg));
		TS_ASSERT(cfg.fontIdSettings(4) == nullptr);
		TS_ASSERT(cfg.fontIdSettings(5) == nullptr);
		TS_ASSERT(cfg.fontIdSettings(6) && cfg.fontIdSettings(6)->size == 22);
		TS_ASSERT_EQUALS(cfg.fontIds.size(), 1u);

		// Case does not matter: section names are case-insensitive.
		cfg.clear();
		TS_ASSERT(parse("[FONT.7]\nsize=23\n", cfg));
		TS_ASSERT(cfg.fontIdSettings(7) && cfg.fontIdSettings(7)->size == 23);
	}
};
