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

#ifndef AGS_ENGINE_GFX_HIRES_TWIN_H
#define AGS_ENGINE_GFX_HIRES_TWIN_H

#include "common/array.h"
#include "common/rect.h"
#include "ags/shared/gfx/bitmap.h"

namespace AGS3 {
namespace AGS {
namespace Engine {

/**
 * ScummVM (C23): a bitmap's N x twin (AGS_HIRES_TEXT_DESIGN.md section
 * 4.2): 32-bit ARGB, N x the bitmap's size, and the rects (bitmap pixels)
 * of the text records drawn into it at N x, for the invariant checks.
 */
class HiResTwin : public AGS::Shared::Bitmap {
public:
	HiResTwin(int width, int height) : AGS::Shared::Bitmap(width, height, 32) {}
	Common::Array<Common::Rect> Rects;
};

} // namespace Engine
} // namespace AGS
} // namespace AGS3

#endif
