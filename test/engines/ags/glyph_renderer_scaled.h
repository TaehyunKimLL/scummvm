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


#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/hashmap.h"
#include "common/system.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/hires_text/unicode_props.h"

#include "ags/shared/font/glyph_font_draw.h"

namespace {

/**
 * A glyph source of solid ink boxes: each glyph covers columns
 * [inkX, inkX + inkW) and rows [inkY, inkY + inkH) of its cell at full
 * coverage, with its own advance and originX.
 */
class AgsBoxGlyphSource : public Graphics::UnicodeGlyphSource {
public:
	struct Glyph {
		int inkX, inkW, inkY, inkH;
		int16 advance, originX;
	};

	AgsBoxGlyphSource(int cellW, int cellH) : _cellW(cellW), _cellH(cellH), _row(cellW * 2, 0) {}

	void add(uint32 cp, int16 advance, int16 originX, int inkX, int inkW, int inkY, int inkH) {
		Glyph g = { inkX, inkW, inkY, inkH, advance, originX };
		_glyphs[cp] = g;
	}

	byte cellWidth() const override { return (byte)_cellW; }
	byte cellHeight() const override { return (byte)_cellH; }
	byte advanceNarrow() const override { return (byte)(_cellW / 2); }
	byte advanceWide() const override { return (byte)_cellW; }
	int bitsPerPixel() const override { return 8; }
	int cells(uint32 cp) override { return _glyphs.contains(cp) ? 1 : 0; }
	const byte *row(uint32 cp, int y) override {
		const Glyph &g = _glyphs[cp];
		for (int x = 0; x < _cellW * 2; x++)
			_row[x] = (y >= g.inkY && y < g.inkY + g.inkH && x >= g.inkX && x < g.inkX + g.inkW) ? 255 : 0;
		return _row.begin();
	}
	int advance(uint32 cp) override { return _glyphs.contains(cp) ? _glyphs[cp].advance : 0; }
	bool metrics(uint32 cp, Graphics::GlyphMetrics &m) override {
		if (!_glyphs.contains(cp))
			return false;
		m = Graphics::GlyphMetrics();
		m.advance = _glyphs[cp].advance;
		m.originX = _glyphs[cp].originX;
		m.combining = Graphics::Unicode::isCombining(cp);
		return true;
	}
	uint32 glyphCount() const override { return _glyphs.size(); }

private:
	int _cellW, _cellH;
	Common::Array<byte> _row;
	Common::HashMap<uint32, Glyph> _glyphs;
};

/** The N x half for the drawer: a source and a constant row shift. */
class AgsFakeScaled : public AGS3::ScaledGlyphs {
public:
	AgsFakeScaled(Graphics::UnicodeGlyphSource *src, int scale, int shift = 0) : _src(src), _scale(scale), _shift(shift) {}
	Graphics::UnicodeGlyphSource *source() override { return _src; }
	int scale() const override { return _scale; }
	int rowShift(uint32 cp) override { return _shift; }
private:
	Graphics::UnicodeGlyphSource *_src;
	int _scale, _shift;
};

/** A real face at two sizes; the row shift puts the big baseline at N x the small one. */
class AgsTtfScaled : public AGS3::ScaledGlyphs {
public:
	AgsTtfScaled(Graphics::TtfGlyphSource *small, Graphics::TtfGlyphSource *big, int scale)
		: _small(small), _big(big), _scale(scale) {}
	Graphics::UnicodeGlyphSource *source() override { return _big; }
	int scale() const override { return _scale; }
	int rowShift(uint32 cp) override { return _scale * _small->baseline() - _big->baseline(); }
private:
	Graphics::TtfGlyphSource *_small, *_big;
	int _scale;
};

/** Records drawCharScaled(); 5 game pixels per character. */
class AgsFakeScaledFallback : public AGS3::GlyphFallback {
public:
	AgsFakeScaledFallback() : scaledCalls(0), lastX(-1), lastY(-1), lastScale(0) {}
	int charWidth(uint32 cp) override { return 5; }
	void drawChar(uint32 cp, int x, int y, uint32 colour) override {}
	void drawCharScaled(uint32 cp, int x, int y, uint32 colour, int scale) override {
		scaledCalls++;
		lastX = x;
		lastY = y;
		lastScale = scale;
	}
	int scaledCalls, lastX, lastY, lastScale;
};

Graphics::TtfGlyphSource *agsOpenTtf(const char *path, int size, const uint32 *probes = nullptr, uint probeCount = 0) {
	Common::FSNode node(path);
	if (!node.exists())
		return nullptr;
	Common::SeekableReadStream *s = node.createReadStream();
	if (!s)
		return nullptr;
	Common::String error;
	return Graphics::TtfGlyphSource::create(s, DisposeAfterUse::YES, size, error, false, false, probes, probeCount);
}

const Graphics::PixelFormat kArgbScaled(4, 8, 8, 8, 8, 16, 8, 0, 24);

void agsClear8(Graphics::Surface &s) {
	memset(s.getPixels(), 0, s.pitch * s.h);
}

/** Leftmost and rightmost inked column in [x0, x1) of an 8-bit surface; -1 if none. */
void agsInkColumns(const Graphics::Surface &s, int x0, int x1, int &left, int &right) {
	left = right = -1;
	for (int x = MAX(0, x0); x < MIN((int)s.w, x1); x++)
		for (int y = 0; y < s.h; y++)
			if (*(const byte *)s.getBasePtr(x, y)) {
				if (left < 0)
					left = x;
				right = x;
				break;
			}
}

/** Bounding box of the ink of an 8-bit surface; empty if none. */
Common::Rect agsInkBox(const Graphics::Surface &s) {
	Common::Rect r;
	bool any = false;
	for (int y = 0; y < s.h; y++)
		for (int x = 0; x < s.w; x++)
			if (*(const byte *)s.getBasePtr(x, y)) {
				if (!any)
					r = Common::Rect(x, y, x + 1, y + 1);
				else
					r.extend(Common::Rect(x, y, x + 1, y + 1));
				any = true;
			}
	return r;
}

} // End of anonymous namespace

