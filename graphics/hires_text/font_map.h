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
#include "graphics/hires_text/font_face.h"
#include "graphics/hires_text/font_value.h"
#include "graphics/hires_text/glyph_mirror.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_ranges.h"

namespace Common {
class SearchSet;
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
 * What sets the layout cell of an SCI hi-res face ([font] cell=, [font.N]
 * cell=): the cell is the advance of a wide glyph (half of it for a narrow
 * one) and the height the font reports. SCUMM does not read it.
 */
enum HiResCellMode {
	/// The engine's own cell (16 hi-res px on SCI), whatever size= says:
	/// size= only sets how large the glyphs are drawn, and a glyph larger
	/// than the cell draws over its neighbours instead of being clipped.
	kHiResCellGame = 0,
	/// The cell is size= itself: a larger size= spaces the text wider and
	/// wraps it earlier. The behaviour before C41.
	kHiResCellGlyph
};

/**
 * Where an SCI hi-res face sits vertically ([font] align=, [font.N]
 * align=). SCUMM does not read it.
 */
enum HiResAlign {
	/// The face's baseline on the game font's baseline, so every glyph of
	/// the face stands where the game's own letters stood.
	kHiResAlignGame = 0,
	/// The probe fit's box centred on the cell: the placement before C41.
	kHiResAlignCell,
	/// The face's own line: its line top on the text line's top, its
	/// baseline the face's ascent below it, as FreeType lays a line out -
	/// independent of the game font.
	kHiResAlignFont
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

/// One game code's [glyphs] table: game code -> rule (ranges already expanded
/// into individual entries).
typedef Common::HashMap<uint32, HiResGlyphRule> HiResGlyphTable;

/**
 * One [font] or [font.N] scope, its `:q` qualifier already merged in
 * (design section 6.2.1). Every scalar field carries a "Set" companion, so a
 * consumer can tell "the map said so" from "silent, fall back a level".
 */
struct HiResFontScope {
	HiResFontScope();

	HiResFontValue face;          ///< face=
	bool faceSet;
	Common::String faceText;      ///< face= as written, kept for re-resolution
	int size;                     ///< size=, px
	bool sizeSet;
	int pixel;                    ///< pixel=, ppem
	bool pixelSet;
	int shift;                    ///< shift=, hi-res px, -32..32
	bool shiftSet;
	HiResCellMode cell;            ///< cell=game|glyph
	bool cellSet;
	HiResAlign align;              ///< align=game|cell|font
	bool alignSet;
	uint32 missing;                ///< missing=<cp>; missing=off is 0 with missingSet
	bool missingSet;
	HiResAdvance advance;           ///< advance= (id-wide)
	bool advanceSet;
	HiResOrigin origin;             ///< origin= (id-wide)
	bool originSet;
	HiResMirror mirror;             ///< mirror=
	bool mirrorSet;

	/// range.<spec>=: parallel arrays, rangeSpecs[i] <-> rangeValues[i].
	Common::Array<HiResRangeSpec> rangeSpecs;
	Common::Array<HiResFontValue> rangeValues;
	/// advance.<spec>=
	Common::Array<HiResRangeSpec> advanceSpecs;
	Common::Array<HiResAdvance> advanceValues;
	/// origin.<spec>=
	Common::Array<HiResRangeSpec> originSpecs;
	Common::Array<HiResOrigin> originValues;
};

/**
 * Bitmask of the map keys one engine reads (design section 3.2's "Read by"
 * columns): a bit the engine's HiResEngineKeys::honoured does not set means
 * the loader warns once per section/key the map sets it in (design 10.3),
 * though the key is still parsed.
 */
enum HiResKeyFlag {
	kHiResKeyTarget       = 1 << 0,  ///< [render] target=
	kHiResKeyBlend        = 1 << 1,  ///< [render] blend=
	kHiResKeyScale        = 1 << 2,  ///< [render] scale=
	kHiResKeyGamma        = 1 << 3,  ///< [render] gamma=
	kHiResKeyTextEncoding = 1 << 4,  ///< [text] encoding=
	kHiResKeyLayout       = 1 << 5,  ///< [layout] hangul=/kinsoku=/thai=
	kHiResKeyShift        = 1 << 6,  ///< [font]/[font.N] shift=
	kHiResKeyCell         = 1 << 7,  ///< [font]/[font.N] cell=
	kHiResKeyAlign        = 1 << 8,  ///< [font]/[font.N] align=
	kHiResKeyMissing      = 1 << 9,  ///< [font]/[font.N] missing=
	kHiResKeyAdvance      = 1 << 10, ///< [font]/[font.N] advance=, advance.<spec>=
	kHiResKeyOrigin       = 1 << 11, ///< [font]/[font.N] origin=, origin.<spec>=
	kHiResKeyRange        = 1 << 12, ///< [font]/[font.N] range.<spec>=
	kHiResKeyMirror       = 1 << 13, ///< [font]/[font.N] mirror=
	kHiResKeyGlyphs       = 1 << 14, ///< [glyphs]/[glyphs.N]
	kHiResKeyShadow       = 1 << 15  ///< [shadow]
};

/// One engine's name (for the "<engine> does not use ..." warning, design
/// 10.3) and the HiResKeyFlag bits it honours.
struct HiResEngineKeys {
	const char *engine;
	uint32 honoured;
};

extern const HiResEngineKeys kHiResKeysSci, kHiResKeysScumm, kHiResKeysAgs;

/**
 * A fully parsed version-2 map (design section 3): every `:q` qualifier
 * already merged into its scope, every removed/unknown/unhonoured key
 * reported in @ref warnings, nothing left for a caller to re-derive.
 */
struct HiResMap {
	HiResMap();

