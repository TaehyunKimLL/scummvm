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

#ifndef AGI_HIRESTEXT_H
#define AGI_HIRESTEXT_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/id_plan.h"

namespace Graphics {
class RangeRoutedGlyphSource;
class UnicodeGlyphSource;
struct HiResFaceEntry;
}

namespace Agi {

/**
 * The shared hi-res text module, as AGI uses it (phase 1 of the AGI Korean
 * design): the ini keys, the map (HIRESBIT/HIRESVF/HIRESTTF.MAP by
 * hires_text_font, or hires_text_map), the one [font] compiled into a plan,
 * its faces opened, and a 16x16 1 bpp glyph per code point for
 * GfxMgr::drawCharacterOnDisplay(). AGI paints every text cell's
 * background, so a glyph is a stencil of foreground over background: no
 * text plane, no blending (D1).
 */
class HiresText {
public:
	HiresText();
	~HiresText();

	/** The game's ini keys from ConfMan, then the map they pick. */
	bool load(const Common::String &domain, const Common::Path &gameDir);

	/** A given map with given ini overrides (tests, and load()). */
	bool loadMap(const Common::Path &mapPath, const Common::Path &gameDir, const Graphics::HiResIniOverrides &ini);

	bool isActive() const { return _source != nullptr; }
	bool hangulAny() const;

	/**
	 * The 16x16 glyph of @p cp, bit 15 of rows[y] the leftmost pixel, or
	 * false when AGI's own font draws it (the plan says original, or no
	 * face and no missing box has it). A narrower face glyph is centred.
	 */
	bool glyph(uint32 cp, uint16 rows[16]);

	/** cp has a real glyph: a face's own, or ASCII for AGI's font. */
	bool canDraw(uint32 cp);

private:
	Graphics::UnicodeGlyphSource *openFace(const Graphics::HiResFaceEntry &e);
	void clear();

	Graphics::HiResIniOverrides _ini;
	Graphics::HiResMap _map;
	Graphics::HiResIdPlan _plan;
	Graphics::RangeRoutedGlyphSource *_source;
};

} // End of namespace Agi

#endif
