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

#include "common/hashmap.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/unicode_props.h"

#include "ags/shared/font/glyph_font_draw.h"

namespace {

/**
 * A glyph source with hand-made glyphs: one row each, 8 bpp, 4 px cells
 * (row stride 8 px). A glyph is its coverage row, its advance and originX.
 */
class AgsFakeGlyphSource : public Graphics::UnicodeGlyphSource {
public:
	struct Glyph {
		byte row[8];
		int16 advance;
		int16 originX;
	};

	void add(uint32 cp, int16 advance, int16 originX, byte c0, byte c1 = 0, byte c2 = 0, byte c3 = 0) {
		Glyph g;
		memset(g.row, 0, sizeof(g.row));
		g.row[0] = c0; g.row[1] = c1; g.row[2] = c2; g.row[3] = c3;
		g.advance = advance;
		g.originX = originX;
		_glyphs[cp] = g;
	}

	byte cellWidth() const override { return 4; }
	byte cellHeight() const override { return 1; }
	byte advanceNarrow() const override { return 2; }
	byte advanceWide() const override { return 4; }
	int bitsPerPixel() const override { return 8; }
	int cells(uint32 cp) override { return _glyphs.contains(cp) ? 1 : 0; }
	const byte *row(uint32 cp, int y) override { return _glyphs[cp].row; }
	int advance(uint32 cp) override { return _glyphs.contains(cp) ? _glyphs[cp].advance : 0; }
	bool metrics(uint32 cp, Graphics::GlyphMetrics &m) override {
		if (!_glyphs.contains(cp))
			return false;
		m = Graphics::GlyphMetrics();
		m.advance = _glyphs[cp].advance;	// a spacing mark keeps it: the drawer must not advance
		m.originX = _glyphs[cp].originX;
		m.combining = Graphics::Unicode::isCombining(cp);
		return true;
	}
	uint32 glyphCount() const override { return _glyphs.size(); }

private:
	Common::HashMap<uint32, Glyph> _glyphs;
};

/** The game's own font for what the source lacks: 5 px per character. */
class AgsFakeFallback : public AGS3::GlyphFallback {
public:
	AgsFakeFallback() : drawn(0), lastCp(0), lastX(-1) {}
	int charWidth(uint32 cp) override { return 5; }
	void drawChar(uint32 cp, int x, int y, uint32 colour) override {
		drawn++;
		lastCp = cp;
		lastX = x;
	}
	int drawn;
	uint32 lastCp;
	int lastX;
};

const Graphics::PixelFormat kArgb(4, 8, 8, 8, 8, 16, 8, 0, 24);
const Graphics::PixelFormat kRgb565(2, 5, 6, 5, 0, 11, 5, 0, 0);

uint32 px32(const Graphics::Surface &s, int x) { return *(const uint32 *)s.getBasePtr(x, 0); }
uint16 px16(const Graphics::Surface &s, int x) { return *(const uint16 *)s.getBasePtr(x, 0); }
byte px8(const Graphics::Surface &s, int x) { return *(const byte *)s.getBasePtr(x, 0); }

} // End of anonymous namespace

/**
 * GlyphFontRenderer's drawing half (AGS fonts from hires_text.map): per-glyph
 * placement with zero-advance combining marks, coverage blended into 16/32-bit
 * targets, thresholded into 8-bit ones (I18N_TEXT_DESIGN.md section 4.2).
 */
