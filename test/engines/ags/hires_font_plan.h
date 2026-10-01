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

#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../../system/null_osystem.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"

#include "ags/shared/font/hires_font_config.h"

namespace {

Graphics::HiResMap agsLoadMap(const char *text, const char *qualifier,
							  Graphics::HiResRenderTarget target = Graphics::kHiResTargetAuto, bool *ok = nullptr) {
	Common::MemoryReadStream in((const byte *)text, strlen(text));
	Common::Array<Common::String> q;
	q.push_back(qualifier);
	Graphics::HiResMapLoadOptions opts;
	opts.target = target;
	opts.quiet = true;
	Graphics::HiResMap map;
	const bool loaded = Graphics::HiResFontMap::loadMap(in, Common::Path("/maps"), q, Graphics::kHiResKeysAgs, map, opts);
	if (ok)
		*ok = loaded;
	return map;
}

Common::String agsFaces(const AGS3::HiResFontPlan &p) {
	Common::String s;
	for (uint i = 0; i < p.faces.size(); i++)
		s += (i ? "," : "") + p.faces[i].toString();
	return s;
}

bool agsHasWarning(const Common::Array<Common::String> &warnings, const char *text) {
	for (uint i = 0; i < warnings.size(); i++) {
		if (warnings[i].contains(text))
			return true;
	}
	return false;
}

struct AgsFakeIni {
	Common::HashMap<Common::String, Common::String> game;
};

bool agsFakeIniGet(const char *key, bool globalFallback, Common::String &value, void *ctx) {
	const AgsFakeIni *ini = static_cast<const AgsFakeIni *>(ctx);
	if (!ini->game.contains(key))
		return false;
	value = ini->game.getVal(key);
	return true;
}

const char *const kAgsPlanMap =
	"[map]\n"
	"version=2\n"
	"[font]\n"
	"face=hf\n"
	"size=14\n"
	"[fonts]\n"
	"hf=/f/hires.ttf\n"
	"a=/f/a.ttf\n"
	"b=/f/b.ttf\n"
	"z=/f/zero.svf\n"
	"[font.0]\n"
	"face=z\n"
	"[font.1]\n"
	"face=a, b\n"
	"size=20\n"
	"[font.2:5daysastranger]\n"
	"face=b\n";

} // End of anonymous namespace

