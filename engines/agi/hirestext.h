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
#include "common/hashmap.h"
#include "common/noncopyable.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/map_pick.h"
#include "graphics/hires_text/text_plane.h"

namespace Agi {

/**
 * The hi-res text of a Korean AGI game (DESIGN.md section 5.1, AGI row): no
 * text plane on screen. AGI already draws text in its own 640x400 display
 * buffer (GfxMgr DISPLAY_UPSCALED_640x400, CLUT8, EGA indices), one 16x16
 * cell per character, and paints every cell's background. So a face glyph
 * is a 1 bpp stencil the display paints in the cell's foreground over its
 * background, with AGI's invert and greyed-menu transforms. The screen is
 * CLUT8: the map is read with its [font:clut8] sections, a 1 bpp face draws
 * as it is, an anti-aliased face is cut at coverage 128 (with one warning).
 *
 * Basic Latin (below U+0080) is always AGI's own font, doubled, as the
 * English game draws it, so an untranslated line looks exactly as upstream.
 */
class AgiHiResText : Common::NonCopyable {
public:
	/** The engine's map when the game has none (the shipped OFL face). */
	static const char *const kDefaultMap;
	enum { kCell = 16 };

	AgiHiResText();
	~AgiHiResText();

	/** ConfMan's keys, the disk; false (and inactive) when hires_text=false or no map and no face opens. */
	bool setup(const Common::Path &gameDir, const Common::String &langFolder, Common::Array<Common::String> &notes);
	/** setup() with files of the caller's own (tests). */
	bool setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
	           const Common::String &langFolder, Common::Array<Common::String> &notes);

	bool isActive() const { return _set != nullptr; }
	/** [layout] hangul=any (or hangul_break=any): a Hangul word may break between syllables. */
	bool hangulAny() const { return _hangulAny; }
	const Graphics::HiResPick &getPick() const { return _pick; }

	/**
	 * The 16x16 stencil of @p cp, bit 15 of rows[y] the leftmost pixel; false
	 * when AGI's own font draws it: below U+0080, or the map leaves it to the
	 * game (range=original, no face has it and the chain ends in original).
	 * A narrower glyph is centred, the faces' line centred in the cell (shift= moves it down).
	 */
	bool glyph(uint32 cp, uint16 rows[16]);

	/** How many times the anti-aliased-face warning was given (0 or 1). */
	uint coverageWarnings() const { return _coverageWarnings; }

private:
	void clear();

	Graphics::HiResFiles *_files;
	bool _ownFiles;
	Graphics::HiResFaceCache *_faces;
	Graphics::HiResPick _pick;
	Graphics::HiResFontSet *_set;
	Graphics::TextPlane _cell; ///< one 16x16 cell, scale 1: the stencil is read from it
	bool _hangulAny;
	uint _coverageWarnings;
	bool _log;                               ///< hires_text_log
	Common::HashMap<uint32, bool> _missLogged;
};

} // End of namespace Agi

#endif
