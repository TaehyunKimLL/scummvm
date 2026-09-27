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
#include "common/hash-str.h"
#include "common/path.h"
#include "common/str.h"
#include "common/str-enc.h"
#include "graphics/hires_text/text_layout.h"

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
 * The shape of the pen an outline is drawn with.
 *
 * Round and square are soft disks: the glyph's coverage is dilated by them,
 * so the outline keeps the antialiasing of the letterform. Legacy is the old
 * binary offset table of each mode (eight neighbours for an outline, the
 * lower-left weighted stroke for a stroke), for a map that wants exactly the
 * look the layer had before outlines were antialiased.
 */
enum HiResOutlineShape {
	kHiResOutlineRound = 0,
	kHiResOutlineSquare,
	kHiResOutlineLegacy
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
 * How a font's Latin (ASCII) range is drawn, for engines that route it to a
 * replacement face ([latin] mode=, [font.N] latin=). SCUMM does not read it.
 */
enum HiResLatinMode {
	kHiResLatinOff = 0,       ///< the game's own font draws Latin text
	kHiResLatinHalf,          ///< routed to the replacement face, at its own code point
	kHiResLatinFullwidth,     ///< remapped to the fullwidth-forms block
	kHiResLatinProportional   ///< routed to the replacement face, advance per [metrics]
};

/**
 * One [font.N] section: settings for one engine font id. Every field carries a
 * "set" flag, so an adapter can tell "the map said so" from "the map is
 * silent" and fall back to [latin]/[hires]. Face values are kept as written: a
 * [fonts] name or a path, see HiResTextConfig::resolveFace().
 */
struct HiResFontIdSettings {
	HiResFontIdSettings();

	Common::String face;              ///< face=
	bool faceSet;
	int size;                         ///< size=, pixels
	bool sizeSet;
	/// pixel=, the design size of a pixel font in pixels per em: the face
	/// is opened at the largest whole multiple of it the cell holds
	/// (TtfGlyphSource::pixelGridSize()), never shrunk by the probe fit.
	/// The cell is size= when that is set, else the engine's own.
	int pixel;
	bool pixelSet;
	HiResLatinMode latin;             ///< latin=
	bool latinSet;
	Common::String latinFont;         ///< latin_font= (or latin_face=)
	bool latinFontSet;
	bool latinFullwidthSpace;         ///< latin_space=fullwidth
	bool latinSpaceSet;
	HiResMetricsSource metrics;       ///< metrics=game|font
	bool metricsSet;

	/// face= as a fallback chain: "face=ko, ja, th", each entry a [fonts]
	/// name or a path, resolved against the map's folder. A single value is
	/// a chain of one (a path when it is no name, as before); in a list, an
	/// entry that is neither a name nor path-like (no '/', '\\' or '.') is
	/// dropped with a warning. face is the first surviving entry as written,
	/// so faceChain[0] is what resolving face gives.
	Common::Array<Common::Path> faceChain;
	Common::Path bitmap;              ///< bitmap=, an SVFN file relative to the map
	bool bitmapSet;
};

/* HangulBreak (word/any) is defined in text_layout.h, I18N_TEXT_DESIGN.md section 3.2. */

/**
 * The [layout] section: line-breaking conventions a translator may override.
 * Every field carries a "set" flag; unset fields keep the engine's default
 * (the values below are the design's BreakRules defaults).
 *
 *     [layout]
 *     hangul=word      ; word | any
 *     kinsoku=on       ; on | off
 *     thai=on          ; on | off (the syllable-ish fallback)
 */
struct HiResLayoutSettings {
	HiResLayoutSettings();

