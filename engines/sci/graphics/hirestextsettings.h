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

#ifndef SCI_GRAPHICS_HIRESTEXTSETTINGS_H
#define SCI_GRAPHICS_HIRESTEXTSETTINGS_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/id_plan.h"

namespace Sci {

/**
 * The hi-res text settings of one SCI font id, resolved from the ini keys
 * and hires_text.map into one compiled plan (design sections 5, 6, 8) plus
 * the geometry GfxCache reads directly. Engine-free: GfxCache reads ConfMan
 * and the map file and hands both in, so this is tested alone.
 */
struct FontSettings {
	FontSettings();

	/// The cell and face size when nothing sets them, hi-res px.
	static const int kDefaultCell = 16;

	/// The fully compiled plan (design sections 5, 6, 8): every id chain,
	/// range/advance/origin rule, `[glyphs]` remap and target this font id
	/// resolves to. GfxCache opens its faces and routes every character
	/// through it (Graphics::pickGlyph(), Graphics::RangeRoutedGlyphSource).
	Graphics::HiResIdPlan plan;

	/// plan.original with no id-wide range rules of its own: the resource
	/// font draws every character, not even the shared .uni bundle a
	/// faceless id otherwise falls back to. Distinct from a plan whose
	/// `[font.N]` face is `original` but that still names its own range
	/// rules (design 6.5 step 3): those still route some characters away
	/// from the resource font, so they are not "original" in this sense.
	bool original;
	/// plan.idChain's first face, native separators; empty = none.
	Common::String facePath;
	/// The rest of plan.idChain, as paths (faceChain[0] == facePath).
	Common::Array<Common::String> faceChain;
	int size;                        ///< resolved size=, px (the default when unset)
	/// The layout cell in hi-res px (plan.cell==glyph makes it @ref size).
	int cell;
	int baseline;                    ///< resolved shift=, hi-res px
	Graphics::HiResAlign align;      ///< resolved align=
	int pixel;                       ///< resolved pixel=; 0 = not a pixel face
};

/**
 * SCI's engine scope (design section 8, the defaults consulted below
 * `[font]`): `range.basic-latin=original` (the game's own resource font
 * draws ASCII unless a map rule says otherwise - today's behaviour),
 * `advance.basic-latin=game` (an id that does route ASCII to a face steps
 * it by the game font's own width unless the map asks for
 * `advance.basic-latin=font`).
 */
Graphics::HiResFontScope sciEngineScope();

/**
 * The settings for @p fontId: hires_text.map's `[font]`/`[font.N]` scopes
 * plus @p ini's overrides, compiled into one plan
 * (Graphics::compileIdPlan()) against sciEngineScope(), and unpacked into
 * the geometry GfxCache reads directly.
 *
 * @param map        the parsed hires_text.map
 * @param mapLoaded  false when there is no map (or it is out of scope):
 *                   @p map is then ignored entirely
 * @param mapDir     the directory holding the map file (design section 4:
 *                   what a `[fonts]` name or a path written in place of one
 *                   resolves against)
 * @param gameDir    the game's own folder (design section 4: what the ini
 *                   `hires_text_face`'s own path entries resolve against)
 */
FontSettings resolveFontSettings(const Graphics::HiResMap &map, bool mapLoaded, int fontId,
								 const Graphics::HiResIniOverrides &ini, const Common::Path &mapDir,
								 const Common::Path &gameDir);

/**
 * The key GfxCache shares a Unicode bundle (GfxFontUnicode) under: the main
 * face, its size, and the compiled plan's own hash
 * (Graphics::HiResIdPlan::hash() - every range/advance/origin/glyphs rule
 * that could change what the bundle draws). Two font ids with the same
 * face, size and plan share one bundle; nothing else about the map affects
 * what gets drawn.
 */
Common::String unicodeBundleKey(const Common::String &mainPath, int size, uint32 planHash);

/**
 * Whether @p plan names any face at all: the id chain, any rule chain
 * (design 6.2's `range.*` faces), or any `[glyphs]` target naming a file.
 * An id can have an empty id chain (no `face=` anywhere) while still
 * naming faces through `range.*`/`[glyphs]` alone (design 6.5 step 4,
 * design 6.7) - GfxCache::unicodeFaceFor() must still open them, not treat
 * the id as if it named nothing.
 */
bool planNamesAnyFace(const Graphics::HiResIdPlan &plan);

/**
 * One face a plan names, already opened (or not): what
 * checkPlanLoadWarnings() needs to run design 5.4's cell-height refusal and
 * design 6.4/6.7's missing=/targeted-glyph warnings, without depending on
 * GfxCache's own face cache - so it can be tested with a handful of fakes.
 */
struct FontIdFace {
	Common::String path;                   ///< resolved, as HiResFaceEntry::path.toString('/')
	Graphics::UnicodeGlyphSource *source;   ///< the opened source; nullptr = failed to open
	bool isSvf;                             ///< only an SVF's cell height is compared (design 5.4)
};

/**
 * design sections 5.4, 6.4 and 6.7's load-time checks for font id @p fontId's
 * @p plan, run once every face it names (the id chain, every rule chain and
 * every `[glyphs]` target) has been opened or found to fail - @p faces, one
 * entry per distinct path, in plan order.
 *
 * An SVF whose cell height differs from the id's first SVF is refused
 * (design 5.4): its path is added to @p excludedPaths, for the caller to
 * leave out of every chain and target that names it before building the
 * id's Unicode source, and warned about once. A `[glyphs]` target lacking
 * its own code point in the face it names (design 6.7), and a `missing=`
 * code point no face of the id's own chain(s) has (design 6.4), are each
 * warned about once too - an id with no chain of its own at all (a pure
 * `original`) is not checked at all, since it opens no face.
 *
 * Every text is the design 10.4 wording (the `HIRESTXT.MAP` prefix), pushed
 * onto @p map's own warnings (design section 10: "kept in HiResMap::warnings
 * for tests") and printed with warning() unless @p warnedOnceThisLoad
 * already has that exact text - the map's own causes (`missing=`, a target)
 * do not depend on which font id happens to reach them first, so the caller
 * shares one map and one dedup table across every font id it resolves in a
 * load.
 *
 * @param failedTargetCodes  every game code (design 6.7's key) whose target
 *                           failed the check, appended in plan order. Spec
 *                           6.7's fallback ("the game's font draws `c`") is
 *                           a load-time decision: the caller rewrites each
 *                           of these codes' `[glyphs]` rule to `original`
 *                           before building anything from the plan, so no
 *                           per-draw lookup is ever needed.
 */
void checkPlanLoadWarnings(int fontId, const Graphics::HiResIdPlan &plan, const Common::Array<FontIdFace> &faces,
						   Common::Array<Common::String> &excludedPaths, Common::Array<uint32> &failedTargetCodes,
						   Graphics::HiResMap &map, Common::HashMap<Common::String, bool> &warnedOnceThisLoad);

/**
 * design 6.7's load-time fallback: rewrite @p plan's `[glyphs]` rule for
 * each of @p failedTargetCodes to `original`, so
 * TextCompose::glyphCode()/goesToUnicodeFace() decline it like any other
 * `original` code from then on - the game's own font draws it, with no
 * per-draw target lookup. Called once, right after checkPlanLoadWarnings(),
 * before the plan is used to build anything (a bundle key, a routed source,
 * a GfxFontSet/GfxFontUnicodeAdapter).
 */
void declineFailedTargets(Graphics::HiResIdPlan &plan, const Common::Array<uint32> &failedTargetCodes);

/**
 * design 10's "once per cause per load": `hires_text_face=same` (no
 * meaning) or an unknown name, checked once against @p mapFaces - the
 * map's own `[fonts]` names when @p mapLoaded, empty otherwise - so a name
 * the map itself defines is never flagged "unknown face name" first.
 * Appends each warning verbatim (design 10.2's wording); never calls
 * warning() itself. GfxCache::resolveHiresText() calls this only after
 * loadMapFile() returns, passing @p mapLoaded/@p mapFaces from its result,
 * for exactly this reason - calling it before (or always with an empty
 * @p mapFaces) is the bug this guards against.
 */
void checkIniFaceWarnings(const Graphics::HiResIniOverrides &ini, bool mapLoaded,
						  const Graphics::HiResFaceNames &mapFaces, const Common::Path &mapDir,
						  const Common::Path &gameDir, Common::Array<Common::String> &warnings);

/**
 * The upscaled driver's screen request (design section 7.1.1, SCI):
 * whether it asks for RGB, and the render target it asks
 * Graphics::formatRequest() for. `target` is `kHiResTargetAuto` only when
 * the upstream request stands unchanged (the driver's own format choice);
 * `warning` is the one message the choice itself causes, or empty.
 */
struct SciRenderChoice {
	bool requestRGB;
	Graphics::HiResRenderTarget target;
	Common::String warning;
};

/**
 * The SCI screen choice. @p upstreamRgb is `rgb_rendering || palette_mods`;
 * @p target the phase-1 target (ini > `[scummvm]` > map, `auto` when
 * unset); @p anyCoverage and @p blend the phase-1 view's.
 *
 * - `auto` with @p upstreamRgb: `{true, auto}`, upstream's own request.
 * - `auto` otherwise: Graphics::resolveAutoTarget(), RGB unless clut8.
 * - `clut8`: no RGB; with @p upstreamRgb one warning that it wins.
 * - `rgb565`, `rgb888`: RGB, that target (SCI has no 16-bit compositor yet,
 *   so formatRequest() turns rgb565 into rgb888 with its note).
 */
SciRenderChoice chooseSciRender(bool upstreamRgb, Graphics::HiResRenderTarget target, bool anyCoverage,
								Graphics::HiResBlend blend);

/**
 * SCI's hi-res text scale (design section 7.4): always 2. The ini
 * `hires_text_scale`, else a loaded map's `[render] scale`, else 2, is
 * checked against the platform and SCI's own 2..2; @p warning gets at most
 * one message (`SCI draws hi-res text at 2x only; using 2`, or the DOS
 * backend's), empty when nothing asked for another scale.
 */
int sciHiresScale(const Graphics::HiResMap &map, bool mapLoaded, const Graphics::HiResIniOverrides &ini,
				  const Graphics::HiResScaleLimits &platform, Common::String &warning);

/** What phase 1 (design 7.1.1) tells the driver: see sciPhase1(). */
struct SciPhase1 {
	Graphics::HiResRenderTarget target; ///< ini unless auto, else the map's, else auto
	Graphics::HiResBlend blend;         ///< ini > map > auto
	bool anyCoverage;                   ///< some face of the phase-1 view has coverage
};

/**
 * Phase 1's answer for the driver. When hi-res text does not apply to the
 * game (@p applies false: not SCI16, no CJK code page, no UTF-8
 * translation) or `hires_text=false`, it is `{auto, auto, false}`: the
 * driver's request stays exactly upstream's. Otherwise the target is the
 * ini `render_target` unless it is `auto`, else the phase-1 map's
 * `[render] target` (only when @p mapLoaded) unless `auto`, else `auto`;
 * the blend is the ini's, else the map's, else `auto`; @p anyCoverage is
 * passed through (Graphics::mapHasCoverage() of the phase-1 view).
 */
SciPhase1 sciPhase1(bool applies, const Graphics::HiResIniOverrides &ini, const Graphics::HiResMap &map,
					bool mapLoaded, bool anyCoverage);

/** The phase-2 blend (design 7.2): the ini's, else a loaded map's `[render] blend`, else `auto`. */
Graphics::HiResBlend sciBlend(const Graphics::HiResIniOverrides &ini, const Graphics::HiResMap &map, bool mapLoaded);

/**
 * Whether a glyph of a @p faceBpp face is cut into a hard stencil (coverage
 * thresholded at 50%) before it reaches the text layer: exactly when the
 * face has coverage, the screen is RGB and Graphics::blendActive() says no.
 * On a CLUT8 screen the driver stamps at 50% itself, so nothing changes
 * there.
 */
bool sciThresholdCoverage(Graphics::HiResBlend blend, int faceBpp, bool screenIsClut8);

/**
 * The once-per-start warning when `blend=on` meets a CLUT8 screen with a
 * face that has coverage (design 7.2, 10.4; Graphics::blendRefusedOnClut8()),
 * or empty.
 */
Common::String sciBlendWarning(Graphics::HiResBlend blend, bool anyCoverage, bool screenIsClut8);

/**
 * `render_target=<wanted> is not available here; using <actual>` when an
 * explicit @p wanted (not `auto`) differs from the screen's @p actual
 * family; empty otherwise.
 */
Common::String sciTargetNote(Graphics::HiResRenderTarget wanted, Graphics::HiResRenderTarget actual);

} // End of namespace Sci

#endif
