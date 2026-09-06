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

#include "graphics/hires_text/glyph_source.h"

#include "common/algorithm.h"
#include "common/rect.h"
#include "common/textconsole.h"
#include "graphics/font.h"
#include "graphics/surface.h"

namespace Graphics {

bool HiResBitmapGlyphSource::hasGlyph(uint32 codepoint) const {
	return _font.hasGlyph(codepoint);
}

bool HiResBitmapGlyphSource::glyph(uint32 codepoint, GlyphBitmap &out) const {
	const int index = _font.glyphIndex(codepoint);
	const byte *pixels = _font.glyphData(index);
	if (!pixels)
		return false;

	out.pixels = pixels;
	out.pitch = _font.glyphPitch();
	out.width = _font.cellWidth();
	out.height = _font.cellHeight();
	out.bpp = _font.bpp();
	out.originX = 0;
	out.originY = 0;
	return true;
}

bool HiResBitmapGlyphSource::metrics(uint32 codepoint, GlyphMetrics &out) const {
	return _font.glyphMetrics(_font.glyphIndex(codepoint), out);
}

#ifdef USE_FREETYPE2

HiResTtfGlyphSource::HiResTtfGlyphSource() {
	_font = nullptr;
	_supersample = 1;
	_scratch = nullptr;
	_scratchSize = 0;
	_cachedPoint = 0;
	_cachedValid = false;
}

HiResTtfGlyphSource::~HiResTtfGlyphSource() {
	free();
}

void HiResTtfGlyphSource::free() {
	delete _font;
	_font = nullptr;

	delete[] _scratch;
	_scratch = nullptr;
	_scratchSize = 0;

	_supersample = 1;
	_cachedPoint = 0;
	_cachedValid = false;
}

void HiResTtfGlyphSource::setFont(Font *font, int supersample) {
	free();
	_font = font;
	_supersample = MAX(1, supersample);
}

int HiResTtfGlyphSource::fontHeight() const {
	return _font ? _font->getFontHeight() / _supersample : 0;
}

int HiResTtfGlyphSource::ascent() const {
	return _font ? _font->getFontAscent() / _supersample : 0;
}

bool HiResTtfGlyphSource::hasGlyph(uint32 codepoint) const {
	if (!_font)
		return false;

	// A face reports a zero advance and an empty box for a code point it does
	// not cover. Space legitimately has an empty box but a real advance.
	return _font->getCharWidth(codepoint) > 0 || !_font->getBoundingBox(codepoint).isEmpty();
}

bool HiResTtfGlyphSource::metrics(uint32 codepoint, GlyphMetrics &out) const {
	if (!_font)
		return false;

	const Common::Rect box = _font->getBoundingBox(codepoint);

	out.advance = _font->getCharWidth(codepoint) / _supersample;
	out.bearingX = box.left / _supersample;
	out.bearingY = ascent();
	out.width = (box.width() > 0) ? box.width() / _supersample : 0;
	out.height = fontHeight();
	return true;
}

bool HiResTtfGlyphSource::glyph(uint32 codepoint, GlyphBitmap &out) const {
	if (!_font)
		return false;

	if (_cachedValid && _cachedPoint == codepoint) {
		out = _cached;
		return true;
	}

	// Glyphs are not confined to their advance box: descenders reach below the
	// baseline, punctuation sticks out past the nominal line height, and
	// italics overhang at the sides. Size the scratch from the glyph's own box
	// and remember where its origin ended up, so nothing is clipped and the
	// caller can still place it correctly.
	const Common::Rect box = _font->getBoundingBox(codepoint);
	const int advance = _font->getCharWidth(codepoint);

	const int originX = MIN(0, (int)box.left);
	const int originY = MIN(0, (int)box.top);
	const int rawW = MAX((int)box.right, advance) - originX;
	const int rawH = MAX((int)box.bottom, _font->getFontHeight()) - originY;

	if (rawW <= 0 || rawH <= 0)
		return false;

	// Guard against a face reporting an unreasonable box: this allocates.
	if (rawW > 4096 || rawH > 4096) {
		warning("HiResText: glyph U+%04X has an implausible box of %dx%d",
				codepoint, rawW, rawH);
		return false;
	}

	// ScummVM's TTF wrapper writes the coverage into an alpha channel, which a
	// paletted destination has nowhere to put. Rasterise into a 32bpp scratch
	// in opaque white instead, and read the coverage back out of the alpha.
	const PixelFormat fmt(4, 8, 8, 8, 8, 24, 16, 8, 0);
	Surface raster;
	raster.create(rawW, rawH, fmt);
	raster.fillRect(Common::Rect(0, 0, rawW, rawH), 0);
	_font->drawAlphaChar(&raster, codepoint, -originX, -originY,
						 fmt.ARGBToColor(0xFF, 0xFF, 0xFF, 0xFF));

	const int outW = rawW / _supersample;
	const int outH = rawH / _supersample;
	if (outW <= 0 || outH <= 0) {
		raster.free();
		return false;
	}

	const int needed = outW * outH;
	if (needed > _scratchSize) {
		delete[] _scratch;
		_scratch = new byte[needed];
		_scratchSize = needed;
	}

	if (_supersample == 1) {
		for (int y = 0; y < outH; ++y) {
			const uint32 *src = (const uint32 *)raster.getBasePtr(0, y);
			byte *dst = _scratch + y * outW;
			for (int x = 0; x < outW; ++x) {
				byte a, r, g, b;
				fmt.colorToARGB(src[x], a, r, g, b);
				dst[x] = a;
			}
		}
	} else {
		// Box-filter the supersampled raster back down. This is what keeps a
		// pixel font on its own grid when the line box is not a multiple of it.
		const int area = _supersample * _supersample;
		for (int y = 0; y < outH; ++y) {
			byte *dst = _scratch + y * outW;
			for (int x = 0; x < outW; ++x) {
				uint32 sum = 0;
				for (int sy = 0; sy < _supersample; ++sy) {
					const uint32 *src = (const uint32 *)raster.getBasePtr(0, y * _supersample + sy);
					for (int sx = 0; sx < _supersample; ++sx) {
						byte a, r, g, b;
						fmt.colorToARGB(src[x * _supersample + sx], a, r, g, b);
						sum += a;
					}
				}
				dst[x] = (byte)(sum / area);
			}
		}
	}

	raster.free();

	_cached.pixels = _scratch;
	_cached.pitch = outW;
	_cached.width = outW;
	_cached.height = outH;
	_cached.bpp = 8;
	_cached.originX = originX / _supersample;
	_cached.originY = originY / _supersample;
	_cachedPoint = codepoint;
	_cachedValid = true;

	out = _cached;
	return true;
}

#endif // USE_FREETYPE2

} // End of namespace Graphics
