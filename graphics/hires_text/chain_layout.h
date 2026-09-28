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

#ifndef GRAPHICS_HIRES_TEXT_CHAIN_LAYOUT_H
#define GRAPHICS_HIRES_TEXT_CHAIN_LAYOUT_H

#include "common/array.h"

namespace Graphics {

/** One face of a face chain, as the layout sees it. */
struct ChainFaceInfo {
	ChainFaceInfo() : cellWidth(0), cellHeight(0), rowPad(0), bpp(8), baselineRow(-1), trueType(false) {}

	int cellWidth;
	int cellHeight;
	int rowPad;        ///< TtfGlyphSource::rowPad(): where its cell proper starts; 0 for a bitmap font
	int bpp;           ///< bitsPerPixel()
	int baselineRow;   ///< UnicodeGlyphSource::baselineRow(), -1 when unknown
	bool trueType;     ///< a live TrueType face (TtfGlyphSource)
};

/** Where every face of a chain, and the .uni bundle behind it, sits in the chain's one cell. */
struct ChainLayout {
	ChainLayout() : cellWidth(0), cellHeight(0), uniTop(0), byBaseline(false) {}

	int cellWidth;
	int cellHeight;
	Common::Array<int> tops;         ///< the cell row each face's row 0 is presented at
	Common::Array<bool> normalize;   ///< the face needs a NormalizedGlyphSource to join the chain
	int uniTop;                      ///< the cell row the .uni bundle's row 0 is presented at
	bool byBaseline;                 ///< faces were aligned by their baselines (else by their row pads)
};

/**
 * The cell a face chain is drawn in and where each face sits in it
 * (FallbackGlyphSource wants one cell at one depth from every source).
 *
 * A chain of TrueType faces only keeps the first face's cell, every face
 * at row 0 and none normalised; the .uni bundle starts at the first face's
 * row pad (and is refused by NormalizedGlyphSource when it does not fit).
 * This is what such a chain always did.
 *
 * A chain with a bitmap face in it is brought to one cell: when every face
 * knows its baseline (baselineRow() >= 0) the faces are stacked so their
 * baselines fall on one row - an 18 px and a 16 px bitmap font stand on
 * one line - otherwise so their cells proper (below each row pad) start on
 * one row. The .uni bundle (no baseline of its own) starts where the first
 * face's cell proper does. The cell holds every face and the bundle; each
 * face that is not already 8 bpp at that cell and at row 0 is to be
 * normalised - except a lone face with no bundle behind it, which is the
 * chain itself.
 *
 * @param faces    the chain's faces, in order; empty gives an empty layout
 * @param haveUni  a .uni bundle stands behind the faces
 * @param uniCellWidth, uniCellHeight  its cell
 */
ChainLayout layoutFaceChain(const Common::Array<ChainFaceInfo> &faces, bool haveUni,
							int uniCellWidth, int uniCellHeight);

} // End of namespace Graphics

#endif