class AgsGlyphRendererTestSuite : public CxxTest::TestSuite {
public:
	void test_coverage_blends_into_32bit() {
		AgsFakeGlyphSource src;
		src.add('X', 4, 0, 0, 64, 128, 255);
		Graphics::Surface s;
		s.create(4, 1, kArgb);
		for (int x = 0; x < 4; x++)
			*(uint32 *)s.getBasePtr(x, 0) = 0xFF000000;
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 'X' };
		d.drawText(s, Common::Rect(0, 0, 4, 1), text, 1, 0, 0, 0xFFFFFFFF, nullptr);
		TS_ASSERT_EQUALS(px32(s, 0), 0xFF000000u);
		TS_ASSERT_EQUALS(px32(s, 1), 0xFF404040u);
		TS_ASSERT_EQUALS(px32(s, 2), 0xFF808080u);
		TS_ASSERT_EQUALS(px32(s, 3), 0xFFFFFFFFu);
		s.free();
	}

	void test_coverage_thresholds_into_8bit() {
		AgsFakeGlyphSource src;
		src.add('X', 4, 0, 0, 64, 128, 255);
		Graphics::Surface s;
		s.create(4, 1, Graphics::PixelFormat::createFormatCLUT8());
		memset(s.getPixels(), 0, 4);
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 'X' };
		d.drawText(s, Common::Rect(0, 0, 4, 1), text, 1, 0, 0, 7, nullptr);
		TS_ASSERT_EQUALS(px8(s, 0), 0);
		TS_ASSERT_EQUALS(px8(s, 1), 0);
		TS_ASSERT_EQUALS(px8(s, 2), 7);
		TS_ASSERT_EQUALS(px8(s, 3), 7);
		s.free();
	}

	void test_alpha_off_thresholds_32bit() {
		AgsFakeGlyphSource src;
		src.add('X', 4, 0, 0, 64, 128, 255);
		Graphics::Surface s;
		s.create(4, 1, kArgb);
		for (int x = 0; x < 4; x++)
			*(uint32 *)s.getBasePtr(x, 0) = 0xFF000000;
		AGS3::GlyphTextDrawer d(&src, false);
		const uint32 text[] = { 'X' };
		d.drawText(s, Common::Rect(0, 0, 4, 1), text, 1, 0, 0, 0xFFFFFFFF, nullptr);
		TS_ASSERT_EQUALS(px32(s, 1), 0xFF000000u);
		TS_ASSERT_EQUALS(px32(s, 2), 0xFFFFFFFFu);
		s.free();
	}

	void test_coverage_blends_into_16bit() {
		AgsFakeGlyphSource src;
		src.add('X', 4, 0, 0, 64, 128, 255);
		Graphics::Surface s;
		s.create(4, 1, kRgb565);
		memset(s.getPixels(), 0, 8);
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 'X' };
		d.drawText(s, Common::Rect(0, 0, 4, 1), text, 1, 0, 0, 0xFFFF, nullptr);
		TS_ASSERT_EQUALS(px16(s, 0), 0);
		TS_ASSERT_EQUALS(px16(s, 2), kRgb565.RGBToColor(0x80, 0x80, 0x80));
		TS_ASSERT_EQUALS(px16(s, 3), 0xFFFF);
		s.free();
	}

	void test_transparent_pixel_takes_coverage_as_alpha() {
		// alfont's rule over the mask colour: colour RGB, alpha = coverage.
		TS_ASSERT_EQUALS(AGS3::GlyphTextDrawer::blendPixel(0x00FF00FF, 0xFFFFFFFF, 64, kArgb), 0x40FFFFFFu);
		TS_ASSERT_EQUALS(AGS3::GlyphTextDrawer::blendPixel(0x00000000, 0xFF102030, 128, kArgb), 0x80102030u);
		// 16-bit has no alpha: the colour where coverage >= 128, else the mask stays.
		const uint32 mask16 = kRgb565.RGBToColor(255, 0, 255);
		TS_ASSERT_EQUALS(AGS3::GlyphTextDrawer::blendPixel(mask16, 0x07E0, 64, kRgb565), mask16);
		TS_ASSERT_EQUALS(AGS3::GlyphTextDrawer::blendPixel(mask16, 0x07E0, 200, kRgb565), 0x07E0u);
	}

	void test_width_is_the_sum_of_advances() {
		AgsFakeGlyphSource src;
		src.add(0xAC00, 12, 0, 255);   // 가
		src.add('A', 6, 0, 255);
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 0xAC00, 'A' };
		TS_ASSERT_EQUALS(d.textWidth(text, 2, nullptr), 18);
	}

	void test_marks_do_not_advance() {
		// ที่: the base U+0E17 and two marks, which even a source that
		// gives them an advance (a face that needs shaping) must not add.
		AgsFakeGlyphSource src;
		src.add(0x0E17, 9, 0, 255);
		src.add(0x0E35, 7, 3, 255);
		src.add(0x0E48, 5, 2, 255);
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 0x0E17, 0x0E35, 0x0E48 };
		TS_ASSERT_EQUALS(d.textWidth(text, 3, nullptr), 9);
	}

	void test_mark_drawn_against_the_base() {
		// Base: ink in column 0, advance 3. Mark: origin at column 2 of its
		// row, ink in column 0, i.e. 2 px left of the pen after the base.
		AgsFakeGlyphSource src;
		src.add(0x0E17, 3, 0, 255);
		src.add(0x0E48, 0, 2, 255);
		src.add('A', 2, 0, 255);
		Graphics::Surface s;
		s.create(8, 1, Graphics::PixelFormat::createFormatCLUT8());
		memset(s.getPixels(), 0, 8);
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 0x0E17, 0x0E48, 'A' };
		d.drawText(s, Common::Rect(0, 0, 8, 1), text, 3, 2, 0, 9, nullptr);
		TS_ASSERT_EQUALS(px8(s, 2), 9);   // base at pen 2
		TS_ASSERT_EQUALS(px8(s, 3), 9);   // mark: anchor 5 - originX 2
		TS_ASSERT_EQUALS(px8(s, 4), 0);
		TS_ASSERT_EQUALS(px8(s, 5), 9);   // 'A' at 5: the mark did not advance
		s.free();
	}

	void test_missing_glyph_goes_to_the_game_font() {
		AgsFakeGlyphSource src;
		src.add('A', 6, 0, 255);
		AgsFakeFallback fb;
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 'A', 'Z', 'A' };
		TS_ASSERT_EQUALS(d.textWidth(text, 3, &fb), 17);
		TS_ASSERT_EQUALS(d.textWidth(text, 3, nullptr), 12);
		Graphics::Surface s;
		s.create(20, 1, Graphics::PixelFormat::createFormatCLUT8());
		memset(s.getPixels(), 0, 20);
		d.drawText(s, Common::Rect(0, 0, 20, 1), text, 3, 1, 0, 9, &fb);
		TS_ASSERT_EQUALS(fb.drawn, 1);
		TS_ASSERT_EQUALS(fb.lastCp, (uint32)'Z');
		TS_ASSERT_EQUALS(fb.lastX, 7);
		TS_ASSERT_EQUALS(px8(s, 12), 9);  // the second 'A' after the fallback's 5 px
		s.free();
	}

	void test_clip_is_respected() {
		AgsFakeGlyphSource src;
		src.add('X', 4, 0, 255, 255, 255, 255);
		Graphics::Surface s;
		s.create(4, 1, Graphics::PixelFormat::createFormatCLUT8());
		memset(s.getPixels(), 0, 4);
		AGS3::GlyphTextDrawer d(&src, true);
		const uint32 text[] = { 'X' };
		d.drawText(s, Common::Rect(1, 0, 3, 1), text, 1, -2, 0, 9, nullptr);
		TS_ASSERT_EQUALS(px8(s, 0), 0);
		TS_ASSERT_EQUALS(px8(s, 1), 9);
		TS_ASSERT_EQUALS(px8(s, 2), 0);   // the glyph ends at column 1 (drawn from -2)
		s.free();
	}
};