	HangulBreak hangul;
	bool hangulSet;
	bool kinsoku;
	bool kinsokuSet;
	bool thai;
	bool thaiSet;
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
	bool latinEnabled;              ///< enabled=, or implied by bitmap= (SCUMM's reading)
	bool latinEnabledSet;           ///< an explicit enabled= parsed
	bool latinEnabledValue;         ///< its literal value (bitmap= does not touch it)
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
	/// [hires] gamma=, in hundredths (2.2 is 220): a curve applied to a
	/// TrueType glyph's coverage when it is rasterised, 255*(c/255)^(1/g).
	/// Above 100 stems read heavier; 100 (the default) leaves every byte as
	/// FreeType drew it. Range 50..400.
	int coverageGamma;

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
	/// Outline radius in quarters of an output pixel (width=1.5 is 6);
	/// -1 = offset when that is set, else 0.75 of the scale.
	int shadowWidthQ;
	HiResOutlineShape shadowStyle;        ///< style=round|square|legacy
	bool shadowShiftSet;                  ///< shadow=dx,dy was given
	int shadowDx;                         ///< output pixels
	int shadowDy;
	byte shadowShiftColor;                ///< shadow_color; defaults to color
	bool shadowShiftColorSet;
	byte shadowAlpha;                     ///< shadow_alpha as 0..255; 255 = solid

	// --- translation bundle ---------------------------------------------
	Common::String translationName;

	// --- per-font settings (SCI; SCUMM ignores these) --------------------
	// All optional: each has a "set" flag and is untouched by a map that does
	// not name it. Face values are kept as written; see resolveFace().
	Common::String hiresFace;         ///< [hires] font=, the face a [font.N] names none
	bool hiresFaceSet;
	int hiresSize;                    ///< [hires] size=, pixels
	bool hiresSizeSet;
	int hiresPixel;                   ///< [hires] pixel=, see HiResFontIdSettings::pixel
	bool hiresPixelSet;
	/// [hires] face= as a fallback chain; see HiResFontIdSettings::faceChain.
	Common::Array<Common::Path> hiresFaceChain;
	HiResLatinMode latinMode;         ///< [latin] mode=
	bool latinModeSet;
	bool latinFullwidthSpace;         ///< [latin] space=fullwidth
	bool latinSpaceSet;
	Common::String latinFont;         ///< [latin] font= (or face=), as written
	bool latinFontSet;
	HiResMetricsSource latinMetrics;  ///< [latin] metrics=game|font
	bool latinMetricsSet;

	/// [fonts] as a whole: every face name -> the path as written (relative
	/// paths are left for the adapter to resolve). SCUMM's ttfPath[] roles are
	/// filled as before; this table is in addition to them.
	typedef Common::HashMap<Common::String, Common::String,
							Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> FaceTable;
	FaceTable fontFaces;

	/// [font.N] and [font.N:<qualifier>] sections, keyed by font id.
	Common::HashMap<int, HiResFontIdSettings> fontIds;

	/// The [font.N] settings for @p id, or nullptr if the map has none.
	const HiResFontIdSettings *fontIdSettings(int id) const;

	/// The [fonts] entry for a face name, or @p nameOrPath itself when it is
	/// not a name in the table (it is then a path).
	Common::String resolveFace(const Common::String &nameOrPath) const;

	/// [layout] hangul=, kinsoku=, thai=.
	HiResLayoutSettings layout;

	/// The warnings the last load raised about face chains, [font.N] bitmap=
	/// and [layout] (each also printed with warning()), one per cause.
	Common::Array<Common::String> mapWarnings;

	/// Adapter-defined line height key -> font role (no scaling in this parser).
	Common::HashMap<int, int> heightRoles;

	/// Role for a line box the map did not mention.
	int roleForHeight(int height) const;
};

/**
 * Reader for the ".map" font description file.
 *
 * A line starting with ';' or '#' is a comment. A ';' preceded by a space or
 * a tab ends a value and starts a comment ("color=0 ; DOS" is 0); a ';' with
 * anything else before it is part of the value ("single=my;font.fnt"), and a
 * value that is only a comment ("single= ; none") is empty. '#' never starts
 * an inline comment.
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

} // End of namespace Graphics

#endif
