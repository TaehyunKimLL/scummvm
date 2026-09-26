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
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "sci/graphics/hirestextsettings.h"

using Sci::FontSettings;
using Sci::HiresTextOverrides;
using Sci::resolveFontSettings;

/**
 * resolveFontSettings(): hires_text.map plus the ini keys, resolved per SCI
 * font id. Precedence, per setting:
 *   ini > [font.N:<platform>] > [font.N] > [latin:<platform>]/[latin]
 *   (or [hires] for face and size) > the built-in default.
 */
class SciHiresTextSettingsTestSuite : public CxxTest::TestSuite {
private:
	/// Parse a map as GfxCache would: the platform code is the qualifier.
	static Graphics::HiResTextConfig parse(const char *text, const char *platform = nullptr) {
		Common::Array<Common::String> qualifiers;
		if (platform)
			qualifiers.push_back(platform);
		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		Graphics::HiResTextConfig cfg;
		const bool ok = Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/games/kq5"),
															   qualifiers, cfg);
		TS_ASSERT(ok);
		return cfg;
	}

	/// The directory holding the map; for a game's own hires_text.map it
	/// is the game directory.
	static const Common::Path &mapDir() {
		static const Common::Path dir("/games/kq5");
		return dir;
	}

public:
	void test_empty_map_defaults() {
		const HiresTextOverrides noIni;
		const Graphics::HiResTextConfig empty;

		// No map at all, and a loaded but empty one, give the same defaults.
		const bool loaded[] = { false, true };
		for (uint i = 0; i < ARRAYSIZE(loaded); ++i) {
			const FontSettings s = resolveFontSettings(empty, loaded[i], 0, noIni, mapDir());
			TS_ASSERT(s.facePath.empty());
			TS_ASSERT_EQUALS(s.size, 16);
			TS_ASSERT_EQUALS(s.latin, Sci::kLatinOff);
			TS_ASSERT(s.latinFacePath.empty());
			TS_ASSERT(!s.fullwidthSpace);
			TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsGame);
		}

		// A map that was not loaded is ignored even if it holds settings.
		const Graphics::HiResTextConfig full = parse(
			"[hires]\nfont=/f/a.ttf\nsize=20\n[latin]\nmode=half\n[font.0]\nsize=12\n");
		const FontSettings s = resolveFontSettings(full, false, 0, noIni, mapDir());
		TS_ASSERT(s.facePath.empty());
		TS_ASSERT_EQUALS(s.size, 16);
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinOff);
	}

	void test_font_section_overrides_latin_section() {
		const Graphics::HiResTextConfig map = parse(
			"[hires]\n"
			"font=default\n"
			"size=16\n"
			"[fonts]\n"
			"default=/fonts/Nanum.ttf\n"
			"title=/fonts/Title.ttf\n"
			"latin=/fonts/Latin.ttf\n"
			"[latin]\n"
			"mode=fullwidth\n"
			"space=fullwidth\n"
			"font=latin\n"
			"metrics=game\n"
			"[font.4]\n"
			"latin=proportional\n"
			"latin_space=keep\n"
			"metrics=font\n"
			"[font.300]\n"
			"face=title\n"
			"size=24\n"
			"latin_font=/fonts/Other.ttf\n");
		const HiresTextOverrides noIni;

		// Font 4 takes its own section's Latin settings over [latin]'s...
		FontSettings s = resolveFontSettings(map, true, 4, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinProportional);
		TS_ASSERT(!s.fullwidthSpace);
		TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsFont);
		// ...and [latin]/[hires] for what it does not name.
		TS_ASSERT_EQUALS(s.latinFacePath, "/fonts/Latin.ttf");
		TS_ASSERT_EQUALS(s.facePath, "/fonts/Nanum.ttf");
		TS_ASSERT_EQUALS(s.size, 16);

		// Font 300 overrides face and size over [hires], the Latin face over [latin].
		s = resolveFontSettings(map, true, 300, noIni, mapDir());
		TS_ASSERT_EQUALS(s.facePath, "/fonts/Title.ttf");
		TS_ASSERT_EQUALS(s.size, 24);
		TS_ASSERT_EQUALS(s.latinFacePath, "/fonts/Other.ttf");
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinFullwidth);
		TS_ASSERT(s.fullwidthSpace);
		TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsGame);

		// A font id with no section gets [latin] and [hires] as they stand.
		s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.facePath, "/fonts/Nanum.ttf");
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinFullwidth);
		TS_ASSERT(s.fullwidthSpace);
		TS_ASSERT_EQUALS(s.latinFacePath, "/fonts/Latin.ttf");
	}

	void test_platform_section_wins_on_its_platform_only() {
		const char *text =
			"[latin]\n"
			"mode=half\n"
			"[latin:pc98]\n"
			"mode=proportional\n"
			"[font.0]\n"
			"size=14\n"
			"[font.0:pc98]\n"
			"latin=fullwidth\n"
			"size=18\n";
		const HiresTextOverrides noIni;

		// On PC-98: [font.0:pc98] beats [font.0], [latin:pc98] beats [latin].
		const Graphics::HiResTextConfig pc98 = parse(text, "pc98");
		FontSettings s = resolveFontSettings(pc98, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinFullwidth);
		TS_ASSERT_EQUALS(s.size, 18);
		s = resolveFontSettings(pc98, true, 4, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinProportional);

		// On DOS neither qualified section applies.
		const Graphics::HiResTextConfig dos = parse(text, "dos");
		s = resolveFontSettings(dos, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinHalf);
		TS_ASSERT_EQUALS(s.size, 14);
		s = resolveFontSettings(dos, true, 4, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinHalf);
		TS_ASSERT_EQUALS(s.size, 16);
	}

	void test_ini_overrides_map() {
		const Graphics::HiResTextConfig map = parse(
			"[hires]\n"
			"font=/fonts/Map.ttf\n"
			"size=20\n"
			"[latin]\n"
			"mode=half\n"
			"font=/fonts/MapLatin.ttf\n"
			"[font.4]\n"
			"face=/fonts/Four.ttf\n"
			"size=12\n"
			"latin=proportional\n"
			"latin_font=/fonts/FourLatin.ttf\n"
			"latin_space=keep\n"
			"metrics=font\n");

		HiresTextOverrides ini;
		ini.hasFont = true;
		ini.font = "/fonts/Ini.ttf";
		ini.hasFontSize = true;
		ini.fontSize = 18;
		ini.hasLatin = true;
		ini.latin = Sci::kLatinFullwidth;
		ini.hasLatinFont = true;
		ini.latinFont = "/fonts/IniLatin.ttf";
		ini.hasLatinSpace = true;
		ini.latinFullwidthSpace = true;
		ini.hasMetrics = true;
		ini.metrics = Graphics::kHiResMetricsGame;

		// Every ini key beats even the font's own section.
		const int ids[] = { 0, 4 };
		for (uint i = 0; i < ARRAYSIZE(ids); ++i) {
			const FontSettings s = resolveFontSettings(map, true, ids[i], ini, mapDir());
			TS_ASSERT_EQUALS(s.facePath, "/fonts/Ini.ttf");
			TS_ASSERT_EQUALS(s.size, 18);
			TS_ASSERT_EQUALS(s.latin, Sci::kLatinFullwidth);
			TS_ASSERT_EQUALS(s.latinFacePath, "/fonts/IniLatin.ttf");
			TS_ASSERT(s.fullwidthSpace);
			TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsGame);
		}

		// One key at a time: the ini sets only the size, the map the rest.
		HiresTextOverrides sizeOnly;
		sizeOnly.hasFontSize = true;
		sizeOnly.fontSize = 22;
		const FontSettings s = resolveFontSettings(map, true, 4, sizeOnly, mapDir());
		TS_ASSERT_EQUALS(s.size, 22);
		TS_ASSERT_EQUALS(s.facePath, "/fonts/Four.ttf");
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinProportional);
		TS_ASSERT_EQUALS(s.latinFacePath, "/fonts/FourLatin.ttf");
		TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsFont);

		// With no map the ini keys alone give today's settings, their
		// paths unchanged (GfxCache opens them as it always has).
		HiresTextOverrides relative;
		relative.hasFont = true;
		relative.font = "Nanum.ttf";
		const Graphics::HiResTextConfig none;
		const FontSettings t = resolveFontSettings(none, false, 0, relative, mapDir());
		TS_ASSERT_EQUALS(t.facePath, "Nanum.ttf");
		TS_ASSERT_EQUALS(t.size, 16);
	}

	void test_enabled_true_means_proportional_game() {
		const HiresTextOverrides noIni;

		// SCUMM's legacy [latin] enabled=true: "the engine's current Latin
		// behaviour", which for SCI is proportional with the game's advances.
		Graphics::HiResTextConfig map = parse("[latin]\nenabled=true\n");
		FontSettings s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinProportional);
		TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsGame);

		// An explicit mode= says more than the alias and wins.
		map = parse("[latin]\nenabled=true\nmode=half\n");
		s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinHalf);

		// So does the font's own section.
		map = parse("[latin]\nenabled=true\n[font.4]\nlatin=off\n");
		s = resolveFontSettings(map, true, 4, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinOff);

		// And enabled=false is plain off.
		map = parse("[latin]\nenabled=false\n");
		s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinOff);
	}

	void test_relative_face_path_joins_the_map_dir() {
		const Graphics::HiResTextConfig map = parse(
			"[hires]\n"
			"font=default\n"
			"[fonts]\n"
			"default=NanumGothic.ttf\n"
			"latin=/System/Library/Fonts/Supplemental/AppleGothic.ttf\n"
			"[latin]\n"
			"font=latin\n"
			"[font.4]\n"
			"face=fonts/Narrow.ttf\n"
			"latin_font=fonts/NarrowLatin.ttf\n");
		const HiresTextOverrides noIni;

		// A [fonts] name whose file is relative lands in the map's directory;
		// an absolute one is used as it stands.
		FontSettings s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.facePath, "/games/kq5/NanumGothic.ttf");
		TS_ASSERT_EQUALS(s.latinFacePath, "/System/Library/Fonts/Supplemental/AppleGothic.ttf");

		// A path written in place of a face name is resolved the same way.
		s = resolveFontSettings(map, true, 4, noIni, mapDir());
		TS_ASSERT_EQUALS(s.facePath, "/games/kq5/fonts/Narrow.ttf");
		TS_ASSERT_EQUALS(s.latinFacePath, "/games/kq5/fonts/NarrowLatin.ttf");
	}

	// Relative paths from the map resolve against the map file's directory,
	// not the game's: a map kept outside the game directory (ini
	// hires_text_map=) carries its fonts beside it. Ini paths stay as given.
	void test_relative_map_paths_follow_the_map_dir() {
		const Graphics::HiResTextConfig map = parse(
			"[hires]\n"
			"font=default\n"
			"[fonts]\n"
			"default=NanumGothic.ttf\n"
			"[font.4]\n"
			"face=fonts/Narrow.ttf\n"
			"latin_font=../shared/Latin.ttf\n");
		const HiresTextOverrides noIni;
		const Common::Path elsewhere("/maps/kq5-ko");

		FontSettings s = resolveFontSettings(map, true, 0, noIni, elsewhere);
		TS_ASSERT_EQUALS(s.facePath, "/maps/kq5-ko/NanumGothic.ttf");
		s = resolveFontSettings(map, true, 4, noIni, elsewhere);
		TS_ASSERT_EQUALS(s.facePath, "/maps/kq5-ko/fonts/Narrow.ttf");
		TS_ASSERT_EQUALS(s.latinFacePath, "/maps/kq5-ko/../shared/Latin.ttf");

		// The ini keys are not map paths: they are left as the player typed them.
		HiresTextOverrides ini;
		ini.hasFont = true;
		ini.font = "Nanum.ttf";
		ini.hasLatinFont = true;
		ini.latinFont = "Latin.ttf";
		s = resolveFontSettings(map, true, 4, ini, elsewhere);
		TS_ASSERT_EQUALS(s.facePath, "Nanum.ttf");
		TS_ASSERT_EQUALS(s.latinFacePath, "Latin.ttf");
	}

	// GfxCache parses every face path with the native separator and keys its
	// TrueType sources by the string: a map face must round-trip exactly, and
	// equal the same file named in the ini, or one file opens twice.
	void test_map_face_path_round_trips_with_the_native_separator() {
		const Graphics::HiResTextConfig map = parse(
			"[fonts]\n"
			"default=fonts/NanumGothic.ttf\n"
			"[hires]\n"
			"font=default\n");
		const HiresTextOverrides noIni;
		const FontSettings s = resolveFontSettings(map, true, 0, noIni, mapDir());

		const Common::Path expected = mapDir().join("fonts").join("NanumGothic.ttf");
		TS_ASSERT_EQUALS(Common::Path(s.facePath, Common::Path::kNativeSeparator), expected);
		TS_ASSERT_EQUALS(s.facePath, expected.toString(Common::Path::kNativeSeparator));

		// The same file given as hires_text_font (a native path, as a player
		// types it) yields the same string, i.e. the same source-cache key.
		HiresTextOverrides ini;
		ini.hasFont = true;
		ini.font = expected.toString(Common::Path::kNativeSeparator);
		const Graphics::HiResTextConfig empty;
		TS_ASSERT_EQUALS(resolveFontSettings(empty, false, 0, ini, mapDir()).facePath, s.facePath);
	}

	void test_bundle_key_distinguishes_latin_modes() {
		const Common::String main = "/f/Main.ttf";
		const Common::String latin = "/f/Latin.ttf";

		// With a Latin face, each mode gets its own router.
		const Common::String half = Sci::unicodeBundleKey(main, 16, latin, Sci::kLatinHalf);
		const Common::String prop = Sci::unicodeBundleKey(main, 16, latin, Sci::kLatinProportional);
		const Common::String full = Sci::unicodeBundleKey(main, 16, latin, Sci::kLatinFullwidth);
		TS_ASSERT_DIFFERS(half, prop);
		TS_ASSERT_DIFFERS(half, full);
		TS_ASSERT_DIFFERS(prop, full);

		// Face and size still count.
		TS_ASSERT_DIFFERS(half, Sci::unicodeBundleKey(main, 18, latin, Sci::kLatinHalf));
		TS_ASSERT_DIFFERS(half, Sci::unicodeBundleKey(main, 16, "/f/Other.ttf", Sci::kLatinHalf));

		// Without one there is no router: the main face alone, whatever the mode.
		TS_ASSERT_EQUALS(Sci::unicodeBundleKey(main, 16, "", Sci::kLatinOff),
						 Sci::unicodeBundleKey(main, 16, "", Sci::kLatinProportional));
		TS_ASSERT_DIFFERS(Sci::unicodeBundleKey(main, 16, "", Sci::kLatinOff), half);
	}

	void test_bitmap_is_scumm_only() {
		const HiresTextOverrides noIni;

		// SCI has no bitmap Latin path: bitmap= alone is off (SCUMM reads it
		// as enabled=true; SCI does not), and gets one warning.
		Graphics::HiResTextConfig map = parse("[latin]\nbitmap=latin24.fnt\n");
		TS_ASSERT(map.legacy.latinEnabled); // SCUMM's reading is unchanged
		FontSettings s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinOff);
		TS_ASSERT_EQUALS(Sci::warnScummOnlyMapKeys(map), 1);

		// enabled=false with bitmap= is off too.
		map = parse("[latin]\nenabled=false\nbitmap=latin24.fnt\n");
		s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinOff);

		// Only an explicit enabled=true is the legacy alias.
		map = parse("[latin]\nenabled=true\nbitmap=latin24.fnt\n");
		s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.latin, Sci::kLatinProportional);
		TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsGame);

		// No bitmap=, no warning.
		map = parse("[latin]\nenabled=true\n");
		TS_ASSERT_EQUALS(Sci::warnScummOnlyMapKeys(map), 0);
	}

	void test_latin_metrics_ttf_means_font() {
		const HiresTextOverrides noIni;
		const Graphics::HiResTextConfig map = parse("[latin]\nmode=proportional\nmetrics=ttf\n");
		const FontSettings s = resolveFontSettings(map, true, 0, noIni, mapDir());
		TS_ASSERT_EQUALS(s.metrics, Graphics::kHiResMetricsFont);
	}
};
