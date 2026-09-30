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

#ifndef GRAPHICS_HIRES_TEXT_HIRES_OPTIONS_H
#define GRAPHICS_HIRES_TEXT_HIRES_OPTIONS_H

#include "common/array.h"
#include "common/list.h"
#include "common/str.h"
#include "graphics/pixelformat.h"

namespace Graphics {

/**
 * The shared render-target, blend and scale layer for hi-res text
 * configuration (design section 7), and the ini reader for all hi-res text
 * keys (design section 11).
 */

enum HiResRenderTarget { kHiResTargetAuto = 0, kHiResTargetClut8, kHiResTargetRgb565, kHiResTargetRgb888 };
enum HiResBlend { kHiResBlendAuto = 0, kHiResBlendOn, kHiResBlendOff };
enum HiResAdvance { kHiResAdvanceEngine = 0, kHiResAdvanceGame, kHiResAdvanceFont, kHiResAdvanceCell };
enum HiResOrigin { kHiResOriginGame = 0, kHiResOriginFace };

/** Parse `auto`/`clut8`/`rgb565`/`rgb888` (design section 7.1), case-insensitive. */
bool parseRenderTarget(const Common::String &text, HiResRenderTarget &out);

/** The ini/map spelling of @p target ("auto", "clut8", "rgb565", "rgb888"). */
const char *renderTargetName(HiResRenderTarget target);

/** Parse `auto`/`on`/`off` (design section 7.2), case-insensitive. */
bool parseBlend(const Common::String &text, HiResBlend &out);

/**
 * Parse `game`/`font`/`cell` (design section 8's `advance` key),
 * case-insensitive. Never "engine": that value is the internal per-engine
 * default, not a spelling any key can name.
 */
bool parseAdvance(const Common::String &text, HiResAdvance &out);

/** Parse `game`/`face` (design section 8's `origin` key), case-insensitive. */
bool parseOrigin(const Common::String &text, HiResOrigin &out);

/**
 * Whether @p format is the screen shape @p target names (design section
 * 7.1): `kHiResTargetClut8` is `format.isCLUT8()`; `kHiResTargetRgb565` is
 * a 2-byte-per-pixel 5-6-5 format; `kHiResTargetRgb888` is a 4-byte-per-pixel
 * 8-8-8 format, the fourth byte (alpha or otherwise) unconstrained.
 * `kHiResTargetAuto` never matches (it is resolved before this is called).
 */
bool formatMatchesTarget(const PixelFormat &format, HiResRenderTarget target);

/**
 * Build the list of formats to hand `initGraphics()` for @p want (already
 * resolved - never `kHiResTargetAuto`) out of the backend's @p supported
 * list (design section 7.1's fallback order): `want == kHiResTargetClut8`
 * asks for CLUT8 alone. Otherwise: the formats of @p supported matching
 * @p want (none at all when @p want is rgb565 and !@p engineCanRgb565),
 * then the formats matching the other RGB family - rgb888 tried before
 * rgb565, rgb565 only when @p engineCanRgb565 - then CLUT8 last.
 *
 * @p note is left empty when a format matching @p want was offered;
 * otherwise it gets the section 7.1 warning naming @p want and the family
 * the returned list starts with.
 */
Common::List<PixelFormat> formatRequest(HiResRenderTarget want, const Common::List<PixelFormat> &supported,
										 bool engineCanRgb565, Common::String &note);

/**
 * `auto`'s resolution (design section 7.1): `kHiResTargetClut8` when
 * @p blend is `kHiResBlendOff`, or @p blend is `kHiResBlendAuto` and
 * !@p anyCoverage; `kHiResTargetRgb888` otherwise.
 */
HiResRenderTarget resolveAutoTarget(bool anyCoverage, HiResBlend blend);

/**
 * Whether one glyph is blended rather than hard-stenciled (design section
 * 7.2): never on `blend=off`, never when the face has no coverage, and
 * never on a `clut8` screen (the `auto` hard stencil, and - until the
 * palette-matched anti-aliasing of a later task lands - the `on` case too);
 * otherwise (`auto` or `on`, coverage present, screen not `clut8`) blended.
 */
bool blendActive(HiResBlend blend, bool faceHasCoverage, bool screenIsClut8);

/**
 * The hi-res text surface scale limits (design section 7.4): the backend's
 * registered `hires_text_platform_scale` default as {n, n}, or {1, 3} when
 * no backend has registered it.
 */
struct HiResScaleLimits {
	int min;
	int max;
};
HiResScaleLimits hiResScaleLimits();

/**
 * Clamp @p requested into the engine's [@p engineMin, @p engineMax] and the
 * @p platform limits (design section 7.4), warning once when either
 * disagrees with @p requested: the platform limit is checked first (only
 * when it is a fixed value, i.e. @p platform.min == @p platform.max), then
 * the engine limit. On either violation the clamped result is
 * @p engineDefault (the ruling's per-engine default: SCUMM 2, SCI 2, AGS 1
 * - never hard-wired here, always the caller's own value, since it need not
 * equal @p engineMin). @p engineName names the engine in the engine-limit
 * message ("SCI", "SCUMM", ...). @p warning is left empty when
 * @p requested needed no clamping.
 */
int clampScale(int requested, int engineMin, int engineMax, const HiResScaleLimits &platform,
			   int engineDefault, const char *engineName, Common::String &warning);

/**
 * One load's resolved ini overrides (design section 11); a field's `xSet`
 * companion says whether the ini set it at all - an unset field keeps
 * whatever the map/engine default supplies.
 */
struct HiResIniOverrides {
	HiResIniOverrides();

	bool enabled;              ///< hires_text (default true)
	bool mapSet;
	Common::String map;        ///< hires_text_map (empty value allowed: "no map")
	bool faceSet;
	Common::String face;       ///< hires_text_face, unparsed (names need the map)
	bool sizeSet;
	int size;                  ///< hires_text_size, 8..64
	bool scaleSet;
	int scale;                 ///< hires_text_scale, 1..3
	bool blendSet;
	HiResBlend blend;          ///< hires_text_blend
	bool advanceSet;
	HiResAdvance advance;      ///< hires_text_advance
	bool targetSet;
	HiResRenderTarget target;  ///< render_target
	bool log;                  ///< hires_text_log (default false)
};

/**
 * One ini lookup: true and @p value set when @p key exists in the active
 * game domain, or - when @p globalFallback - in `[scummvm]`.
 */
typedef bool (*HiResIniGetFn)(const char *key, bool globalFallback, Common::String &value, void *ctx);

/**
 * Read all hi-res text ini keys (design section 11) through @p get, calling
 * it once per key with `globalFallback=true` for `render_target` and
 * `hires_text`, `false` for every other key. An invalid value is a warning
 * appended to @p warnings (`"<key> '<value>' is not <allowed>; ignoring
 * it"`) and leaves the corresponding field unset/at its default; this
 * function never calls warning() itself.
 */
HiResIniOverrides readHiResIni(HiResIniGetFn get, void *ctx, Common::Array<Common::String> &warnings);

/**
 * `readHiResIni`'s ConfMan-backed callback: `ConfMan.hasKey(key,
 * gameDomain)`, then - for global fallback - `ConfMan.hasKey(key,
 * Common::ConfigManager::kApplicationDomain)`.
 */
HiResIniOverrides readHiResIniFromConfMan(const Common::String &gameDomain, Common::Array<Common::String> &warnings);

} // End of namespace Graphics

#endif
