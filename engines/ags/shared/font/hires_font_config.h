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


#ifndef AGS_SHARED_FONT_HIRES_FONT_CONFIG_H
#define AGS_SHARED_FONT_HIRES_FONT_CONFIG_H

#include "common/array.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/text_layout.h"

namespace AGS3 {

/** Where font N's glyphs come from, as HIRESTXT.MAP and the ini say. */
struct HiResFontPlan {
	enum Kind {
		kGame = 0,	///< the game's own agsfnt/extfnt renderers (no face named)
		kFaces		///< hires_text_face, [font.N] face= or [font] face=: a face chain
	};
	HiResFontPlan() : kind(kGame), size(0), gamma(100), pixel(0) {}

	Kind kind;
	/// kFaces: the chain, resolved. Each file is sniffed when it is opened:
	/// the SVFN magic means a baked bitmap font, anything else TrueType.
	Common::Array<Common::Path> faces;
	int size;							///< pixels; 0 = the game's own font height
	Common::String source;				///< for logs: which key chose this
	int gamma;							///< kFaces: [render] gamma=, hundredths; 100 = off
	int pixel;							///< kFaces: [font.N]/[font] pixel=, a pixel font's design size; 0 = none
};

/**
 * The plan font N's faces are opened by at @p scale x its size (N x
 * text). A pixel plan's first face opens at exactly @p scale times the ppem
 * it opened at in 1x (@p smallPixelPpem, its faceSize()), so its glyphs line
 * up with the N x pens: pixelGridSize(N * cell, D) is not always that (cell 15,
 * D 10: 30 against 2 x 10; a cell smaller than D: D against 2 x D). With no
 * 1x pixel face (@p smallPixelPpem 0, or no pixel=) the plan has no pixel
 * face either. Everything else is the plan as it is.
 */
HiResFontPlan scaledPlan(const HiResFontPlan &plan, int scale, int smallPixelPpem);

/**
 * AGS fonts from a version-2 HIRESTXT.MAP and the hi-res text ini keys
 * (docs/superpowers/specs/2026-09-30-hires-config-unify-design.md). The map
 * is the file hires_text_map names (relative: the game folder), else the
 * game folder's HIRESTXT.MAP; its sections are qualified by the game id
 * ([font.0:5daysastranger] before [font.0]) and by the render target of the
 * game's colour depth ([fonts:clut8] for an 8-bit game). hires_text=false
 * turns all of it off. Without a map and without hires_text_face every font
 * is the game's own.
 *
 * Font N's chain is the first of: hires_text_face (ini; relative paths are
 * the game folder's, names the map's [fonts]), [font.N] face=, [font] face=.
 * Size: hires_text_size, else [font.N] size=, else [font] size=, else the
 * game font's own height. AGS reads no range.*, advance, origin, missing,
 * [glyphs] or [render] target keys: the map loader warns about them.
 */
class HiResFontConfig {
public:
	HiResFontConfig();

	/** Forget the game's settings; the next load() reads them again. */
	void clear();
	/**
	 * Read the ini keys and the map of the active game for a game of
	 * @p gameColorDepth bits (8, 16, 32): the map's target-qualified
	 * sections are those of targetForColorDepth(). Read once; read again
	 * only when called with another depth.
	 */
	void load(int gameColorDepth);
	bool isLoaded() const { return _loaded; }

	/** Whether anything names a font at all. */
	bool active() const { return _active; }
	/** The plan for font N; kGame when nothing names it. */
	HiResFontPlan plan(int fontNumber) const;
	/** hires_text_blend, else the map's [render] blend=, else auto. */
	Graphics::HiResBlend blend() const;
	/** True the first time it is asked after a load: the one blend=on-on-8-bit warning. */
	bool takeBlendRefusalNotice() {
		const bool first = !_blendRefusalNoticed;
		_blendRefusalNoticed = true;
		return first;
	}
	/** The render target the map was read for (auto before load()). */
	Graphics::HiResRenderTarget target() const { return _target; }
	/** The shared layout rules: the design's defaults for AGS (hangul=word,
	 *  kinsoku on, Thai on), overridden by the map's [layout]. */
	Graphics::BreakRules breakRules() const;

	/**
	 * The N the ini or the map asks for: hires_text_scale, else the map's
	 * [render] scale=, else 1, within the shared limits (1..3, or the
	 * backend's fixed value; see scaleWarning()). 1 with hires_text=false.
	 * Not yet gated: see gateScale().
	 */
	int requestedScale() const { return _scale; }
	/** Why requestedScale() is not what the ini or the map asked; empty when it is. */
	const Common::String &scaleWarning() const { return _scaleWarning; }
	/**
	 * The gates on a requested N >= 2: a mapped font, a 16/32-bit game and
	 * a 32-bit screen format. Returns N, or 1 with the reason in `why` (for
	 * the one warning). N = 1 passes and leaves `why` alone.
	 */
	static int gateScale(int requested, bool fontsNamed, int gameColorDepth, bool has32BitFormat,
						 Common::String &why);

	/**
	 * The map file for these ini keys: none with hires_text=false or an
	 * empty hires_text_map (@p warning says so); hires_text_map resolved
	 * against @p gameDir ("data:" and absolute paths as they are); else
	 * HIRESTXT.MAP in @p gameDir, matched case-insensitively, when it exists.
	 */
	static Common::Path mapPathFor(const Graphics::HiResIniOverrides &ini, const Common::Path &gameDir,
								   Common::String &warning);

	/** The map target of a game of @p bits colour depth: 8 -> clut8, 16 (and 15) -> rgb565, else rgb888. */
	static Graphics::HiResRenderTarget targetForColorDepth(int bits);

	/**
	 * Take a map as load() read it (nullptr: no map; @p mapLoaded false: it
	 * was refused, and only its warnings are kept) and the ini's overrides.
	 * @p mapDir is the map's folder (its relative paths), @p gameDir the
	 * game folder (the ini's relative paths). For load() and the unit tests.
	 */
	void configure(const Graphics::HiResMap *map, bool mapLoaded, const Graphics::HiResIniOverrides &ini,
				   const Common::Path &mapDir, const Common::Path &gameDir);

	/** Every warning of the last configure(): the map's own, then those about the ini's face. */
	const Common::Array<Common::String> &warnings() const { return _warnings; }

	/** Code points of the loaded UTF-8 translation, sampled (coverage.h). */
	const Common::Array<uint32> &sample() const { return _sample; }
	void setSample(const Common::Array<uint32> &sample) { _sample = sample; }

private:
	/** The shared plan of font N (the id chain, size and pixel). */
	Graphics::HiResIdPlan compile(int fontNumber, Common::Array<Common::String> &warnings) const;

	bool _loaded;
	int _loadedDepth;			///< load()'s colour depth
	bool _active;
	bool _mapLoaded;
	Graphics::HiResMap _map;
	Common::Path _mapDir;		///< relative paths in the map resolve here
	Common::Path _gameDir;		///< relative paths in hires_text_face resolve here
	Graphics::HiResIniOverrides _ini;
	bool _iniFaceUsed;			///< hires_text_face names at least one face (it is the chain)
	Graphics::HiResRenderTarget _target;
	bool _blendRefusalNoticed;
	int _scale;
	Common::String _scaleWarning;
	Common::Array<Common::String> _warnings;
	Common::Array<uint32> _sample;
};

} // namespace AGS3

#endif
