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

#include "common/hash-str.h"
#include "common/hashmap.h"

#include "graphics/hires_text/hires_options.h"

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
		TS_ASSERT(w.empty());
	}

	void test_badValues() {
		OptionKeys k;
		k["hires_text_scale"] = "3";
		k["render_target"] = "rgb555";
		Common::Array<Common::String> w;
		const Graphics::HiResOptions o = parseOptions(k, w);
		TS_ASSERT_EQUALS(o.scale, 0);
		TS_ASSERT_EQUALS(o.target, Graphics::kHiResTargetAuto);
		TS_ASSERT_EQUALS(w.size(), 2u);
		TS_ASSERT_EQUALS(w[0], "hires_text_scale '3' is not auto, 1 or 2; ignoring it");
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
};
