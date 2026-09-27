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

#include "common/fs.h"
#include "common/memstream.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../../system/null_osystem.h"
#include "graphics/hires_text/font_map.h"

#include "ags/shared/font/hires_font_config.h"

namespace {

Graphics::HiResTextConfig agsParseMap(const char *text, const char *qualifier) {
	Common::MemoryReadStream in((const byte *)text, strlen(text));
	Common::Array<Common::String> q;
	q.push_back(qualifier);
	Graphics::HiResTextConfig map;
	Graphics::HiResFontMap::loadFromStream(in, Common::Path("/maps"), q, map);
	return map;
}

Common::String agsFaces(const AGS3::HiResFontPlan &p) {
	Common::String s;
	for (uint i = 0; i < p.faces.size(); i++)
		s += (i ? "," : "") + p.faces[i].toString();
	return s;
}

const char *const kAgsPlanMap =
	"[hires]\n"
	"face=hf\n"
	"size=14\n"
	"[fonts]\n"
	"hf=/f/hires.ttf\n"
	"a=/f/a.ttf\n"
	"b=/f/b.ttf\n"
	"[font.0]\n"
	"bitmap=/f/zero.fnt\n"
	"face=a\n"
	"[font.1]\n"
	"face=a, b\n"
	"size=20\n"
	"[font.2:5daysastranger]\n"
	"face=b\n";

} // End of anonymous namespace

/** Which font an AGS font number gets from hires_text.map and the ini. */
class AgsHiResFontPlanTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_precedence_bitmap_face_ini_hires() {
		const Graphics::HiResTextConfig map = agsParseMap(kAgsPlanMap, "5daysastranger");
		Common::Array<Common::Path> ini;
		ini.push_back(Common::Path("/f/ini.ttf"));
		AGS3::HiResFontConfig c;
		c.configure(&map, Common::Path("/maps"), ini, 0);
		TS_ASSERT(c.active());

