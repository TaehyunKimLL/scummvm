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
#include "graphics/hires_text/banded_plane.h"
#include "scumm/hires_sink.h"

namespace Scumm {

/// CHARSET_MASK_TRANSPARENCY, which gfx.cpp checks is still the same value.
static const byte kHiResTextTransparent = 0xFD;

/**
 * The rows of one plane as compositeTextRows() reads them: a
 * Graphics::BandedPlane from a point in it, or bytes in memory @p stride
 * apart. A row that is all zeros may come back null.
 */
class CompositeRows {
public:
	CompositeRows() : _plane(nullptr), _ptr(nullptr), _stride(0), _x(0), _y(0) {}
	/// The plane's rows from (@p x, @p y) on.
	CompositeRows(const Graphics::BandedPlane *plane, int x, int y) :
		_plane(plane && plane->exists() ? plane : nullptr), _ptr(nullptr), _stride(0), _x(x), _y(y) {}
	/// Rows in memory from @p ptr, @p stride bytes apart.
	CompositeRows(const byte *ptr, int stride) : _plane(nullptr), _ptr(ptr), _stride(stride), _x(0), _y(0) {}

	bool present() const { return _plane || _ptr; }
	/// Bytes of scratch row() may need.
	int scratchBytes() const { return _plane ? _plane->width() : 0; }

	/// Row @p h, from the starting point's column; null when it is all zeros.
	const byte *row(int h, byte *scratch) const {
		if (_ptr)
			return _ptr + h * _stride;
		if (!_plane)
			return nullptr;
		const byte *r = _plane->row(_y + h, scratch);
		return r ? r + _x : nullptr;
	}

private:
	const Graphics::BandedPlane *_plane;
	const byte *_ptr;
	int _stride;
	int _x, _y;
};

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
 * @p srcPitch and @p textPitch are the bytes skipped after a row, not the
 * row stride: the source's after @p width bytes, the text plane's after
 * @p width x @p m.
 *
 * @param coverage  may be absent (not present()), meaning every text pixel
 *                  is fully opaque; a null row of a present one is zeros
 * @param under, underCoverage
 *                  the decoration drawn below the text (C19), or absent.
 *                  Where the text is partly covered it is blended over the
 *                  decoration, not over the picture, which is what keeps an
 *                  antialiased edge from showing the picture as a seam inside
 *                  its outline. Where both are empty nothing changes from the
 *                  two-plane compositor.
 */
template<class Sink>
void compositeTextRows(Sink &sink, const byte *src, int srcPitch,
					   const byte *text, int textPitch,
					   const CompositeRows &coverageRows,
					   int width, int height, int m,
					   const CompositeRows &underRows = CompositeRows(),
					   const CompositeRows &underCoverageRows = CompositeRows()) {
	const int outWidth = width * m;
	const bool withUnder = underRows.present() && underCoverageRows.present();

	// Scratch for the expanded background row: the sink is given indices, and
	// the game buffer holds one per m output pixels.
	Common::Array<byte> bgRow(outWidth);
	// A row of zeros for a plane row no band holds, and room to unpack one.
	Common::Array<byte> zeroRow;
	zeroRow.resize(outWidth);
	memset(zeroRow.begin(), 0, outWidth);
	Common::Array<byte> covScratch, underScratch, underCovScratch;
	covScratch.resize(MAX(1, coverageRows.scratchBytes()));
	underScratch.resize(MAX(1, underRows.scratchBytes()));
	underCovScratch.resize(MAX(1, underCoverageRows.scratchBytes()));

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

		const byte *coverage = nullptr;
		if (coverageRows.present()) {
			coverage = coverageRows.row(h, covScratch.begin());
			if (!coverage)
				coverage = zeroRow.begin();
		}
		const byte *under = nullptr, *underCoverage = nullptr;
		if (withUnder) {
			underCoverage = underCoverageRows.row(h, underCovScratch.begin());
			if (underCoverage) {
				under = underRows.row(h, underScratch.begin());
				if (!under)
					under = zeroRow.begin();
			}
		}

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
	}
}

/**
 * The same, with each plane as bytes in memory: @p covPitch and
 * @p underPitch are the bytes skipped after a row's @p width x @p m.
 * @p coverage may be null (every text pixel opaque); @p under and
 * @p underCoverage are used only together.
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
	compositeTextRows(sink, src, srcPitch, text, textPitch,
					  coverage ? CompositeRows(coverage, outWidth + covPitch) : CompositeRows(),
					  width, height, m,
					  under ? CompositeRows(under, outWidth + underPitch) : CompositeRows(),
					  underCoverage ? CompositeRows(underCoverage, outWidth + underPitch) : CompositeRows());
}

} // End of namespace Scumm

#endif
