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
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/path.h"
#include "common/str.h"

#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_ranges.h"

namespace Common {
class SeekableReadStream;
}

namespace Graphics {

enum HiResRenderTarget { kHiResTargetAuto = 0, kHiResTargetClut8, kHiResTargetRgb565, kHiResTargetRgb888, kHiResTargetRgb555 };
enum HiResBlend { kHiResBlendAuto = 0, kHiResBlendOn, kHiResBlendOff };
/** advance=: the face's own advance, the game font's, or the face's cell width. */
enum HiResAdvance { kHiResAdvanceFont = 0, kHiResAdvanceGame, kHiResAdvanceCell };
/** cell=: the layout cell is the engine's own, or the face's size= (read by the engine). */
enum HiResCellMode { kHiResCellGame = 0, kHiResCellGlyph };
/** align=: the face's baseline on the game font's, centred in the cell, or the face's own line. */
enum HiResAlign { kHiResAlignGame = 0, kHiResAlignCell, kHiResAlignFont };
/** [load]: how a face file is held. */
enum HiResLoadMode { kHiResLoadAuto = 0, kHiResLoadWhole, kHiResLoadStream };

/** A face file named by a map: relative to a data folder ("data:" values) or a resolved path. */
struct HiResFaceRef {
	HiResFaceRef() : data(false) {}
	Common::String name;  ///< the [fonts] name, or the value as written
	Common::Path path;    ///< data: the part after "data:"; else the path
	bool data;
	Common::String key() const { return (data ? "data:" : "") + path.toString('/'); }
};

/** One font value: faces tried in order, then (endsInOriginal) the game's own font. */
struct HiResFontValue {
	HiResFontValue() : same(false), sameAt(0), endsInOriginal(false) {}
	Common::Array<HiResFaceRef> faces;
	bool same;            ///< a `same` entry: the enclosing chain goes where it stood
	uint sameAt;          ///< faces before `same`
	bool endsInOriginal;
	bool empty() const { return faces.empty() && !same && !endsInOriginal; }
};

/** One [font] or [font.N] section, its qualified sections merged in. */
struct HiResFontScope {
	HiResFontScope();

	HiResFontValue face;
	bool faceSet;
	int size;             ///< hi-res px of the chain's tallest cell
	bool sizeSet;
	int shift;            ///< hi-res px down, -32..32
	bool shiftSet;
	HiResCellMode cell;
	bool cellSet;
	HiResAlign align;
	bool alignSet;
	uint32 missing;       ///< code point drawn for one no face has; 0: none
	bool missingSet;
	HiResAdvance advance;
	bool advanceSet;
	Common::Array<HiResRangeSpec> rangeSpecs;
	Common::Array<HiResFontValue> rangeValues;
	Common::Array<HiResRangeSpec> advanceSpecs;
	Common::Array<HiResAdvance> advanceValues;
};

/** [layout]; unset keys keep the engine's BreakRules. */
struct HiResLayoutSettings {
	HiResLayoutSettings() : hangul(kHangulBreakWord), hangulSet(false), kinsoku(true), kinsokuSet(false), thai(true), thaiSet(false) {}
	HangulBreak hangul;
	bool hangulSet;
	bool kinsoku;
	bool kinsokuSet;
	bool thai;
	bool thaiSet;
	/** @p rules with the keys this map sets. */
	BreakRules apply(const BreakRules &rules) const;
};

typedef Common::HashMap<Common::String, HiResFaceRef, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> HiResFaceNames;

/** A version 3 map. */
struct HiResMap {
	HiResMap() { clear(); }
	void clear();

	int version;
	HiResRenderTarget target;   ///< [render] target=
	bool targetSet;
	HiResBlend blend;           ///< [render] blend=
	bool blendSet;
	int scale;                  ///< [render] scale=, 1..2; 0 auto
	bool scaleSet;
	HiResLayoutSettings layout;
	HiResFaceNames faces;       ///< [fonts]
	HiResFontScope font;        ///< [font]
	Common::HashMap<int, HiResFontScope> fontIds; ///< [font.N]
	Common::HashMap<Common::String, HiResLoadMode, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> loads; ///< [load], by face name
	Common::HashMap<Common::String, HiResFaceRef, Common::IgnoreCase_Hash, Common::IgnoreCase_EqualTo> preloads; ///< [preload], by face name
	Common::Array<Common::String> warnings;

	const HiResFontScope *fontIdScope(int id) const;
	HiResLoadMode loadModeFor(const HiResFaceRef &face) const;
};

/** The qualifiers a load merges: `[S:engine:target]`, `[S:engine]`, `[S:target]` over `[S]`. */
struct HiResMapQualifiers {
	HiResMapQualifiers() : target(kHiResTargetAuto) {}
	Common::String engine;      ///< sci, scumm, agi; empty: none
	HiResRenderTarget target;   ///< auto: no target section is merged
};

namespace HiResFontMap {

/** "data:" */
extern const char *const kDataPrefix;

/**
 * Parses a version 3 map. @p name names the map in warnings; relative paths
 * are taken against @p mapDir. False when the map is refused (no [map]
 * version=3, a version 2 map, a line that is not INI); every other problem
 * is a warning in @p out.warnings and the key is ignored.
 */
bool load(Common::SeekableReadStream &stream, const Common::String &name, const Common::Path &mapDir,
          const HiResMapQualifiers &qualifiers, HiResMap &out);

/** A map value as a face reference: "data:" values, absolute paths, or a path relative to @p mapDir. */
HiResFaceRef resolveFace(const Common::String &value, const Common::Path &mapDir);

/** The qualifier name of a resolved target: clut8 or rgb (empty for auto). */
const char *targetQualifier(HiResRenderTarget target);
/** auto, clut8, rgb565, rgb888 or rgb555: the value of render_target. */
const char *targetName(HiResRenderTarget target);

bool parseRenderTarget(const Common::String &text, HiResRenderTarget &out);
bool parseBlend(const Common::String &text, HiResBlend &out);
bool parseBool(const Common::String &text, bool &out);
/** u+XXXX, U+XXXX or 0xXXXX (1 to 6 hex digits, at most U+10FFFF). */
bool parseCodePoint(const Common::String &text, uint32 &out);

} // End of namespace HiResFontMap

} // End of namespace Graphics

#endif
