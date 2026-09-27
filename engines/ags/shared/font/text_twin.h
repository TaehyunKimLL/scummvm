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

#ifndef AGS_SHARED_FONT_TEXT_TWIN_H
#define AGS_SHARED_FONT_TEXT_TWIN_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/pixelformat.h"

namespace Graphics {
struct Surface;
}

namespace AGS3 {

/** What one wouttext_outline() call was asked to draw (C23). */
struct TextDraw {
	TextDraw() : font(0), colour(0), outlineColour(0), x(0), y(0) {}
	int font;
	uint32 colour;				///< in the bitmap's format
	uint32 outlineColour;		///< in the bitmap's format
	Common::String text;
	int x, y;					///< the pen, bitmap pixels
	Common::Rect clip;			///< the bitmap's clip at the time, exclusive right/bottom
};

/** One captured draw: where it changed the bitmap, and the pixels there before and after. */
struct TextRecord {
	TextRecord() : valid(false) {}
	TextDraw draw;
	/** The changed pixels' bounding box grown by one pixel on every side
	 *  (N x ink may reach it), clipped to the bitmap. */
	Common::Rect rect;
	Common::Array<byte> before, after;	///< rect's rows
	bool valid;							///< set by TextCapture::finish()
};

/**
 * The text drawn into one bitmap, and whether each piece is still there
 * (AGS_HIRES_TEXT_DESIGN.md section 4.1). Free of the engine: it sees only
 * surfaces. beginDraw()/endDraw() go around the native draw; finish()
 * later decides, against the bitmap as it is then, which records are
 * valid: walking last to first on a copy, a record is valid only if the
 * copy still shows its "after" pixels in its rect, and then its "before"
 * pixels are put back. Anything drawn over a record later (or over its
 * one-pixel margin) makes it, and every earlier record whose rect it
 * covered, invalid; the copy with the valid records removed is the
 * text-free picture.
 */
class TextCapture {
public:
	TextCapture() : _bpp(0), _w(0), _h(0) {}

	void clear();

	/** Before a draw into s: keep s's pixels in band (clipped to s). */
	void beginDraw(const Graphics::Surface &s, const Common::Rect &band);
	/** After it: a record of what changed inside the band; false (and no
	 *  record) when nothing did. */
	bool endDraw(const Graphics::Surface &s, const TextDraw &draw);

	/**
	 * Decide validity against textFree, a copy of the bitmap as it is now,
	 * and remove the valid records' text from it. Returns how many are
	 * valid. A bitmap of another size or format makes them all invalid.
	 */
	uint finish(Graphics::Surface &textFree);

	/** The records lying wholly inside area of this capture's bitmap, moved
	 *  by -area's top left, as a capture of a copy of that area. */
	void deriveRegion(const Common::Rect &area, TextCapture &out) const;

	const Common::Array<TextRecord> &records() const { return _records; }
	bool empty() const { return _records.empty(); }

private:
	Common::Array<TextRecord> _records;
	Common::Rect _band;
	Common::Array<byte> _bandPixels;
	int _bpp, _w, _h;					///< the bitmap the records belong to
	Graphics::PixelFormat _format;
};

/** Building an N x twin (section 4.2). */
class TextTwin {
public:
	/** A twin's transparent pixel: the mask colour with alpha 0. */
	static const uint32 kTransparent = 0x00FF00FF;

	/**
	 * src (2 or 4 bytes per pixel) nearest-upscaled scale x into dst (ARGB8888,
	 * scale x src's size). The mask colour (RGB 255,0,255, any alpha), and
	 * alpha 0 when srcHasAlpha, become kTransparent; any other pixel keeps
	 * its alpha when srcHasAlpha, else gets alpha 255.
	 */
	static void upscaleToArgb(const Graphics::Surface &src, bool srcHasAlpha, Graphics::Surface &dst, int scale);

	/**
	 * After text was drawn into twin (ARGB8888) over picture (the twin as
	 * it was before, same size): inside r, every pixel that was opaque
	 * (alpha 255) in picture is opaque again. For a bitmap drawn ignoring
	 * its alpha (keyed): an outline stencil stamped into it copies its
	 * coverage alpha, which the native draw never shows.
	 */
	static void keepOpaque(const Graphics::Surface &picture, Graphics::Surface &twin, const Common::Rect &r);

	/** A colour of fmt as ARGB8888 with alpha 255. */
	static uint32 toArgb(uint32 colour, const Graphics::PixelFormat &fmt);
};

} // namespace AGS3

#endif
