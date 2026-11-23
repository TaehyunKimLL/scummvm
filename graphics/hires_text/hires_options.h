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

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/pixelformat.h"

namespace Graphics {

/** hires_text_font: which of a game's maps is used. */
enum HiResFontKind { kHiResFontAuto = 0, kHiResFontSvf, kHiResFontBitmap, kHiResFontTtf };

/**
 * The configuration keys of hi-res text read from P1 on (DESIGN section 7):
 * hires_text, hires_text_font, hires_text_map, hires_text_scale,
 * hires_text_blend, render_target, hangul_break, hires_text_log.
 */
struct HiResOptions {
	HiResOptions();

	bool enabled;                  ///< hires_text (true)
	HiResFontKind font;            ///< hires_text_font (auto)
	Common::String map;            ///< hires_text_map; set and empty: no map
	bool mapSet;
	int scale;                     ///< hires_text_scale: 0 auto, 1, 2
	HiResBlend blend;              ///< hires_text_blend (auto)
	HiResRenderTarget target;      ///< render_target (auto)
	HangulBreak hangulBreak;       ///< hangul_break (word)
	bool hangulBreakSet;
	bool log;                      ///< hires_text_log (false)
};

/** One key lookup: true with @p value when @p key is set. */
typedef bool (*HiResConfigGetFn)(const char *key, Common::String &value, void *ctx);

/** Reads every key through @p get; a bad value is a warning in @p warnings and keeps the default. */
HiResOptions parseHiResOptions(HiResConfigGetFn get, void *ctx, Common::Array<Common::String> &warnings);
/** parseHiResOptions() over ConfMan: the transient, active game and application domains. */
HiResOptions readHiResOptions(Common::Array<Common::String> &warnings);

const char *hiResFontKindName(HiResFontKind kind);
/** HIRESVF.MAP, HIRESBIT.MAP, HIRESTTF.MAP. */
const char *hiResFontKindMap(HiResFontKind kind);
/** Whether this build draws @p kind's faces (P1: no TrueType). */
bool hiResFontKindBuilt(HiResFontKind kind);
/** @p preferred first (unless auto), then SVF, bitmap, TrueType; kinds the build cannot draw left out. */
Common::Array<HiResFontKind> hiResFontOrder(HiResFontKind preferred);

/** Bit (1 << target) for clut8, rgb565, rgb555 and rgb888 when some format of @p formats is that target. */
uint32 hiResTargetsOffered(const Common::List<PixelFormat> &formats);
/** clut8 for CLUT8, rgb565 for 2 bytes 5-6-5, rgb555 for 2 bytes 5-5-5 (with or without the alpha bit), rgb888 for 4 bytes
 *  8-8-8; auto for anything else. */
HiResRenderTarget hiResTargetOf(const PixelFormat &format);

/**
 * The screen hi-res text draws on: render_target unless auto, else the map's
 * target unless auto, else rgb (rgb565, then rgb555, then rgb888: 16 and 15 bit
 * first) when a face has coverage, blending is not off and the backend offers
 * it, else clut8. A wanted target the backend does not offer falls back the
 * same way, ending in clut8, with a note.
 * @p offered: hiResTargetsOffered() bits.
 */
HiResRenderTarget resolveRenderTarget(const HiResOptions &options, const HiResMap *map, bool anyCoverage, uint32 offered,
                                      Common::String &note);
/** hires_text_blend unless auto, else the map's blend. */
HiResBlend resolveBlend(const HiResOptions &options, const HiResMap *map);
/** kStamp on clut8; kThreshold with blend off; kBlend otherwise. */
TextCompose::Mode composeMode(HiResRenderTarget target, HiResBlend blend);
/** hires_text_scale unless auto, else the map's scale unless auto, else 2 up to 320x240 and 1 above. */
int resolveScale(const HiResOptions &options, const HiResMap *map, int gameWidth, int gameHeight);

} // End of namespace Graphics

#endif
