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

#ifndef GRAPHICS_HIRES_TEXT_ID_PLAN_H
#define GRAPHICS_HIRES_TEXT_ID_PLAN_H

#include "common/array.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/font_value.h"
#include "graphics/hires_text/hires_options.h"
#include "graphics/hires_text/unicode_ranges.h"

namespace Graphics {

/**
 * The per-id compiled hi-res text plan (design sections 5.3, 6.2, 6.5): for
 * one engine font id, every precedence chain the map, the ini and the
 * engine scope (design section 8) can produce is resolved once, up front,
 * into flat lookups with no precedence left to work out per character.
 */

/**
 * A face chain (design section 6.5 step 4): every `same` entry of the
 * source font value has already been expanded in place, so this never
 * itself contains a `kHiResFaceSame` entry.
 */
struct HiResFaceChain {
	HiResFaceChain() : endsInOriginal(false) {}

	Common::Array<HiResFaceEntry> faces;
	bool endsInOriginal;
};

/**
 * One `[glyphs]` targeted glyph (design section 6.7): draw @ref cp from
 * exactly @ref face (kind File, or Same meaning the id chain - the first
 * face of it with the glyph). Never part of a HiResFaceChain: a targeted
 * face answers only its own listed code points.
 */
struct HiResGlyphTarget {
	HiResFaceEntry face;
	uint32 cp;
};

/** What one `[glyphs]` lookup (HiResIdPlan::glyphFor) resolves to. */
enum HiResGlyphStep {
	kHiResGlyphStepGame = 0, ///< the game's own font draws the game code
	kHiResGlyphStepDraw      ///< draw `cp`: a real code point (design 6.5
	                         ///< step 3 on) or a virtual targeted-glyph one
	                         ///< (HiResIdPlan::target())
};

/**
 * One font id's fully compiled configuration: every precedence chain of
 * design sections 5.3, 6.2 and 6.5 resolved into flat, run-time lookups.
 * Built once per id by compileIdPlan(); an adapter may cache state keyed on
 * hash() instead of rebuilding it every draw.
 */
struct HiResIdPlan {
	HiResIdPlan();

	/// True when the id's own resolved face is exactly `original` (design
	/// section 6.5 step 3): either the ini set `hires_text_face=original`
	/// (every id, unconditionally - chainFor() then always returns nullptr),
	/// or the id's own `[font.N]`/`[font]` face resolved to a lone
	/// `original` (only `[font.N]`'s own range rules still apply, folded
	/// into faceRules/ruleChains below by compileIdPlan()).
	bool original;

	/// The id chain (design section 5.3): the ini `hires_text_face`
	/// (parsed), else `[font.N] face`, else `[font] face`, else empty.
	/// Never contains a `same` entry - there is no outer scope left to
	/// expand it into.
	HiResFaceChain idChain;

	/// Every distinct chain a `range.<spec>=` rule of any scope names,
	/// indexed by faceRules' compiled value.
	Common::Array<HiResFaceChain> ruleChains;

	HiResRangeTable faceRules, advanceRules, originRules;

	/// Every distinct `advance.<spec>=`/`origin.<spec>=` value, indexed by
	/// advanceRules'/originRules' compiled value.
	Common::Array<HiResAdvance> advanceValues;
	Common::Array<HiResOrigin> originValues;

	HiResAdvance advance;       ///< id-wide advance=; kHiResAdvanceEngine = none
	HiResAdvance forcedAdvance; ///< ini hires_text_advance; kHiResAdvanceEngine = none
	HiResOrigin origin;         ///< id-wide origin=; kHiResOriginGame doubles as "unset"

	uint32 missing; ///< resolved missing=; 0 = off

	int size; ///< resolved size=, px
	bool sizeSet;
	int pixel; ///< resolved pixel=, ppem; 0 = not a pixel face
	int shift; ///< resolved shift=, hi-res px

	HiResCellMode cell;
	HiResAlign align;
	HiResMirror mirror;
	bool mirrorSet;

	/// [glyphs.N] entries over [glyphs] (design section 6.7): game code ->
	/// rule, ranges already expanded into individual entries.
	HiResGlyphTable glyphs;
	/// Every targeted glyph named by @ref glyphs, in game-code order; the
	/// virtual code point `kHiResTargetBase + index` names one (see
	/// target()).
	Common::Array<HiResGlyphTarget> targets;

	/// The chain for @p cp (design sections 6.2 and 6.5 step 4), or nullptr
	/// when the game's own font draws it: the id is off (design 6.5 step
	/// 3), the winning chain is exactly `original`, nothing names a face at
	/// all, or @p cp is a virtual targeted-glyph code point (design 6.7:
	/// "the range table is never consulted for it").
	const HiResFaceChain *chainFor(uint32 cp) const;

	/// Design sections 6.2/6.3: forced (ini `hires_text_advance`) > range
	/// rule > id-wide `advance=` > the engine's own default
	/// (kHiResAdvanceEngine). A virtual targeted-glyph @p cp is decoded to
	/// the target's real code point first (design 6.7: "advance/origin
	/// lookups use the real cp").
	HiResAdvance advanceFor(uint32 cp) const;
	/// As advanceFor(), without a forced ini level (design has none for
	/// `origin`): range rule > id-wide `origin=` > kHiResOriginGame.
	HiResOrigin originFor(uint32 cp) const;

