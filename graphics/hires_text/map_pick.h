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
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"

namespace Graphics {

/**
 * Pick the map of a game folder by font kind (hires_text_font): every kind of
 * hiResFontOrder(@p ini.font) in turn, its file named by hires_text_map_<kind>
 * or by default (HIRESTTF.MAP, HIRESVF.MAP, HIRESBIT.MAP) and found by
 * findGameMapFile(). A kind is skipped - and the next one tried - when its map
 * is missing, does not load, or names a face that is not there (a font pack
 * not unzipped, say). The first kind left is the answer: @p mapPath and
 * @p kind set, true. False when none is left; the caller then falls back to
 * HIRESTXT.MAP and its own defaults.
 *
 * @p exists tells whether a resolved face file is there (tests pass their own).
 * @p notes gets one line per kind tried and why it was passed over, for the
 * caller to log (a warning when the user chose the kind, else debug).
 */
bool pickHiResMap(const HiResIniOverrides &ini, const Common::Path &gameDir,
				  const Common::Array<Common::String> &qualifiers, const HiResEngineKeys &keys,
				  Common::Path &mapPath, HiResFontKind &kind, Common::Array<Common::String> &notes,
				  FontFileExistsFn exists = fontFileExists);

} // End of namespace Graphics

#endif
