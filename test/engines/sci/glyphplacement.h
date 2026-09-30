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

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/rect.h"
#include "common/str.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_svfn.h"
// scumm/detection.h and sci/detection.h (pulled in by textlayout16.h) each
// define GAMEOPTION_TTS; nothing here uses it.
#undef GAMEOPTION_TTS
#include "sci/graphics/hirestextsettings.h"
#include "sci/graphics/textlayer.h"
#include "sci/graphics/textlayout16.h"

#include "../../system/null_osystem.h"

using Sci::FontSettings;
using Sci::GlyphPlacement;

/**
 * design section 5.4 (C41): an SCI hi-res face rasterised at size= in a
 * layout cell that stays the engine's (cell=game), placed on the game
 * font's baseline (align=game) and moved by shift=.
 */
class SciGlyphPlacementTestSuite : public CxxTest::TestSuite {
private:
	static const Common::Path &mapDir() {
		static const Common::Path dir("/games/kq1");
		return dir;
	}

	static Graphics::HiResMap parseMap(const char *text) {
		Common::String full = "[map]\nversion=2\n";
		full += text;
		Common::MemoryReadStream stream((const byte *)full.c_str(), full.size());
		Graphics::HiResMap map;
		Common::Array<Common::String> qualifiers;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(stream, mapDir(), qualifiers, Graphics::kHiResKeysSci, map));
		return map;
	}

	static FontSettings resolve(const Graphics::HiResMap &map, int fontId) {
		return Sci::resolveFontSettings(map, true, fontId, Graphics::HiResIniOverrides(), mapDir(), mapDir());
	}

	/**
	 * A face rasterised in an 18 px cell: every glyph is a solid block
	 * from row 1 to row 17 (its baseline row 16 plus one descender row),
	 * one column in from each side; wide for Hangul, narrow (advance 7)
	 * otherwise. 8 bpp.
	 */
	class BlockSource : public Graphics::UnicodeGlyphSource {
	public:
		static const int kCell = 18;
		BlockSource() {
			for (int y = 0; y < kCell; y++)
				for (int x = 0; x < kCell * 2; x++)
					_rows[y][x] = (y >= 1 && y < kCell - 1 && x >= 1 && x < kCell - 1) ? 255 : 0;
		}
		byte cellWidth() const override { return kCell; }
		byte cellHeight() const override { return kCell; }
		byte advanceNarrow() const override { return kCell / 2; }
		byte advanceWide() const override { return kCell; }
		int bitsPerPixel() const override { return 8; }
		int cells(uint32 cp) override { return cp >= 0xAC00 && cp <= 0xD7A3 ? 2 : (cp >= 0x20 ? 1 : 0); }
		const byte *row(uint32 cp, int y) override { return _rows[y]; }
		int advance(uint32 cp) override { return cells(cp) == 1 ? 7 : kCell; }
		uint32 glyphCount() const override { return 1; }
	private:
		byte _rows[kCell][kCell * 2];
	};

	static GlyphPlacement place(int raster, int cell, GlyphPlacement::Align align, int rasterBaseline,
								int gameBaseline, int shift, int faceLineTop = GlyphPlacement::kUnknown) {
		GlyphPlacement::Input in;
		in.rasterWidth = raster;
		in.rasterHeight = raster;
		in.cellPx = cell;
		in.align = align;
		in.rasterBaseline = rasterBaseline < 0 ? GlyphPlacement::kUnknown : rasterBaseline;
		in.gameBaseline = gameBaseline < 0 ? GlyphPlacement::kUnknown : gameBaseline;
		in.faceLineTop = faceLineTop;
		in.shift = shift;
		return GlyphPlacement::compute(in);
	}

