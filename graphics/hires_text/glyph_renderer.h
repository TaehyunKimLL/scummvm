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

#ifndef GRAPHICS_HIRES_TEXT_GLYPH_RENDERER_H
#define GRAPHICS_HIRES_TEXT_GLYPH_RENDERER_H

#include "common/rect.h"
#include "common/scummsys.h"
#include "graphics/hires_text/font_map.h"

namespace Graphics {

struct Surface;
class HiResBitmapFont;

/**
 * One glyph's pixels, in the form the renderer draws from.
 *
 * Coverage, one byte per pixel: 0 where the glyph is absent and 0xFF where it
 * is solid. A 1bpp stencil and 2bpp coverage (four levels, packed as
 * TextCompose::expandCoverage() reads them) are accepted too, and read
 * through the same path.
 */
struct GlyphBitmap {
	GlyphBitmap() : pixels(nullptr), pitch(0), width(0), height(0), bpp(8), originX(0), originY(0) {}

	const byte *pixels;  ///< coverage (packed at 2bpp), or a 1bpp stencil
	int pitch;           ///< bytes between rows
	int width;
	int height;
	int bpp;             ///< 1, 2 or 8

	/// Where the top left of these pixels sits relative to the pen position.
	/// Glyphs are not confined to their advance box - descenders drop below
	/// the baseline and italics overhang - so this can be negative.
	int originX;
	int originY;
};

/**
 * How a glyph is decorated, and with what.
 *
 * The colours are palette indices: this renderer writes into the same CLUT8
 * surface the engine's own text goes to, and knows nothing about what the
 * indices mean.
 *
 * shadowMode and shadowOffset are all an older caller sets, and mean what
 * they always did: an outline or drop @p shadowOffset pixels wide. The rest
 * refines that geometry (see HiResGlyphRenderer::decorationFor()); a map's
 * [shadow] keys are turned into it by HiResGlyphRenderer::applyMap().
 */
struct GlyphStyle {
	GlyphStyle() :
		color(0), shadowColor(0), shadowMode(kHiResShadowNone), shadowOffset(1),
		outlineQ(-1), outlineShape(kHiResOutlineRound),
		shadowShiftSet(false), shadowDx(0), shadowDy(0),
		shadowShiftColor(0), shadowShiftColorSet(false), shadowAlpha(255) {}

	byte color;
	byte shadowColor;
	HiResShadowMode shadowMode;
	int shadowOffset;   ///< distance from the glyph, in destination pixels

	/// Outline radius in quarters of a destination pixel; -1 = shadowOffset.
	int outlineQ;
	HiResOutlineShape outlineShape;

	/// An explicit shadow of the decoration, replacing the mode's own one
	/// (a drop's (offset, offset), a stroke's (-offset, +offset)); (0, 0)
	/// asks for none.
	bool shadowShiftSet;
	int shadowDx;
	int shadowDy;
	byte shadowShiftColor;      ///< the shadow's colour when set, else shadowColor
	bool shadowShiftColorSet;
	byte shadowAlpha;           ///< 255 = solid; a keyed target draws >= 128 solid, else none
};

/**
 * Where a glyph is drawn: the planes of a text overlay.
 *
 * Only @p index is required. @p coverage makes the body antialiased. The two
 * @p under planes take the decoration - outline and shadow - as a layer of its
 * own below the body, so that the body's antialiased edge is blended over the
 * outline rather than over the game's picture. Without them a decoration
 * shares the body's planes and is drawn solid.
 */
struct GlyphPlanes {
	GlyphPlanes() : index(nullptr), coverage(nullptr), underIndex(nullptr), underCoverage(nullptr) {}
	GlyphPlanes(Surface *i, Surface *c, Surface *ui = nullptr, Surface *uc = nullptr) :
		index(i), coverage(c), underIndex(ui), underCoverage(uc) {}

	Surface *index;
	Surface *coverage;
	Surface *underIndex;
	Surface *underCoverage;
};

/**
 * A decoration resolved to geometry: what GlyphStyle asks for, in pixels.
 */
struct GlyphDecoration {
	GlyphDecoration() : outline(false), outlineQ(0), shape(kHiResOutlineRound), legacyTable(kHiResShadowNone),
		step(1), shadow(false), shadowDx(0), shadowDy(0), shadowAlpha(255),
		outlineColor(0), shadowColor(0) {}

	bool outline;                   ///< the glyph is dilated
	int outlineQ;                   ///< radius in quarter pixels
	HiResOutlineShape shape;
	HiResShadowMode legacyTable;    ///< for kHiResOutlineLegacy: which mode's table
	int step;                       ///< for kHiResOutlineLegacy: the table's scale
	bool shadow;                    ///< a shifted copy of the outline (or of the glyph without one)
	int shadowDx;
	int shadowDy;
	byte shadowAlpha;
	byte outlineColor;
	byte shadowColor;
};

/**
 * The pen a glyph is dilated with: integer offsets and a reach for each.
 *
 * @p k is how far inside the pen's edge a tap lies, in 1/255 of a pixel
 * (r + 1 - distance), and may exceed 255; @p w is that clamped to 0..255,
 * the tap's weight for fully covered ink. A pixel's outline alpha is the
 * strongest of k - (255 - cov) over the taps, clamped: coverage is read as
 * how far into its pixel the ink reaches, so a partly covered stem pushes
 * the pen's edge back rather than dimming the whole outline. The soft disk's
 * rim is what keeps an outline of a fractional width antialiased.
 */
struct DilationKernel {
	enum { kMaxTaps = 33 * 33 };

	DilationKernel() : taps(0), reach(0) {}

