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

#ifndef GRAPHICS_HIRES_TEXT_MAP_PICK_H
#define GRAPHICS_HIRES_TEXT_MAP_PICK_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"

namespace Graphics {

class HiResFiles;

/** The map a game draws hi-res text with. */
struct HiResPick {
	HiResPick() : kind(kHiResFontAuto) {}
	Common::Path path;
	HiResFontKind kind;  ///< auto: named by hires_text_map
	HiResMap map;
};

/**
 * Picks the map: hires_text_map when set (an empty value: no map), else the
 * first kind of hiResFontOrder(hires_text_font) whose map is in the game's
 * language folder or game folder, loads, and names at least one face that
 * locate() finds in each of its font values. Each kind passed over leaves a
 * line in @p notes. False when no map is left (or hires_text is off).
 */
bool pickHiResMap(const HiResOptions &options, const Common::Path &gameDir, const Common::String &langFolder,
                  const HiResMapQualifiers &qualifiers, HiResFiles &files, HiResPick &out,
                  Common::Array<Common::String> &notes);

/** Whether a value names no file (only original) or one @p files locates. */
bool hiResValueAvailable(const HiResFontValue &value, HiResFiles &files);

} // End of namespace Graphics

#endif
