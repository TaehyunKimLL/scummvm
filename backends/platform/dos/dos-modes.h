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

#ifndef BACKENDS_PLATFORM_DOS_DOS_MODES_H
#define BACKENDS_PLATFORM_DOS_DOS_MODES_H

#include "common/array.h"
#include "common/list.h"
#include "common/util.h"
#include "common/str.h"
#include "graphics/pixelformat.h"
#include "graphics/hires_text/hires_options.h"

namespace DOS {

/** One VESA mode as SDL3 lists it. */
struct VideoMode {
	uint16 w, h;
	Graphics::PixelFormat format;
};

inline Graphics::PixelFormat rgb565() { return Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0); }
inline Graphics::PixelFormat xrgb1555() { return Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0); }
inline Graphics::PixelFormat xrgb8888() { return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0); }

/** Index into @p modes of the mode that is exactly w x h in @p f, or -1. */
inline int findExactMode(const Common::Array<VideoMode> &modes, uint w, uint h, const Graphics::PixelFormat &f) {
	for (uint i = 0; i < modes.size(); ++i)
		if (modes[i].w == w && modes[i].h == h && modes[i].format == f)
			return (int)i;
	return -1;
}

/** Result of chooseMode(): which entry of @p modes to set, and whether it
 *  needs the 640x480 line-repeat fallback to show a w x h picture. */
struct ModeChoice {
	int index;
	bool lineRepeat;
};

/**
 * Pick the mode to set for a w x h game in format @p f.
 *
 * Unless @p forceFallback, an exact w x h match wins. Otherwise (or if
 * there is none), fall back to a mode of the same width and format whose
 * height is h * 6 / 5 -- the line-repeat picture size -- but only when
 * h % 5 == 0 (only then does line-repeat produce a whole number of extra
 * rows). If neither exists, {-1, false}.
 */
inline ModeChoice chooseMode(const Common::Array<VideoMode> &modes, uint w, uint h, const Graphics::PixelFormat &f, bool forceFallback) {
	if (!forceFallback) {
		int exact = findExactMode(modes, w, h, f);
		if (exact >= 0)
			return ModeChoice{ exact, false };
	}
	if (h % 5 == 0) {
		int fallback = findExactMode(modes, w, h * 6 / 5, f);
		if (fallback >= 0)
			return ModeChoice{ fallback, true };
	}
	return ModeChoice{ -1, false };
}

/**
 * What getSupportedFormats() reports for a w x h game: the true-colour
 * formats chooseMode() can set (exactly or via the line-repeat fallback),
 * then CLUT8, which is always there (the engine falls back to it).
 * kHiResTargetAuto reports every one, cheapest on the bus first (rgb565,
 * xrgb1555, xrgb8888). An explicit @p cap puts its own family first, then
 * the other true-colour family, the order an engine falls back in (rgb888,
 * rgb565, clut8): rgb565 gives 5-6-5 then 4-byte 8-8-8, rgb888 the
 * reverse, clut8 CLUT8 alone. 1-5-5-5 is never in either family.
 */
inline Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h,
															 Graphics::HiResRenderTarget cap = Graphics::kHiResTargetAuto) {
	static const Graphics::PixelFormat cheapest[] = { rgb565(), xrgb1555(), xrgb8888() };
	static const Graphics::PixelFormat want565[] = { rgb565(), xrgb8888() };
	static const Graphics::PixelFormat want888[] = { xrgb8888(), rgb565() };
	const Graphics::PixelFormat *order = cheapest;
	uint n = ARRAYSIZE(cheapest);
	if (cap == Graphics::kHiResTargetRgb565) {
		order = want565;
		n = ARRAYSIZE(want565);
	} else if (cap == Graphics::kHiResTargetRgb888) {
		order = want888;
		n = ARRAYSIZE(want888);
	} else if (cap == Graphics::kHiResTargetClut8) {
		n = 0;
	}
	Common::List<Graphics::PixelFormat> out;
	for (uint i = 0; i < n; ++i)
		if (chooseMode(modes, w, h, order[i], false).index >= 0)
			out.push_back(order[i]);
	out.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return out;
}

/**
 * The render_target cap getSupportedFormats() applies. With no game running
 * (@p gameActive false: the launcher and its options dialogs) there is none,
 * so the options can list every screen the hardware has. Otherwise @p value
 * (the ConfMan render_target, empty when unset) is parsed; a value that is
 * not auto, clut8, rgb565 or rgb888 gives no cap and sets @p invalid.
 */
inline Graphics::HiResRenderTarget renderTargetCap(bool gameActive, const Common::String &value, bool &invalid) {
	if (!gameActive || value.empty())
		return Graphics::kHiResTargetAuto;
	Graphics::HiResRenderTarget t;
	if (Graphics::parseRenderTarget(value, t))
		return t;
	invalid = true;
	return Graphics::kHiResTargetAuto;
}

/** The size getSupportedFormats() answers for; see formatsSize(). */
struct FormatsSize {
	uint w, h;
};

/**
 * getSupportedFormats() has no size argument, and SCI asks it before its
 * own initGraphics(640, 400) while the last initSize() is still the
 * launcher's 320x200 (base/main.cpp setupGraphics()). Answer for 640x400
 * (the hi-res size, exact or line-repeat fallback) unless the last size
 * asked for (@p lastW x @p lastH, 0x0 for none) is larger, then for that.
 */
inline FormatsSize formatsSize(uint lastW, uint lastH) {
	if (lastW > 640 || lastH > 400)
		return FormatsSize{ lastW, lastH };
	return FormatsSize{ 640, 400 };
}

} // End of namespace DOS

#endif
