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

#ifndef SCUMM_TEXT_LOG_H
#define SCUMM_TEXT_LOG_H

// The wording of the text log (`textlog` on the debug socket, and the
// "SCUMM text:" debug line): which face draws a string and how it is
// decorated, as plain strings. Pure functions of values the renderer has
// already resolved, so the wording can be tested without an engine.
// Header only.

#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_renderer.h"

namespace Scumm {

/** "KP12AB34.SVF (54x54, 2bpp)". */
inline Common::String describeFace(const Common::String &name, int cellW, int cellH, int bpp) {
	return Common::String::format("%s (%dx%d, %dbpp)", name.c_str(), cellW, cellH, bpp);
}

inline const char *shadowModeName(Graphics::HiResShadowMode m) {
	switch (m) {
	case Graphics::kHiResShadowGame:
		return "game";
	case Graphics::kHiResShadowNone:
		return "none";
	case Graphics::kHiResShadowDrop:
		return "drop";
	case Graphics::kHiResShadowOutline:
		return "outline";
	case Graphics::kHiResShadowStroke:
		return "stroke";
	}
	return "?";
}

/** A quarter-pixel width as pixels: 6 -> "1.5", 4 -> "1", 3 -> "0.75". */
inline Common::String quarterPixels(int q) {
	Common::String s = Common::String::format("%d", q / 4);
	switch (q % 4) {
	case 1:
		s += ".25";
		break;
	case 2:
		s += ".5";
		break;
	case 3:
		s += ".75";
		break;
	}
	return s;
}

/**
 * The decoration a string is drawn with, in words.
 *
 * @p spec is the map's [shadow] with [shadow.N] over it (HiResMap::shadowFor()),
 * @p hasSpecN whether the map has a [shadow.N] for the charset, @p style what
 * ScummHiResText::glyphStyle() made of them and @p gameShadow / @p korPatch
 * the game's shadow byte and whether a Korean patch font set it. The geometry
 * is HiResGlyphRenderer::decorationFor()'s, the one the renderer draws.
 */
inline Common::String describeDecoration(const Graphics::HiResShadowSpec &spec, bool hasSpecN, int charsetId,
										 const Graphics::GlyphStyle &style, int gameShadow, bool korPatch) {
	const Graphics::GlyphDecoration d = Graphics::HiResGlyphRenderer::decorationFor(style);

	Common::String what;
	if (style.shadowMode == Graphics::kHiResShadowNone) {
		what = "none";
	} else {
		what = shadowModeName(style.shadowMode);
		if (d.outline) {
			// "outline width 1.5", "stroke outline width 1.5"
			if (style.shadowMode != Graphics::kHiResShadowOutline)
				what += " outline";
			what += Common::String::format(" width %s", quarterPixels(d.outlineQ).c_str());
		}
		if (d.shadow) {
			if (style.shadowMode == Graphics::kHiResShadowDrop && d.shadowDx == d.shadowDy && !style.shadowShiftSet)
				what += Common::String::format(" offset %d", d.shadowDx);
			else
				what += Common::String::format(" shadow (%d,%d)", d.shadowDx, d.shadowDy);
		}
		// Without color= the game's shadow colour is used, which is not
		// known here.
		if (spec.colorSet)
			what += Common::String::format(" color %d", spec.color);
		else
			what += " color game";
		if (d.shadow && style.shadowShiftColorSet)
			what += Common::String::format(" shadow color %d", style.shadowShiftColor);
	}

	Common::String from = hasSpecN ? Common::String::format("[shadow.%d]", charsetId) : Common::String("[shadow]");
	from += Common::String::format(" mode=%s", shadowModeName(spec.mode));
	if (spec.mode == Graphics::kHiResShadowGame) {
		from += Common::String::format(", game shadow byte %d", gameShadow);
		if (gameShadow == 0)
			from += korPatch ? ", Korean patch font" : ", no Korean patch font";
	}
	return what + " (from " + from + ")";
}

/** The decoration of a string drawn by the game's own font (no hi-res layer). */
inline Common::String describeGameDecoration(int gameShadow) {
	return Common::String::format("game's own (game shadow byte %d)", gameShadow);
}

/** One line for the debug log. */
inline Common::String textLogLine(uint32 loop, int charset, const Common::String &face,
								  const Common::String &deco, const Common::String &utf8) {
	return Common::String::format("loop %u, charset %d, %s, decoration %s, text '%s'", loop, charset,
								  face.c_str(), deco.c_str(), utf8.c_str());
}

} // End of namespace Scumm

#endif
