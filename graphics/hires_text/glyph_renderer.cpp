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

#include "graphics/hires_text/glyph_renderer.h"

#include "graphics/hires_text/bitmap_font.h"
#include "graphics/surface.h"

#include <math.h>

namespace Graphics {

// The legacy decorations: where the decoration went, as multiples of the
// style's offset, before outlines were antialiased. style=legacy still draws
// with them.
//
// An outline surrounds the glyph. The stroke form is an outline weighted
// towards the lower left, which is what the heavier of the shipped fonts were
// drawn to sit on.
static const int8 kOutlineX[] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const int8 kOutlineY[] = { -1, -1, -1, 0, 0, 1, 1, 1 };

static const int8 kStrokeX[] = { -1, 0, 1, -1, 1, -1, 0, 1, -1, -1, -2 };
static const int8 kStrokeY[] = { -1, -1, -1, 0, 0, 1, 1, 1, 2, 1, 0 };

const byte HiResGlyphRenderer::kKeyedInkThreshold;
const byte HiResGlyphRenderer::kKeyedDecorationThreshold;

// The widest pen buildKernel() makes, in quarter pixels, and the largest step
// a legacy table is grown by. Both keep the kernel inside kMaxTaps.
static const int kMaxOutlineQ = 32;
static const int kMaxLegacyStep = 8;

/// Coverage of one pixel of a glyph, 0 for nothing at all.
static inline byte glyphCoverage(const byte *row, int x, int bpp) {
	if (bpp == 1)
		return (row[x >> 3] & (0x80 >> (x & 7))) ? 0xFF : 0;
	return row[x];
}

GlyphDecoration HiResGlyphRenderer::decorationFor(const GlyphStyle &style) {
	GlyphDecoration d;
	d.outlineColor = style.shadowColor;
	d.shadowColor = style.shadowShiftColorSet ? style.shadowShiftColor : style.shadowColor;
	d.shadowAlpha = style.shadowAlpha;
	d.shape = style.outlineShape;

	const int offset = MAX(1, style.shadowOffset);
	int q = (style.outlineQ >= 0) ? style.outlineQ : offset * 4;
	q = CLIP(q, 0, kMaxOutlineQ);

	switch (style.shadowMode) {
	case kHiResShadowDrop:
		d.shadow = true;
		d.shadowDx = offset;
		d.shadowDy = offset;
		break;
	case kHiResShadowOutline:
	case kHiResShadowStroke:
		d.outline = q > 0;
		d.outlineQ = q;
		if (style.outlineShape == kHiResOutlineLegacy) {
			// The table is the whole decoration, the stroke's lower-left
			// weight included, grown by the offset as it always was.
			d.legacyTable = style.shadowMode;
			d.step = MIN(offset, kMaxLegacyStep);
			d.outline = true;
		} else if (style.shadowMode == kHiResShadowStroke) {
			// The legacy stroke is an outline with one more copy of it to the
			// lower left: that copy is a shadow of the outline.
			d.shadow = true;
			d.shadowDx = -offset;
			d.shadowDy = offset;
		}
		break;
	default:
		return GlyphDecoration();
	}

	if (style.shadowShiftSet) {
		d.shadowDx = style.shadowDx;
		d.shadowDy = style.shadowDy;
		d.shadow = (d.shadowDx || d.shadowDy);
	}
	if (!d.shadowAlpha)
		d.shadow = false;
	return d;
}

void HiResGlyphRenderer::applyMap(GlyphStyle &style, const HiResTextConfig &map, int scale) {
	scale = MAX(1, scale);

	// Half a game pixel, rounded up: the legacy fonts' shadows were one game
	// pixel, which at 2x is a heavy two.
	style.shadowOffset = (map.shadowOffset >= 0) ? MAX(1, map.shadowOffset) : (scale + 1) / 2;

	if (map.shadowWidthQ >= 0)
		style.outlineQ = map.shadowWidthQ;
	else if (map.shadowOffset >= 0)
		style.outlineQ = MAX(1, map.shadowOffset) * 4;
	else
		style.outlineQ = 3 * scale;     // 0.75 of a game pixel

	style.outlineShape = map.shadowStyle;
	style.shadowShiftSet = map.shadowShiftSet;
	style.shadowDx = map.shadowDx;
	style.shadowDy = map.shadowDy;
	style.shadowShiftColorSet = map.shadowShiftColorSet;
	style.shadowShiftColor = map.shadowShiftColor;
	style.shadowAlpha = map.shadowAlpha;
}

void HiResGlyphRenderer::buildKernel(DilationKernel &k, int quarterRadius, HiResOutlineShape shape,
									 HiResShadowMode legacyTable, int step) {
	k.taps = 0;
	k.reach = 0;

	if (shape == kHiResOutlineLegacy) {
		const int8 *offX = kOutlineX, *offY = kOutlineY;
		int count = ARRAYSIZE(kOutlineX), reach = 1;
		if (legacyTable == kHiResShadowStroke) {
			offX = kStrokeX;
			offY = kStrokeY;
			count = ARRAYSIZE(kStrokeX);
			reach = 2;
		}
		step = CLIP(step, 1, kMaxLegacyStep);

		// Grow the table step times (a Minkowski sum with itself), on a grid
		// that holds the furthest it can reach.
		const int R = reach * step;
		const int side = 2 * R + 1;
		byte grid[(2 * 2 * kMaxLegacyStep + 1) * (2 * 2 * kMaxLegacyStep + 1)];
		byte next[ARRAYSIZE(grid)];
		memset(grid, 0, sizeof(grid));
		grid[R * side + R] = 1;
		for (int s = 0; s < step; ++s) {
			memcpy(next, grid, side * side);
			for (int y = 0; y < side; ++y)
				for (int x = 0; x < side; ++x) {
					if (!grid[y * side + x])
						continue;
					for (int c = 0; c < count; ++c) {
						const int nx = x + offX[c], ny = y + offY[c];
						if (nx >= 0 && nx < side && ny >= 0 && ny < side)
							next[ny * side + nx] = 1;
					}
				}
			memcpy(grid, next, side * side);
		}

		for (int y = 0; y < side; ++y)
			for (int x = 0; x < side; ++x) {
				if (!grid[y * side + x] || k.taps >= DilationKernel::kMaxTaps)
					continue;
				k.dx[k.taps] = x - R;
				k.dy[k.taps] = y - R;
				k.w[k.taps] = 255;
				k.reach = MAX(k.reach, MAX(ABS(x - R), ABS(y - R)));
				++k.taps;
			}
		return;
	}

	// A soft disk: full weight within r, fading to nothing over the next
	// pixel, which is what antialiases the outline's own rim.
	const int q = CLIP(quarterRadius, 0, kMaxOutlineQ);
	const double r = q / 4.0;
	const int R = (q + 4 + 3) / 4;     // ceil(r + 1)
	for (int dy = -R; dy <= R; ++dy) {
		for (int dx = -R; dx <= R; ++dx) {
			double d;
			if (shape == kHiResOutlineSquare)
				d = MAX(ABS(dx), ABS(dy));
			else
				d = sqrt((double)(dx * dx + dy * dy));
			double w = r + 1.0 - d;
			if (w > 1.0)
				w = 1.0;
			const int wi = (int)(w * 255.0 + 0.5);
			if (wi <= 0 || k.taps >= DilationKernel::kMaxTaps)
				continue;
			k.dx[k.taps] = dx;
			k.dy[k.taps] = dy;
			k.w[k.taps] = (byte)wi;
			k.reach = MAX(k.reach, MAX(ABS(dx), ABS(dy)));
			++k.taps;
		}
	}
}

void HiResGlyphRenderer::dilate(const GlyphBitmap &glyph, const DilationKernel &k, byte *out,
								byte binaryAt) {
	const int mw = glyph.width + 2 * k.reach;
	const int mh = glyph.height + 2 * k.reach;
	memset(out, 0, mw * mh);

	// Scatter each covered source pixel through the pen, keeping the maximum.
	// Glyphs are a few hundred pixels and pens a few dozen taps, so this is
	// cheap; empty source pixels, most of a glyph, cost nothing.
	for (int gy = 0; gy < glyph.height; ++gy) {
		const byte *row = glyph.pixels + gy * glyph.pitch;
		for (int gx = 0; gx < glyph.width; ++gx) {
			byte cv = glyphCoverage(row, gx, glyph.bpp);
			if (binaryAt)
				cv = (cv >= binaryAt) ? 0xFF : 0;
			if (!cv)
				continue;

			byte *centre = out + (gy + k.reach) * mw + gx + k.reach;
			for (int t = 0; t < k.taps; ++t) {
				const byte v = (cv == 0xFF) ? k.w[t] : (byte)((cv * k.w[t] + 127) / 255);
				byte &o = centre[k.dy[t] * mw + k.dx[t]];
				if (v > o)
					o = v;
			}
		}
	}
}

bool HiResGlyphRenderer::drawGlyph(Surface &dest, Surface *coverage,
								   const HiResBitmapFont &font, int index,
								   int x, int y, const GlyphStyle &style,
								   Common::Rect *dirty) {
	const byte *pixels = font.glyphData(index);
	if (!pixels)
		return false;

	GlyphBitmap glyph;
	glyph.pixels = pixels;
	glyph.pitch = font.glyphPitch();
	glyph.width = font.cellWidth();
	glyph.height = font.cellHeight();
	glyph.bpp = font.bpp();

	return drawGlyph(GlyphPlanes(&dest, coverage), glyph, x, y, style, dirty);
}

bool HiResGlyphRenderer::drawGlyph(Surface &dest, Surface *coverage,
								   const GlyphBitmap &glyph,
								   int x, int y, const GlyphStyle &style,
								   Common::Rect *dirty) {
	return drawGlyph(GlyphPlanes(&dest, coverage), glyph, x, y, style, dirty);
}

/// A plane that can be written at (px, py), which the index plane can.
static inline bool fits(const Surface *s, int px, int py) {
	return s && px < s->w && py < s->h;
}

bool HiResGlyphRenderer::drawGlyph(const GlyphPlanes &planes, const GlyphBitmap &glyph,
								   int x, int y, const GlyphStyle &style,
								   Common::Rect *dirty) {
	if (!planes.index || !glyph.pixels || glyph.width <= 0 || glyph.height <= 0)
		return false;
	Surface &dest = *planes.index;

	// The coverage surface only makes sense for a glyph that has coverage to
	// record, and only where it is large enough to hold it.
	Surface *cov = (planes.coverage && glyph.bpp == 8 && planes.coverage->getPixels()) ? planes.coverage : nullptr;

	// The decoration gets a layer of its own when there is one, and only on
	// a blended target: a keyed one has nothing to blend it with.
	Surface *uIdx = nullptr, *uCov = nullptr;
	if (cov && planes.underIndex && planes.underCoverage &&
		planes.underIndex->getPixels() && planes.underCoverage->getPixels()) {
		uIdx = planes.underIndex;
		uCov = planes.underCoverage;
	}

	// Blended, every covered pixel counts; keyed, only real ink does (see
	// kKeyedInkThreshold). A 1bpp glyph is 0 or 0xFF either way.
	const byte inkMin = cov ? 1 : kKeyedInkThreshold;

	GlyphDecoration deco = decorationFor(style);

	// A decoration in the same colour as the text cannot be seen and only
	// costs time - and, at 8bpp, would thicken the glyph for no reason.
	if (deco.outlineColor == style.color)
		deco.outline = false;
	if (deco.shadowColor == style.color)
		deco.shadow = false;

	// Without a layer of its own the decoration cannot be blended, so it is
	// solid: the dilation of the keyed body, cut at half. A legacy outline is
	// binary by definition, layered or not.
	const bool solid = !uCov || deco.shape == kHiResOutlineLegacy;
	if (!uCov && deco.shadow && deco.shadowAlpha < 128)
		deco.shadow = false;

	// A rasteriser hands back a glyph placed relative to the pen; a baked
	// bitmap font has no offset of its own.
	const int baseX = x + glyph.originX;
	const int baseY = y + glyph.originY;

	Common::Rect touched(baseX, baseY, baseX + glyph.width, baseY + glyph.height);

	// The decoration is built as a mask first - the glyph's coverage dilated
	// by a pen (C19) - rather than by re-blitting the glyph once per offset.
	//
	// Drawing the glyph repeatedly makes each copy carry the body's own
	// antialiasing and overwrite the others. Dilating gives one alpha: the
	// strongest coverage the pen lands on each pixel, with the pen's own soft
	// rim for an antialiased outline of any width. A shadow is that alpha
	// moved; the drop shadow moves the glyph itself.
	if (deco.outline || deco.shadow) {
		DilationKernel kernel;
		if (deco.outline)
			buildKernel(kernel, deco.outlineQ, deco.shape, deco.legacyTable, deco.step);
		else
			buildKernel(kernel, 0, kHiResOutlineSquare);   // the glyph itself: one tap

		const int pad = kernel.reach;
		const int mw = glyph.width + 2 * pad;
		const int mh = glyph.height + 2 * pad;
		Common::Array<byte> alpha;
		alpha.resize(mw * mh);
		dilate(glyph, kernel, alpha.begin(), solid ? inkMin : 0);

		if (solid) {
			for (uint i = 0; i < alpha.size(); ++i)
				alpha[i] = (alpha[i] >= kKeyedDecorationThreshold) ? 0xFF : 0;
		}

		// The area both layers can reach: the mask, and the mask moved.
		const int sdx = deco.shadow ? deco.shadowDx : 0;
		const int sdy = deco.shadow ? deco.shadowDy : 0;
		const int left = MIN(0, sdx), top = MIN(0, sdy);
		const int right = mw + MAX(0, sdx), bottom = mh + MAX(0, sdy);

		const byte shadowAlpha = solid ? 0xFF : deco.shadowAlpha;

		for (int my = top; my < bottom; ++my) {
			const int py = baseY - pad + my;
			if (py < 0 || py >= dest.h)
				continue;

			for (int mx = left; mx < right; ++mx) {
				const int px = baseX - pad + mx;
				if (px < 0 || px >= dest.w)
					continue;

				byte a = 0, s = 0;
				if (deco.outline && mx >= 0 && mx < mw && my >= 0 && my < mh)
					a = alpha[my * mw + mx];
				if (deco.shadow) {
					const int sx = mx - sdx, sy = my - sdy;
					if (sx >= 0 && sx < mw && sy >= 0 && sy < mh) {
						s = alpha[sy * mw + sx];
						if (shadowAlpha != 0xFF)
							s = (byte)((s * shadowAlpha + 127) / 255);
					}
				}
				if (!a && !s)
					continue;

				const byte colour = (a >= s) ? deco.outlineColor : deco.shadowColor;

				if (uCov) {
					// Its own layer: the strongest decoration wins, whichever
					// glyph drew it, and the body planes are not touched.
					if (!fits(uCov, px, py) || !fits(uIdx, px, py))
						continue;
					const byte v = MAX(a, s);
					byte &u = *(byte *)uCov->getBasePtr(px, py);
					if (v <= u)
						continue;
					u = v;
					*(byte *)uIdx->getBasePtr(px, py) = colour;
				} else if (cov) {
					// Sharing the body's planes, the stroke must yield to body
					// ink already there, or each character would erase the
					// tail of the one before it - including its antialiased
					// edge, which is faint but still the letterform.
					if (!fits(cov, px, py))
						continue;
					byte &c = *(byte *)cov->getBasePtr(px, py);
					if (c)
						continue;
					c = 0xFF;
					*(byte *)dest.getBasePtr(px, py) = colour;
				} else {
					// Keyed, there is no coverage to consult, but the text
					// colour is: an earlier glyph's body stays.
					byte &d = *(byte *)dest.getBasePtr(px, py);
					if (d == style.color)
						continue;
					d = colour;
				}
			}
		}

		touched.extend(Common::Rect(baseX - pad + left, baseY - pad + top,
									baseX - pad + right, baseY - pad + bottom));
	}

	// The body, drawn over the stroke with its antialiasing intact.
	{
		for (int gy = 0; gy < glyph.height; ++gy) {
			const int py = baseY + gy;
			if (py < 0 || py >= dest.h)
				continue;

			const byte *row = glyph.pixels + gy * glyph.pitch;

			for (int gx = 0; gx < glyph.width; ++gx) {
				const byte cv = glyphCoverage(row, gx, glyph.bpp);
				if (cv < inkMin)
					continue;

				const int px = baseX + gx;
				if (px < 0 || px >= dest.w)
					continue;

				*(byte *)dest.getBasePtr(px, py) = style.color;

				if (fits(cov, px, py))
					*(byte *)cov->getBasePtr(px, py) = cv;
			}
		}
	}

	if (dirty) {
		// The decoration reaches beyond the cell, so the caller is told about
		// the whole area that may have changed.
		if (dirty->isEmpty())
			*dirty = touched;
		else
			dirty->extend(touched);
	}

	return true;
}

} // End of namespace Graphics