		AGS3::HiResFontPlan p = c.plan(0);   // bitmap beats face
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kBitmap);
		TS_ASSERT_EQUALS(p.bitmap.toString(), "/f/zero.fnt");

		p = c.plan(1);                       // [font.N] face beats the ini
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kFaces);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/a.ttf,/f/b.ttf");
		TS_ASSERT_EQUALS(p.size, 20);

		p = c.plan(2);                       // qualified by the game id
		TS_ASSERT_EQUALS(agsFaces(p), "/f/b.ttf");
		TS_ASSERT_EQUALS(p.size, 14);        // [hires] size

		p = c.plan(3);                       // the ini beats [hires] face
		TS_ASSERT_EQUALS(agsFaces(p), "/f/ini.ttf");
		TS_ASSERT_EQUALS(p.source, "hires_text_font");
	}

	void test_hires_face_without_ini_and_other_game() {
		const Graphics::HiResTextConfig map = agsParseMap(kAgsPlanMap, "othergame");
		AGS3::HiResFontConfig c;
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		AGS3::HiResFontPlan p = c.plan(2);   // [font.2:5daysastranger] is not this game's
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kFaces);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/hires.ttf");
		TS_ASSERT_EQUALS(p.source, "[hires] face");
		p = c.plan(7);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/hires.ttf");
		TS_ASSERT_EQUALS(p.size, 14);
	}

	void test_fonts_default_is_the_last_fallback() {
		// HIRES_TEXT_MAP.md: with no face named, [fonts] default=.
		const Graphics::HiResTextConfig map = agsParseMap("[fonts]\ndefault=d.ttf\n", "5daysastranger");
		AGS3::HiResFontConfig c;
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT(c.active());
		const AGS3::HiResFontPlan p = c.plan(4);
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kFaces);
		TS_ASSERT_EQUALS(agsFaces(p), "/maps/d.ttf");
		TS_ASSERT_EQUALS(p.size, 0);         // the game font's height
	}

	void test_plan_carries_the_map_gamma() {
		// C20: [hires] gamma= reaches every TrueType plan; off without it.
		const Graphics::HiResTextConfig plain = agsParseMap("[fonts]\ndefault=d.ttf\n", "x");
		const Graphics::HiResTextConfig dark = agsParseMap("[hires]\ngamma=1.8\n[fonts]\ndefault=d.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&plain, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.plan(0).gamma, 100);
		c.configure(&dark, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.plan(0).gamma, 180);
		c.configure(nullptr, Common::Path(), Common::Array<Common::Path>(), 16);
		TS_ASSERT_EQUALS(c.plan(0).gamma, 100);
	}

	void test_plan_carries_the_pixel_design_size() {
		// C28: [font.N] pixel= over [hires] pixel=; 0 when neither names one.
		const Graphics::HiResTextConfig map = agsParseMap(
			"[hires]\npixel=16\n[fonts]\ndefault=d.ttf\n[font.1]\nface=d.ttf\npixel=12\n", "x");
		const Graphics::HiResTextConfig plain = agsParseMap("[fonts]\ndefault=d.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.plan(0).pixel, 16);
		TS_ASSERT_EQUALS(c.plan(1).pixel, 12);
		c.configure(&plain, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.plan(0).pixel, 0);
	}

	void test_pixel_is_only_for_a_face_the_map_names() {
		// C28 review: the ini's hires_text_font is not the map's pixel face.
		const Graphics::HiResTextConfig map = agsParseMap("[hires]\npixel=10\n[fonts]\ndefault=d.ttf\n", "x");
		AGS3::HiResFontConfig c;
		Common::Array<Common::Path> ini;
		ini.push_back(Common::Path("/f/ini.ttf"));
		c.configure(&map, Common::Path("/maps"), ini, 0);
		TS_ASSERT_EQUALS(c.plan(0).source, "hires_text_font");
		TS_ASSERT_EQUALS(c.plan(0).pixel, 0);
	}

	void test_scaled_pixel_plan_is_n_times_the_small_face() {
		// C28 review: at N x the pixel face opens at N times the 1x ppem,
		// not at pixelGridSize(N * cell, D).
		AGS3::HiResFontPlan p;
		p.kind = AGS3::HiResFontPlan::kFaces;
		p.pixel = 10;
		TS_ASSERT_EQUALS(AGS3::scaledPlan(p, 2, 10).pixel, 20);
		TS_ASSERT_EQUALS(AGS3::scaledPlan(p, 3, 20).pixel, 60);
		// The pixel face did not open at 1x: nothing to scale.
		TS_ASSERT_EQUALS(AGS3::scaledPlan(p, 2, 0).pixel, 0);
		p.pixel = 0;
		TS_ASSERT_EQUALS(AGS3::scaledPlan(p, 2, 16).pixel, 0);
		// Everything else is the plan as it was.
		p.gamma = 180;
		TS_ASSERT_EQUALS(AGS3::scaledPlan(p, 2, 16).gamma, 180);
	}

	void test_scaled_pixel_face_opens_at_n_times_the_small_ppem() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		Common::String path;
		{
#pragma push_macro("getenv")
#undef getenv
			const char *dir = getenv("SCUMMVM_TEST_PIXEL_FONT_DIR");
			const char *data = getenv("SCUMMVM_TEST_I18N_DATA");
#pragma pop_macro("getenv")
			if (dir && *dir)
				path = Common::String::format("%s/Galmuri9.ttf", dir);
			else if (data && *data)
				path = Common::String::format("%s/../fonts/pixel/galmuri/Galmuri9.ttf", data);
		}
		if (path.empty() || !Common::FSNode(Common::Path(path, '/')).exists()) {
			TS_SKIP("Galmuri9.ttf not found (SCUMMVM_TEST_PIXEL_FONT_DIR)");
			return;
		}
		AGS3::HiResFontPlan p;
		p.kind = AGS3::HiResFontPlan::kFaces;
		p.pixel = 10;
		// (cell, N): the review's cases, cell 15 and a cell smaller than D.
		static const int kCases[][2] = { { 15, 2 }, { 9, 2 }, { 12, 3 }, { 25, 2 } };
		for (uint c = 0; c < ARRAYSIZE(kCases); c++) {
			const int cell = kCases[c][0], n = kCases[c][1];
			Common::String error;
			Graphics::TtfGlyphSource *small = Graphics::TtfGlyphSource::createPixel(
				Common::FSNode(Common::Path(path, '/')).createReadStream(), DisposeAfterUse::YES, cell, p.pixel, error);
			TS_ASSERT(small);
			if (!small)
				continue;
			const AGS3::HiResFontPlan big = AGS3::scaledPlan(p, n, small->faceSize());
			Graphics::TtfGlyphSource *large = Graphics::TtfGlyphSource::createPixel(
				Common::FSNode(Common::Path(path, '/')).createReadStream(), DisposeAfterUse::YES, cell * n, big.pixel, error);
			TS_ASSERT(large);
			if (large)
				TSM_ASSERT_EQUALS(Common::String::format("cell %d at %dx", cell, n).c_str(),
								  large->faceSize(), n * small->faceSize());
			delete small;
			delete large;
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	void test_nothing_named_is_the_game_font() {
		AGS3::HiResFontConfig c;
		c.configure(nullptr, Common::Path(), Common::Array<Common::Path>(), 16);
		TS_ASSERT(!c.active());
		TS_ASSERT_EQUALS(c.plan(0).kind, AGS3::HiResFontPlan::kGame);
		const Graphics::HiResTextConfig map = agsParseMap("[hires]\nalpha=false\n", "x");
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT(!c.active());
		TS_ASSERT(!c.alpha());
	}

	// C23 T1: [hires] scale= and hires_text_scale (AGS_HIRES_TEXT_DESIGN.md section 6)
	void test_scale_absent_is_one() {
		// Ruling (C23 Q1): a map without scale= is N = 1, whatever it names
		const Graphics::HiResTextConfig map = agsParseMap("[hires]\nface=/f/a.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
		c.configure(nullptr, Common::Path(), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
	}

	void test_scale_map_and_ini_precedence() {
		const Graphics::HiResTextConfig map = agsParseMap("[hires]\nscale=2\nface=/f/a.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.requestedScale(), 2);
		// the ini overrides the map, both ways
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0, 3);
		TS_ASSERT_EQUALS(c.requestedScale(), 3);
		c.configure(&map, Common::Path("/maps"), Common::Array<Common::Path>(), 0, 1);
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
		// the ini alone
		c.configure(nullptr, Common::Path(), Common::Array<Common::Path>(), 0, 2);
		TS_ASSERT_EQUALS(c.requestedScale(), 2);
		// clear() forgets it
		c.clear();
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
	}

	void test_scale_map_range() {
		// the shared reader keeps 1..3; out of range is the default 1
		const Graphics::HiResTextConfig four = agsParseMap("[hires]\nscale=4\nface=/f/a.ttf\n", "x");
		const Graphics::HiResTextConfig zero = agsParseMap("[hires]\nscale=0\nface=/f/a.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&four, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
		c.configure(&zero, Common::Path("/maps"), Common::Array<Common::Path>(), 0);
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
	}

	void test_parse_ini_scale() {
		int n = -1;
		TS_ASSERT(AGS3::HiResFontConfig::parseScale("1", n));
		TS_ASSERT_EQUALS(n, 1);
		TS_ASSERT(AGS3::HiResFontConfig::parseScale("3", n));
		TS_ASSERT_EQUALS(n, 3);
		TS_ASSERT(!AGS3::HiResFontConfig::parseScale("0", n));
		TS_ASSERT(!AGS3::HiResFontConfig::parseScale("4", n));
		TS_ASSERT(!AGS3::HiResFontConfig::parseScale("", n));
		TS_ASSERT(!AGS3::HiResFontConfig::parseScale("2x", n));
		TS_ASSERT(!AGS3::HiResFontConfig::parseScale("-2", n));
	}

	void test_scale_gates() {
		Common::String why;
		// all gates open
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::gateScale(2, true, 16, true, why), 2);
		TS_ASSERT(why.empty());
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::gateScale(3, true, 32, true, why), 3);
		TS_ASSERT(why.empty());
		// N = 1 needs no gate and says nothing
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::gateScale(1, false, 8, false, why), 1);
		TS_ASSERT(why.empty());
		// no mapped font
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::gateScale(2, false, 32, true, why), 1);
		TS_ASSERT(why.contains("mapped font"));
		// 8-bit game
		why.clear();
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::gateScale(2, true, 8, true, why), 1);
		TS_ASSERT(why.contains("8-bit"));
		// no 32-bit screen format
		why.clear();
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::gateScale(2, true, 16, false, why), 1);
		TS_ASSERT(why.contains("32-bit"));
	}
};