public:
	// --- the map keys -------------------------------------------------------

	void test_shift_cell_align_parse_per_font_and_map_wide() {
		const Graphics::HiResMap map = parseMap(
			"[font]\nshift=+3\ncell=glyph\nalign=cell\n"
			"[font.300]\nsize=18\nshift=-2\ncell=game\nalign=game\n"
			"[font.4]\nshift=0\n");
		TS_ASSERT(map.font.shiftSet);
		TS_ASSERT_EQUALS(map.font.shift, 3);
		TS_ASSERT(map.font.cellSet);
		TS_ASSERT_EQUALS(map.font.cell, Graphics::kHiResCellGlyph);
		TS_ASSERT(map.font.alignSet);
		TS_ASSERT_EQUALS(map.font.align, Graphics::kHiResAlignCell);
		const Graphics::HiResFontScope *f = map.fontIdScope(300);
		TS_ASSERT(f && f->shiftSet && f->cellSet && f->alignSet);
		TS_ASSERT_EQUALS(f->shift, -2);
		TS_ASSERT_EQUALS(f->cell, Graphics::kHiResCellGame);
		TS_ASSERT_EQUALS(f->align, Graphics::kHiResAlignGame);
		const Graphics::HiResFontScope *g = map.fontIdScope(4);
		TS_ASSERT(g && g->shiftSet && !g->cellSet && !g->alignSet);
		TS_ASSERT_EQUALS(g->shift, 0);
	}

	void test_bad_values_are_ignored() {
		const Graphics::HiResMap map = parseMap(
			"[font]\nshift=2.5\ncell=16\nalign=top\n"
			"[font.1]\nshift=-33\n[font.2]\nshift=--1\n[font.3]\nshift=\n[font.5]\nshift=-32\n");
		TS_ASSERT(!map.font.shiftSet);
		TS_ASSERT(!map.font.cellSet);
		TS_ASSERT(!map.font.alignSet);
		TS_ASSERT(!map.fontIdScope(1)->shiftSet);
		TS_ASSERT(!map.fontIdScope(2)->shiftSet);
		TS_ASSERT(!map.fontIdScope(3)->shiftSet);
		TS_ASSERT(map.fontIdScope(5)->shiftSet);
		TS_ASSERT_EQUALS(map.fontIdScope(5)->shift, -32);
	}

	void test_resolve_defaults_and_precedence() {
		// Nothing set: the 16 px cell, no shift, on the game's baseline.
		const FontSettings d = resolve(parseMap("[render]\nscale=2\n"), 0);
		TS_ASSERT_EQUALS(d.size, 16);
		TS_ASSERT_EQUALS(d.cell, 16);
		TS_ASSERT_EQUALS(d.baseline, 0);
		TS_ASSERT_EQUALS(d.align, Graphics::kHiResAlignGame);

		const Graphics::HiResMap map = parseMap(
			"[font]\nsize=20\nshift=1\n"
			"[font.300]\nsize=18\nshift=-2\n"
			"[font.4]\ncell=glyph\nalign=cell\n");
		// size= no longer grows the cell (cell=game is the default) ...
		const FontSettings s300 = resolve(map, 300);
		TS_ASSERT_EQUALS(s300.size, 18);
		TS_ASSERT_EQUALS(s300.cell, 16);
		TS_ASSERT_EQUALS(s300.baseline, -2);
		TS_ASSERT_EQUALS(s300.align, Graphics::kHiResAlignGame);
		// ... unless cell=glyph asks for the old behaviour; [font] fills in.
		const FontSettings s4 = resolve(map, 4);
		TS_ASSERT_EQUALS(s4.size, 20);
		TS_ASSERT_EQUALS(s4.cell, 20);
		TS_ASSERT_EQUALS(s4.baseline, 1);
		TS_ASSERT_EQUALS(s4.align, Graphics::kHiResAlignCell);
		const FontSettings s0 = resolve(map, 0);
		TS_ASSERT_EQUALS(s0.cell, 16);
		TS_ASSERT_EQUALS(s0.baseline, 1);
	}

	// --- the placement --------------------------------------------------------

	void test_placement_on_the_game_baseline() {
		// KQ1 font 300: 18 px Gowun Batang Bold (baseline row 16) in the 16 px
		// cell, on the game's baseline 16 hi-res px below the line top.
		GlyphPlacement p = place(18, 16, GlyphPlacement::kAlignGame, 16, 16, 0);
		TS_ASSERT(p.active());
		TS_ASSERT_EQUALS(p.cellPx, 16);
		TS_ASSERT_EQUALS(p.offsetX(true), -1);   // centred: one px over each side
		TS_ASSERT_EQUALS(p.offsetX(false), 0);   // narrow glyphs start at the pen
		TS_ASSERT_EQUALS(p.offsetY(), 0);
		// shift=-2: two px up, above the cell's top.
		p = place(18, 16, GlyphPlacement::kAlignGame, 16, 16, -2);
		TS_ASSERT_EQUALS(p.offsetY(), -2);
		// Font 4: the game's baseline is 14.
		p = place(18, 16, GlyphPlacement::kAlignGame, 16, 14, -2);
		TS_ASSERT_EQUALS(p.offsetY(), -4);
		// Positive: down.
		p = place(16, 16, GlyphPlacement::kAlignGame, 13, 14, 2);
		TS_ASSERT_EQUALS(p.offsetY(), 3);
	}

	void test_ink_is_kept_to_the_text_line() {
		// An 18-row face on font 4's baseline in a 16 px cell: drawn a row
		// up. The rows it may ink are the line the game erases - font 4's
		// 18 hi-res rows - never the row above it.
		GlyphPlacement p = place(18, 16, GlyphPlacement::kAlignGame, 15, 14, 0);
		TS_ASSERT_EQUALS(p.offsetY(), -1);
		int top, bottom;
		p.lineRows(100, 18, top, bottom);
		TS_ASSERT_EQUALS(top, 100);
		TS_ASSERT_EQUALS(bottom, 118);
		// A 16 px line (LB1's title, font 0): the row under the cell is
		// outside it too.
		p.lineRows(100, 16, top, bottom);
		TS_ASSERT_EQUALS(bottom, 116);
		// A game line shorter than the cell (or unknown, 0): the cell.
		p.lineRows(100, 0, top, bottom);
		TS_ASSERT_EQUALS(bottom, 116);
		// With the clip, TextLayer draws the glyph's rows 1..16 only.
		Sci::TextLayer l(40, 140, 2);
		Common::Array<byte> cov;
		cov.resize(18 * 18);
		for (uint i = 0; i < cov.size(); i++)
			cov[i] = 255;
		p.lineRows(100, 16, top, bottom);
		const Common::Rect line(-0x4000, top, 0x4000, bottom);
		l.putGlyph(0, 100 + p.offsetY(), cov.begin(), 18, 18, 1, &line);
		TS_ASSERT_EQUALS(l.row(99)[5].fgCoverage, 0);
		TS_ASSERT_EQUALS(l.row(100)[5].fgCoverage, 255);
		TS_ASSERT_EQUALS(l.row(115)[5].fgCoverage, 255);
		TS_ASSERT_EQUALS(l.row(116)[5].fgCoverage, 0);
	}

	void test_placement_centred_when_asked_or_unknown() {
		// align=cell, or a baseline that cannot be measured: the raster cell
		// centred on the layout cell, rounded down.
		GlyphPlacement p = place(18, 16, GlyphPlacement::kAlignCell, 16, 16, 0);
		TS_ASSERT_EQUALS(p.offsetY(), -1);
		p = place(17, 16, GlyphPlacement::kAlignGame, 15, -1, 0);
		TS_ASSERT_EQUALS(p.offsetY(), -1);
		TS_ASSERT_EQUALS(p.offsetX(true), -1);
		p = place(14, 16, GlyphPlacement::kAlignCell, -1, -1, -2);
		TS_ASSERT_EQUALS(p.offsetY(), 1 - 2);
		TS_ASSERT_EQUALS(p.offsetX(true), 1);
		// Same cell, no shift, nothing measured: inactive, exactly as before.
		p = place(16, 16, GlyphPlacement::kAlignGame, -1, -1, 0);
		TS_ASSERT(!p.active());
		p = place(16, 16, GlyphPlacement::kAlignGame, 14, 14, 0);
		TS_ASSERT(!p.active());
	}

	void test_align_font_uses_the_face_line() {
		// align=font: the raster's line top (row 5 of a padded raster, say)
		// on the text line's top, whatever the game font is.
		const Graphics::HiResMap map = parseMap("[font]\nalign=font\n[font.300]\nalign=font\nshift=-2\n");
		TS_ASSERT_EQUALS(map.font.align, Graphics::kHiResAlignFont);
		const FontSettings s = resolve(map, 300);
		TS_ASSERT_EQUALS(s.align, Graphics::kHiResAlignFont);
		TS_ASSERT_EQUALS(resolve(map, 0).align, Graphics::kHiResAlignFont);
		GlyphPlacement p = place(18, 16, GlyphPlacement::kAlignFont, 16, 16, 0, 5);
		TS_ASSERT_EQUALS(p.offsetY(), -5);
		p = place(18, 16, GlyphPlacement::kAlignFont, 16, 16, -2, 5);
		TS_ASSERT_EQUALS(p.offsetY(), -7);
		// A fit that moved the line top up (negative) moves the raster down.
		p = place(16, 16, GlyphPlacement::kAlignFont, -1, -1, 0, -3);
		TS_ASSERT_EQUALS(p.offsetY(), 3);
		// Unknown (no TrueType face): centred.
		p = place(18, 16, GlyphPlacement::kAlignFont, 16, 16, 0);
		TS_ASSERT_EQUALS(p.offsetY(), -1);
	}

	void test_padded_raster_centres_by_its_height() {
		// An 18 px face with 5 rows of headroom each side (28 rows): centred,
		// its cell proper still lands one row above the layout cell.
		GlyphPlacement::Input in;
		in.rasterWidth = 18;
		in.rasterHeight = 28;
		in.cellPx = 16;
		in.align = GlyphPlacement::kAlignCell;
		const GlyphPlacement p = GlyphPlacement::compute(in);
		TS_ASSERT_EQUALS(p.offsetY(), -6);
		TS_ASSERT_EQUALS(p.offsetX(true), -1);
	}

	void test_larger_face_keeps_the_cell_advance() {
		// An 18 px face in the 16 px cell: Hangul still advances 16 hi-res px
		// (8 game px), so text wraps exactly as with a 16 px face; a narrow
		// glyph keeps the face's own advance.
		BlockSource src;
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0xAC00, 2, true, 16), 8);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0xAC00, 2, false, 16), 8);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x00E9, 2, true, 16), 4);   // 7 / 2 rounded half up
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x00E9, 2, false, 16), 4);  // the half cell
		// cell=glyph: the face's own cell, as before C41.
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0xAC00, 2, true, 0), 9);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0xAC00, 2, true), 9);
	}

	void test_overflow_is_drawn_not_clipped() {
		// The 18 px block (ink rows 1..16, columns 1..16) drawn for the 16 px
		// cell at hi-res (32, 32), which spans rows and columns 32..47.
		BlockSource src;
		Common::Array<byte> cov(18 * 36);
		for (int y = 0; y < 18; y++)
			memcpy(&cov[y * 36], src.row(0xAC00, y), 36);

		// shift=-2: the raster's top row at 30, ink rows 31..46 - the
		// first above the cell - and, one column to the left, columns 32..47.
		GlyphPlacement p = place(18, 16, GlyphPlacement::kAlignGame, 16, 16, -2);
		Sci::TextLayer up(128, 128, 2);
		up.putGlyph(32 + p.offsetX(true), 32 + p.offsetY(), cov.begin(), 36, 18, 7);
		TS_ASSERT_EQUALS(up.row(31)[35].fgCoverage, 255);  // above the cell
		TS_ASSERT_EQUALS(up.row(30)[35].fgCoverage, 0);
		TS_ASSERT_EQUALS(up.row(46)[35].fgCoverage, 255);
		TS_ASSERT_EQUALS(up.row(47)[35].fgCoverage, 0);
		TS_ASSERT_EQUALS(up.row(40)[32].fgCoverage, 255);  // raster column 1
		TS_ASSERT_EQUALS(up.row(40)[31].fgCoverage, 0);
		TS_ASSERT_EQUALS(up.row(40)[47].fgCoverage, 255);
		TS_ASSERT_EQUALS(up.row(40)[48].fgCoverage, 0);

		// shift=+2: ink rows 35..50, past the cell's last row.
		p = place(18, 16, GlyphPlacement::kAlignGame, 16, 16, 2);
		Sci::TextLayer down(128, 128, 2);
		down.putGlyph(32 + p.offsetX(true), 32 + p.offsetY(), cov.begin(), 36, 18, 7);
		TS_ASSERT_EQUALS(down.row(34)[35].fgCoverage, 0);
		TS_ASSERT_EQUALS(down.row(35)[35].fgCoverage, 255);
		TS_ASSERT_EQUALS(down.row(50)[35].fgCoverage, 255);  // below the cell
		TS_ASSERT_EQUALS(down.row(51)[35].fgCoverage, 0);
	}

	void test_overflow_is_clipped_only_to_the_port() {
		// The port rect bounds what a later erase of the port clears, so the
		// overflow stops there - above its top here - and nowhere else.
		BlockSource src;
		Common::Array<byte> cov(18 * 36);
		for (int y = 0; y < 18; y++)
			memcpy(&cov[y * 36], src.row(0xAC00, y), 36);
		Sci::TextLayer layer(128, 128, 2);
		const Common::Rect port(0, 30, 128, 128);
		layer.putGlyph(31, 29 - 1, cov.begin(), 36, 18, 7, &port);
		TS_ASSERT_EQUALS(layer.row(29)[35].fgCoverage, 0);    // above the port
		TS_ASSERT_EQUALS(layer.row(30)[35].fgCoverage, 255);
		TS_ASSERT_EQUALS(layer.row(44)[35].fgCoverage, 255);
		layer.clearLowresRect(Common::Rect(0, 15, 64, 64));    // the port, in lowres
		for (int yy = 0; yy < 128; yy++)
			for (int xx = 0; xx < 128; xx++)
				TS_ASSERT_EQUALS(layer.row(yy)[xx].fgCoverage, 0);
	}

	void test_bitmap_font_baseline() {
		// KQ1 font 300: every capital and '0' ends on row 8.
		TS_ASSERT_EQUALS(Sci::bitmapFontBaseline([](uint32) { return 8; }), 8);
		// A '0' that dips one row does not move it: the most common row wins.
		TS_ASSERT_EQUALS(Sci::bitmapFontBaseline([](uint32 c) { return c == '0' ? 9 : 7; }), 7);
		// Ties go to the higher row; missing glyphs do not count.
		TS_ASSERT_EQUALS(Sci::bitmapFontBaseline([](uint32 c) { return c == 'H' ? 6 : (c == 'I' ? 5 : -1); }), 5);
		TS_ASSERT_EQUALS(Sci::bitmapFontBaseline([](uint32) { return -1; }), -1);
	}

	// The DOS L preset's KO2350.SVF in a code-page game's layout (per-glyph
	// off, as LB1's EUC-KR text is laid out): the East Asian Ambiguous
	// symbols neodgm draws full width (U+25CB, U+25A1, U+2015: advance 16)
	// advance a whole cell, 8 game px - as far as they draw - and the ones it
	// draws narrow (U+00B7, U+2018: advance 8) half of one.
	void test_ko2350_ambiguous_symbols_advance_as_drawn() {
#if NULL_OSYSTEM_IS_AVAILABLE
		// The source root, from this header's own path (the runner may run
		// from an out-of-tree build folder).
		Common::String root = __FILE__;
		const size_t cut = root.rfind("test/engines/sci/");
		if (cut == Common::String::npos) {
			TS_SKIP("source root not known");
			return;
		}
		root = Common::String(root.c_str(), cut);
		Common::install_null_g_system();
		const Common::FSNode node(Common::Path(root + "dists/engine-data/hires_text/dos/KO2350.SVF", '/'));
		Common::SeekableReadStream *stream = node.exists() ? node.createReadStream() : nullptr;
		Graphics::HiResBitmapFont font;
		const bool loaded = stream && font.load(*stream);
		delete stream;
		Common::uninstall_null_g_system();
		TS_ASSERT(loaded);
		if (!loaded)
			return;
		Graphics::SvfnGlyphSource src(&font, DisposeAfterUse::NO);
		const uint32 full[] = { 0x25CB, 0x25CF, 0x25A1, 0x25A0, 0x25B3, 0x2015, 0xAC00 };
		for (uint i = 0; i < ARRAYSIZE(full); i++) {
			TS_ASSERT_EQUALS(src.cells(full[i]), 2);
			TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, full[i], 2, false), 8);
			// A UTF-8 game's per-glyph layout, by the metrics advance, as before.
			TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, full[i], 2, true), 8);
		}
		const uint32 half[] = { 0x00B7, 0x2018, 0x0041 };
		for (uint i = 0; i < ARRAYSIZE(half); i++) {
			TS_ASSERT_EQUALS(src.cells(half[i]), 1);
			TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, half[i], 2, false), 4);
			TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, half[i], 2, true), 4);
		}
#endif
	}
};
