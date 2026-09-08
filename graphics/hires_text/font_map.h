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

#ifndef GRAPHICS_HIRES_TEXT_FONT_MAP_H
#define GRAPHICS_HIRES_TEXT_FONT_MAP_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/str.h"
#include "common/str-enc.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

/**
 * How a replacement glyph is outlined or shadowed.
 *
 * The engine's own shadow setting describes the game's bitmap font, which a
 * replacement font has no reason to match, so the map can override it.
 */
enum HiResShadowMode {
	kHiResShadowGame = 0,   ///< follow whatever the engine already does
	kHiResShadowNone,
	kHiResShadowDrop,
	kHiResShadowOutline,
	kHiResShadowStroke
};

/**
 * Which of the three font roles a line of text belongs to.
 *
 * Games change font in the middle of a scene - a title card is not drawn with
 * the same face as a line of dialogue - and the map ties each of the game's
 * own line heights to one of these.
 */
enum HiResFontRole {
	kHiResRoleDefault = 0,
	kHiResRoleBold,
	kHiResRoleTitle,
	kHiResRoleCount
};

/**
 * Where a glyph's advance width comes from.
 *
 * A game script decides where to break a line and how large to make a speech
 * bubble using the metrics of its own font. A replacement font whose glyphs
 * are wider will overflow those boxes, so the caller has to be able to ask for
 * the original layout even when the glyphs themselves are new.
 */
enum HiResMetricsSource {
	kHiResMetricsGame = 0,  ///< keep the game's advance; glyphs are fitted to it
	kHiResMetricsFont       ///< use the replacement font's own advance
};

/**
 * What to do with a character code the game repurposed.
 *
 * A game's own font is not a character set: LucasArts titles draw an ellipsis
 * at 0x5E, where Latin-1 has '^', and solid arrows at 0x5F and 0x7F, where it
 * has '_' and a control code. A replacement font baked from Latin-1 has a
 * caret, an underscore and nothing at those indices, so the picture the game
 * meant is lost - and because the hi-res layer reports the character as drawn,
 * the original is not drawn either.
 */
enum HiResGlyphAction {
	kHiResGlyphKeep = 0, ///< leave it to the game's own font
	kHiResGlyphRemap     ///< draw a different code point from the replacement
};

/** One entry of the [glyphs] table. */
struct HiResGlyphOverride {
	HiResGlyphOverride() : action(kHiResGlyphKeep), codepoint(0) {}
	HiResGlyphOverride(HiResGlyphAction a, uint32 cp) : action(a), codepoint(cp) {}

	HiResGlyphAction action;
	uint32 codepoint;    ///< for kHiResGlyphRemap; unused otherwise
};

/** Compatibility data for old maps; not generic character classification. */
struct LegacyFontMapOptions {
	bool latinEnabled;
	Common::Path latinTtfPath;
	Common::String latinBitmapName;
	HiResMetricsSource latinTtfMetrics;
	HiResMetricsSource latinBitmapMetrics;
};

/** Parsed map data. No engine state is read or modified by the parser.
 * Missing or invalid optional keys preserve the supplied values. Adapters apply
 * explicit user overrides after parsing, then resolve logical font sizes.
 */
struct HiResTextConfig {
	HiResTextConfig();

	/// Reset every field to the documented default.
	void clear();

	// --- geometry -------------------------------------------------------
	int scale;                  ///< text surface multiplier, 1..3. 1 = off
	bool alpha;                 ///< keep an 8bpp coverage surface for blending
	bool scaleFromMap;          ///< true if 'scale' came from the map, not the user
	bool alphaFromMap;

	// --- source text ----------------------------------------------------
	/// Code page of the game's own strings. Decoding happens in the engine
	/// adapter; the renderer itself only ever sees Unicode code points.
	Common::CodePage encoding;
	bool encodingFromMap;

	// --- bitmap fonts (the primary path; no FreeType needed) ------------
	Common::String bitmapPattern;   ///< legacy template (not a trusted printf format), e.g. "svfn%02d.fnt"
	Common::String bitmapSingle;    ///< single file used when no numbered one matches
	int bitmapGlyphs;               ///< glyph count of the game's own font, 0 = unset

	// --- TrueType fonts (optional convenience path) ---------------------
	Common::Path ttfPath[kHiResRoleCount];
	int ttfSize[kHiResRoleCount];         ///< unresolved size; 0 = auto-fit to the line box
	bool ttfSizeRelative[kHiResRoleCount]; ///< legacy pt means logical pixels, not typographic points
	int ttfSupersample[kHiResRoleCount];  ///< rasterise NxM then box-filter down
	bool ttfStringMode;                   ///< lay out whole runs, not single glyphs

	// Encoding byte width must never select a font in the generic renderer.
	HiResMetricsSource metricsSource; ///< [render] metrics=game|font, for all text
	LegacyFontMapOptions legacy;      ///< old [latin] keys; interpreted by adapters only

