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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_H
#define GRAPHICS_HIRES_TEXT_GLYPH_SOURCE_H

#include "common/scummsys.h"
#include "graphics/hires_text/bitmap_font.h"

namespace Graphics {

class Font;

/**
 * A glyph, rasterised and ready to place.
 *
 * The coverage is what the renderer needs and what a paletted surface cannot
 * hold: one byte per pixel, 0 where the glyph is absent and 0xFF where it is
 * solid. A bitmap font already stores this, and a rasteriser is asked to
 * produce it, so the two reach the renderer in the same shape.
 */
struct GlyphBitmap {
	GlyphBitmap() : pixels(nullptr), pitch(0), width(0), height(0), bpp(8), originX(0), originY(0) {}

	const byte *pixels;  ///< coverage, or a 1bpp stencil when bpp is 1
	int pitch;           ///< bytes between rows
	int width;
	int height;
	int bpp;             ///< 1 or 8

	/// Where the top left of these pixels sits relative to the pen position.
	/// Glyphs are not confined to their advance box - descenders drop below
	/// the baseline and italics overhang - so this can be negative.
	int originX;
	int originY;
};

/**
 * Something that can produce glyphs by Unicode code point.
 *
 * This is what lets a build without FreeType behave the same as one with it:
 * the renderer and the engine adapters only ever see this interface, and a
 * baked bitmap font satisfies it just as well as a rasteriser does.
 */
class HiResGlyphSource {
public:
	virtual ~HiResGlyphSource() {}

	virtual bool hasGlyph(uint32 codepoint) const = 0;

	/**
	 * Fetch a glyph.
	 *
	 * The pixels stay owned by the source and remain valid until the next
	 * call on it.
	 *
	 * @return false when this source has no glyph for the code point
	 */
	virtual bool glyph(uint32 codepoint, GlyphBitmap &out) const = 0;

	virtual bool metrics(uint32 codepoint, GlyphMetrics &out) const = 0;

	virtual int fontHeight() const = 0;
	virtual int ascent() const = 0;
};

/**
 * A baked bitmap font as a glyph source.
 *
 * Adds no rasterising and no allocation: the glyphs are handed out as they sit
 * in the file.
 */
class HiResBitmapGlyphSource : public HiResGlyphSource {
public:
	explicit HiResBitmapGlyphSource(const HiResBitmapFont &font) : _font(font) {}

	bool hasGlyph(uint32 codepoint) const override;
	bool glyph(uint32 codepoint, GlyphBitmap &out) const override;
	bool metrics(uint32 codepoint, GlyphMetrics &out) const override;
	int fontHeight() const override { return _font.cellHeight(); }
	int ascent() const override { return _font.ascent(); }

private:
	const HiResBitmapFont &_font;
};

#ifdef USE_FREETYPE2

/**
 * A TrueType face as a glyph source.
 *
 * Convenience only: everything it produces can be baked into a bitmap font
 * ahead of time, which is what a build without FreeType uses. It exists so a
 * translation can point at a .ttf during development without baking first.
 *
 * ScummVM's own TTF wrapper draws with the coverage in an alpha channel, which
 * a CLUT8 destination cannot express, so this rasterises to a 32bpp scratch
 * surface in white and reads the coverage back out of the alpha byte.
 */
class HiResTtfGlyphSource : public HiResGlyphSource {
public:
	HiResTtfGlyphSource();
	~HiResTtfGlyphSource() override;

	/**
	 * Take ownership of a font loaded by Graphics::loadTTFFont().
	 *
	 * @param font        the face; this object deletes it
	 * @param supersample the face was rasterised this many times larger than
	 *                    wanted, and glyphs are box-filtered back down. Keeps
	 *                    a pixel font on its native grid when the line box is
	 *                    not a multiple of it.
	 */
	void setFont(Font *font, int supersample = 1);

	void free();

	bool isLoaded() const { return _font != nullptr; }

	bool hasGlyph(uint32 codepoint) const override;
	bool glyph(uint32 codepoint, GlyphBitmap &out) const override;
	bool metrics(uint32 codepoint, GlyphMetrics &out) const override;
	int fontHeight() const override;
	int ascent() const override;

private:
	Font *_font;
	int _supersample;

	// Scratch for the glyph most recently asked for.
	mutable byte *_scratch;
	mutable int _scratchSize;
	mutable uint32 _cachedPoint;
	mutable GlyphBitmap _cached;
	mutable bool _cachedValid;
};

#endif // USE_FREETYPE2

} // End of namespace Graphics

#endif