	/// Reset every field to its documented default, including @ref warnings.
	void clear();

	int version;

	// --- [render] --------------------------------------------------------
	HiResRenderTarget target;
	bool targetSet;
	HiResBlend blend;
	bool blendSet;
	int scale;
	bool scaleSet;
	int coverageGamma;             ///< hundredths; 100 = off

	// --- [text] ------------------------------------------------------------
	Common::CodePage encoding;
	bool encodingSet;

	// --- [layout] ----------------------------------------------------------
	HiResLayoutSettings layout;

	// --- [fonts], [font], [font.N] -----------------------------------------
	HiResFaceNames faces;                        ///< [fonts], qualified entries merged
	HiResFontScope font;                         ///< [font]
	Common::HashMap<int, HiResFontScope> fontIds; ///< [font.N]

	/// The [font.N] scope for @p id, or nullptr if the map has none.
	const HiResFontScope *fontIdScope(int id) const;

	// --- [glyphs], [glyphs.N] ------------------------------------------------
	HiResGlyphTable glyphs;
	Common::HashMap<int, HiResGlyphTable> glyphIds;

	// --- [shadow] ------------------------------------------------------------
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

	/// Every warning the last load raised (design section 10), in the order
	/// raised; each was also printed with warning(), unless the load was
	/// quiet (HiResMapLoadOptions::quiet).
	Common::Array<Common::String> warnings;

	/// The HiResMapLoadOptions::target of the load that produced this map:
	/// `kHiResTargetAuto` means the phase-1 view (design 7.1.1) - no
	/// target-qualified section was merged in.
	HiResRenderTarget loadedFor;

	/// Internal to the loader: true while this load's warnings are only
	/// collected in @ref warnings, not printed with warning() (design
	/// 3.4's "warnings once" - phase 1 is silent). Set from
	/// HiResMapLoadOptions::quiet, cleared by clear().
	bool quietLoad;
};

/**
 * One loadMap()/loadMapFile() call's options (design sections 3.4, 7.1.1).
 */
struct HiResMapLoadOptions {
	HiResMapLoadOptions();

	/// The resolved render target (never itself resolved by the loader):
	/// `kHiResTargetAuto` asks for the phase-1 view - target-qualified
	/// sections (`[S:t]`, `[S:e:t]`) are not merged in, only the engine's
	/// own and the bare ones. Any other value expands the qualifier list
	/// with qualifiersForTarget() before the section merge (design 3.4).
	HiResRenderTarget target;

	/// True for phase 1 (design 7.1.1): warnings are collected in
	/// HiResMap::warnings but not printed with warning(). Phase 2 (and
	/// every other caller) leaves this false so every warning is reported
	/// once, as before.
	bool quiet;
};

/**
 * Reader for the ".map" font description file.
 *
 * A line starting with ';' or '#' is a comment. A ';' preceded by a space or
 * a tab ends a value and starts a comment ("color=0 ; DOS" is 0); a ';' with
 * anything else before it is part of the value ("ko=my;font.ttf"), and a
 * value that is only a comment ("ko= ; none") is empty. '#' never starts
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
	 * Parse a version-2 map that has already been opened (design section 3).
	 *
	 * @param stream      the map text
	 * @param mapDir      the map's own folder: relative paths inside the map,
	 *                    and the folder named in the version-gate warning
	 * @param qualifiers  section suffixes to try, most specific first
	 * @param engine      the calling engine's name and honoured-key bitmask
	 *                    (design section 10.3); see kHiResKeysSci et al.
	 * @param out         cleared, then filled in; on a version-gate refusal
	 *                    (design 10.1) every field but @ref HiResMap::warnings
	 *                    stays at its cleared default
	 * @return false when the map is refused outright (design 10.1); every
	 *         other problem (10.2, 10.3) is a warning and this still returns
	 *         true
	 */
	static bool loadMap(Common::SeekableReadStream &stream, const Common::Path &mapDir,
						const Common::Array<Common::String> &qualifiers, const HiResEngineKeys &engine,
						HiResMap &out);