	/// Design section 6.5 step 2: `[glyphs.N:q]`/`[glyphs.N]`/`[glyphs:q]`/
	/// `[glyphs]`, first match (already merged into @ref glyphs by
	/// compileIdPlan()). @p code is the game's own character code (an SCI
	/// decoded code point, an SCUMM charset byte - design section 6.7's
	/// "game code key per engine"); @p decoded is what @p cp becomes when
	/// no rule overrides it. Returns kHiResGlyphStepGame for an `original`
	/// rule or one that names it (@p cp is left untouched: the caller keeps
	/// drawing @p code with the game's own font); otherwise
	/// kHiResGlyphStepDraw with @p cp set to a real code point (continue at
	/// design 6.5 step 3) or a virtual targeted-glyph one (see target()).
	HiResGlyphStep glyphFor(uint32 code, uint32 decoded, uint32 &cp) const;

	/// The targeted glyph a virtual code point (`cp >= kHiResTargetBase`,
	/// design 6.7's implementation contract) names, or nullptr for any
	/// other @p cp, or one this plan never produced.
	const HiResGlyphTarget *target(uint32 cp) const;

	/// FNV-1a over every field above (chains, tables, value arrays,
	/// missing, size, pixel, shift, cell, align, the glyph table and the
	/// targets), stable across equal input: an adapter may key a cache of
	/// anything built from this plan on it.
	uint32 hash() const;

	/// Game code -> index into @ref targets. Bookkeeping for glyphFor(),
	/// not part of the design's data model; rebuilt with @ref targets.
	Common::HashMap<uint32, uint32> targetIndexByCode;
};

/**
 * Compile id @p id's plan (design sections 5.3, 6.2, 6.5) from @p map's
 * `[font]`/`[font.id]` scopes, @p ini's overrides and @p engineScope (the
 * engine's own defaults, design section 8, consulted as the scope below
 * `[font]`). @p mapLoaded false (no map, or it was refused at load) makes
 * every map-sourced key behave as if the map were empty; @p ini and
 * @p engineScope still apply as usual. @p mapDir resolves a map-relative
 * path (a `[fonts]` name, or a path written in `range.*`/`[glyphs]`);
 * @p gameDir resolves the ini `hires_text_face`'s path entries (design
 * section 4). Warnings raised while resolving @p ini's face are appended to
 * @p warnings; every warning about the map's own keys was already raised
 * (and kept in HiResMap::warnings) when @p map was loaded, and is not
 * repeated here.
 */
HiResIdPlan compileIdPlan(const HiResMap &map, bool mapLoaded, int id, const HiResIniOverrides &ini,
						  const HiResFontScope &engineScope, const Common::Path &mapDir,
						  const Common::Path &gameDir, Common::Array<Common::String> &warnings);

/**
 * The two-phase render-target helpers (design section 7.1.1): phase 1 loads
 * the map with the engine qualifiers only (HiResMapLoadOptions target auto,
 * quiet true); these two answer the questions phase 1 needs before the
 * target is resolved and the map is loaded again for real.
 */

/**
 * true when @p face (already resolved to a path) is a 2 bpp or 8 bpp SVF, or
 * a TrueType face (design 7.1.1's "every face the phase-1 view names"): the
 * caller's own face-opening code decides what that means; this layer only
 * walks the view and calls @p fn once per distinct path.
 */
typedef bool (*HiResCoverageFn)(const Common::Path &face, void *ctx);

/**
 * Whether any face the phase-1 view @p map names has coverage (design
 * 7.1.1's `auto` rule): the ini `hires_text_face` (parsed against
 * @p map.faces when @p mapLoaded, paths resolved against @p gameDir),
 * [font] and every [font.N] `face=`/`range.<spec>=` value, and every
 * [glyphs]/[glyphs.N] targeted-glyph face (paths already resolved against
 * @p mapDir at load time) - each distinct path is asked of @p fn once, and
 * the first true wins. @p mapLoaded false makes every map-sourced part
 * behave as if the map were empty (as compileIdPlan() does), leaving only
 * the ini face to check.
 */
bool mapHasCoverage(const HiResMap &map, bool mapLoaded, const HiResIniOverrides &ini, const Common::Path &mapDir,
					const Common::Path &gameDir, HiResCoverageFn fn, void *ctx);

/**
 * Design section 7.1.1's phase-1 "wanted target": the ini `render_target`
 * unless it is `auto`; else the phase-1 @p phase1's `[render] target`
 * (@p mapLoaded and set, and not `auto`); else `resolveAutoTarget()` of
 * @p anyCoverage (design 7.1.1's mapHasCoverage()) and the blend - the ini
 * `hires_text_blend` if set, else @p mapLoaded's phase-1 `[render] blend`,
 * else `kHiResBlendAuto`. Never returns `kHiResTargetAuto`. @p explicitTarget
 * is set to true exactly when the ini or the map named a target (the first
 * two cases); false when the `auto` rule resolved it.
 */
HiResRenderTarget wantedRenderTarget(const HiResMap &phase1, bool mapLoaded, const HiResIniOverrides &ini,
									 bool anyCoverage, bool &explicitTarget);

} // End of namespace Graphics

#endif
