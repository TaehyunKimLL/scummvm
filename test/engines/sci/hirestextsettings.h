/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/hashmap.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"
#include "sci/graphics/hirestextsettings.h"
#include "sci/graphics/textlatin.h"

using Sci::FontSettings;

/**
 * resolveFontSettings(): hires_text.map (design section 3, version 2) plus
 * the ini keys, compiled into one plan per SCI font id
 * (Graphics::compileIdPlan(), against Sci::sciEngineScope()).
 */
class SciHiresTextSettingsTestSuite : public CxxTest::TestSuite {
private:
	static const Common::Path &mapDir() {
		static const Common::Path dir("/maps");
		return dir;
	}
	static const Common::Path &gameDir() {
		static const Common::Path dir("/games/kq1");
		return dir;
	}

	/// Parse a map as GfxCache would: `[map] version=2` is prepended so
	/// every test map string can stay to the point.
	static Graphics::HiResMap parseMap(const char *text, const Common::Array<Common::String> &qualifiers = Common::Array<Common::String>()) {
		Common::String full = "[map]\nversion=2\n";
		full += text;
		Common::MemoryReadStream stream((const byte *)full.c_str(), full.size());
		Graphics::HiResMap map;
		const bool ok = Graphics::HiResFontMap::loadMap(stream, mapDir(), qualifiers, Graphics::kHiResKeysSci, map);
		TS_ASSERT(ok);
		return map;
	}

	static FontSettings settings(const char *text, int fontId) {
		const Graphics::HiResMap map = parseMap(text);
		return Sci::resolveFontSettings(map, true, fontId, Graphics::HiResIniOverrides(), mapDir(), gameDir());
	}

	static FontSettings settingsWithIni(const char *text, int fontId, const Graphics::HiResIniOverrides &ini) {
		const Graphics::HiResMap map = parseMap(text);
		return Sci::resolveFontSettings(map, true, fontId, ini, mapDir(), gameDir());
	}

	/** A minimal fake face for checkPlanLoadWarnings(): one glyph, at @p cp. */
	class FakeFace : public Graphics::UnicodeGlyphSource {
	public:
		FakeFace(int cellW, int cellH, uint32 glyphCp)
			: _w(cellW), _h(cellH), _cp(glyphCp) {
			memset(_row, 0, sizeof(_row));
		}
		byte cellWidth() const override { return (byte)_w; }
		byte cellHeight() const override { return (byte)_h; }
		byte advanceNarrow() const override { return (byte)_w; }
		byte advanceWide() const override { return (byte)(_w * 2); }
		int bitsPerPixel() const override { return 1; }
		int cells(uint32 cp) override { return cp == _cp ? 1 : 0; }
		const byte *row(uint32 cp, int y) override { return _row; }
		uint32 glyphCount() const override { return 1; }
	private:
		int _w, _h;
		uint32 _cp;
		byte _row[4];
	};

public:
	// ---- Step 1's given tests (spec 6.2, 6.5, 6.7, 5, 8) -----------------