	/**
	 * loadMap() with explicit HiResMapLoadOptions (design 3.4, 7.1.1): @p out
	 * gains the render-target section qualifiers (expanded from
	 * @p qualifiers with qualifiersForTarget()) for every qualifiable
	 * section, and @ref HiResMap::loadedFor is set to @p options.target. The
	 * plain overload above means @p options's default (target auto, quiet
	 * false) - the phase-1 view, reported as loudly as before.
	 */
	static bool loadMap(Common::SeekableReadStream &stream, const Common::Path &mapDir,
						const Common::Array<Common::String> &qualifiers, const HiResEngineKeys &engine,
						HiResMap &out, const HiResMapLoadOptions &options);

	/**
	 * loadMap() on a file named by path; the file's own folder is @p mapDir.
	 * Behaves as loadMap() when the file cannot be opened at all (returns
	 * false, with one 10.1 warning naming @p mapPath).
	 */
	static bool loadMapFile(const Common::Path &mapPath, const Common::Array<Common::String> &qualifiers,
						const HiResEngineKeys &engine, HiResMap &out);

	/** loadMapFile() with explicit HiResMapLoadOptions; see loadMap(). */
	static bool loadMapFile(const Common::Path &mapPath, const Common::Array<Common::String> &qualifiers,
						const HiResEngineKeys &engine, HiResMap &out, const HiResMapLoadOptions &options);

	/**
	 * Resolve a path named inside a map file.
	 *
	 * Relative paths are taken against @p baseDir - normally the map's own
	 * folder - so a translation can ship its fonts next to the map and stay
	 * movable. Absolute paths are returned unchanged.
	 *
	 * A value starting with "data:" names a file shipped with ScummVM
	 * instead ("data:hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf"):
	 * it is looked up under dataRoots() by resolveDataPath(), never under
	 * @p baseDir.
	 */
	static Common::Path resolvePath(const Common::String &value, const Common::Path &baseDir);

	/// Prefix of a map value naming a file in ScummVM's data directories.
	static const char *const kDataPrefix;

	/// Whether @p value starts with kDataPrefix ("data:", case-sensitive).
	static bool isDataPath(const Common::String &value);

	/**
	 * Whether @p relative (the part after "data:") stays inside a data
	 * folder: not empty, not absolute (no leading separator or drive
	 * letter) and no ".." component.
	 */
	static bool isSafeDataRelative(const Common::String &relative);

	/**
	 * Find @p relative (the part after "data:") under each of @p roots in
	 * order and return the first existing file; a trailing "#<N>" face
	 * suffix is honoured as resolveFontFace() does and kept in the result.
	 * When no root holds it, or it fails isSafeDataRelative(), the value
	 * comes back as "data:<relative>" so the opener's warning names what
	 * the map wrote.
	 */
	static Common::Path resolveDataPath(const Common::String &relative,
										const Common::Array<Common::Path> &roots,
										FontFileExistsFn exists = fontFileExists);

	/**
	 * The folders "data:" searches, in order, each once: the extrapath
	 * given on the command line (transient domain), the active game's
	 * extrapath, the global (application) extrapath, the in-tree default
	 * a non-release build sets (session domain, dists/engine-data/), then
	 * the compiled-in DATA_PATH (where "make install" puts engine data),
	 * then searchSetRoots(SearchMan): the folders ScummVM finds its own
	 * engine data in on this platform (the current folder, which is the
	 * one holding scummvm.exe when Windows starts it; the app bundle's
	 * Resources on macOS; DATA_PATH; extrapaths; the game's folder).
	 */
	static Common::Array<Common::Path> dataRoots();

	/**
	 * The root folders of the file-system directories in @p set that hold
	 * at least one file, in the set's priority order, each once. Archives
	 * that are not folders (zips, Win32 resources) are skipped. The root
	 * is taken whatever the directory's search depth, so a data: path
	 * deeper than the set would search is still found under it.
	 */
	static Common::Array<Common::Path> searchSetRoots(const Common::SearchSet &set);

	/// Map a code page name ("cp932", "sjis", ...) to a CodePage.
	/// Returns Common::kCodePageInvalid when the name is not known.
	static Common::CodePage parseCodePage(const Common::String &name);
};

} // End of namespace Graphics

#endif
