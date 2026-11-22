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

#include "graphics/hires_text/hires_options.h"

#include "common/config-manager.h"

namespace Graphics {

HiResOptions::HiResOptions()
	: enabled(true), font(kHiResFontAuto), mapSet(false), scale(0), blend(kHiResBlendAuto), target(kHiResTargetAuto),
	  hangulBreak(kHangulBreakWord), hangulBreakSet(false), log(false) {
}

namespace {

void bad(Common::Array<Common::String> &warnings, const char *key, const Common::String &value, const char *allowed) {
	warnings.push_back(Common::String::format("%s '%s' is not %s; ignoring it", key, value.c_str(), allowed));
}

bool confManGet(const char *key, Common::String &value, void *) {
	if (!ConfMan.hasKey(key))
		return false;
	value = ConfMan.get(key);
	return true;
}

} // End of anonymous namespace

HiResOptions parseHiResOptions(HiResConfigGetFn get, void *ctx, Common::Array<Common::String> &warnings) {
	HiResOptions o;
	Common::String v;
	bool b;
	if (get("hires_text", v, ctx)) {
		if (HiResFontMap::parseBool(v, b))
			o.enabled = b;
		else
			bad(warnings, "hires_text", v, "true or false");
	}
	if (get("hires_text_font", v, ctx)) {
		static const char *const names[] = { "auto", "svf", "bitmap", "ttf" };
		bool found = false;
		for (int i = 0; i < 4 && !found; ++i)
			if (v.equalsIgnoreCase(names[i])) {
				o.font = (HiResFontKind)i;
				found = true;
			}
		if (!found)
			bad(warnings, "hires_text_font", v, "auto, svf, bitmap or ttf");
	}
	if (get("hires_text_map", v, ctx)) {
		o.map = v;
		o.mapSet = true;
	}
	if (get("hires_text_scale", v, ctx)) {
		if (v.equalsIgnoreCase("auto"))
			o.scale = 0;
		else if (v == "1" || v == "2")
			o.scale = v[0] - '0';
		else
			bad(warnings, "hires_text_scale", v, "auto, 1 or 2");
	}
	if (get("hires_text_blend", v, ctx) && !HiResFontMap::parseBlend(v, o.blend))
		bad(warnings, "hires_text_blend", v, "auto, on or off");
	if (get("render_target", v, ctx) && !HiResFontMap::parseRenderTarget(v, o.target))
		bad(warnings, "render_target", v, "auto, clut8, rgb565 or rgb888");
	if (get("hangul_break", v, ctx)) {
		if (v.equalsIgnoreCase("word") || v.equalsIgnoreCase("any")) {
			o.hangulBreak = v.equalsIgnoreCase("any") ? kHangulBreakAny : kHangulBreakWord;
			o.hangulBreakSet = true;
		} else
			bad(warnings, "hangul_break", v, "word or any");
	}
	if (get("hires_text_log", v, ctx)) {
		if (HiResFontMap::parseBool(v, b))
			o.log = b;
		else
			bad(warnings, "hires_text_log", v, "true or false");
	}
	return o;
}

HiResOptions readHiResOptions(Common::Array<Common::String> &warnings) {
	return parseHiResOptions(confManGet, nullptr, warnings);
}

const char *hiResFontKindName(HiResFontKind kind) {
	static const char *const names[] = { "auto", "svf", "bitmap", "ttf" };
	return names[kind];
}

const char *hiResFontKindMap(HiResFontKind kind) {
	switch (kind) {
	case kHiResFontSvf:
		return "HIRESVF.MAP";
	case kHiResFontBitmap:
		return "HIRESBIT.MAP";
	case kHiResFontTtf:
		return "HIRESTTF.MAP";
	default:
		return "";
	}
}

bool hiResFontKindBuilt(HiResFontKind kind) {
	return kind == kHiResFontSvf || kind == kHiResFontBitmap;
}

Common::Array<HiResFontKind> hiResFontOrder(HiResFontKind preferred) {
	Common::Array<HiResFontKind> order;
	const HiResFontKind all[] = { preferred, kHiResFontSvf, kHiResFontBitmap, kHiResFontTtf };
	for (int i = 0; i < 4; ++i) {
		bool seen = false;
		for (uint j = 0; j < order.size(); ++j)
			seen = seen || order[j] == all[i];
		if (all[i] != kHiResFontAuto && !seen && hiResFontKindBuilt(all[i]))
			order.push_back(all[i]);
	}
	return order;
}

HiResRenderTarget hiResTargetOf(const PixelFormat &f) {
	if (f.isCLUT8())
		return kHiResTargetClut8;
	if (f.bytesPerPixel == 2 && f.rBits() == 5 && f.gBits() == 6 && f.bBits() == 5)
		return kHiResTargetRgb565;
	if (f.bytesPerPixel == 4 && f.rBits() == 8 && f.gBits() == 8 && f.bBits() == 8)
		return kHiResTargetRgb888;
	return kHiResTargetAuto;
}

uint32 hiResTargetsOffered(const Common::List<PixelFormat> &formats) {
	uint32 bits = 0;
	for (Common::List<PixelFormat>::const_iterator it = formats.begin(); it != formats.end(); ++it) {
		const HiResRenderTarget t = hiResTargetOf(*it);
		if (t != kHiResTargetAuto)
			bits |= 1u << t;
	}
	return bits;
}

HiResBlend resolveBlend(const HiResOptions &options, const HiResMap *map) {
	if (options.blend != kHiResBlendAuto)
		return options.blend;
	return map && map->blendSet ? map->blend : kHiResBlendAuto;
}

HiResRenderTarget resolveRenderTarget(const HiResOptions &options, const HiResMap *map, bool anyCoverage, uint32 offered,
                                      Common::String &note) {
	note.clear();
	HiResRenderTarget want = options.target;
	if (want == kHiResTargetAuto && map && map->targetSet)
		want = map->target;
	offered |= 1u << kHiResTargetClut8;
	if (want == kHiResTargetAuto) {
		if (!anyCoverage || resolveBlend(options, map) == kHiResBlendOff)
			return kHiResTargetClut8;
		if (offered & (1u << kHiResTargetRgb888))
			return kHiResTargetRgb888;
		return (offered & (1u << kHiResTargetRgb565)) ? kHiResTargetRgb565 : kHiResTargetClut8;
	}
	if (offered & (1u << want))
		return want;
	HiResRenderTarget got = kHiResTargetClut8;
	if (offered & (1u << kHiResTargetRgb888))
		got = kHiResTargetRgb888;
	else if (offered & (1u << kHiResTargetRgb565))
		got = kHiResTargetRgb565;
	note = Common::String::format("render_target %s is not offered by this backend; using %s",
	                              want == kHiResTargetRgb565 ? "rgb565" : "rgb888",
	                              got == kHiResTargetClut8 ? "clut8" : got == kHiResTargetRgb565 ? "rgb565" : "rgb888");
	return got;
}

TextCompose::Mode composeMode(HiResRenderTarget target, HiResBlend blend) {
	if (target == kHiResTargetClut8)
		return TextCompose::kStamp;
	return blend == kHiResBlendOff ? TextCompose::kThreshold : TextCompose::kBlend;
}

int resolveScale(const HiResOptions &options, const HiResMap *map, int gameWidth, int gameHeight) {
	if (options.scale)
		return options.scale;
	if (map && map->scaleSet && map->scale)
		return map->scale;
	return (gameWidth <= 320 && gameHeight <= 240) ? 2 : 1;
}

} // End of namespace Graphics
