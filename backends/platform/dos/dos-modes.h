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
#include "graphics/pixelformat.h"

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
 * cheapest on the bus first, then CLUT8, which is always there (the
 * engine falls back to it). If @p allowTrueColor is false (dos_truecolor
 * off), only CLUT8 is reported.
 */
inline Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h, bool allowTrueColor) {
	static const Graphics::PixelFormat order[] = { rgb565(), xrgb1555(), xrgb8888() };
	Common::List<Graphics::PixelFormat> out;
	if (allowTrueColor) {
		for (uint i = 0; i < ARRAYSIZE(order); ++i)
			if (chooseMode(modes, w, h, order[i], false).index >= 0)
				out.push_back(order[i]);
	}
	out.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return out;
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

/** Compatibility overload: true-colour formats allowed. */
inline Common::List<Graphics::PixelFormat> supportedFormats(const Common::Array<VideoMode> &modes, uint w, uint h) {
	return supportedFormats(modes, w, h, true);
}

} // End of namespace DOS

#endif
