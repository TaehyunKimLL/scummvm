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

#ifndef GRAPHICS_HIRES_TEXT_FONT_SET_H
#define GRAPHICS_HIRES_TEXT_FONT_SET_H

#include "common/array.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/noncopyable.h"
#include "common/rect.h"
#include "common/types.h"
#include "common/str.h"

#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/text_layout.h"

namespace Graphics {

class HiResFiles;
class SvfChain;
class SvfFace;
class TextPlane;

/** The game's own font, for code points a map leaves to it (range.<spec>=original). Hi-res pixels. */
class HiResOriginalFont {
public:
	virtual ~HiResOriginalFont() {}
	virtual int getAdvance(uint32 cp) const = 0;
	virtual void drawChar(uint32 cp, int x, int yBaseline) const = 0;
};

/**
 * Every face a game's map names, each opened once: whole or streamed as the
 * map's [load] says (auto: whole up to @p wholeLimit bytes).
 */
class HiResFaceCache : Common::NonCopyable {
public:
	explicit HiResFaceCache(HiResFiles &files, uint32 wholeLimit = 256 * 1024);
	~HiResFaceCache();

	/** The face @p ref names; nullptr, with one warning, when it cannot be opened. */
	SvfFace *get(const HiResFaceRef &ref, HiResLoadMode mode, Common::Array<Common::String> &warnings);
	uint getCount() const { return _faces.size(); }
	/**
	 * Reads the glyphs of the characters the map's [preload] lists for @p ref's face name once (a streamed face only), so
	 * they sit in its block cache before the first line is drawn. A list that cannot be opened is one warning.
	 */
	void warm(SvfFace *face, const HiResFaceRef &ref, const HiResMap &map, Common::Array<Common::String> &warnings);

private:
	HiResFiles &_files;
	uint32 _wholeLimit;
	Common::HashMap<Common::String, SvfFace *> _faces; ///< by HiResFaceRef::key(); nullptr: failed
	Common::HashMap<Common::String, bool> _warmed;     ///< faces whose [preload] list was read
};

/**
 * One engine font id drawn in hi-res: the id plan's chains opened as
 * SvfChains, and text drawn one code point at a time into a TextPlane.
 * Advances, pen and baseline are hi-res pixels.
 */
class HiResFontSet : Common::NonCopyable {
public:
	HiResFontSet();
	~HiResFontSet();

	/**
	 * Opens @p plan's chains through @p faces (replacing any chains the set held). The faces belong to @p faces, which
	 * must outlive the set. False when the id chain has no face that opens and is not original.
	 */
	bool build(const HiResIdPlan &plan, const HiResMap &map, HiResFaceCache &faces, Common::Array<Common::String> &warnings);
	/** Adds a chain (tests, callers with faces of their own); the set deletes it with DisposeAfterUse::YES. */
	void addChain(SvfChain *chain, bool original, DisposeAfterUse::Flag dispose);
	void setRules(const HiResIdPlan &plan) { _plan = plan; }

	uint getChainCount() const { return _chains.size(); }
	SvfChain *getChain(uint i) const { return _chains[i]; }
	int getAscent() const;
	int getDescent() const;
	int getShift() const { return _plan.shift; }

	/** The chain that draws @p cp, or -1 when the game's own font does. */
	int route(uint32 cp) const;
	int getAdvance(uint32 cp, const HiResOriginalFont *original = nullptr) const;
	/** The pen's travel over @p len bytes of UTF-8. */
	int measure(const char *text, uint32 len, const HiResOriginalFont *original = nullptr) const;

	/** Draws @p cp in @p index with its baseline at @p yBaseline; returns the advance. */
	int drawChar(uint32 cp, int x, int yBaseline, TextPlane &plane, byte index, const Common::Rect *clip = nullptr,
	             const HiResOriginalFont *original = nullptr) const;
	/** Draws @p len bytes of UTF-8 from pen @p x; returns the pen after it. */
	int drawText(const char *text, uint32 len, int x, int yBaseline, TextPlane &plane, byte index,
	             const Common::Rect *clip = nullptr, const HiResOriginalFont *original = nullptr) const;

	enum { kSyllableCacheSlots = 64 };

private:
	void clearChains();
	uint32 substitute(uint32 cp, int chain) const;
	bool hasReplacement() const;

	HiResIdPlan _plan;
	Common::Array<SvfChain *> _chains;
	Common::Array<bool> _original;
	Common::Array<DisposeAfterUse::Flag> _dispose;
};

/** TextLayout's metrics over a font set: advances in hi-res pixels. */
class HiResFontMetrics : public LayoutMetrics {
public:
	HiResFontMetrics(const HiResFontSet &set, const HiResOriginalFont *original = nullptr) : _set(set), _original(original) {}
	int advance(uint32 cp) override;

private:
	const HiResFontSet &_set;
	const HiResOriginalFont *_original;
};

/** One code point of UTF-8 at @p p (p < end): its bytes (>= 1); @p invalid for a bad sequence (one byte). */
int decodeUtf8(const char *p, const char *end, uint32 &cp, bool &invalid);

} // End of namespace Graphics

#endif
