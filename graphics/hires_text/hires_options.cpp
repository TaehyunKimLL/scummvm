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
#include "common/util.h"

namespace Graphics {

namespace {

/** Decimal integer, no sign, no surrounding whitespace (never
 *  ConfMan.getInt()'s error() on a bad ini value). */
bool parseDecimalInt(const Common::String &s, int &out) {
	if (s.empty())
		return false;
	long value = 0;
	for (uint i = 0; i < s.size(); ++i) {
		const char c = s[i];
		if (c < '0' || c > '9')
			return false;
		value = value * 10 + (c - '0');
		if (value > 1000000)
			return false;
	}
	out = (int)value;
	return true;
}

void addInvalidWarning(Common::Array<Common::String> &warnings, const char *key,
						const Common::String &value, const char *allowed) {
	warnings.push_back(Common::String::format("%s '%s' is not %s; ignoring it", key, value.c_str(), allowed));
}

} // End of anonymous namespace

bool parseRenderTarget(const Common::String &text, HiResRenderTarget &out) {
	if (text.equalsIgnoreCase("auto")) {
		out = kHiResTargetAuto;
		return true;
	}
	if (text.equalsIgnoreCase("clut8")) {
		out = kHiResTargetClut8;
		return true;
	}
	if (text.equalsIgnoreCase("rgb565")) {
		out = kHiResTargetRgb565;
		return true;
	}
	if (text.equalsIgnoreCase("rgb888")) {
		out = kHiResTargetRgb888;
		return true;
	}
	return false;
}

const char *renderTargetName(HiResRenderTarget target) {
	switch (target) {
	case kHiResTargetClut8:
		return "clut8";
	case kHiResTargetRgb565:
		return "rgb565";
	case kHiResTargetRgb888:
		return "rgb888";
	case kHiResTargetAuto:
	default:
		return "auto";
	}
}

bool parseBlend(const Common::String &text, HiResBlend &out) {
	if (text.equalsIgnoreCase("auto")) {
		out = kHiResBlendAuto;
		return true;
	}
	if (text.equalsIgnoreCase("on")) {
		out = kHiResBlendOn;
		return true;
	}
	if (text.equalsIgnoreCase("off")) {
		out = kHiResBlendOff;
		return true;
	}
	return false;
}

bool parseAdvance(const Common::String &text, HiResAdvance &out) {
	if (text.equalsIgnoreCase("game")) {
		out = kHiResAdvanceGame;
		return true;
	}
	if (text.equalsIgnoreCase("font")) {
		out = kHiResAdvanceFont;
		return true;
	}
	if (text.equalsIgnoreCase("cell")) {
		out = kHiResAdvanceCell;
		return true;
	}
	return false;
}

bool parseOrigin(const Common::String &text, HiResOrigin &out) {
	if (text.equalsIgnoreCase("game")) {
		out = kHiResOriginGame;
		return true;
	}
	if (text.equalsIgnoreCase("face")) {
		out = kHiResOriginFace;
		return true;
	}
	return false;
}

bool parseRenderTargetQualifier(const Common::String &q, HiResRenderTarget &t) {
	if (q.equalsIgnoreCase("clut8")) {
		t = kHiResTargetClut8;
		return true;
	}
	if (q.equalsIgnoreCase("rgb565")) {
		t = kHiResTargetRgb565;
		return true;
	}
	if (q.equalsIgnoreCase("rgb888")) {
		t = kHiResTargetRgb888;
		return true;
	}
	return false;
}

Common::Array<Common::String> qualifiersForTarget(const Common::Array<Common::String> &engineQualifiers,
												   HiResRenderTarget t) {
	Common::Array<Common::String> nonEmpty;
	for (uint i = 0; i < engineQualifiers.size(); ++i) {
		if (!engineQualifiers[i].empty())
			nonEmpty.push_back(engineQualifiers[i]);
	}

	if (t == kHiResTargetAuto)
		return nonEmpty;

	const char *const tName = renderTargetName(t);
	Common::Array<Common::String> result;
	for (uint i = 0; i < nonEmpty.size(); ++i)
		result.push_back(Common::String::format("%s:%s", nonEmpty[i].c_str(), tName));
	for (uint i = 0; i < nonEmpty.size(); ++i)
		result.push_back(nonEmpty[i]);
	result.push_back(tName);
	return result;
}

HiResRenderTarget targetOfFormat(const PixelFormat &f) {
	if (f.isCLUT8())
		return kHiResTargetClut8;
	if (f.bytesPerPixel == 2)
		return kHiResTargetRgb565;
	if (f.bytesPerPixel == 3 || f.bytesPerPixel == 4)
		return kHiResTargetRgb888;
	return kHiResTargetAuto; // never reached by a real screen format
}

bool formatMatchesTarget(const PixelFormat &format, HiResRenderTarget target) {
	switch (target) {
	case kHiResTargetClut8:
		return format.isCLUT8();
	case kHiResTargetRgb565:
		return format.bytesPerPixel == 2 && format.rBits() == 5 && format.gBits() == 6 && format.bBits() == 5;
	case kHiResTargetRgb888:
		return format.bytesPerPixel == 4 && format.rBits() == 8 && format.gBits() == 8 && format.bBits() == 8;
	case kHiResTargetAuto:
	default:
		return false;
	}
}

Common::List<PixelFormat> formatRequest(HiResRenderTarget want, const Common::List<PixelFormat> &supported,
										 bool engineCanRgb565, Common::String &note) {
	note.clear();
	Common::List<PixelFormat> result;

	if (want == kHiResTargetClut8) {
		for (Common::List<PixelFormat>::const_iterator it = supported.begin(); it != supported.end(); ++it) {
			if (it->isCLUT8()) {
				result.push_back(*it);
				break;
			}
		}
		return result;
	}

	bool wantSatisfied = false;
	if (want != kHiResTargetRgb565 || engineCanRgb565) {
		for (Common::List<PixelFormat>::const_iterator it = supported.begin(); it != supported.end(); ++it) {
			if (formatMatchesTarget(*it, want)) {
				result.push_back(*it);
				wantSatisfied = true;
			}
		}
	}

	const HiResRenderTarget other = (want == kHiResTargetRgb888) ? kHiResTargetRgb565 : kHiResTargetRgb888;
	if (other != kHiResTargetRgb565 || engineCanRgb565) {
		for (Common::List<PixelFormat>::const_iterator it = supported.begin(); it != supported.end(); ++it) {
			if (formatMatchesTarget(*it, other))
				result.push_back(*it);
		}
	}

	for (Common::List<PixelFormat>::const_iterator it = supported.begin(); it != supported.end(); ++it) {
		if (it->isCLUT8())
			result.push_back(*it);
	}

	if (!wantSatisfied) {
		const char *startsWith = "clut8";
		if (!result.empty()) {
			if (formatMatchesTarget(result.front(), kHiResTargetRgb888))
				startsWith = "rgb888";
			else if (formatMatchesTarget(result.front(), kHiResTargetRgb565))
				startsWith = "rgb565";
		}
		note = Common::String::format("render_target=%s is not available here; using %s",
									   renderTargetName(want), startsWith);
	}

	return result;
}

HiResRenderTarget predictedTarget(HiResRenderTarget want, const Common::List<PixelFormat> &supported,
								  bool engineCanRgb565) {
	Common::String note;
	const Common::List<PixelFormat> request = formatRequest(want, supported, engineCanRgb565, note);
	if (request.empty())
		return kHiResTargetAuto; // never reached: formatRequest() always ends in CLUT8
	return targetOfFormat(request.front());
}

HiResRenderTarget resolveAutoTarget(bool anyCoverage, HiResBlend blend) {
	if (blend == kHiResBlendOff)
		return kHiResTargetClut8;
	if (blend == kHiResBlendOn)
		return kHiResTargetRgb888;
	return anyCoverage ? kHiResTargetRgb888 : kHiResTargetClut8;
}

bool blendActive(HiResBlend blend, bool faceHasCoverage, bool screenIsClut8) {
	if (blend == kHiResBlendOff)
		return false;
	if (!faceHasCoverage)
		return false;
	// clut8: `auto` draws the hard stencil; `on` would be palette-matched
	// anti-aliasing, not implemented yet (a later task), so also false.
	if (screenIsClut8)
		return false;
	return true;
}

bool blendRefusedOnClut8(HiResBlend blend, bool screenIsClut8) {
	return blend == kHiResBlendOn && screenIsClut8;
}

HiResScaleLimits hiResScaleLimits() {
	// get(), not hasKey(): the backend sets this with registerDefault().
	const Common::String value = ConfMan.get("hires_text_platform_scale");
	if (value.empty())
		return { 1, 3 };
	int n;
	if (!parseDecimalInt(value, n))
		return { 1, 3 };
	return { n, n };
}

int clampScale(int requested, int engineMin, int engineMax, const HiResScaleLimits &platform,
			   int engineDefault, const char *engineName, Common::String &warning) {
	warning.clear();

	if (platform.min == platform.max) {
		const int p = platform.min;
		if (requested != p) {
			if (p == 2)
				warning = "the DOS backend runs hi-res text at 2x only";
			else
				warning = Common::String::format("the backend runs hi-res text at %dx only", p);
			// A backend cap wins; every registered cap lies inside each engine's range.
			return p;
		}
	}

	if (requested < engineMin || requested > engineMax) {
		if (engineMin == engineMax)
			warning = Common::String::format("%s draws hi-res text at %dx only", engineName, engineMin);
		else
			warning = Common::String::format("hires text scale %d is out of range %d..%d",
											  requested, engineMin, engineMax);
		return engineDefault;
	}

	return requested;
}

HiResIniOverrides::HiResIniOverrides() :
		enabled(true),
		mapSet(false),
		faceSet(false),
		sizeSet(false), size(0),
		scaleSet(false), scale(0),
		blendSet(false), blend(kHiResBlendAuto),
		advanceSet(false), advance(kHiResAdvanceEngine),
		targetSet(false), target(kHiResTargetAuto),
		log(false) {
}

HiResIniOverrides readHiResIni(HiResIniGetFn get, void *ctx, Common::Array<Common::String> &warnings) {
	HiResIniOverrides out;
	Common::String value;

	if (get("hires_text", true, value, ctx)) {
		bool b;
		if (Common::parseBool(value, b))
			out.enabled = b;
		else
			addInvalidWarning(warnings, "hires_text", value, "true or false");
	}

	if (get("hires_text_map", false, value, ctx)) {
		out.mapSet = true;
		out.map = value;
	}

	if (get("hires_text_face", false, value, ctx)) {
		out.faceSet = true;
		out.face = value;
	}

	if (get("hires_text_size", false, value, ctx)) {
		int n;
		if (parseDecimalInt(value, n) && n >= 8 && n <= 64) {
			out.sizeSet = true;
			out.size = n;
		} else {
			addInvalidWarning(warnings, "hires_text_size", value, "8..64");
		}
	}

	if (get("hires_text_scale", false, value, ctx)) {
		int n;
		if (parseDecimalInt(value, n) && n >= 1 && n <= 3) {
			out.scaleSet = true;
			out.scale = n;
		} else {
			addInvalidWarning(warnings, "hires_text_scale", value, "1, 2 or 3");
		}
	}

	if (get("hires_text_blend", false, value, ctx)) {
		HiResBlend b;
		if (parseBlend(value, b)) {
			out.blendSet = true;
			out.blend = b;
		} else {
			addInvalidWarning(warnings, "hires_text_blend", value, "auto, on or off");
		}
	}

	if (get("hires_text_advance", false, value, ctx)) {
		HiResAdvance a;
		if (parseAdvance(value, a)) {
			out.advanceSet = true;
			out.advance = a;
		} else {
			addInvalidWarning(warnings, "hires_text_advance", value, "game, font or cell");
		}
	}

	if (get("render_target", true, value, ctx)) {
		HiResRenderTarget t;
		if (parseRenderTarget(value, t)) {
			out.targetSet = true;
			out.target = t;
		} else {
			addInvalidWarning(warnings, "render_target", value, "auto, clut8, rgb565 or rgb888");
		}
	}

	if (get("hires_text_log", false, value, ctx)) {
		bool b;
		if (Common::parseBool(value, b))
			out.log = b;
		else
			addInvalidWarning(warnings, "hires_text_log", value, "true or false");
	}

	return out;
}

namespace {

bool confManGet(const char *key, bool globalFallback, Common::String &value, void *ctx) {
	const Common::String &gameDomain = *static_cast<const Common::String *>(ctx);
	if (ConfMan.hasKey(key, gameDomain)) {
		value = ConfMan.get(key, gameDomain);
		return true;
	}
	if (globalFallback && ConfMan.hasKey(key, Common::ConfigManager::kApplicationDomain)) {
		value = ConfMan.get(key, Common::ConfigManager::kApplicationDomain);
		return true;
	}
	return false;
}

} // End of anonymous namespace

HiResIniOverrides readHiResIniFromConfMan(const Common::String &gameDomain, Common::Array<Common::String> &warnings) {
	return readHiResIni(confManGet, const_cast<Common::String *>(&gameDomain), warnings);
}

} // End of namespace Graphics