/** Which font an AGS font number gets from HIRESTXT.MAP and the ini. */
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

	void test_precedence_ini_id_face_map_face() {
		const Graphics::HiResMap map = agsLoadMap(kAgsPlanMap, "5daysastranger");
		AGS3::HiResFontConfig c;
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(c.active());

		AGS3::HiResFontPlan p = c.plan(0);   // an SVF is a face like any other (sniffed when opened)
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kFaces);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/zero.svf");
		TS_ASSERT_EQUALS(p.source, "[font.0] face");

		p = c.plan(1);                       // [font.N] face beats [font] face
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kFaces);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/a.ttf,/f/b.ttf");
		TS_ASSERT_EQUALS(p.size, 20);

		p = c.plan(2);                       // qualified by the game id
		TS_ASSERT_EQUALS(agsFaces(p), "/f/b.ttf");
		TS_ASSERT_EQUALS(p.size, 14);        // [font] size

		p = c.plan(3);                       // [font] face for every other font
		TS_ASSERT_EQUALS(agsFaces(p), "/f/hires.ttf");
		TS_ASSERT_EQUALS(p.source, "[font] face");

		// The ini's hires_text_face beats every level of the map; the
		// size is still taken key by key.
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "/f/ini.ttf";
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/game"));
		p = c.plan(1);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/ini.ttf");
		TS_ASSERT_EQUALS(p.source, "hires_text_face");
		TS_ASSERT_EQUALS(p.size, 20);
		p = c.plan(3);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/ini.ttf");
		TS_ASSERT_EQUALS(p.size, 14);
	}

	void test_font_face_without_ini_and_other_game() {
		const Graphics::HiResMap map = agsLoadMap(kAgsPlanMap, "othergame");
		AGS3::HiResFontConfig c;
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		AGS3::HiResFontPlan p = c.plan(2);   // [font.2:5daysastranger] is not this game's
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kFaces);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/hires.ttf");
		TS_ASSERT_EQUALS(p.source, "[font] face");
		p = c.plan(7);
		TS_ASSERT_EQUALS(agsFaces(p), "/f/hires.ttf");
		TS_ASSERT_EQUALS(p.size, 14);
	}

	void test_font_face_is_the_map_wide_default() {
		// [font] face= is the face of every font no [font.N] names; [fonts]
		// is only a name table, so a name "default" there names nothing.
		const Graphics::HiResMap names = agsLoadMap("[map]\nversion=2\n[fonts]\ndefault=d.ttf\n", "5daysastranger");
		AGS3::HiResFontConfig c;
		c.configure(&names, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(!c.active());
		TS_ASSERT_EQUALS(c.plan(4).kind, AGS3::HiResFontPlan::kGame);

		const Graphics::HiResMap map = agsLoadMap("[map]\nversion=2\n[font]\nface=d.ttf\n", "5daysastranger");
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(c.active());
		const AGS3::HiResFontPlan p = c.plan(4);
		TS_ASSERT_EQUALS(p.kind, AGS3::HiResFontPlan::kFaces);
		TS_ASSERT_EQUALS(agsFaces(p), "/maps/d.ttf");
		TS_ASSERT_EQUALS(p.size, 0);         // the game font's height
	}

	void test_plan_carries_the_map_gamma() {
		// [render] gamma= reaches every TrueType plan; off without it.
		const Graphics::HiResMap plain = agsLoadMap("[map]\nversion=2\n[font]\nface=d.ttf\n", "x");
		const Graphics::HiResMap dark = agsLoadMap("[map]\nversion=2\n[render]\ngamma=1.8\n[font]\nface=d.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&plain, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.plan(0).gamma, 100);
		c.configure(&dark, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.plan(0).gamma, 180);
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "/f/ini.ttf";
		ini.sizeSet = true;
		ini.size = 16;
		c.configure(nullptr, false, ini, Common::Path(), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.plan(0).gamma, 100);
	}

	void test_plan_carries_the_pixel_design_size() {
		// [font.N] pixel= over [font] pixel=; 0 when neither names one.
		const Graphics::HiResMap map = agsLoadMap(
			"[map]\nversion=2\n[font]\nface=d.ttf\npixel=16\n[font.1]\nface=d.ttf\npixel=12\n", "x");
		const Graphics::HiResMap plain = agsLoadMap("[map]\nversion=2\n[font]\nface=d.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.plan(0).pixel, 16);
		TS_ASSERT_EQUALS(c.plan(1).pixel, 12);
		c.configure(&plain, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.plan(0).pixel, 0);
	}

	void test_pixel_applies_to_the_ini_face() {
		// pixel= holds the first face of the id chain on its grid, and the
		// ini's hires_text_face is that chain: it takes the map's pixel=,
		// [font.N]'s over [font]'s.
		const Graphics::HiResMap map = agsLoadMap(
			"[map]\nversion=2\n[font]\nface=d.ttf\npixel=10\n[font.1]\npixel=12\n", "x");
		AGS3::HiResFontConfig c;
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "/f/ini.ttf";
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.plan(0).source, "hires_text_face");
		TS_ASSERT_EQUALS(agsFaces(c.plan(0)), "/f/ini.ttf");
		TS_ASSERT_EQUALS(c.plan(0).pixel, 10);
		TS_ASSERT_EQUALS(c.plan(1).source, "hires_text_face");
		TS_ASSERT_EQUALS(c.plan(1).pixel, 12);
	}

	void test_scaled_pixel_plan_is_n_times_the_small_face() {
		// At N x the pixel face opens at N times the 1x ppem, not at
		// pixelGridSize(N * cell, D).
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
		// (cell, N): cell 15 and a cell smaller than D, among others.
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
		Graphics::HiResIniOverrides ini;
		ini.sizeSet = true;
		ini.size = 16;
		c.configure(nullptr, false, ini, Common::Path(), Common::Path("/game"));
		TS_ASSERT(!c.active());
		TS_ASSERT_EQUALS(c.plan(0).kind, AGS3::HiResFontPlan::kGame);
		TS_ASSERT_EQUALS(c.blend(), Graphics::kHiResBlendAuto);
		const Graphics::HiResMap map = agsLoadMap("[map]\nversion=2\n[render]\nblend=off\n", "x");
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(!c.active());
		TS_ASSERT_EQUALS(c.blend(), Graphics::kHiResBlendOff);
	}

	void test_blend_ini_beats_the_map() {
		const Graphics::HiResMap map = agsLoadMap("[map]\nversion=2\n[render]\nblend=off\n[font]\nface=d.ttf\n", "x");
		Graphics::HiResIniOverrides ini;
		ini.blendSet = true;
		ini.blend = Graphics::kHiResBlendOn;
		AGS3::HiResFontConfig c;
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.blend(), Graphics::kHiResBlendOn);
	}

	void test_scale_absent_is_one() {
		// A map without scale= is N = 1, whatever it names
		const Graphics::HiResMap map = agsLoadMap("[map]\nversion=2\n[font]\nface=/f/a.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
		c.configure(nullptr, false, Graphics::HiResIniOverrides(), Common::Path(), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
	}

	void test_scale_map_and_ini_precedence() {
		const Graphics::HiResMap map = agsLoadMap("[map]\nversion=2\n[render]\nscale=2\n[font]\nface=/f/a.ttf\n", "x");
		AGS3::HiResFontConfig c;
		Graphics::HiResIniOverrides ini;
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 2);
		// the ini overrides the map, both ways
		ini.scaleSet = true;
		ini.scale = 3;
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 3);
		ini.scale = 1;
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
		// the ini alone
		ini.scale = 2;
		c.configure(nullptr, false, ini, Common::Path(), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 2);
		// clear() forgets it
		c.clear();
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
	}

	void test_scale_map_range() {
		// the shared reader keeps 1..3; out of range is the default 1
		const Graphics::HiResMap four = agsLoadMap("[map]\nversion=2\n[render]\nscale=4\n[font]\nface=/f/a.ttf\n", "x");
		const Graphics::HiResMap zero = agsLoadMap("[map]\nversion=2\n[render]\nscale=0\n[font]\nface=/f/a.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&four, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
		c.configure(&zero, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
	}

	void test_parse_ini_scale() {
		// hires_text_scale: decimal 1..3 only; anything else is one warning
		// and leaves the scale unset (the map or 1 applies).
		static const char *const kGood[] = { "1", "3" };
		static const char *const kBad[] = { "0", "4", "", "2x", "-2" };
		for (uint i = 0; i < ARRAYSIZE(kGood); i++) {
			AgsFakeIni f;
			f.game["hires_text_scale"] = kGood[i];
			Common::Array<Common::String> w;
			const Graphics::HiResIniOverrides o = Graphics::readHiResIni(agsFakeIniGet, &f, w);
			TS_ASSERT(o.scaleSet);
			TS_ASSERT_EQUALS(o.scale, kGood[i][0] - '0');
			TS_ASSERT(w.empty());
		}
		for (uint i = 0; i < ARRAYSIZE(kBad); i++) {
			AgsFakeIni f;
			f.game["hires_text_scale"] = kBad[i];
			Common::Array<Common::String> w;
			const Graphics::HiResIniOverrides o = Graphics::readHiResIni(agsFakeIniGet, &f, w);
			TSM_ASSERT(kBad[i], !o.scaleSet);
			TSM_ASSERT_EQUALS(kBad[i], w.size(), 1u);
			AGS3::HiResFontConfig c;
			c.configure(nullptr, false, o, Common::Path(), Common::Path("/game"));
			TS_ASSERT_EQUALS(c.requestedScale(), 1);
		}
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

	void test_hires_text_false_gives_no_plan() {
		// hires_text=false: no map, no faces, no scale - the game draws as
		// without the layer, whatever the map or the other ini keys say.
		const Graphics::HiResMap map = agsLoadMap(kAgsPlanMap, "5daysastranger");
		const Graphics::HiResMap scaled = agsLoadMap("[map]\nversion=2\n[render]\nscale=2\n[font]\nface=/f/a.ttf\n", "x");
		Graphics::HiResIniOverrides ini;
		ini.enabled = false;
		ini.faceSet = true;
		ini.face = "/f/ini.ttf";
		AGS3::HiResFontConfig c;
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(!c.active());
		for (int n = 0; n < 4; n++)
			TS_ASSERT_EQUALS(c.plan(n).kind, AGS3::HiResFontPlan::kGame);
		c.configure(&scaled, true, ini, Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
	}

	void test_range_key_is_warned_for_ags() {
		const Graphics::HiResMap map = agsLoadMap(
			"[map]\nversion=2\n[render]\ntarget=clut8\n[font]\nface=/f/a.ttf\nrange.basic-latin=/f/b.ttf\n", "x");
		AGS3::HiResFontConfig c;
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(agsHasWarning(c.warnings(), "AGS does not use [font] range.basic-latin"));
		// AGS takes its screen from the game's colour depth
		TS_ASSERT(agsHasWarning(c.warnings(), "AGS does not use [render] target"));
		// the rest of the map still applies; the range does not
		TS_ASSERT_EQUALS(agsFaces(c.plan(0)), "/f/a.ttf");
	}

	void test_relative_ini_face_resolves_against_the_game_folder() {
		Graphics::HiResIniOverrides ini;
		ini.faceSet = true;
		ini.face = "fonts/KO.TTF";
		AGS3::HiResFontConfig c;
		c.configure(nullptr, false, ini, Common::Path(), Common::Path("/games/five"));
		TS_ASSERT(c.active());
		TS_ASSERT_EQUALS(agsFaces(c.plan(0)), "/games/five/fonts/KO.TTF");
		TS_ASSERT_EQUALS(c.plan(0).source, "hires_text_face");
		// A [fonts] name in the ini is the map's: its path is the map's.
		const Graphics::HiResMap map = agsLoadMap("[map]\nversion=2\n[fonts]\nui=U.TTF\n", "x");
		ini.face = "ui, fonts/KO.TTF";
		c.configure(&map, true, ini, Common::Path("/maps"), Common::Path("/games/five"));
		TS_ASSERT_EQUALS(agsFaces(c.plan(3)), "/maps/U.TTF,/games/five/fonts/KO.TTF");
	}

	void test_target_follows_the_game_colour_depth() {
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::targetForColorDepth(8), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::targetForColorDepth(16), Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::targetForColorDepth(32), Graphics::kHiResTargetRgb888);
	}

	void test_target_qualified_fonts_follow_the_colour_depth() {
		const char *const text =
			"[map]\nversion=2\n"
			"[fonts]\nui=G.TTF\n"
			"[fonts:clut8]\nui=L.TTF\n"
			"[font]\nface=ui\n";
		static const int kDepths[] = { 8, 16, 32 };
		static const char *const kWant[] = { "/maps/L.TTF", "/maps/G.TTF", "/maps/G.TTF" };
		for (uint i = 0; i < ARRAYSIZE(kDepths); i++) {
			const Graphics::HiResMap map = agsLoadMap(text, "x", AGS3::HiResFontConfig::targetForColorDepth(kDepths[i]));
			AGS3::HiResFontConfig c;
			c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
			TSM_ASSERT_EQUALS(Common::String::format("%d-bit", kDepths[i]).c_str(), agsFaces(c.plan(0)), kWant[i]);
		}
	}

	void test_refused_map_is_no_map() {
		// An old map (no [map] version=2) is refused: nothing it names applies.
		bool ok = true;
		const Graphics::HiResMap old = agsLoadMap("[hires]\nface=/f/a.ttf\nscale=2\n", "x", Graphics::kHiResTargetAuto, &ok);
		TS_ASSERT(!ok);
		AGS3::HiResFontConfig c;
		c.configure(&old, false, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(!c.active());
		TS_ASSERT_EQUALS(c.requestedScale(), 1);
		TS_ASSERT(agsHasWarning(c.warnings(), "not a version 2 map"));
	}

	void test_layout_rules_from_the_map() {
		AGS3::HiResFontConfig c;
		c.configure(nullptr, false, Graphics::HiResIniOverrides(), Common::Path(), Common::Path("/game"));
		const Graphics::BreakRules defaults = c.breakRules();
		TS_ASSERT(defaults.kinsoku);
		const Graphics::HiResMap map = agsLoadMap("[map]\nversion=2\n[layout]\nkinsoku=off\n", "x");
		c.configure(&map, true, Graphics::HiResIniOverrides(), Common::Path("/maps"), Common::Path("/game"));
		TS_ASSERT(!c.breakRules().kinsoku);
	}

	void test_map_path_choice() {
#if NULL_OSYSTEM_IS_AVAILABLE
		// A game folder of this run's own, holding a lower-case hirestxt.map
		char name[] = "ags-hires-map-XXXXXX";
		TS_ASSERT(mkdtemp(name));
		char *full = realpath(name, nullptr);
		const Common::String dir = full ? full : name;
		free(full);
		const Common::Path gameDir(dir, '/');
		const Common::String mapFile = dir + "/hirestxt.map";
		{
			Common::DumpFile f;
			TS_ASSERT(f.open(Common::Path(mapFile, '/')));
			f.writeString("[map]\nversion=2\n");
			f.close();
		}
		Common::String warning;
		Graphics::HiResIniOverrides ini;
		// unset: the game folder's HIRESTXT.MAP, whatever its case
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::mapPathFor(ini, gameDir, warning).toString('/'), mapFile);
		TS_ASSERT(warning.empty());
		// relative: the game folder's, whether the file exists or not
		ini.mapSet = true;
		ini.map = "sub/X.MAP";
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::mapPathFor(ini, gameDir, warning).toString('/'), dir + "/sub/X.MAP");
		// absolute: as written
		ini.map = "/maps/Y.MAP";
		TS_ASSERT_EQUALS(AGS3::HiResFontConfig::mapPathFor(ini, gameDir, warning).toString('/'), "/maps/Y.MAP");
		// empty: no map, one warning (the game folder's file is not used)
		ini.map = "";
		TS_ASSERT(AGS3::HiResFontConfig::mapPathFor(ini, gameDir, warning).empty());
		TS_ASSERT(warning.contains("empty"));
		// hires_text=false: no map at all
		warning.clear();
		ini = Graphics::HiResIniOverrides();
		ini.enabled = false;
		TS_ASSERT(AGS3::HiResFontConfig::mapPathFor(ini, gameDir, warning).empty());
		TS_ASSERT(warning.empty());
		// no file in the folder: no map
		remove(mapFile.c_str());
		ini.enabled = true;
		TS_ASSERT(AGS3::HiResFontConfig::mapPathFor(ini, gameDir, warning).empty());
		remove(dir.c_str());
#else
		TS_SKIP("needs a real filesystem");
#endif
	}
};