/**
 * GlyphTextDrawer::drawTextScaled(): text drawn N x into an N x target
 * with the game-resolution pen positions (AGS_HIRES_TEXT_DESIGN.md
 * section 4.3, ruling R2).
 */
class AgsGlyphRendererScaledTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		// the real-face tests open files through FSNode
		if (!g_system)
			Common::install_null_g_system();
	}

	void tearDown() {
		Common::uninstall_null_g_system();
	}

	void test_pens_are_n_times_the_game_pens() {
		// Game font: A advances 5, B 6. The 2x faces round differently
		// (11 and 13): each cluster still starts at 2 x its game pen.
		AgsBoxGlyphSource small(8, 4), big(16, 8);
		small.add('A', 5, 0, 0, 4, 0, 4);
		small.add('B', 6, 0, 0, 5, 0, 4);
		big.add('A', 11, 0, 0, 9, 0, 8);
		big.add('B', 13, 0, 0, 11, 0, 8);
		AgsFakeScaled scaled(&big, 2);
		AGS3::GlyphTextDrawer d(&small, true);
		Graphics::Surface s;
		s.create(80, 8, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(s);
		const uint32 text[] = { 'A', 'B', 'A' };
		d.drawTextScaled(s, Common::Rect(0, 0, 80, 8), text, 3, 3, 0, 9, nullptr, scaled);
		int l, r;
		agsInkColumns(s, 0, 16, l, r);
		TS_ASSERT_EQUALS(l, 6);          // A at 2 x 3
		agsInkColumns(s, 16, 28, l, r);
		TS_ASSERT_EQUALS(l, 16);         // B at 2 x (3 + 5), not 6 + 11
		agsInkColumns(s, 28, 80, l, r);
		TS_ASSERT_EQUALS(l, 28);         // A at 2 x (3 + 5 + 6)
		s.free();
	}

	void test_line_covers_n_times_its_game_box() {
		// Faces that scale exactly: every covered pixel lies inside 2 x the
		// game-resolution text rect (pen x .. x + width, top .. top + cell).
		AgsBoxGlyphSource small(8, 5), big(16, 10);
		small.add('A', 5, 0, 0, 5, 1, 4);
		small.add('B', 6, 0, 1, 5, 0, 5);
		big.add('A', 10, 0, 0, 10, 2, 8);
		big.add('B', 12, 0, 2, 10, 0, 10);
		AgsFakeScaled scaled(&big, 2);
		AGS3::GlyphTextDrawer d(&small, true);
		const uint32 text[] = { 'A', 'B', 'B', 'A' };
		const int x = 4, y = 3;
		const int width = d.textWidth(text, 4, nullptr);
		Graphics::Surface s;
		s.create(100, 30, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(s);
		d.drawTextScaled(s, Common::Rect(0, 0, 100, 30), text, 4, x, y, 9, nullptr, scaled);
		const Common::Rect ink = agsInkBox(s);
		const Common::Rect box(2 * x, 2 * y, 2 * (x + width), 2 * (y + small.cellHeight()));
		TS_ASSERT(!ink.isEmpty());
		TS_ASSERT(box.contains(ink));
		TS_ASSERT_EQUALS(ink.left, box.left);
		TS_ASSERT_EQUALS(ink.right, box.right);
		s.free();
	}

	void test_row_shift_moves_the_cell() {
		AgsBoxGlyphSource small(4, 4), big(8, 8);
		small.add('A', 4, 0, 0, 4, 0, 1);
		big.add('A', 8, 0, 0, 8, 0, 1);   // ink on the cell's first row
		AgsFakeScaled scaled(&big, 2, 3);
		AGS3::GlyphTextDrawer d(&small, true);
		Graphics::Surface s;
		s.create(16, 16, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(s);
		const uint32 text[] = { 'A' };
		d.drawTextScaled(s, Common::Rect(0, 0, 16, 16), text, 1, 0, 2, 9, nullptr, scaled);
		const Common::Rect ink = agsInkBox(s);
		TS_ASSERT_EQUALS(ink.top, 2 * 2 + 3);
		TS_ASSERT_EQUALS(ink.height(), 1);
		s.free();
	}

	void test_mark_placed_against_its_base_with_n_times_metrics() {
		// Game: base advance 3, mark origin 2. 2x: base advance 7, mark
		// origin 5. The mark sits at 2 x base pen + 7 - 5, where the 2x
		// base glyph ends, not at the grid-locked next pen (2 x 3).
		AgsBoxGlyphSource small(4, 1), big(8, 2);
		small.add(0x0E17, 3, 0, 0, 1, 0, 1);
		small.add(0x0E48, 0, 2, 0, 1, 0, 1);
		big.add(0x0E17, 7, 0, 0, 1, 0, 2);
		big.add(0x0E48, 0, 5, 0, 1, 0, 2);
		AgsFakeScaled scaled(&big, 2);
		AGS3::GlyphTextDrawer d(&small, true);
		Graphics::Surface s;
		s.create(32, 2, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(s);
		const uint32 text[] = { 0x0E17, 0x0E48 };
		d.drawTextScaled(s, Common::Rect(0, 0, 32, 2), text, 2, 4, 0, 9, nullptr, scaled);
		TS_ASSERT_EQUALS(*(const byte *)s.getBasePtr(8, 0), 9);       // base at 2 x 4
		TS_ASSERT_EQUALS(*(const byte *)s.getBasePtr(8 + 7 - 5, 0), 9);  // the mark
		int l, r;
		agsInkColumns(s, 0, 32, l, r);
		TS_ASSERT_EQUALS(r, 10);
		s.free();
	}

	void test_missing_glyph_goes_to_the_game_font_upscaled() {
		AgsBoxGlyphSource small(8, 4), big(16, 8);
		small.add('A', 6, 0, 0, 4, 0, 4);
		big.add('A', 12, 0, 0, 8, 0, 8);
		AgsFakeScaled scaled(&big, 3);
		AgsFakeScaledFallback fb;
		AGS3::GlyphTextDrawer d(&small, true);
		Graphics::Surface s;
		s.create(80, 12, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(s);
		const uint32 text[] = { 'A', 'Z', 'A' };
		d.drawTextScaled(s, Common::Rect(0, 0, 80, 12), text, 3, 1, 0, 9, &fb, scaled);
		TS_ASSERT_EQUALS(fb.scaledCalls, 1);
		TS_ASSERT_EQUALS(fb.lastX, 7);     // the game pen, 1 + 6
		TS_ASSERT_EQUALS(fb.lastY, 0);
		TS_ASSERT_EQUALS(fb.lastScale, 3);
		int l, r;
		agsInkColumns(s, 30, 80, l, r);
		TS_ASSERT_EQUALS(l, 3 * (7 + 5));  // the second A after the fallback's 5 px
		s.free();
	}

	void test_clip_is_in_target_pixels() {
		AgsBoxGlyphSource small(4, 1), big(8, 2);
		small.add('X', 4, 0, 0, 4, 0, 1);
		big.add('X', 8, 0, 0, 8, 0, 2);
		AgsFakeScaled scaled(&big, 2);
		AGS3::GlyphTextDrawer d(&small, true);
		Graphics::Surface s;
		s.create(8, 2, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(s);
		const uint32 text[] = { 'X' };
		d.drawTextScaled(s, Common::Rect(3, 0, 5, 1), text, 1, 0, 0, 9, nullptr, scaled);
		const Common::Rect ink = agsInkBox(s);
		TS_ASSERT_EQUALS(ink, Common::Rect(3, 0, 5, 1));
		s.free();
	}

	void test_real_face_pens_and_box() {
		// Arial at 10 px and at 20 px (baselines 8 and 17: the row shift
		// matters): the line stays inside 2 x its game box (vertically
		// exactly), and starts and ends within a pixel or two of 2 x where
		// the game's does.
		Graphics::TtfGlyphSource *small = agsOpenTtf("/System/Library/Fonts/Supplemental/Arial.ttf", 10);
		Graphics::TtfGlyphSource *big = agsOpenTtf("/System/Library/Fonts/Supplemental/Arial.ttf", 20);
		if (!small || !big) {
			delete small;
			delete big;
			TS_SKIP("Arial.ttf not available");
		}
		AgsTtfScaled scaled(small, big, 2);
		AGS3::GlyphTextDrawer d(small, true);
		const char *str = "Hello, Wilma";
		Common::Array<uint32> text;
		for (const char *p = str; *p; p++)
			text.push_back((byte)*p);
		const int width = d.textWidth(text.begin(), text.size(), nullptr);

		Graphics::Surface lo, hi;
		lo.create(width + 8, small->cellHeight() + 4, Graphics::PixelFormat::createFormatCLUT8());
		hi.create(2 * lo.w, 2 * lo.h, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(lo);
		agsClear8(hi);
		d.drawText(lo, Common::Rect(0, 0, lo.w, lo.h), text.begin(), text.size(), 2, 1, 9, nullptr);
		d.drawTextScaled(hi, Common::Rect(0, 0, hi.w, hi.h), text.begin(), text.size(), 2, 1, 9, nullptr, scaled);
		const Common::Rect loInk = agsInkBox(lo), hiInk = agsInkBox(hi);
		TS_ASSERT(!loInk.isEmpty());
		// the big ink inside 2 x the game text rect (pen .. pen + width,
		// top .. top + cell), or 2 x the game ink where that overhangs it,
		// give or take one game pixel sideways: the last glyph is the 2x
		// face's, which may end a pixel past 2 x the small face's advance
		// ([measured] here: 'a' ends at 95, the box at 94)
		Common::Rect box(2 * 2, 2 * 1, 2 * (2 + width), 2 * (1 + small->cellHeight()));
		box.extend(Common::Rect(2 * loInk.left, 2 * loInk.top, 2 * loInk.right, 2 * loInk.bottom));
		TS_ASSERT_LESS_THAN_EQUALS(box.left - 2, hiInk.left);
		TS_ASSERT_LESS_THAN_EQUALS(box.top, hiInk.top);
		TS_ASSERT_LESS_THAN_EQUALS(hiInk.right, box.right + 2);
		TS_ASSERT_LESS_THAN_EQUALS(hiInk.bottom, box.bottom);
		// the line's start and end: at 2 x the game ones
		TS_ASSERT_LESS_THAN_EQUALS(ABS(hiInk.left - 2 * loInk.left), 2);
		TS_ASSERT_LESS_THAN_EQUALS(ABS(hiInk.right - 2 * loInk.right), 2);
		lo.free();
		hi.free();
		delete small;
		delete big;
	}

	void test_real_thai_mark_stays_over_its_base() {
		// ที่ (U+0E17 U+0E35 U+0E48) at 12 and 24 px: the marks' ink lies
		// over the base's columns, as at game resolution.
		// the translation's marks in the vertical fit, as SetTranslationSample() does
		const uint32 probes[] = { 0x0E17, 0x0E35, 0x0E48 };
		Graphics::TtfGlyphSource *small = agsOpenTtf("/System/Library/Fonts/Supplemental/Ayuthaya.ttf", 12, probes, 3);
		Graphics::TtfGlyphSource *big = agsOpenTtf("/System/Library/Fonts/Supplemental/Ayuthaya.ttf", 24, probes, 3);
		if (!small || !big) {
			delete small;
			delete big;
			TS_SKIP("Ayuthaya.ttf not available");
		}
		AgsTtfScaled scaled(small, big, 2);
		AGS3::GlyphTextDrawer d(small, true);
		const uint32 base[] = { 0x0E17 };
		const uint32 full[] = { 0x0E17, 0x0E35, 0x0E48 };
		Graphics::Surface a, b;
		a.create(80, 2 * small->cellHeight() + 8, Graphics::PixelFormat::createFormatCLUT8());
		b.create(80, 2 * small->cellHeight() + 8, Graphics::PixelFormat::createFormatCLUT8());
		agsClear8(a);
		agsClear8(b);
		d.drawTextScaled(a, Common::Rect(0, 0, a.w, a.h), base, 1, 10, 1, 9, nullptr, scaled);
		d.drawTextScaled(b, Common::Rect(0, 0, b.w, b.h), full, 3, 10, 1, 9, nullptr, scaled);
		const Common::Rect baseInk = agsInkBox(a), allInk = agsInkBox(b);
		TS_ASSERT(!baseInk.isEmpty());
		// the marks add ink above the base, not beside it
		TS_ASSERT_LESS_THAN(allInk.top, baseInk.top);
		TS_ASSERT_LESS_THAN_EQUALS(baseInk.left - 1, allInk.left);
		TS_ASSERT_LESS_THAN_EQUALS(allInk.right, baseInk.right + 1);
		a.free();
		b.free();
		delete small;
		delete big;
	}

	// --- the game's own rendering, upscaled -------------------------------

	void test_upscale_onto_skips_the_key_and_fills_blocks() {
		// A 3x1 game rendering: key, red, key. At 2x at (1, 0): the red
		// pixel is a 2x2 block at (1 + 2 x 1, 0); nothing else changes.
		const uint32 key = 0x00FF00FF, red = 0xFFFF0000, bg = 0xFF000000;
		Graphics::Surface src, dst;
		src.create(3, 1, kArgbScaled);
		*(uint32 *)src.getBasePtr(0, 0) = key;
		*(uint32 *)src.getBasePtr(1, 0) = red;
		*(uint32 *)src.getBasePtr(2, 0) = key;
		dst.create(10, 3, kArgbScaled);
		for (int y = 0; y < 3; y++)
			for (int x = 0; x < 10; x++)
				*(uint32 *)dst.getBasePtr(x, y) = bg;
		AGS3::GlyphTextDrawer::upscaleOnto(dst, Common::Rect(0, 0, 10, 3), src, key, 1, 0, 2);
		for (int y = 0; y < 3; y++)
			for (int x = 0; x < 10; x++) {
				const bool ink = (x == 3 || x == 4) && y < 2;
				TS_ASSERT_EQUALS(*(const uint32 *)dst.getBasePtr(x, y), ink ? red : bg);
			}
		src.free();
		dst.free();
	}

	void test_upscale_onto_blends_partial_alpha_and_clips() {
		// alfont over the mask colour leaves colour with alpha = coverage:
		// that is blended by its alpha, as a glyph's coverage would be.
		const uint32 key = 0x00FF00FF, bg = 0xFF000000;
		Graphics::Surface src, dst;
		src.create(2, 1, kArgbScaled);
		*(uint32 *)src.getBasePtr(0, 0) = 0x80FFFFFF;
		*(uint32 *)src.getBasePtr(1, 0) = 0xFFFFFFFF;
		dst.create(4, 2, kArgbScaled);
		for (int y = 0; y < 2; y++)
			for (int x = 0; x < 4; x++)
				*(uint32 *)dst.getBasePtr(x, y) = bg;
		AGS3::GlyphTextDrawer::upscaleOnto(dst, Common::Rect(0, 0, 3, 2), src, key, 0, 0, 2);
		TS_ASSERT_EQUALS(*(const uint32 *)dst.getBasePtr(0, 0), 0xFF808080u);
		TS_ASSERT_EQUALS(*(const uint32 *)dst.getBasePtr(1, 1), 0xFF808080u);
		TS_ASSERT_EQUALS(*(const uint32 *)dst.getBasePtr(2, 0), 0xFFFFFFFFu);
		TS_ASSERT_EQUALS(*(const uint32 *)dst.getBasePtr(3, 0), bg);   // clipped
		src.free();
		dst.free();
	}
};
