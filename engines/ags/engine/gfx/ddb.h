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

//=============================================================================
//
// Driver-dependant bitmap interface.
//
// This interface describes an individual sprite object. The actual texture
// data (pixel data) may be shared among multiple DDBs, while DDB define
// additional settings telling how to present the texture: transform, colorize,
// and so on.
//=============================================================================

#ifndef AGS_ENGINE_GFX_DDB_H
#define AGS_ENGINE_GFX_DDB_H

#include "common/std/memory.h"

namespace AGS3 {
namespace AGS {
namespace Shared {
class Bitmap;
} // namespace Shared

namespace Engine {
class HiResTwin;

class IDriverDependantBitmap {
public:
	// Get an arbitrary sprite ID, returns UINT32_MAX if does not have one
	virtual uint32_t GetRefID() const = 0;

	virtual int  GetAlpha() const = 0;
	virtual void SetAlpha(int alpha) = 0;  // 0-255
	virtual void SetFlippedLeftRight(bool isFlipped) = 0;
	virtual void SetStretch(int width, int height, bool useResampler = true) = 0;
	virtual void SetLightLevel(int light_level) = 0;   // 0-255
	virtual void SetTint(int red, int green, int blue, int tintSaturation) = 0;  // 0-255

	virtual int GetWidth() const = 0;
	virtual int GetHeight() const = 0;
	virtual int GetColorDepth() const = 0;
	virtual bool MatchesFormat(AGS::Shared::Bitmap *other) const = 0;

	// ScummVM (C23): an N x 32-bit ARGB copy of this bitmap with its text
	// drawn at N x, for a driver that composes an N x frame
	// (AGS_HIRES_TEXT_DESIGN.md section 4.4). nullptr: none. Ignored by
	// default.
	virtual void SetHiResTwin(std::shared_ptr<HiResTwin> twin) {}

protected:
	IDriverDependantBitmap() {}
	virtual ~IDriverDependantBitmap() {}
};

} // namespace Engine
} // namespace AGS
} // namespace AGS3

#endif
