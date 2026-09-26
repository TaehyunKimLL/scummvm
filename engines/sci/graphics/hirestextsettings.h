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

#ifndef SCI_GRAPHICS_HIRESTEXTSETTINGS_H
#define SCI_GRAPHICS_HIRESTEXTSETTINGS_H

#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "sci/graphics/textlatin.h"

namespace Sci {

/**
 * The hi-res text settings of one SCI font id, resolved from the ini keys and
 * hires_text.map. Engine-free: GfxCache reads ConfMan and the map file and
 * hands both in, so this is tested alone.
 */
struct FontSettings {
	FontSettings();

	Common::String facePath;         ///< main TrueType face; empty = none named
	int size;                        ///< face size in pixels
	LatinMode latin;                 ///< how ASCII is drawn
	Common::String latinFacePath;    ///< face for the Latin range; empty = the main face
	bool fullwidthSpace;             ///< kLatinFullwidth: remap ' ' to U+3000 too
	Graphics::HiResMetricsSource metrics; ///< kLatinProportional: whose advances
};

/**
 * The hi-res text ini keys, already validated by the caller. Each has a
 * has-flag: an unset key leaves the setting to the map.
 */
struct HiresTextOverrides {
	HiresTextOverrides();

	bool hasFont;                    ///< hires_text_font
	Common::String font;
	bool hasFontSize;                ///< hires_text_font_size
	int fontSize;
	bool hasLatin;                   ///< hires_text_latin
	LatinMode latin;
	bool hasLatinFont;               ///< hires_text_latin_font
	Common::String latinFont;
	bool hasLatinSpace;              ///< hires_text_latin_space
	bool latinFullwidthSpace;
	bool hasMetrics;                 ///< hires_text_metrics
	Graphics::HiResMetricsSource metrics;
};

/**
 * The settings for @p fontId.
 *
 * Precedence, per setting: ini > [font.N:<platform>] > [font.N] >
 * [latin:<platform>]/[latin] (or [hires] for face/size) > default.
 * The platform qualifier is applied when the map is parsed (GfxCache passes
 * the platform code as the qualifier), so @p map already holds the winner of
 * each qualified/bare pair.
 *
 * Defaults: size 16, latin off, space keep, metrics game, no face.
 * A face name resolves through [fonts]; a relative map path is taken against
 * @p gameDir. Ini paths are used as given, as GfxCache always has.
 *
 * @param map        the parsed hires_text.map
 * @param mapLoaded  false when there is no map (or it is out of scope):
 *                   @p map is then ignored entirely
 */
FontSettings resolveFontSettings(const Graphics::HiResTextConfig &map, bool mapLoaded, int fontId,
								 const HiresTextOverrides &ini, const Common::Path &gameDir);

} // End of namespace Sci

#endif
