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

#include "sci/graphics/hirestextsettings.h"

#include "common/textconsole.h"

namespace Sci {

FontSettings::FontSettings()
	: size(16), latin(kLatinOff), fullwidthSpace(false), metrics(Graphics::kHiResMetricsGame) {
}

HiresTextOverrides::HiresTextOverrides()
	: hasFont(false), hasFontSize(false), fontSize(0), hasLatin(false), latin(kLatinOff),
	  hasLatinFont(false), hasLatinSpace(false), latinFullwidthSpace(false),
	  hasMetrics(false), metrics(Graphics::kHiResMetricsGame) {
}

namespace {

/// A face as the map wrote it - a [fonts] name or a path - as a file path.
/// Relative paths are taken against the game directory. Spelled with the
/// native separator: GfxCache parses every face path (ini or map) back with
/// Common::Path::kNativeSeparator, and keys its sources by the string, so a
/// map face and the same file named in the ini must come out identical.
Common::String mapFacePath(const Graphics::HiResTextConfig &map, const Common::String &nameOrPath,
						   const Common::Path &gameDir) {
	return Graphics::HiResFontMap::resolvePath(map.resolveFace(nameOrPath), gameDir)
		.toString(Common::Path::kNativeSeparator);
}

LatinMode toLatinMode(Graphics::HiResLatinMode mode) {
	switch (mode) {
	case Graphics::kHiResLatinHalf:
		return kLatinHalf;
	case Graphics::kHiResLatinFullwidth:
		return kLatinFullwidth;
	case Graphics::kHiResLatinProportional:
		return kLatinProportional;
	case Graphics::kHiResLatinOff:
	default:
		return kLatinOff;
	}
}

} // End of anonymous namespace

FontSettings resolveFontSettings(const Graphics::HiResTextConfig &map, bool mapLoaded, int fontId,
								 const HiresTextOverrides &ini, const Common::Path &gameDir) {
	FontSettings s;

	// With no map, only the ini keys (and the defaults) count. Otherwise the
	// map's [font.N] section - already the winner of [font.N:<platform>] over
	// [font.N], key by key, as the parser applied the platform qualifier.
	const Graphics::HiResFontIdSettings *font = mapLoaded ? map.fontIdSettings(fontId) : nullptr;

	// Face: ini > [font.N] face > [hires] font > none.
	if (ini.hasFont)
		s.facePath = ini.font;
	else if (font && font->faceSet)
		s.facePath = mapFacePath(map, font->face, gameDir);
	else if (mapLoaded && map.hiresFaceSet)
		s.facePath = mapFacePath(map, map.hiresFace, gameDir);

	// Size: ini > [font.N] size > [hires] size > 16.
	if (ini.hasFontSize)
		s.size = ini.fontSize;
	else if (font && font->sizeSet)
		s.size = font->size;
	else if (mapLoaded && map.hiresSizeSet)
		s.size = map.hiresSize;

	// Latin mode: ini > [font.N] latin > [latin] mode > [latin] enabled=true
	// (SCUMM's legacy switch: "the engine's current Latin behaviour", which
	// for SCI is proportional; its metrics default to game below) > off.
	// Only the literal enabled=true counts: SCUMM's parser also sets
	// latinEnabled for bitmap=, a path SCI does not have.
	if (ini.hasLatin)
		s.latin = ini.latin;
	else if (font && font->latinSet)
		s.latin = toLatinMode(font->latin);
	else if (mapLoaded && map.latinModeSet)
		s.latin = toLatinMode(map.latinMode);
	else if (mapLoaded && map.legacy.latinEnabledSet && map.legacy.latinEnabledValue)
		s.latin = kLatinProportional;

	// Latin face: ini > [font.N] latin_font > [latin] font > none (the main face).
	if (ini.hasLatinFont)
		s.latinFacePath = ini.latinFont;
	else if (font && font->latinFontSet)
		s.latinFacePath = mapFacePath(map, font->latinFont, gameDir);
	else if (mapLoaded && map.latinFontSet)
		s.latinFacePath = mapFacePath(map, map.latinFont, gameDir);

	// Space: ini > [font.N] latin_space > [latin] space > keep.
	if (ini.hasLatinSpace)
		s.fullwidthSpace = ini.latinFullwidthSpace;
	else if (font && font->latinSpaceSet)
		s.fullwidthSpace = font->latinFullwidthSpace;
	else if (mapLoaded && map.latinSpaceSet)
		s.fullwidthSpace = map.latinFullwidthSpace;

	// Metrics: ini > [font.N] metrics > [latin] metrics > game.
	if (ini.hasMetrics)
		s.metrics = ini.metrics;
	else if (font && font->metricsSet)
		s.metrics = font->metrics;
	else if (mapLoaded && map.latinMetricsSet)
		s.metrics = map.latinMetrics;

	return s;
}

int warnScummOnlyMapKeys(const Graphics::HiResTextConfig &map) {
	int warnings = 0;
	if (!map.legacy.latinBitmapName.empty()) {
		warning("hires_text.map: [latin] bitmap= is SCUMM-only, ignored");
		warnings++;
	}
	return warnings;
}

Common::String unicodeBundleKey(const Common::String &mainPath, int size,
								const Common::String &latinPath, LatinMode mode) {
	if (latinPath.empty())
		return Common::String::format("%s|%d||-", mainPath.c_str(), size);
	return Common::String::format("%s|%d|%s|%d", mainPath.c_str(), size, latinPath.c_str(), (int)mode);
}

} // End of namespace Sci
