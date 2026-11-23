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

#ifndef SCI_GRAPHICS_HIRESTEXT_H
#define SCI_GRAPHICS_HIRESTEXT_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/noncopyable.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/hires_files.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/map_pick.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"

#include "sci/graphics/scifont.h"

namespace Sci {

/**
 * The hi-res text of one SCI game (DESIGN.md section 5.1): the picked map,
 * its faces, one Graphics::HiResFontSet per SCI font id, and the 2x text
 * plane GfxScreen keeps in step and UpscaledGfxDriver composites.
 */
class SciHiResText : Common::NonCopyable {
public:
	/** The map the engine falls back to when the game has none: the shipped OFL face. */
	static const char *const kDefaultMap;
	enum { kScale = 2 };

	SciHiResText();
	~SciHiResText();

	/**
	 * Reads the hires_text_* keys, picks the game's map (language folder,
	 * game folder, else kDefaultMap), resolves the render target against the
	 * backend's @p offeredTargets (Graphics::hiResTargetsOffered()) and loads
	 * the map again with that target's qualifiers. False: no hi-res text
	 * (the game draws as upstream). Each step a user may wonder about leaves
	 * a line in @p notes.
	 */
	bool setup(const Common::Path &gameDir, const Common::String &langFolder, uint32 offeredTargets,
	           Common::Array<Common::String> &notes);
	/** setup() with files of the caller's own (tests). */
	bool setup(Graphics::HiResFiles *files, const Graphics::HiResOptions &options, const Common::Path &gameDir,
	           const Common::String &langFolder, uint32 offeredTargets, Common::Array<Common::String> &notes);

	bool isActive() const { return _active; }
	Graphics::HiResRenderTarget getTarget() const { return _target; }
	/** The screen format the target needs: CLUT8, or the RGB format the driver asks the backend for. */
	Graphics::PixelFormat getScreenFormat() const { return Graphics::hiResTargetFormat(_target); }
	/** How many times the engine has said that an anti-aliased face is drawn on the CLUT8 screen (once per run at most). */
	uint coverageOnClut8Warnings() const { return _coverageWarnings; }
	/**
	 * The screen the driver really got from the backend. When the text wanted RGB and the screen's target is another (a
	 * backend that gave another RGB format, or CLUT8 for an RGB request) the text switches to the screen's target: the map is loaded again
	 * with that target's sections, the font sets are replaced (the old ones are kept alive, the generation changes), a note says so (also in @p notes).
	 */
	void setScreenFormat(const Graphics::PixelFormat &format, Common::Array<Common::String> &notes);
	/** Changes whenever the font sets were replaced (setScreenFormat): fonts built over the old sets are stale. */
	uint getGeneration() const { return _generation; }
	bool wantsRgb() const { return _active && _target != Graphics::kHiResTargetClut8; }
	Graphics::TextCompose::Mode getComposeMode() const { return Graphics::composeMode(_target, _blend); }
	const Graphics::HiResPick &getPick() const { return _pick; }
	/** Line breaking: the map's [layout], then hangul_break. */
	const Graphics::BreakRules &getBreakRules() const { return _breakRules; }

	/** The plane for a @p lowW x @p lowH screen (created on the first call). */
	Graphics::TextPlane &getPlane() { return _plane; }
	bool createPlane(int lowW, int lowH) { return _plane.exists() || _plane.create(lowW, lowH, kScale); }

	/** The font set of SCI font @p fontId, built on first use; nullptr when the map leaves the id to the game's font. */
	const Graphics::HiResFontSet *getFontSet(int fontId);

private:
	bool anyCoverageFace(Common::Array<Common::String> &notes);
	/** Loads the pick's map again with the sections of _target ([font:clut8], [font:rgb]). */
	void loadTargetSections(Common::Array<Common::String> &notes);

	uint _coverageWarnings;
	uint _generation;
	Common::Array<Graphics::HiResFontSet *> _retiredSets; ///< replaced by setScreenFormat, kept for the fonts built over them
	bool _hangulBreakSet;
	Graphics::HangulBreak _hangulBreak;
	bool _active;
	Graphics::HiResFiles *_files;
	bool _ownFiles;
	Graphics::HiResFaceCache *_faces;
	Graphics::HiResPick _pick;
	Graphics::HiResRenderTarget _target;
	Graphics::HiResBlend _blend;
	Graphics::BreakRules _breakRules;
	Graphics::TextPlane _plane;
	Common::HashMap<int, Graphics::HiResFontSet *> _sets; ///< nullptr: the id is the game's font
};

/**
 * A SCI font whose code points the map routes to faces are drawn into the
 * text plane at SciHiResText::kScale; the rest (range ...=original) by the
 * game's own font. Widths are low-res pixels, rounded up.
 */
class GfxFontHiRes : public GfxFont {
public:
	/** Takes @p original (deleted with this font); @p set and @p plane belong to the SciHiResText. */
	GfxFontHiRes(GfxFont *original, const Graphics::HiResFontSet &set, Graphics::TextPlane &plane);
	~GfxFontHiRes() override;

	GuiResourceId getResourceId() override { return _original->getResourceId(); }
	byte getHeight() override { return _height; }
	byte getCharWidth(uint32 chr) override;
	void draw(uint32 chr, int16 top, int16 left, byte color, bool greyedOutput) override;

	GfxFont *getOriginal() const { return _original; }

private:
	bool isOriginal(uint32 chr) const;

	GfxFont *_original;
	const Graphics::HiResFontSet &_set;
	Graphics::TextPlane &_plane;
	byte _height;
	int _lineHeight; ///< hi-res px of the faces' ascent + descent
};

} // End of namespace Sci

#endif
