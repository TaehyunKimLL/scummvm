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

#include "engines/sci/graphics/hirestextsettings.h"
#include "graphics/hires_text/hires_options.h"

/**
 * SCI's render target, blend and scale (design sections 7.1.1, 7.2, 7.4):
 * the screen the upscaled driver asks for, whether a face's coverage is
 * blended or thresholded, and the one scale SCI draws at.
 */
class SciHiresRenderTestSuite : public CxxTest::TestSuite {
public:
	void test_auto_keeps_upstream_rgb_rendering() {
		Sci::SciRenderChoice c = Sci::chooseSciRender(true, Graphics::kHiResTargetAuto, false, Graphics::kHiResBlendAuto);
		TS_ASSERT(c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetAuto);
		TS_ASSERT(c.warning.empty());
	}

	void test_auto_without_upstream_follows_coverage() {
		TS_ASSERT(!Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, false, Graphics::kHiResBlendAuto).requestRGB);
		Sci::SciRenderChoice c = Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, true, Graphics::kHiResBlendAuto);
		TS_ASSERT(c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetRgb888);
		TS_ASSERT(!Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, true, Graphics::kHiResBlendOff).requestRGB);
	}

	void test_explicit_clut8_beats_rgb_rendering() {
		Sci::SciRenderChoice c = Sci::chooseSciRender(true, Graphics::kHiResTargetClut8, true, Graphics::kHiResBlendAuto);
		TS_ASSERT(!c.requestRGB);
		TS_ASSERT_EQUALS(c.warning, "render_target=clut8 wins over rgb_rendering/palette_mods; the screen stays paletted");
	}

	void test_explicit_rgb_targets() {
		TS_ASSERT_EQUALS(Sci::chooseSciRender(false, Graphics::kHiResTargetRgb888, false, Graphics::kHiResBlendAuto).target,
						 Graphics::kHiResTargetRgb888);
		TS_ASSERT(Sci::chooseSciRender(false, Graphics::kHiResTargetRgb565, false, Graphics::kHiResBlendAuto).requestRGB);
	}

	void test_scale_is_two() {
		Graphics::HiResMap m;
		m.scale = 3;
		m.scaleSet = true;
		Common::String w;
		const Graphics::HiResScaleLimits desktop = { 1, 3 };
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, true, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT_EQUALS(w, "SCI draws hi-res text at 2x only; using 2");
	}

	// --- further cases ---

	void test_explicit_clut8_without_upstream_has_no_warning() {
		Sci::SciRenderChoice c = Sci::chooseSciRender(false, Graphics::kHiResTargetClut8, true, Graphics::kHiResBlendOn);
		TS_ASSERT(!c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetClut8);
		TS_ASSERT(c.warning.empty());
	}

	void test_explicit_rgb_targets_ignore_upstream_and_coverage() {
		Sci::SciRenderChoice c = Sci::chooseSciRender(true, Graphics::kHiResTargetRgb565, false, Graphics::kHiResBlendOff);
		TS_ASSERT(c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetRgb565);
		TS_ASSERT(c.warning.empty());
		c = Sci::chooseSciRender(true, Graphics::kHiResTargetRgb888, true, Graphics::kHiResBlendAuto);
		TS_ASSERT(c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetRgb888);
	}

	void test_auto_without_upstream_blend_on_wants_rgb_even_without_coverage() {
		// design 7.1.1: clut8 only when the faces are all 1 bpp *and* blend is not on.
		Sci::SciRenderChoice c = Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, false, Graphics::kHiResBlendOn);
		TS_ASSERT(c.requestRGB);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetRgb888);
		c = Sci::chooseSciRender(false, Graphics::kHiResTargetAuto, false, Graphics::kHiResBlendAuto);
		TS_ASSERT_EQUALS(c.target, Graphics::kHiResTargetClut8);
	}

	void test_scale_unset_or_two_is_quiet() {
		Graphics::HiResMap m;
		Common::String w = "stale";
		const Graphics::HiResScaleLimits desktop = { 1, 3 };
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, false, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT(w.empty());
		m.scale = 2;
		m.scaleSet = true;
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, true, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT(w.empty());
	}

	void test_scale_ini_beats_map_and_unloaded_map_is_ignored() {
		Graphics::HiResMap m;
		m.scale = 3;
		m.scaleSet = true;
		Common::String w;
		const Graphics::HiResScaleLimits desktop = { 1, 3 };
		// The map is not loaded: its scale= is not read.
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, false, Graphics::HiResIniOverrides(), desktop, w), 2);
		TS_ASSERT(w.empty());
		Graphics::HiResIniOverrides ini;
		ini.scaleSet = true;
		ini.scale = 2;
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, true, ini, desktop, w), 2);
		TS_ASSERT(w.empty());
		ini.scale = 1;
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, true, ini, desktop, w), 2);
		TS_ASSERT_EQUALS(w, "SCI draws hi-res text at 2x only; using 2");
	}

	void test_scale_on_the_dos_backend() {
		Graphics::HiResMap m;
		m.scale = 3;
		m.scaleSet = true;
		Common::String w;
		const Graphics::HiResScaleLimits dos = { 2, 2 };
		TS_ASSERT_EQUALS(Sci::sciHiresScale(m, true, Graphics::HiResIniOverrides(), dos, w), 2);
		TS_ASSERT_EQUALS(w, "the DOS backend runs hi-res text at 2x only; using 2");
	}

	void test_phase1_out_of_scope_reports_auto() {
		// Hi-res text does not apply to the game: the driver request stays upstream.
		Graphics::HiResIniOverrides ini;
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetRgb888;
		ini.blendSet = true;
		ini.blend = Graphics::kHiResBlendOn;
		Graphics::HiResMap m;
		Sci::SciPhase1 p = Sci::sciPhase1(false, ini, m, true, true);
		TS_ASSERT_EQUALS(p.target, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(p.blend, Graphics::kHiResBlendAuto);
		TS_ASSERT(!p.anyCoverage);
	}

	void test_phase1_hires_text_off_reports_auto() {
		Graphics::HiResIniOverrides ini;
		ini.enabled = false;
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetClut8;
		Graphics::HiResMap m;
		Sci::SciPhase1 p = Sci::sciPhase1(true, ini, m, true, true);
		TS_ASSERT_EQUALS(p.target, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(p.blend, Graphics::kHiResBlendAuto);
		TS_ASSERT(!p.anyCoverage);
	}

	void test_phase1_ini_beats_map_beats_auto() {
		Graphics::HiResIniOverrides ini;
		Graphics::HiResMap m;
		m.targetSet = true;
		m.target = Graphics::kHiResTargetClut8;
		m.blendSet = true;
		m.blend = Graphics::kHiResBlendOff;

		Sci::SciPhase1 p = Sci::sciPhase1(true, ini, m, true, true);
		TS_ASSERT_EQUALS(p.target, Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(p.blend, Graphics::kHiResBlendOff);
		TS_ASSERT(p.anyCoverage);

		// The map is not loaded: none of its keys count.
		p = Sci::sciPhase1(true, ini, m, false, false);
		TS_ASSERT_EQUALS(p.target, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(p.blend, Graphics::kHiResBlendAuto);

		// An ini auto falls through to the map.
		ini.targetSet = true;
		ini.target = Graphics::kHiResTargetAuto;
		p = Sci::sciPhase1(true, ini, m, true, true);
		TS_ASSERT_EQUALS(p.target, Graphics::kHiResTargetClut8);

		ini.target = Graphics::kHiResTargetRgb888;
		ini.blendSet = true;
		ini.blend = Graphics::kHiResBlendOn;
		p = Sci::sciPhase1(true, ini, m, true, true);
		TS_ASSERT_EQUALS(p.target, Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(p.blend, Graphics::kHiResBlendOn);
	}

	void test_phase2_blend() {
		Graphics::HiResIniOverrides ini;
		Graphics::HiResMap m;
		TS_ASSERT_EQUALS(Sci::sciBlend(ini, m, true), Graphics::kHiResBlendAuto);
		m.blendSet = true;
		m.blend = Graphics::kHiResBlendOff;
		TS_ASSERT_EQUALS(Sci::sciBlend(ini, m, true), Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(Sci::sciBlend(ini, m, false), Graphics::kHiResBlendAuto);
		ini.blendSet = true;
		ini.blend = Graphics::kHiResBlendOn;
		TS_ASSERT_EQUALS(Sci::sciBlend(ini, m, true), Graphics::kHiResBlendOn);
	}

	void test_threshold_only_unblended_coverage_on_rgb() {
		// An RGB screen with blend off: a 2 bpp or 8 bpp face is cut into a stencil.
		TS_ASSERT(Sci::sciThresholdCoverage(Graphics::kHiResBlendOff, 2, false));
		TS_ASSERT(Sci::sciThresholdCoverage(Graphics::kHiResBlendOff, 8, false));
		// blend auto/on on RGB: blended.
		TS_ASSERT(!Sci::sciThresholdCoverage(Graphics::kHiResBlendAuto, 2, false));
		TS_ASSERT(!Sci::sciThresholdCoverage(Graphics::kHiResBlendOn, 8, false));
		// A 1 bpp face has nothing to threshold.
		TS_ASSERT(!Sci::sciThresholdCoverage(Graphics::kHiResBlendOff, 1, false));
		// CLUT8: the driver's own 50% stamp, untouched.
		TS_ASSERT(!Sci::sciThresholdCoverage(Graphics::kHiResBlendOff, 2, true));
		TS_ASSERT(!Sci::sciThresholdCoverage(Graphics::kHiResBlendAuto, 2, true));
		TS_ASSERT(!Sci::sciThresholdCoverage(Graphics::kHiResBlendOn, 2, true));
	}

	void test_threshold_matches_blend_active() {
		// Glyph coverage is blended iff Graphics::blendActive() says so.
		const Graphics::HiResBlend blends[] = { Graphics::kHiResBlendAuto, Graphics::kHiResBlendOn, Graphics::kHiResBlendOff };
		const int bpps[] = { 1, 2, 8 };
		for (int b = 0; b < 3; b++) {
			for (int i = 0; i < 3; i++) {
				const bool blended = Graphics::blendActive(blends[b], bpps[i] > 1, false);
				TS_ASSERT_EQUALS(Sci::sciThresholdCoverage(blends[b], bpps[i], false), !blended && bpps[i] > 1);
			}
		}
	}

	void test_blend_on_clut8_warning() {
		const char *const text = "hires_text_blend=on needs an RGB screen until palette-matched blending exists; drawing hard-edged text";
		TS_ASSERT_EQUALS(Sci::sciBlendWarning(Graphics::kHiResBlendOn, true, true), text);
		TS_ASSERT(Sci::sciBlendWarning(Graphics::kHiResBlendOn, true, false).empty());
		TS_ASSERT(Sci::sciBlendWarning(Graphics::kHiResBlendOn, false, true).empty());
		TS_ASSERT(Sci::sciBlendWarning(Graphics::kHiResBlendAuto, true, true).empty());
		TS_ASSERT(Sci::sciBlendWarning(Graphics::kHiResBlendOff, true, true).empty());
	}

	void test_target_note() {
		TS_ASSERT_EQUALS(Sci::sciTargetNote(Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb888),
						 "render_target=rgb565 is not available here; using rgb888");
		TS_ASSERT_EQUALS(Sci::sciTargetNote(Graphics::kHiResTargetRgb888, Graphics::kHiResTargetClut8),
						 "render_target=rgb888 is not available here; using clut8");
		TS_ASSERT(Sci::sciTargetNote(Graphics::kHiResTargetRgb888, Graphics::kHiResTargetRgb888).empty());
		TS_ASSERT(Sci::sciTargetNote(Graphics::kHiResTargetAuto, Graphics::kHiResTargetClut8).empty());
	}
};
