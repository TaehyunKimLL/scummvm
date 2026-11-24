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

#include "common/config-manager.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/list.h"
#include "common/util.h"

#include "graphics/hires_text/hires_options.h"

#include "test/system/null_osystem.h"

namespace {

typedef Common::HashMap<Common::String, Common::String> OptionKeys;

bool getOptionKey(const char *key, Common::String &value, void *ctx) {
	const OptionKeys &keys = *(const OptionKeys *)ctx;
	if (!keys.contains(key))
		return false;
	value = keys[key];
	return true;
}

Graphics::HiResOptions parseOptions(OptionKeys &keys, Common::Array<Common::String> &w) {
	return Graphics::parseHiResOptions(getOptionKey, &keys, w);
}

} // End of anonymous namespace

class HiResTextHiResOptionsTestSuite : public CxxTest::TestSuite {
public:
	void test_defaults() {
		OptionKeys k;
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = parseOptions(k, w);
		TS_ASSERT(o.enabled);
		TS_ASSERT_EQUALS(o.font, Graphics::kHiResFontAuto);
		TS_ASSERT(!o.mapSet);
		TS_ASSERT_EQUALS(o.scale, 0);
		TS_ASSERT_EQUALS(o.blend, Graphics::kHiResBlendAuto);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetAuto);
		TS_ASSERT(!o.hangulBreakSet);
		TS_ASSERT(!o.log);
		TS_ASSERT_EQUALS(o.wholeKb, 256);
		TS_ASSERT(w.empty());
	}

	void test_values() {
		OptionKeys k;
		k["hires_text"] = "false";
		k["hires_text_font"] = "Bitmap";
		k["hires_text_map"] = "";
		k["hires_text_scale"] = "1";
		k["hires_text_blend"] = "off";
		k["render_target"] = "rgb565";
		k["hangul_break"] = "any";
		k["hires_text_log"] = "true";
		k["hires_text_whole_kb"] = "64";
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = parseOptions(k, w);
		TS_ASSERT(!o.enabled);
		TS_ASSERT_EQUALS(o.font, Graphics::kHiResFontBitmap);
		TS_ASSERT(o.mapSet);
		TS_ASSERT(o.map.empty());
		TS_ASSERT_EQUALS(o.scale, 1);
		TS_ASSERT_EQUALS(o.blend, Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(o.hangulBreak, Graphics::kHangulBreakAny);
		TS_ASSERT(o.log);
		TS_ASSERT_EQUALS(o.wholeKb, 64);
		TS_ASSERT(w.empty());
	}

	void test_badValues() {
		OptionKeys k;
		k["hires_text_scale"] = "3";
		k["render_target"] = "rgb444";
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = parseOptions(k, w);
		TS_ASSERT_EQUALS(o.scale, 0);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(w.size(), 2u);
		TS_ASSERT_EQUALS(w[0], "hires_text_scale '3' is not auto, 1 or 2; ignoring it");
	}

	void test_wholeKbBadValues() {
		static const char *const bad[] = { "-1", "99999", "abc", "", "64k", "1.5" };
		for (uint i = 0; i < ARRAYSIZE(bad); ++i) {
			OptionKeys k;
			k["hires_text_whole_kb"] = bad[i];
			Common::Array<Common::String> w;
			const Graphics::HiResOptions o = parseOptions(k, w);
			TS_ASSERT_EQUALS(o.wholeKb, 256);
			TS_ASSERT_EQUALS(w.size(), 1u);
		}
		// The ends of the range are values.
		OptionKeys k;
		Common::Array<Common::String> w;
		k["hires_text_whole_kb"] = "0";
		TS_ASSERT_EQUALS(parseOptions(k, w).wholeKb, 0);
		k["hires_text_whole_kb"] = "16384";
		TS_ASSERT_EQUALS(parseOptions(k, w).wholeKb, 16384);
		TS_ASSERT(w.empty());
	}

	void test_fontOrder() {
		Common::Array<Graphics::HiResFontKind> o = Graphics::hiResFontOrder(Graphics::kHiResFontAuto);
		TS_ASSERT_EQUALS(o.size(), 2u);
		TS_ASSERT_EQUALS(o[0], Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(o[1], Graphics::kHiResFontBitmap);
		o = Graphics::hiResFontOrder(Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(o[0], Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(o[1], Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(Graphics::hiResFontOrder(Graphics::kHiResFontTtf).size(), 2u);
	}

	void test_targets() {
		Common::List<Graphics::PixelFormat> formats;
		formats.push_back(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
		formats.push_back(Graphics::PixelFormat::createFormatCLUT8());
		const uint32 offered = Graphics::hiResTargetsOffered(formats);
		TS_ASSERT_EQUALS(offered, (1u << Graphics::kHiResTargetClut8) | (1u << Graphics::kHiResTargetRgb565));
		Graphics::HiResOptions o;
		Common::String note;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, false, offered, note), Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb565);
		o.blend = Graphics::kHiResBlendOff;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetClut8);
		o.target = Graphics::kHiResTargetRgb888;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(note, "render_target rgb888 is not offered by this backend; using rgb565");
		Graphics::HiResMap m;
		m.target = Graphics::kHiResTargetClut8;
		m.targetSet = true;
		o = Graphics::HiResOptions();
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, &m, true, offered, note), Graphics::kHiResTargetClut8);
		TS_ASSERT(note.empty());
	}

	// 15-bit targets (RGB555, XRGB1555) are mandatory for the AA faces: they are a render target like the others, and
	// auto prefers 32-bit where it is offered (it keeps the VGA picture's palette), then 16-bit, then 15-bit.
	void test_rgb555() {
		Graphics::HiResRenderTarget t;
		TS_ASSERT(Graphics::HiResFontMap::parseRenderTarget("rgb555", t));
		TS_ASSERT_EQUALS(t, Graphics::kHiResTargetRgb555);
		TS_ASSERT(Graphics::HiResFontMap::parseRenderTarget("RGB555", t));
		TS_ASSERT(!Graphics::HiResFontMap::parseRenderTarget("rgb444", t));
		TS_ASSERT_EQUALS(Common::String(Graphics::HiResFontMap::targetQualifier(Graphics::kHiResTargetRgb555)), "rgb");
		TS_ASSERT_EQUALS(Graphics::hiResTargetOf(Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0)), Graphics::kHiResTargetRgb555);
		TS_ASSERT_EQUALS(Graphics::hiResTargetOf(Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15)), Graphics::kHiResTargetRgb555);	// XRGB1555
		TS_ASSERT_EQUALS(Graphics::hiResTargetOf(Graphics::PixelFormat(2, 5, 5, 5, 0, 0, 5, 10, 0)), Graphics::kHiResTargetRgb555);	// BGR555
		TS_ASSERT_EQUALS(Graphics::hiResTargetOf(Graphics::PixelFormat(2, 4, 4, 4, 4, 12, 8, 4, 0)), Graphics::kHiResTargetAuto);	// RGBA4444: none
		TS_ASSERT_EQUALS(Graphics::hiResTargetOf(Graphics::PixelFormat(2, 5, 6, 5, 0, 0, 5, 11, 0)), Graphics::kHiResTargetRgb565);	// BGR565

		Common::Array<Common::String> w;
		OptionKeys k;
		k["render_target"] = "rgb555";
		TS_ASSERT_EQUALS(parseOptions(k, w).target, Graphics::kHiResTargetRgb555);
		TS_ASSERT(w.empty());

		// a backend offering only 555 (and XRGB1555): auto takes it, CLUT8 stays the answer without coverage
		Common::List<Graphics::PixelFormat> f555;
		f555.push_back(Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15));
		f555.push_back(Graphics::PixelFormat::createFormatCLUT8());
		uint32 offered = Graphics::hiResTargetsOffered(f555);
		TS_ASSERT_EQUALS(offered, (1u << Graphics::kHiResTargetClut8) | (1u << Graphics::kHiResTargetRgb555));
		Graphics::HiResOptions o;
		Common::String note;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb555);
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, false, offered, note), Graphics::kHiResTargetClut8);
		// rgb565 asked for, only 555 offered: 555 with a note
		o.target = Graphics::kHiResTargetRgb565;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb555);
		TS_ASSERT_EQUALS(note, "render_target rgb565 is not offered by this backend; using rgb555");
		o.target = Graphics::kHiResTargetRgb555;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb555);
		TS_ASSERT(note.empty());

		// everything offered: auto prefers 888, then 565, then 555
		offered = (1u << Graphics::kHiResTargetClut8) | (1u << Graphics::kHiResTargetRgb565) | (1u << Graphics::kHiResTargetRgb555) |
		          (1u << Graphics::kHiResTargetRgb888);
		o = Graphics::HiResOptions();
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb888);
		offered &= ~(1u << Graphics::kHiResTargetRgb888);
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb565);
		offered &= ~(1u << Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb555);
		// an explicit 565 is honoured while it is offered, and an unoffered 888 falls to the same order
		offered |= 1u << Graphics::kHiResTargetRgb565;
		o.target = Graphics::kHiResTargetRgb565;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb565);
		o.target = Graphics::kHiResTargetRgb888;
		TS_ASSERT_EQUALS(Graphics::resolveRenderTarget(o, nullptr, true, offered, note), Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(note, "render_target rgb888 is not offered by this backend; using rgb565");
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetRgb555, Graphics::kHiResBlendAuto), Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetRgb555, Graphics::kHiResBlendOff), Graphics::TextCompose::kThreshold);
	}

	// The pixel format the engine asks the backend for, and that it maps back to the same target.
	void test_targetFormats() {
		const Graphics::HiResRenderTarget all[] = { Graphics::kHiResTargetClut8, Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb555,
		                                            Graphics::kHiResTargetRgb888 };
		for (uint i = 0; i < ARRAYSIZE(all); ++i)
			TS_ASSERT_EQUALS(Graphics::hiResTargetOf(Graphics::hiResTargetFormat(all[i])), all[i]);
		TS_ASSERT(Graphics::hiResTargetFormat(Graphics::kHiResTargetClut8).isCLUT8());
		const Graphics::PixelFormat f565 = Graphics::hiResTargetFormat(Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(f565.bytesPerPixel, 2);
		TS_ASSERT_EQUALS(f565.gBits(), 6);
		const Graphics::PixelFormat f555 = Graphics::hiResTargetFormat(Graphics::kHiResTargetRgb555);
		TS_ASSERT_EQUALS(f555.bytesPerPixel, 2);
		TS_ASSERT_EQUALS(f555.gBits(), 5);
		TS_ASSERT_EQUALS(f555.aBits(), 0);
		TS_ASSERT_EQUALS(Graphics::hiResTargetFormat(Graphics::kHiResTargetRgb888).bytesPerPixel, 4);
		TS_ASSERT(Graphics::hiResTargetFormat(Graphics::kHiResTargetAuto).isCLUT8());	// no RGB wanted: the 8-bit screen
	}

	void test_targetNames() {
		const Graphics::HiResRenderTarget all[] = { Graphics::kHiResTargetAuto, Graphics::kHiResTargetClut8, Graphics::kHiResTargetRgb565,
		                                            Graphics::kHiResTargetRgb888, Graphics::kHiResTargetRgb555 };
		for (uint i = 0; i < ARRAYSIZE(all); ++i) {
			Graphics::HiResRenderTarget t = Graphics::kHiResTargetRgb888;
			TS_ASSERT(Graphics::HiResFontMap::parseRenderTarget(Graphics::HiResFontMap::targetName(all[i]), t));
			TS_ASSERT_EQUALS(t, all[i]);
		}
		TS_ASSERT_EQUALS(Common::String(Graphics::HiResFontMap::targetName(Graphics::kHiResTargetRgb555)), "rgb555");
	}

	// What initGraphics() would give for a one-format list: the first BACKEND format the list has (engines/engine.cpp
	// findCompatibleFormat), CLUT8 when none.
	static Graphics::PixelFormat backendGives(const Common::List<Graphics::PixelFormat> &backend, const Common::List<Graphics::PixelFormat> &list) {
		for (Common::List<Graphics::PixelFormat>::const_iterator b = backend.begin(); b != backend.end(); ++b)
			for (Common::List<Graphics::PixelFormat>::const_iterator f = list.begin(); f != list.end(); ++f)
				if (*b == *f)
					return *b;
		return Graphics::PixelFormat::createFormatCLUT8();
	}

	// The format the driver asks initGraphics for: the target's first backend format, else the first of the other RGB targets
	// in the order 888, 565, 555; never CLUT8. And what a backend whose own favourite is another format then gives.
	void test_pickScreenFormat() {
		const Graphics::PixelFormat f565(2, 5, 6, 5, 0, 11, 5, 0, 0), f555(2, 5, 5, 5, 0, 10, 5, 0, 0), x1555(2, 5, 5, 5, 1, 10, 5, 0, 15),
		    rgba5551(2, 5, 5, 5, 1, 11, 6, 1, 0), bgr555(2, 5, 5, 5, 0, 0, 5, 10, 0), rgba8888(4, 8, 8, 8, 8, 24, 16, 8, 0),
		    argb8888(4, 8, 8, 8, 8, 16, 8, 0, 24), x8888(4, 8, 8, 8, 0, 16, 8, 0, 0), clut8 = Graphics::PixelFormat::createFormatCLUT8(),
		    rgba4444(2, 4, 4, 4, 4, 12, 8, 4, 0);
		Graphics::PixelFormat got;

		// SDL-like: nothing wider than the 16-bit screen it has, the hardware format first, CLUT8 last
		Common::List<Graphics::PixelFormat> sdl;
		sdl.push_back(f565);
		sdl.push_back(x1555);
		sdl.push_back(f555);
		sdl.push_back(rgba4444);
		sdl.push_back(bgr555);
		sdl.push_back(clut8);
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb565, sdl, got));
		TS_ASSERT_EQUALS(got, f565);
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb555, sdl, got));
		TS_ASSERT_EQUALS(got, x1555);	// the first 555 of the backend, not its favourite 565
		// ... and handing initGraphics that one format gives it, where a list with 565 in it would give the backend's 565
		Common::List<Graphics::PixelFormat> one;
		one.push_back(got);
		TS_ASSERT_EQUALS(backendGives(sdl, one), x1555);
		Common::List<Graphics::PixelFormat> two = one;
		two.push_back(f565);
		TS_ASSERT_EQUALS(backendGives(sdl, two), f565);
		// 888 asked and not offered: 565 (the order 888, 565, 555)
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb888, sdl, got));
		TS_ASSERT_EQUALS(got, f565);

		// OpenGL-like: 32-bit first, then 16-bit; RGBA5551 is its 15-bit format
		Common::List<Graphics::PixelFormat> gl;
		gl.push_back(rgba8888);
		gl.push_back(argb8888);
		gl.push_back(f565);
		gl.push_back(rgba5551);
		gl.push_back(rgba4444);
		gl.push_back(clut8);
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb565, gl, got));
		TS_ASSERT_EQUALS(got, f565);
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb888, gl, got));
		TS_ASSERT_EQUALS(got, rgba8888);
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb555, gl, got));
		TS_ASSERT_EQUALS(got, rgba5551);
		one.clear();
		one.push_back(got);
		TS_ASSERT_EQUALS(backendGives(gl, one), rgba5551);

		// 888 only (no 16-bit format): a 565 request falls to 888; 555 as well
		Common::List<Graphics::PixelFormat> x;
		x.push_back(x8888);
		x.push_back(clut8);
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb565, x, got));
		TS_ASSERT_EQUALS(got, x8888);
		// 555 only: 888 and 565 fall to it
		Common::List<Graphics::PixelFormat> only555;
		only555.push_back(bgr555);
		only555.push_back(clut8);
		TS_ASSERT(Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb888, only555, got));
		TS_ASSERT_EQUALS(got, bgr555);

		// no RGB format that is a target, CLUT8 only, an empty list: nothing (initGraphics gives CLUT8)
		Common::List<Graphics::PixelFormat> noRgb;
		noRgb.push_back(rgba4444);
		noRgb.push_back(clut8);
		TS_ASSERT(!Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb565, noRgb, got));
		Common::List<Graphics::PixelFormat> only;
		only.push_back(clut8);
		TS_ASSERT(!Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb565, only, got));
		TS_ASSERT(!Graphics::hiResPickScreenFormat(Graphics::kHiResTargetRgb888, Common::List<Graphics::PixelFormat>(), got));
		// CLUT8 and auto ask for no RGB screen
		TS_ASSERT(!Graphics::hiResPickScreenFormat(Graphics::kHiResTargetClut8, sdl, got));
		TS_ASSERT(!Graphics::hiResPickScreenFormat(Graphics::kHiResTargetAuto, sdl, got));
	}

	void test_modeAndScale() {
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetClut8, Graphics::kHiResBlendOn), Graphics::TextCompose::kStamp);
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetRgb565, Graphics::kHiResBlendOff), Graphics::TextCompose::kThreshold);
		TS_ASSERT_EQUALS(Graphics::composeMode(Graphics::kHiResTargetRgb888, Graphics::kHiResBlendAuto), Graphics::TextCompose::kBlend);
		Graphics::HiResOptions o;
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, nullptr, 320, 200), 2);
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, nullptr, 640, 480), 1);
		Graphics::HiResMap m;
		m.scale = 1;
		m.scaleSet = true;
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, &m, 320, 200), 1);
		o.scale = 2;
		TS_ASSERT_EQUALS(Graphics::resolveScale(o, &m, 320, 200), 2);
	}

	// The ConfMan path: transient keys are read, a bad one is a warning.
	void test_readThroughConfMan() {
		Common::install_null_g_system();
		ConfMan.set("hires_text_scale", " 2 ", Common::ConfigManager::kTransientDomain);
		ConfMan.set("hangul_break", "any", Common::ConfigManager::kTransientDomain);
		ConfMan.set("hires_text_blend", "sideways", Common::ConfigManager::kTransientDomain);
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = Graphics::readHiResOptions(w);
		TS_ASSERT_EQUALS(o.scale, 2);
		TS_ASSERT(o.hangulBreakSet && o.hangulBreak == Graphics::kHangulBreakAny);
		TS_ASSERT_EQUALS(w.size(), 1u);
		ConfMan.removeKey("hires_text_scale", Common::ConfigManager::kTransientDomain);
		ConfMan.removeKey("hangul_break", Common::ConfigManager::kTransientDomain);
		ConfMan.removeKey("hires_text_blend", Common::ConfigManager::kTransientDomain);
		Common::Array<Common::String> w2;
		const Graphics::HiResOptions d = Graphics::readHiResOptions(w2);
		TS_ASSERT(w2.empty());
		TS_ASSERT_EQUALS(d.scale, 0);
		Common::uninstall_null_g_system();
	}
};
