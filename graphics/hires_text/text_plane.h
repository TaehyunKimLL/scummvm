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

#ifndef GRAPHICS_HIRES_TEXT_TEXT_PLANE_H
#define GRAPHICS_HIRES_TEXT_TEXT_PLANE_H

#include "common/noncopyable.h"
#include "common/rect.h"
#include "common/scummsys.h"

#include "graphics/hires_text/banded_plane.h"

namespace Graphics {

/**
 * Hi-res text over a low-res paletted game screen: for each pixel of the
 * screen at getScale() times its size, a palette index and a coverage
 * (0 = no text). Colours are resolved when the plane is composited, so a
 * palette change recolours text drawn earlier. Both planes are banded
 * (BandedPlane): only the rows text covers hold memory, and the coverage
 * plane keeps 1 and 2 bpp coverage at 4 bits a pixel.
 *
 * Rects named "lowres" are in game pixels; every other coordinate is a
 * hi-res pixel. Nothing here knows an engine.
 */
class TextPlane : Common::NonCopyable {
public:
	TextPlane() : _scale(0) {}

	/** A plane for a @p lowW x @p lowH screen drawn at @p scale (1..4), without text. */
	bool create(int lowW, int lowH, int scale);
	void free();
	bool exists() const { return _coverage.exists(); }

	int getWidth() const { return _coverage.width(); }
	int getHeight() const { return _coverage.height(); }
	int getScale() const { return _scale; }

	/** No pixel holds text. */
	bool isEmpty() const { return _coverage.bandsHeld() == 0; }
	/** Whether row @p y may hold text (false: it holds none, or it is outside the plane). */
	bool rowHasText(int y) const { return inside(0, y) && _coverage.rowHeld(y); }

	/** Both are 0 outside the plane. */
	byte getIndex(int x, int y) const { return inside(x, y) ? _index.get(x, y) : 0; }
	byte getCoverage(int x, int y) const { return inside(x, y) ? _coverage.get(x, y) : 0; }

	/**
	 * Text of colour @p index and coverage @p coverage at (@p x, @p y),
	 * ignored outside the plane or at coverage 0. Over text of the same
	 * index the larger coverage stays (two cells of one line, the GRAY8
	 * rule of the renderer); over another index the new text replaces it.
	 */
	void plot(int x, int y, byte coverage, byte index);

	void clear();
	void clearRect(const Common::Rect &hires);
	void clearLowresRect(const Common::Rect &lowres) { clearRect(toHires(lowres)); }
	/** An XOR invert: the index of every pixel with text in the rect is XORed with @p mask. */
	void xorIndicesLowresRect(const Common::Rect &lowres, byte mask);
	/** Inside the rect, text of index @p a becomes @p b and the other way round. */
	void swapIndicesLowresRect(const Common::Rect &lowres, byte a, byte b);

	/** The bytes save() writes for @p lowres: 1 when it holds no text. */
	uint32 saveSize(const Common::Rect &lowres) const;
	/** A flag byte, then (flag 1) each hi-res row's indices and coverages. */
	void save(const Common::Rect &lowres, byte *&out) const;
	/** Puts back what save() wrote for the same rect (flag 0 clears it). */
	void restore(const Common::Rect &lowres, const byte *&in);

	/**
	 * Pixels [@p x0, @p x0 + @p n) of row @p y: false when the row holds no
	 * text. Otherwise @p index and @p coverage point at the pixels, in the
	 * plane or in the scratch buffers (room for @p n bytes each).
	 */
	bool getRow(int y, int x0, int n, byte *indexScratch, byte *coverageScratch,
	            const byte *&index, const byte *&coverage) const;

	/** @p lowres in hi-res pixels, clipped to the plane. */
	Common::Rect toHires(const Common::Rect &lowres) const;
	uint32 getBytesHeld() const { return _index.bytesHeld() + _coverage.bytesHeld(); }

private:
	bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < getWidth() && y < getHeight(); }
	bool covers(const Common::Rect &r) const;

	int _scale;
	BandedPlane _index;
	BandedPlane _coverage;
};

} // End of namespace Graphics

#endif