	void test_default_latin_is_the_resource_font() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n", 4);
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 0xAC00));
	}

	void test_range_same_routes_ascii_to_the_face() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nadvance.basic-latin=font\n", 4);
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		TS_ASSERT_EQUALS(s.plan.advanceFor('A'), Graphics::kHiResAdvanceFont);
	}

	void test_per_id_advance_game() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nadvance.basic-latin=font\n"
									   "[font.8]\nadvance.basic-latin=game\n", 8);
		TS_ASSERT_EQUALS(s.plan.advanceFor(' '), Graphics::kHiResAdvanceGame);
	}

	void test_fullwidth_recipe_via_glyphs() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n0x20=u+3000\n", 0);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 'A'), 0xFF21u);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, ' '), 0x3000u);
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 0xFF21));
	}

	void test_glyphs_original_is_the_resource_font() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\n[glyphs]\n0x40=original\n", 0);
		const uint32 code = Sci::TextCompose::glyphCode(s.plan, '@');
		TS_ASSERT_EQUALS(code, Graphics::kHiResGameCodeBase + '@');
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, code));
	}

	void test_targeted_glyph_code() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[glyphs]\n0x2605=ICONS.SVF:u+e001\n", 0);
		const uint32 code = Sci::TextCompose::glyphCode(s.plan, 0x2605);
		TS_ASSERT(code >= Graphics::kHiResTargetBase && code < Graphics::kHiResGameCodeBase);
		TS_ASSERT_EQUALS(s.plan.target(code)->cp, 0xE001u);
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, code));
	}

	void test_face_original_keeps_the_resource_font_whole() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[font.2]\nface=original\n", 2);
		TS_ASSERT(s.original);
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, 0xAC00));
	}

	void test_ini_paths_are_game_folder_relative() {
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "fonts/X.TTF";
		Sci::FontSettings s = settingsWithIni("[font]\nface=KO.SVF\n", 0, ini);
		TS_ASSERT_EQUALS(s.facePath, Common::Path("/games/kq1/fonts/X.TTF", '/').toString(Common::Path::kNativeSeparator));
	}

	void test_shift_cell_align_size() {
		Sci::FontSettings s = settings("[font]\nface=KO.SVF\nshift=-1\nalign=cell\n[font.40]\ncell=glyph\nsize=18\n", 40);
		TS_ASSERT_EQUALS(s.baseline, -1);
		TS_ASSERT_EQUALS(s.align, Graphics::kHiResAlignCell);
		TS_ASSERT_EQUALS(s.cell, 18);
		TS_ASSERT_EQUALS(s.size, 18);
	}

	// ---- the load-time checks (spec 5.4, 6.4, 6.7), pure and testable
	// against a handful of fake faces, without GfxCache/ResourceManager ----

	void test_cell_height_mismatch_is_refused_and_warned() {
		Sci::FontSettings s = settings("[font]\nface=A.SVF,B.SVF\nrange.basic-latin=same\n", 0);
		FakeFace a(16, 16, 'A');
		FakeFace b(16, 18, 'A');
		Common::Array<Sci::FontIdFace> faces;
		faces.push_back({ "/maps/A.SVF", &a, true });
		faces.push_back({ "/maps/B.SVF", &b, true });
		Common::Array<Common::String> excluded;
		Graphics::HiResMap map;
		Common::HashMap<Common::String, bool> warnedOnce;
		Sci::checkPlanLoadWarnings(0, s.plan, faces, excluded, map, warnedOnce);

		TS_ASSERT_EQUALS(excluded.size(), 1u);
		TS_ASSERT_EQUALS(excluded[0], Common::String("/maps/B.SVF"));
		TS_ASSERT_EQUALS(map.warnings.size(), 1u);
		TS_ASSERT_EQUALS(map.warnings[0],
			Common::String("HIRESTXT.MAP: /maps/B.SVF: cell height 18 differs from /maps/A.SVF's 16 on 0; not used"));
	}

	void test_target_lacking_its_glyph_is_warned() {
		Sci::FontSettings s = settings("[font]\nface=A.SVF\n[glyphs]\n0x2605=ICONS.SVF:u+e001\n", 0);
		FakeFace a(16, 16, 'A');
		FakeFace icons(16, 16, 0xE002); // lacks U+E001
		Common::Array<Sci::FontIdFace> faces;
		faces.push_back({ "/maps/A.SVF", &a, true });
		faces.push_back({ "/maps/ICONS.SVF", &icons, true });
		Common::Array<Common::String> excluded;
		Graphics::HiResMap map;
		Common::HashMap<Common::String, bool> warnedOnce;
		Sci::checkPlanLoadWarnings(0, s.plan, faces, excluded, map, warnedOnce);

		TS_ASSERT_EQUALS(map.warnings.size(), 1u);
		TS_ASSERT_EQUALS(map.warnings[0],
			Common::String("HIRESTXT.MAP: [glyphs] 0x2605 -> ICONS.SVF:U+E001: the face has no such glyph; the game's font draws it"));
	}

	void test_missing_without_a_glyph_is_warned() {
		Sci::FontSettings s = settings("[font]\nface=A.SVF\nmissing=u+25a1\n", 0);
		FakeFace a(16, 16, 'A'); // lacks U+25A1
		Common::Array<Sci::FontIdFace> faces;
		faces.push_back({ "/maps/A.SVF", &a, true });
		Common::Array<Common::String> excluded;
		Graphics::HiResMap map;
		Common::HashMap<Common::String, bool> warnedOnce;
		Sci::checkPlanLoadWarnings(0, s.plan, faces, excluded, map, warnedOnce);

		TS_ASSERT_EQUALS(map.warnings.size(), 1u);
		TS_ASSERT_EQUALS(map.warnings[0],
			Common::String("HIRESTXT.MAP: missing=U+25A1 has no effect: A.SVF has no glyph for it"));
	}

	// ---- ported coverage from the version-1 suite -------------------------

	// (was test_empty_map_defaults) An id with no map at all - or a loaded
	// but empty one - gets the built-in defaults: no replacement face, the
	// 16 px cell, ASCII on the resource font.
	void test_no_map_gives_the_defaults() {
		const Graphics::HiResMap empty;
		const bool loaded[] = { false, true };
		for (uint i = 0; i < ARRAYSIZE(loaded); ++i) {
			const FontSettings s = Sci::resolveFontSettings(empty, loaded[i], 0, Graphics::HiResIniOverrides(), mapDir(), gameDir());
			TS_ASSERT(s.facePath.empty());
			TS_ASSERT_EQUALS(s.size, 16);
			TS_ASSERT_EQUALS(s.cell, 16);
			TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		}
	}

	// (was test_platform_section_wins_on_its_platform_only) The generic
	// qualifier merge (design 3.4, 6.2.1) is the shared map/plan layer's own
	// coverage; this only pins that GfxCache's one qualifier (the platform
	// code) actually reaches compileIdPlan() through resolveFontSettings()'s
	// map argument.
	void test_platform_qualifier_still_applies() {
		Common::Array<Common::String> pc98;
		pc98.push_back("pc98");
		const Graphics::HiResMap map = parseMap("[font]\nface=KO.SVF\n[font:pc98]\nface=PC98.SVF\n", pc98);
		const FontSettings s = Sci::resolveFontSettings(map, true, 0, Graphics::HiResIniOverrides(), mapDir(), gameDir());
		TS_ASSERT_EQUALS(s.facePath, Common::Path("/maps/PC98.SVF", '/').toString(Common::Path::kNativeSeparator));
	}

	// (was test_map_face_path_round_trips_with_the_native_separator) GfxCache
	// keys its opened sources by this string, so a map face and the same
	// file named by the ini must produce the identical string.
	void test_map_face_path_round_trips_with_the_native_separator() {
		const Graphics::HiResMap map = parseMap("[fonts]\ndefault=fonts/NanumGothic.ttf\n[font]\nface=default\n");
		const FontSettings s = Sci::resolveFontSettings(map, true, 0, Graphics::HiResIniOverrides(), mapDir(), gameDir());

		const Common::Path expected = mapDir().join("fonts").join("NanumGothic.ttf");
		TS_ASSERT_EQUALS(Common::Path(s.facePath, Common::Path::kNativeSeparator), expected);

		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = expected.toString(Common::Path::kNativeSeparator);
		const Graphics::HiResMap none;
		TS_ASSERT_EQUALS(Sci::resolveFontSettings(none, false, 0, ini, mapDir(), gameDir()).facePath, s.facePath);
	}

	// (was test_ini_overrides_map) hires_text_face/_size beat every [font.N]
	// and [font] level, as design 5.3/6.2's ini-first precedence says.
	void test_ini_overrides_map() {
		const Graphics::HiResMap map = parseMap("[font]\nface=Map.SVF\nsize=20\n[font.4]\nface=Four.SVF\nsize=12\n");
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "Ini.ttf";
		ini.sizeSet = true;
		ini.size = 18;
		const FontSettings s = Sci::resolveFontSettings(map, true, 4, ini, mapDir(), gameDir());
		// design section 4: a relative ini hires_text_face now resolves
		// against the game folder, not "as the player typed it".
		TS_ASSERT_EQUALS(s.facePath, Common::Path("/games/kq1/Ini.ttf", '/').toString(Common::Path::kNativeSeparator));
		TS_ASSERT_EQUALS(s.size, 18);
	}

	// (was test_bundle_key_distinguishes_latin_modes, now over the plan hash
	// - Graphics::HiResIdPlan::hash() is what actually varies the routing).
	void test_bundle_key_distinguishes_plans() {
		const Sci::FontSettings same = settings("[font]\nface=KO.SVF\n", 0);
		const Sci::FontSettings other = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\n", 0);
		TS_ASSERT_DIFFERS(Sci::unicodeBundleKey("/f/Main.ttf", 16, same.plan.hash()),
						  Sci::unicodeBundleKey("/f/Main.ttf", 16, other.plan.hash()));
		TS_ASSERT_DIFFERS(Sci::unicodeBundleKey("/f/Main.ttf", 16, same.plan.hash()),
						  Sci::unicodeBundleKey("/f/Main.ttf", 18, same.plan.hash()));
		TS_ASSERT_EQUALS(Sci::unicodeBundleKey("/f/Main.ttf", 16, same.plan.hash()),
						 Sci::unicodeBundleKey("/f/Main.ttf", 16, same.plan.hash()));
	}
};