	/// [glyphs] exceptions, keyed by the game's own character code.
	///
	/// Games reuse punctuation slots for pictograms, and which slots differ
	/// between a game's own charsets: 0x5F is a left arrow in the dialogue
	/// font but a real underscore elsewhere. So the common table below is
	/// refined by per-scope ones, a scope being whatever the caller names in
	/// `scopes` - the SCUMM adapter passes "cs0", "cs1", ... for its charsets.
	Common::HashMap<uint32, HiResGlyphOverride> glyphOverrides;

	/// Per-scope refinements, in the order the caller listed its scopes.
	Common::Array<Common::HashMap<uint32, HiResGlyphOverride> > scopedGlyphOverrides;

	/// Look up an override, preferring @p scope's table over the common one.
	/// A negative or unknown scope consults only the common table.
	bool glyphOverride(uint32 code, HiResGlyphOverride &out, int scope = -1) const;

	// --- decoration -----------------------------------------------------
	HiResShadowMode shadowMode;
	int shadowOffset;                     ///< scaled pixels; -1 = follow the scale
	byte shadowColor;
	bool shadowColorSet;

	// --- translation bundle ---------------------------------------------
	Common::String translationName;

	/// Adapter-defined line height key -> font role (no scaling in this parser).
	Common::HashMap<int, int> heightRoles;

	/// Role for a line box the map did not mention.
	int roleForHeight(int height) const;
};

/**
 * Reader for the ".map" font description file.
 *
 * Comments occupy their own lines; inline comments are not supported.
 * The file is an INI. Sections may carry a qualifier after a colon, and the
 * reader tries the qualifiers a caller supplies before falling back to the
 * bare section:
 *
 *     [fonts:maniac]   most specific - matched first
 *     [fonts:v2]
 *     [fonts]          the fallback
 *
 * The qualifiers are opaque strings here. An engine passes whatever it uses to
 * tell its games apart, so one map file can carry the settings for several
 * games without them stepping on each other, and nothing about the engine's
 * naming leaks into this layer.
 */
class HiResFontMap {
public:
	/**
	 * Parse a map file.
	 *
	 * @param mapPath     the file to read
	 * @param qualifiers  section suffixes to try, most specific first
	 * @param out         filled in on success; untouched fields keep their value
	 * @return false if the file could not be read or parsed
	 */
	static bool load(const Common::Path &mapPath,
					 const Common::Array<Common::String> &qualifiers,
					 HiResTextConfig &out,
					 const Common::Array<Common::String> *scopes = nullptr);

	/**
	 * Parse a map that has already been opened.
	 *
	 * @param stream      the map text
	 * @param baseDir     what relative paths inside the map are taken against
	 * @param qualifiers  section suffixes to try, most specific first
	 * @param out         filled in on success; untouched fields keep their value
	 * @return false if the text could not be parsed
	 */
	static bool loadFromStream(Common::SeekableReadStream &stream,
							   const Common::Path &baseDir,
							   const Common::Array<Common::String> &qualifiers,
							   HiResTextConfig &out,
							   const Common::Array<Common::String> *scopes = nullptr);

	/**
	 * Resolve a path named inside a map file.
	 *
	 * Relative paths are taken against @p baseDir - normally the map's own
	 * folder - so a translation can ship its fonts next to the map and stay
	 * movable. Absolute paths are returned unchanged.
	 */
	static Common::Path resolvePath(const Common::String &value, const Common::Path &baseDir);

	/// Map a code page name ("cp932", "sjis", ...) to a CodePage.
	/// Returns Common::kCodePageInvalid when the name is not known.
	static Common::CodePage parseCodePage(const Common::String &name);

	/// Map a role name ("title", "bold", anything else) to a HiResFontRole.
	static int parseRole(const Common::String &name);
};

/**
 * Whether a set of replacement font cells fits a scale over the game's cell.
 *
 * The whole-multiple policy, in one place. A text surface can only be
 * enlarged by an integer, so a replacement lands on the original's grid only
 * when some font in the set is exactly the game's own cell times the scale. A
 * font baked at 1.8x has no scale that draws it correctly; rounding it to 2
 * would stretch every glyph by a ninth, and a set too large for the line box
 * it is given has the bottom of every glyph cut off.
 *
 * "Some font", not "the smallest": a set holds one font per charset at that
 * charset's own cell, and only one of those cells is known to the caller.
 * Dividing the smallest cell by that one height pairs a font with the wrong
 * charset and refuses a set whose every charset is exactly 2x.
 *
 * "Exactly", not "no larger than": a font smaller than the box sits in it
 * with a gap underneath, which is a different fault.
 *
 * @param cells           the set's cell heights, in scaled pixels
 * @param count           how many of them
 * @param gameCellHeight  the game's own cell, in game pixels; 0 when unknown
 * @param scale           the multiplier to test
 * @return false for an empty set, an unknown game cell, or no match
 */
bool hiResCellsFitScale(const int *cells, int count,
						int gameCellHeight, int scale);

} // End of namespace Graphics

#endif
