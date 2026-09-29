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

#ifndef BACKENDS_PLATFORM_DOS_LOADING_SCREEN_H
#define BACKENDS_PLATFORM_DOS_LOADING_SCREEN_H

#include "common/rect.h"
#include "common/scummsys.h"
#include "common/str.h"
#include "common/util.h"
#include "graphics/pixelformat.h"

namespace DOS {

/**
 * @p s as plain ASCII of at most @p maxChars characters, for the text-mode
 * screen and the ROM font: a UTF-8 (or other 8-bit) character becomes one
 * '?', control characters become spaces, and a string that is too long
 * ends in "...".
 */
inline Common::String asciiTitle(const Common::String &s, uint maxChars) {
	Common::String out;
	for (uint i = 0; i < s.size(); ++i) {
		const byte c = (byte)s[i];
		if (c >= 0x80 && c < 0xC0 && i > 0 && (byte)s[i - 1] >= 0x80)
			continue;	// a UTF-8 continuation byte: its character is one '?'
		out += c >= 0x80 ? '?' : (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
	}
	if (out.size() > maxChars) {
		if (maxChars <= 3)
			return Common::String(out.c_str(), maxChars);
		out = Common::String(out.c_str(), maxChars - 3) + "...";
	}
	return out;
}

/** Where the graphics loading screen puts its parts, for a w x h mode. */
struct LoadScreenLayout {
	Common::Rect title;	///< the game's name, 8x16 ROM font
	Common::Rect caption;	///< "Loading..." (English, ROM font) or the Korean bitmap
	Common::Rect bar;	///< outline included
	Common::Rect label;	///< the phase, ROM font
};

/** 8x16 character cells. */
static const int kLoadGlyphW = 8;
static const int kLoadGlyphH = 16;

/**
 * Centered, top to bottom: title at 36% of the height, the caption under
 * it, the bar at 58% (60% of the width, at most 400 pixels, a 25th of the
 * height tall but at least 6), the phase under the bar. Everything is
 * inside the screen for any mode from 320x200 up.
 */
inline LoadScreenLayout loadScreenLayout(int w, int h, int titleChars, int captionW, int captionH, int labelChars) {
	LoadScreenLayout l;
	const int titleW = titleChars * kLoadGlyphW;
	int y = h * 36 / 100;
	l.title = Common::Rect((w - titleW) / 2, y, (w - titleW) / 2 + titleW, y + kLoadGlyphH);
	y += kLoadGlyphH + kLoadGlyphH / 2;
	l.caption = Common::Rect((w - captionW) / 2, y, (w - captionW) / 2 + captionW, y + captionH);
	const int barW = MIN(w * 60 / 100, 400);
	const int barH = MAX(6, h / 25);
	y = MAX(h * 58 / 100, (int)l.caption.bottom + kLoadGlyphH / 2);
	l.bar = Common::Rect((w - barW) / 2, y, (w - barW) / 2 + barW, y + barH);
	const int labelW = labelChars * kLoadGlyphW;
	y = l.bar.bottom + kLoadGlyphH / 2;
	l.label = Common::Rect((w - labelW) / 2, y, (w - labelW) / 2 + labelW, y + kLoadGlyphH);
	return l;
}

/** Inside of @p bar (1 pixel outline, 1 pixel gap) filled to @p permille, as a rect. */
inline Common::Rect loadBarFill(const Common::Rect &bar, uint permille) {
	Common::Rect in(bar.left + 2, bar.top + 2, bar.right - 2, bar.bottom - 2);
	if (in.isEmpty())
		return Common::Rect(bar.left, bar.top, bar.left, bar.top);
	const int wFill = in.width() * (int)MIN<uint>(permille, 1000) / 1000;
	in.right = in.left + wFill;
	return in;
}

/**
 * A raw w x h surface, 1, 2 or 4 bytes a pixel, that the loading screen
 * draws on. Pixel values are already in the surface's format. Every call
 * clips to the surface.
 */
class LoadCanvas {
public:
	LoadCanvas(byte *pixels, int pitch, int w, int h, int bpp) : _pixels(pixels), _pitch(pitch), _w(w), _h(h), _bpp(bpp) {}

	void fill(const Common::Rect &r, uint32 color) {
		Common::Rect c(r);
		c.clip(Common::Rect(_w, _h));
		if (c.isEmpty())
			return;
		// Row by row: a whole 640x400 screen, pixel by pixel through put(),
		// took a tenth of a second at 60000 cycles.
		for (int y = c.top; y < c.bottom; ++y) {
			byte *row = _pixels + y * _pitch + c.left * _bpp;
			const int n = c.width();
			switch (_bpp) {
			case 1:
				memset(row, (byte)color, n);
				break;
			case 2:
				for (int x = 0; x < n; ++x)
					((uint16 *)row)[x] = (uint16)color;
				break;
			default:
				for (int x = 0; x < n; ++x)
					((uint32 *)row)[x] = color;
				break;
			}
		}
	}

	/** A 1-pixel outline of @p r. */
	void frame(const Common::Rect &r, uint32 color) {
		fill(Common::Rect(r.left, r.top, r.right, r.top + 1), color);
		fill(Common::Rect(r.left, r.bottom - 1, r.right, r.bottom), color);
		fill(Common::Rect(r.left, r.top, r.left + 1, r.bottom), color);
		fill(Common::Rect(r.right - 1, r.top, r.right, r.bottom), color);
	}

	/** 1bpp rows, MSB first, @p pitch bytes each: set bits in @p color, the rest untouched. */
	void bitmap(int x0, int y0, const byte *bits, int pitch, int w, int h, uint32 color) {
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (bits[y * pitch + x / 8] & (0x80 >> (x % 8)))
					put(x0 + x, y0 + y, color);
	}

	/** @p text in an 8x16 font (256 glyphs of 16 bytes, as the VGA BIOS has it). */
	void text(int x0, int y0, const char *text, const byte *font, uint32 color) {
		if (!font)
			return;
		for (int i = 0; text[i]; ++i)
			bitmap(x0 + i * kLoadGlyphW, y0, font + (byte)text[i] * kLoadGlyphH, 1, kLoadGlyphW, kLoadGlyphH, color);
	}

private:
	void put(int x, int y, uint32 color) {
		if (x < 0 || y < 0 || x >= _w || y >= _h)
			return;
		byte *p = _pixels + y * _pitch + x * _bpp;
		switch (_bpp) {
		case 1: *p = (byte)color; break;
		case 2: *(uint16 *)p = (uint16)color; break;
		default: *(uint32 *)p = color; break;
		}
	}

	byte *_pixels;
	int _pitch, _w, _h, _bpp;
};

/** A colour is lit if any channel reaches this (of 255): above black on a 6-bit DAC. */
static const byte kLoadLit = 16;

/**
 * The first-frame test's cheap first look (markUsedColors(), anyPixelLit())
 * reads every kLoadGateStep-th pixel of every kLoadGateStep-th row of each
 * rectangle the game drew, from its top left: a game redraws its whole
 * screen for each step of a fade, and this runs on each. A rectangle
 * thinner than that still gets its first row and column read (SCI's
 * transitions draw the first picture in strips a pixel or two wide).
 */
static const int kLoadGateStep = 4;

/** Marks in @p used[256] the CLUT8 indices found in @p r of the surface (see kLoadGateStep). */
inline void markUsedColors(const byte *pixels, int pitch, const Common::Rect &r, bool used[256]) {
	for (int y = r.top; y < r.bottom; y += kLoadGateStep) {
		const byte *p = pixels + y * pitch;
		for (int x = r.left; x < r.right; x += kLoadGateStep)
			used[p[x]] = true;
	}
}

/** Whether any index marked in @p used has a lit colour in @p palette (RGB triplets). */
inline bool anyUsedColorLit(const bool used[256], const byte *palette) {
	for (int i = 0; i < 256; ++i)
		if (used[i] && (palette[i * 3] >= kLoadLit || palette[i * 3 + 1] >= kLoadLit || palette[i * 3 + 2] >= kLoadLit))
			return true;
	return false;
}

/**
 * Whether a true-colour surface (@p f, 2 or 4 bytes a pixel) has a lit
 * pixel in @p r (see kLoadGateStep).
 */
inline bool anyPixelLit(const byte *pixels, int pitch, const Graphics::PixelFormat &f, const Common::Rect &r) {
	const int x0 = r.left;
	// 8 bits a channel: a channel >= 16 has one of its top four bits set.
	const bool fast32 = f.bytesPerPixel == 4 && f.rLoss == 0 && f.gLoss == 0 && f.bLoss == 0;
	const uint32 mask = (0xF0u << f.rShift) | (0xF0u << f.gShift) | (0xF0u << f.bShift);
	for (int y = r.top; y < r.bottom; y += kLoadGateStep) {
		const byte *row = pixels + y * pitch;
		if (fast32) {
			uint32 any = 0;
			const uint32 *p = (const uint32 *)row;
			for (int x = x0; x < r.right; x += kLoadGateStep)
				any |= p[x];
			if (any & mask)
				return true;
			continue;
		}
		for (int x = x0; x < r.right; x += kLoadGateStep) {
			const uint32 c = f.bytesPerPixel == 2 ? *(const uint16 *)(row + x * 2) : *(const uint32 *)(row + x * 4);
			byte cr, cg, cb;
			f.colorToRGB(c, cr, cg, cb);
			if (cr >= kLoadLit || cg >= kLoadLit || cb >= kLoadLit)
				return true;
		}
	}
	return false;
}

/**
 * The lit pixels among every other pixel of every other row of a w x h
 * surface (@p f, 1, 2 or 4 bytes a pixel; @p palette, RGB triplets, for
 * CLUT8), counted up to @p limit.
 */
inline uint countLitSamples(const byte *pixels, int pitch, const Graphics::PixelFormat &f, const byte *palette, int w, int h, uint limit) {
	bool lit[256];
	if (f.bytesPerPixel == 1)
		for (int i = 0; i < 256; ++i)
			lit[i] = palette[i * 3] >= kLoadLit || palette[i * 3 + 1] >= kLoadLit || palette[i * 3 + 2] >= kLoadLit;
	// 8 bits a channel: a channel >= 16 has one of its top four bits set.
	const bool fast32 = f.bytesPerPixel == 4 && f.rLoss == 0 && f.gLoss == 0 && f.bLoss == 0;
	const uint32 mask = (0xF0u << f.rShift) | (0xF0u << f.gShift) | (0xF0u << f.bShift);
	uint n = 0;
	for (int y = 0; y < h && n < limit; y += 2) {
		const byte *row = pixels + y * pitch;
		if (fast32) {
			const uint32 *p = (const uint32 *)row;
			for (int x = 0; x < w; x += 2)
				n += (p[x] & mask) != 0;
			continue;
		}
		for (int x = 0; x < w; x += 2) {
			bool on;
			if (f.bytesPerPixel == 1) {
				on = lit[row[x]];
			} else {
				const uint32 c = f.bytesPerPixel == 2 ? *(const uint16 *)(row + x * 2) : *(const uint32 *)(row + x * 4);
				byte cr, cg, cb;
				f.colorToRGB(c, cr, cg, cb);
				on = cr >= kLoadLit || cg >= kLoadLit || cb >= kLoadLit;
			}
			n += on;
		}
	}
	return MIN(n, limit);
}

/**
 * The samples countLitSamples() must find lit for a w x h game frame to
 * count as its first real one: 0.5% of them, so a stray dot or dash
 * before the picture (KQ1's title has one for a fifth of a second) does
 * not end the loading screen on a black frame.
 */
inline uint firstFrameLitSamples(int w, int h) {
	return MAX<uint>(1, (uint)(((w + 1) / 2) * ((h + 1) / 2) / 200));
}

} // End of namespace DOS

#endif
