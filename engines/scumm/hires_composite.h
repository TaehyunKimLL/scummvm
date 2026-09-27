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


#ifndef SCUMM_HIRES_COMPOSITE_H
#define SCUMM_HIRES_COMPOSITE_H

#include "common/array.h"
#include "common/scummsys.h"
#include "scumm/hires_sink.h"

namespace Scumm {

/// CHARSET_MASK_TRANSPARENCY, which gfx.cpp checks is still the same value.
static const byte kHiResTextTransparent = 0xFD;

/**
 * Composite one strip of text over the game's picture.
 *
 * The game buffer is at the unscaled size, so each of its pixels is read @p m
 * times across and @p m times down; the text and coverage planes are already
 * at the output size.
 *
 * Pixels are grouped into runs of the same kind before being handed to the
 * sink, so a virtual call covers a span rather than a single pixel.
 *
 * Every pitch here is the bytes skipped after a row, not the row stride: the
 * source's after @p width bytes, the planes' after @p width x @p m.
 *
 * @param coverage  may be null, meaning every text pixel is fully opaque
 * @param under, underCoverage
 *                  the decoration drawn below the text (C19), or null. Where
 *                  the text is partly covered it is blended over the
 *                  decoration, not over the picture, which is what keeps an
 *                  antialiased edge from showing the picture as a seam inside
 *                  its outline. Where both are empty nothing changes from the
 *                  two-plane compositor.
 */
template<class Sink>
void compositeText(Sink &sink, const byte *src, int srcPitch,
				   const byte *text, int textPitch,
				   const byte *coverage, int covPitch,
				   int width, int height, int m,
				   const byte *under = nullptr, const byte *underCoverage = nullptr,
				   int underPitch = 0) {
	const int outWidth = width * m;
	if (!under || !underCoverage)
		under = underCoverage = nullptr;

	// Scratch for the expanded background row: the sink is given indices, and
	// the game buffer holds one per m output pixels.
	Common::Array<byte> bgRow(outWidth);

	enum {
		kBackground,    ///< no text, no decoration
		kOpaque,        ///< text hides everything
		kBlended,       ///< text over the picture
		kUnderOpaque,   ///< no text; the decoration hides the picture
		kUnderBlended,  ///< no text; the decoration over the picture
		kLayered        ///< text over the decoration over the picture
	};

	for (int h = 0; h < height * m; ++h) {
		const byte *srcRow = src + (h / m) * (width + srcPitch);
		for (int w = 0; w < outWidth; ++w)
			bgRow[w] = srcRow[w / m];

		int runStart = 0;
		int runKind = -1;

		for (int w = 0; w <= outWidth; ++w) {
			int kind = -1;
			if (w < outWidth) {
				const byte t = text[w];
				const byte a = coverage ? coverage[w] : 0xFF;
				const byte u = underCoverage ? underCoverage[w] : 0;

				if (t == kHiResTextTransparent || (t == 0 && a == 0)) {
					// No text here. Index zero with no coverage is not text
					// either: that is a spot the surface was cleared to rather
					// than keyed, and painting palette entry 0 there would
					// punch a hole in the background.
					kind = !u ? kBackground : (u == 0xFF ? kUnderOpaque : kUnderBlended);
				} else if (a == 0 || a == 0xFF) {
					// Fully covered, or drawn by a path that leaves the
					// coverage channel alone - a zero there means opaque,
					// not invisible.
					kind = kOpaque;
				} else {
					kind = u ? kLayered : kBlended;
				}
			}

			if (kind != runKind) {
				const int count = w - runStart;
				if (count > 0) {
					switch (runKind) {
					case kBackground:
						sink.writeBackground(bgRow.begin() + runStart, count);
						break;
					case kOpaque:
						sink.writeOpaque(text + runStart, count);
						break;
					case kBlended:
						sink.writeBlended(text + runStart, bgRow.begin() + runStart,
										  coverage + runStart, count);
						break;
					case kUnderOpaque:
						sink.writeOpaque(under + runStart, count);
						break;
					case kUnderBlended:
						sink.writeBlended(under + runStart, bgRow.begin() + runStart,
										  underCoverage + runStart, count);
						break;
					default:
						sink.writeLayered(text + runStart, coverage + runStart,
										  under + runStart, underCoverage + runStart,
										  bgRow.begin() + runStart, count);
						break;
					}
				}
				runStart = w;
				runKind = kind;
			}
		}

		text += outWidth + textPitch;
		if (coverage)
			coverage += outWidth + covPitch;
		if (underCoverage) {
			under += outWidth + underPitch;
			underCoverage += outWidth + underPitch;
		}
	}
}

} // End of namespace Scumm

#endif
