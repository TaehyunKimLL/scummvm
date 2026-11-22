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

#include "graphics/hires_text/font_map.h"

#include "common/stream.h"
#include "common/util.h"

namespace Graphics {

HiResFontScope::HiResFontScope()
	: faceSet(false), size(0), sizeSet(false), shift(0), shiftSet(false), cell(kHiResCellGame), cellSet(false),
	  align(kHiResAlignGame), alignSet(false), missing(0), missingSet(false), advance(kHiResAdvanceFont), advanceSet(false) {
}

BreakRules HiResLayoutSettings::apply(const BreakRules &rules) const {
	BreakRules r = rules;
	if (hangulSet)
		r.hangul = hangul;
	if (kinsokuSet)
		r.kinsoku = kinsoku;
	if (thaiSet)
		r.thaiFallback = thai;
	return r;
}

void HiResMap::clear() {
	version = 0;
	target = kHiResTargetAuto;
	targetSet = false;
	blend = kHiResBlendAuto;
	blendSet = false;
	scale = 0;
	scaleSet = false;
	layout = HiResLayoutSettings();
	faces.clear();
	font = HiResFontScope();
	fontIds.clear();
	loads.clear();
	preloads.clear();
	warnings.clear();
}

const HiResFontScope *HiResMap::fontIdScope(int id) const {
	Common::HashMap<int, HiResFontScope>::const_iterator it = fontIds.find(id);
	return it == fontIds.end() ? nullptr : &it->_value;
}

HiResLoadMode HiResMap::loadModeFor(const HiResFaceRef &face) const {
	return loads.getValOrDefault(face.name, kHiResLoadAuto);
}

namespace HiResFontMap {

const char *const kDataPrefix = "data:";
const char *targetQualifier(HiResRenderTarget target) {
	switch (target) {
	case kHiResTargetClut8:
		return "clut8";
	case kHiResTargetRgb565:
	case kHiResTargetRgb888:
		return "rgb";
	default:
		return "";
	}
}

bool parseRenderTarget(const Common::String &text, HiResRenderTarget &out) {
	static const char *const names[] = { "auto", "clut8", "rgb565", "rgb888" };
	for (int i = 0; i < 4; ++i) {
		if (text.equalsIgnoreCase(names[i])) {
			out = (HiResRenderTarget)i;
			return true;
		}
	}
	return false;
}

bool parseBlend(const Common::String &text, HiResBlend &out) {
	if (text.equalsIgnoreCase("auto"))
		out = kHiResBlendAuto;
	else if (text.equalsIgnoreCase("on"))
		out = kHiResBlendOn;
	else if (text.equalsIgnoreCase("off"))
		out = kHiResBlendOff;
	else
		return false;
	return true;
}

bool parseBool(const Common::String &text, bool &out) {
	if (text.equalsIgnoreCase("on")) {
		out = true;
		return true;
	}
	if (text.equalsIgnoreCase("off")) {
		out = false;
		return true;
	}
	return !text.empty() && Common::parseBool(text, out);
}

bool parseCodePoint(const Common::String &text, uint32 &out) {
	const char *p = text.c_str();
	if ((p[0] == 'u' || p[0] == 'U') && p[1] == '+')
		p += 2;
	else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
		p += 2;
	else
		return false;
	const size_t n = strlen(p);
	if (n < 1 || n > 6)
		return false;
	uint32 v = 0;
	for (; *p; ++p) {
		if (!Common::isXDigit(*p))
			return false;
		v = v * 16 + (uint32)(Common::isDigit(*p) ? *p - '0' : (*p | 0x20) - 'a' + 10);
	}
	if (v > 0x10FFFF)
		return false;
	out = v;
	return true;
}

HiResFaceRef resolveFace(const Common::String &value, const Common::Path &mapDir) {
	HiResFaceRef ref;
	ref.name = value;
	if (value.hasPrefix(kDataPrefix)) {
		ref.data = true;
		ref.path = Common::Path(value.c_str() + strlen(kDataPrefix), '/');
	} else if (!value.empty() && (value[0] == '/' || value[0] == '\\' || (value.size() > 2 && value[1] == ':'))) {
		ref.path = Common::Path(value, value.contains('\\') ? '\\' : '/');
	} else {
		ref.path = mapDir.join(Common::Path(value, '/'));
	}
	return ref;
}
} // End of namespace HiResFontMap

} // End of namespace Graphics
