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

#include "common/util.h"
#include "graphics/hires_text/chain_layout.h"

namespace Graphics {

ChainLayout layoutFaceChain(const Common::Array<ChainFaceInfo> &faces, bool haveUni,
							int uniCellWidth, int uniCellHeight) {
	ChainLayout l;
	const uint n = faces.size();
	if (!n)
		return l;
	l.tops.resize(n);
	l.normalize.resize(n);
	for (uint i = 0; i < n; i++) {
		l.tops[i] = 0;
		l.normalize[i] = false;
	}
	l.cellWidth = faces[0].cellWidth;
	l.cellHeight = faces[0].cellHeight;
	l.uniTop = faces[0].trueType ? faces[0].rowPad : 0;

	bool allTrueType = true, allBaselines = true;
	for (uint i = 0; i < n; i++) {
		allTrueType &= faces[i].trueType;
		allBaselines &= faces[i].baselineRow >= 0;
	}
	// TrueType faces only: the first face's cell, as it always was.
	if (allTrueType)
		return l;

	// The row every face is anchored on: its baseline, or else where its
	// cell proper starts (below a TrueType face's row pad).
	l.byBaseline = allBaselines;
	int anchor = 0;
	for (uint i = 0; i < n; i++)
		anchor = MAX(anchor, l.byBaseline ? faces[i].baselineRow : faces[i].rowPad);
	l.cellWidth = 0;
	l.cellHeight = 0;
	for (uint i = 0; i < n; i++) {
		l.tops[i] = anchor - (l.byBaseline ? faces[i].baselineRow : faces[i].rowPad);
		l.cellWidth = MAX(l.cellWidth, faces[i].cellWidth);
		l.cellHeight = MAX(l.cellHeight, l.tops[i] + faces[i].cellHeight);
	}
	// The bundle has no baseline of its own: it starts where the first
	// face's cell proper does, and the cell holds it too.
	l.uniTop = l.tops[0] + (faces[0].trueType ? faces[0].rowPad : 0);
	if (haveUni) {
		l.cellWidth = MAX(l.cellWidth, uniCellWidth);
		l.cellHeight = MAX(l.cellHeight, l.uniTop + uniCellHeight);
	}

	// A lone face with nothing behind it is the chain itself.
	if (n == 1 && !haveUni)
		return l;
	for (uint i = 0; i < n; i++) {
		const ChainFaceInfo &f = faces[i];
		l.normalize[i] = !(f.bpp == 8 && f.cellWidth == l.cellWidth && f.cellHeight == l.cellHeight && !l.tops[i]);
	}
	return l;
}

} // End of namespace Graphics
