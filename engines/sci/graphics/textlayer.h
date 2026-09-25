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

#ifndef SCI_GRAPHICS_TEXTLAYER_H
#define SCI_GRAPHICS_TEXTLAYER_H

#include "common/array.h"
#include "common/rect.h"
#include "common/scummsys.h"

namespace Sci {

/** One hi-res pixel of text: a foreground and an outline, each a palette
 *  index plus 8-bit coverage. Colour is resolved when composited, so text
 *  follows palette changes the way the original's indexed text did. */
struct TextPixel {
	byte fgIndex;
	byte fgCoverage;
	byte outlineIndex;
	byte outlineCoverage;
};

/**
 * Hi-res text as part of the visual plane (HIRES_COMPOSITOR_DESIGN.md D3):
 * whatever the game does to a low-res visual pixel it does to the scale x
 * scale block here - a pixel write clears it, underbits save and restore
 * carry it. Nothing here knows about the engine, so it is unit tested
 * alone.
 */
class TextLayer {
public:
	TextLayer(uint16 width, uint16 height, uint16 scale);

	uint16 width() const { return _width; }
	uint16 height() const { return _height; }
	uint16 scale() const { return _scale; }

	/** No row has ever held text since the last clear(). Cheap: callers on
	 *  hot paths test this first. */
	bool isEmpty() const { return !_any; }
	/** Conservative: true if the row may hold text. */
	bool rowHasText(uint16 y) const { return _rowFlags[y] != 0; }
	const TextPixel *row(uint16 y) const { return &_pixels[(uint32)y * _width]; }

	/** Coverage 0 leaves a pixel alone; any other value sets fg index and
	 *  coverage. Clipped to the layer. */
	void putGlyph(int16 hx, int16 hy, const byte *coverage, int16 w, int16 h, byte fgIndex);

	void clear();
	void clearLowresRect(const Common::Rect &lowres);
	void clearLowresPixel(int16 x, int16 y) { if (_any) clearLowresRect(Common::Rect(x, y, x + 1, y + 1)); }

	/** Upper bound of what save() writes for this rect. */
	uint32 saveSize(const Common::Rect &lowres) const;
	/** A flag byte, then the block's pixels if the flag is 1. */
	void save(const Common::Rect &lowres, byte *&out) const;
	void restore(const Common::Rect &lowres, const byte *&in);

private:
	Common::Rect toHires(const Common::Rect &lowres) const;

	uint16 _width, _height, _scale;
	bool _any;
	Common::Array<TextPixel> _pixels;
	Common::Array<byte> _rowFlags;
};

} // End of namespace Sci

#endif
