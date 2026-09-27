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
#include "graphics/hires_text/text_layout.h"

namespace AGS3 {

/** Where font N's glyphs come from, as hires_text.map and the ini say. */
struct HiResFontPlan {
	enum Kind {
		kGame = 0,	///< the game's own agsfnt/extfnt renderers (no map entry)
		kBitmap,	///< [font.N] bitmap=: an SVFN file
		kFaces		///< [font.N] face=, hires_text_font or [hires] face=: a TrueType chain
	};
	HiResFontPlan() : kind(kGame), size(0) {}

	Kind kind;
	Common::Array<Common::Path> faces;	///< kFaces: the chain, resolved
	Common::Path bitmap;				///< kBitmap: the SVFN file, resolved
	int size;							///< pixels; 0 = the game's own font height
	Common::String source;				///< for logs: which key chose this
};

/**
 * AGS fonts from hires_text.map (I18N_TEXT_DESIGN.md sections 4.4-4.6).
 * The map is read only when the game directory has hires_text.map or the
 * ini names one with hires_text_map; its sections are qualified by the game
 * id ([font.0:5daysastranger] before [font.0]). Without a map and without
 * the ini keys nothing here applies and every font is the game's own.
 *
 * For font N, most specific first:
 *   [font.N] bitmap=           an SVFN file (GlyphFontRenderer over SvfnGlyphSource)
 *   [font.N] face=a, b, c      a TrueType chain
 *   hires_text_font=           (ini) a face or chain for every font
 *   [hires] face=              the map's face or chain for every font
 *   [fonts] default=           the face when none of the above names one
 * Size: [font.N] size=, else hires_text_font_size (ini), else [hires] size=,
 * else the game font's own height.
 */
class HiResFontConfig {
public:
	HiResFontConfig();

	/** Forget the game's settings; the next load() reads them again. */
	void clear();
	/** Read the map and the ini keys of the active game once. */
	void load();
	bool isLoaded() const { return _loaded; }

	/** Whether anything names a font at all. */
	bool active() const { return _active; }
	/** The plan for font N; kGame when nothing names it. */
	HiResFontPlan plan(int fontNumber) const;
	/** [hires] alpha=, default true: coverage is blended into 16/32-bit targets. */
	bool alpha() const;
	/** The shared layout rules: the design's defaults for AGS (hangul=word,
	 *  kinsoku on, Thai on), overridden by the map's [layout]. */
	Graphics::BreakRules breakRules() const;

	/** Take a parsed map (nullptr: none) and the ini's face chain and size
	 *  as load() found them; for load() and the unit tests. */
	void configure(const Graphics::HiResTextConfig *map, const Common::Path &mapDir,
				   const Common::Array<Common::Path> &iniChain, int iniSize);

	/** Code points of the loaded UTF-8 translation, sampled (coverage.h). */
	const Common::Array<uint32> &sample() const { return _sample; }
	void setSample(const Common::Array<uint32> &sample) { _sample = sample; }

private:
	void updateActive();
	Common::Path defaultFace() const;

	bool _loaded;
	bool _active;
	bool _mapLoaded;
	Graphics::HiResTextConfig _map;
	Common::Path _mapDir;		///< relative paths in the map and in hires_text_font resolve here
	Common::Array<Common::Path> _iniChain;
	int _iniSize;
	Common::Array<uint32> _sample;
};

} // namespace AGS3

#endif
