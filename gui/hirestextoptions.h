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


#ifndef GUI_HIRESTEXTOPTIONS_H
#define GUI_HIRESTEXTOPTIONS_H

#include "common/array.h"
#include "common/str.h"
#include "common/ustr.h"
#include "graphics/hires_text/hires_options.h"

namespace GUI {

/**
 * The Graphics options' "Hi-res text screen" popup (design section 11.1):
 * its entries and the `render_target` key it reads and writes. Kept apart
 * from OptionsDialog so it can be tested without a GUI.
 */

/** One popup entry: a target, and whether the backend can give it. */
struct HiResTargetEntry {
	Graphics::HiResRenderTarget target;
	bool available;
};

/**
 * The popup's entries: Auto, then 8-bit / 16-bit / true colour as
 * @p offered has them (Graphics::hiResTargetsOffered()). A stored explicit
 * target (@p storedSet, @p stored) that is not offered is appended with
 * `available = false`, so the dialog never rewrites it silently. Empty (hide
 * the popup) when fewer than two targets are offered and no such stored
 * target is kept.
 */
Common::Array<HiResTargetEntry> renderTargetEntries(uint32 offered, bool storedSet, Graphics::HiResRenderTarget stored);

/** "Auto", "8-bit palette", "16-bit colour" or "True colour", plus " (not available here)" when unavailable. */
Common::U32String renderTargetLabel(const HiResTargetEntry &e);

/**
 * `render_target` of @p domain alone (no `[scummvm]` fallback). False when
 * the key is unset or its value is not a render target.
 */
bool readRenderTarget(const Common::String &domain, Graphics::HiResRenderTarget &t);

/**
 * Store @p t as `render_target` in @p domain (`auto` for Auto, as "Render
 * mode"'s `<default>` stores its code). True when the stored text changed.
 */
bool writeRenderTarget(const Common::String &domain, Graphics::HiResRenderTarget t);

} // End of namespace GUI

#endif