	int taps;
	int reach;          ///< the largest |dx| or |dy|
	int8 dx[kMaxTaps];
	int8 dy[kMaxTaps];
	int16 k[kMaxTaps];
	byte w[kMaxTaps];
};

/**
 * Draws glyphs of a bitmap font onto a CLUT8 surface.
 *
 * An 8bpp (or 2bpp) font stores coverage rather than a stencil, which a
 * paletted surface cannot express on its own. The renderer therefore writes the colour into the
 * text surface and the coverage into a parallel 8bpp one, leaving the caller to
 * blend the two against whatever is behind them. That parallel surface is
 * optional: without it such a font still draws, keyed, as a stencil of the
 * pixels covered at least kKeyedInkThreshold.
 *
 * Nothing here knows about scaling. A font is baked at the size it will be
 * drawn, so glyphs go down one pixel per pixel.
 */
class HiResGlyphRenderer {
public:
	/**
	 * The coverage an 8bpp glyph pixel needs to be drawn when there is no
	 * coverage surface to blend it with (a paletted game, or blending off).
	 *
	 * Any coverage at all made the fringe around every stroke solid ink and
	 * closed small glyphs into blobs; half coverage deletes the thin CJK
	 * strokes a 9-16 px face draws at a quarter to a third (王 becomes 三).
	 */
	static const byte kKeyedInkThreshold = 0x40;

	/**
	 * The coverage a decoration pixel needs to be drawn when it cannot be
	 * blended: a keyed target, or a coverage plane with no under planes.
	 * Such a decoration is the dilation of the keyed body, cut at half.
	 */
	static const byte kKeyedDecorationThreshold = 0x80;

	/**
	 * Resolve a style to the decoration it draws.
	 *
	 * - drop: no outline; a shadow of the glyph at (offset, offset)
	 * - outline: an outline of outlineQ (or offset) radius, no shadow
	 * - stroke: that outline, plus a shadow of it at (-offset, +offset)
	 *
	 * An explicit shadow (shadowShiftSet) replaces the mode's own, and (0, 0)
	 * removes it. A style=legacy outline uses the mode's old offset table at
	 * a step of offset, and carries no separate stroke shadow: the table is
	 * already weighted that way.
	 */
	static GlyphDecoration decorationFor(const GlyphStyle &style);

	/**
	 * Fill in a style's geometry from a map's [shadow] keys, for text drawn
	 * @p scale output pixels to one game pixel.
	 *
	 * The mode itself (style.shadowMode) is the caller's: SCUMM resolves
	 * mode=game from the game's own shadow setting first. Defaults when the
	 * map leaves them out:
	 * - outline width 0.75 x scale (1.5 pixels at 2x), round;
	 * - offset= alone keeps meaning the outline width, as it always has;
	 * - shadow distance offset=, else half a game pixel rounded up.
	 */
	static void applyMap(GlyphStyle &style, const HiResMap &map, int scale);

	/**
	 * Build the pen for an outline.
	 *
	 * Round: weight = clamp(r + 1 - |d|) x 255, r = @p quarterRadius / 4.
	 * Square: the same with the larger of |dx|, |dy| as the distance.
	 * Legacy: the offset table of @p legacyTable (outline or stroke) grown
	 * @p step times, every tap at 255, with (0, 0) included. Growing it rather
	 * than multiplying the offsets is what keeps a step above one gap-free.
	 */
	static void buildKernel(DilationKernel &kernel, int quarterRadius, HiResOutlineShape shape,
							HiResShadowMode legacyTable = kHiResShadowOutline, int step = 1);

	/**
	 * Dilate a glyph's coverage by @p kernel into @p out: at each pixel the
	 * strongest clamp(k - (255 - cov)) the pen lands there (DilationKernel).
	 *
	 * @p out holds (width + 2 reach) x (height + 2 reach) bytes, the glyph at
	 * (reach, reach). With @p binaryAt above zero the input is first cut to
	 * 0 or 255 at that coverage, which is how a keyed decoration follows the
	 * keyed body.
	 */
	static void dilate(const GlyphBitmap &glyph, const DilationKernel &kernel, byte *out,
					   byte binaryAt = 0);

	/**
	 * Draw one glyph.
	 *
	 * @param dest      CLUT8 surface receiving the colour
	 * @param coverage  optional 8bpp surface receiving the coverage, or null
	 * @param font      the font to take the glyph from
	 * @param index     glyph index within that font
	 * @param x, y      top left of the glyph cell, in destination pixels
	 * @param style     colour and decoration
	 * @param dirty     if not null, extended by the area actually written
	 * @return false when the font has no such glyph
	 */
	static bool drawGlyph(Surface &dest, Surface *coverage,
						  const HiResBitmapFont &font, int index,
						  int x, int y, const GlyphStyle &style,
						  Common::Rect *dirty = nullptr);

	/**
	 * Draw a glyph that has already been rasterised.
	 *
	 * This is the form that does not care where the pixels came from, so a
	 * baked bitmap font and a TrueType face are drawn by exactly the same
	 * code. @p x and @p y are the pen position; the glyph's own origin is
	 * applied on top of them.
	 */
	static bool drawGlyph(Surface &dest, Surface *coverage,
						  const GlyphBitmap &glyph,
						  int x, int y, const GlyphStyle &style,
						  Common::Rect *dirty = nullptr);

	/**
	 * Draw a glyph into a full set of planes.
	 *
	 * With both under planes present (and a coverage plane), the decoration
	 * goes into them alone: their coverage is the strongest decoration any
	 * glyph put there, so glyphs may be drawn in any order and one's outline
	 * never erases another's body. The body planes get exactly what they
	 * would without a decoration.
	 */
	static bool drawGlyph(const GlyphPlanes &planes, const GlyphBitmap &glyph,
						  int x, int y, const GlyphStyle &style,
						  Common::Rect *dirty = nullptr);
};

} // End of namespace Graphics

#endif
