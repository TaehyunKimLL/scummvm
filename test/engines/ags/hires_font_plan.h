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

#include "common/memstream.h"
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
};
