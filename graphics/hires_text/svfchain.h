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

#ifndef GRAPHICS_HIRES_TEXT_SVFCHAIN_H
#define GRAPHICS_HIRES_TEXT_SVFCHAIN_H

#include "common/array.h"
#include "common/noncopyable.h"
#include "common/rect.h"
#include "common/types.h"

#include "graphics/hires_text/svfface.h"

namespace Graphics {

/**
 * A fallback chain of SvfFaces drawn by svf_draw_utf8: each code point comes from the first face
 * that has it. Owns the scratch cell and the optional composed-syllable cache. Text is UTF-8.
 * One chain is not drawn from two threads at once.
 */
class SvfChain : Common::NonCopyable {
public:
	typedef void (*PlotFunc)(void *ctx, int16_t x, int16_t y, uint8_t coverage);

	SvfChain();
	~SvfChain();

	/** Appends an open face (at most 255). The chain deletes it with DisposeAfterUse::YES. */
	bool addFace(SvfFace *face, DisposeAfterUse::Flag dispose = DisposeAfterUse::NO);
	uint getFaceCount() const { return _faces.size(); }
	SvfFace *getFace(uint i) const { return _faces[i]; }

	/** Advance of a code point no face has (before scaling). */
	void setMissingAdvance(uint8 advance);
	/** Scale num/den, each 1..16; the ratio is kept reduced. */
	bool setScale(uint num, uint den);
	uint getScaleNum() const { return _chain.scale_num ? _chain.scale_num : 1; }
	uint getScaleDen() const { return _chain.scale_den ? _chain.scale_den : 1; }
	/** Gives the chain a composed-syllable cache of @p slots cells (0: none). */
	bool setSyllableCache(uint slots);

	/** Rows above the baseline: the largest scaled ascent of the faces. */
	int getAscent() const;
	/** Rows below the baseline: the largest scaled cell height minus scaled ascent. */
	int getDescent() const;
	int getMaxCellWidth() const;
	bool has(uint32 cp) const;

	svf_err measure(const char *text, uint32 len, svf_extent &extent) const;
	/** The pen's travel over @p text; 0 if the chain cannot lay it out. */
	int getWidth(const char *text, uint32 len) const;

	/** Draws @p text on @p target, the pen starting at (@p x, @p yBaseline). */
	svf_err draw(const char *text, uint32 len, int x, int yBaseline, svf_target &target) const;
	/** Draws through SVF_T_COVERAGE_CB on a w x h area, clipped to @p clip (nullptr: the whole area). */
	svf_err drawCoverage(const char *text, uint32 len, int x, int yBaseline, int w, int h, PlotFunc plot, void *ctx,
	                     const Common::Rect *clip = nullptr) const;

	const svf_chain *raw() const { return &_chain; }

	static int scaleLength(int v, uint num, uint den);
	static int scaleDimension(int v, uint num, uint den);

private:
	void update();

	Common::Array<SvfFace *> _faces;
	Common::Array<DisposeAfterUse::Flag> _dispose;
	Common::Array<svf_face *> _rawFaces;
	svf_chain _chain;
	uint _cacheSlots;
	Common::Array<byte> _syllableCache;
	mutable Common::Array<byte> _scratch;
};

} // End of namespace Graphics

#endif
