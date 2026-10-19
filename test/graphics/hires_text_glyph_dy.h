#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_jamo.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/jamo_set.h"
#include "graphics/hires_text/text_compose.h"

/**
 * The SVFN per-glyph vertical delta (graphics/hires_text/README.md,
 * "Per-glyph dy"): header bytes 17 and 18 give the font's dy range, metrics
 * byte 3 each glyph's dy; a glyph is drawn dy rows below its cell position.
 */
class HiResTextGlyphDyTestSuite : public CxxTest::TestSuite {
public:
	// These fonts are a few KB: streamed only with no threshold.
	void setUp() override { Graphics::HiResBitmapFont::setStreamThreshold(0); }
	void tearDown() override { Graphics::HiResBitmapFont::setStreamThreshold(HIRES_SVF_STREAM_MIN); }

private:
	typedef Common::Array<byte> Bytes;
	enum { kCell = 16 };

	static void put16(Bytes &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Bytes &b, uint pos, uint32 v) {
		put16(b, pos, v & 0xffff);
		put16(b, pos + 2, v >> 16);
	}

	/// One glyph: its code point, its dy, and a vertical bar of ink in column x from row y0 to y1 - 1.
	struct G {
		uint32 cp;
		int dy;
		int x, y0, y1;
	};

	/**
	 * A version 2, 16x16, 1 bpp SVFN as dists/svf-fonts/tools/jamoset.py lays it out (header, code point table, JSET when
	 * @p jset is not empty, metrics, glyphs), with header bytes 17/18 = @p dyMin / @p dyMax.
	 * @p metrics false: no metrics table (and no proportional flag).
	 */
	static Bytes makeFont(const Common::Array<G> &g, int dyMin, int dyMax, const Bytes &jset = Bytes(),
						  bool metrics = true) {
		const uint32 n = g.size(), pitch = 2, stride = pitch * kCell;
		const uint32 cmapOff = 36, metricsOff = cmapOff + n * 8 + jset.size();
		const uint32 dataOff = metricsOff + (metrics ? n * 4 : 0);
		Bytes b;
		b.resize(dataOff + n * stride, 0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, (metrics ? 1 : 0) | (jset.empty() ? 0 : 2));
		b[8] = 1;
		put16(b, 12, n);
		b[14] = kCell;
		b[15] = kCell;
		b[16] = 12;
		b[17] = (byte)(int8)dyMin;
		b[18] = (byte)(int8)dyMax;
		put32(b, 20, metrics ? metricsOff : 0);
		put32(b, 24, dataOff);
		put32(b, 28, n * stride);
		put32(b, 32, cmapOff);
		for (uint i = 0; i < n; ++i) {
			put32(b, cmapOff + i * 8, g[i].cp);
			put32(b, cmapOff + i * 8 + 4, i);
			if (metrics) {
				b[metricsOff + i * 4 + 0] = g[i].cp < 0x80 ? 8 : 16;
				b[metricsOff + i * 4 + 1] = 0;
				b[metricsOff + i * 4 + 2] = 1;
				b[metricsOff + i * 4 + 3] = (byte)(int8)g[i].dy;
			}
			for (int y = g[i].y0; y < g[i].y1; ++y)
				b[dataOff + i * stride + y * pitch + g[i].x / 8] |= 0x80 >> (g[i].x % 8);
		}
		for (uint i = 0; i < jset.size(); ++i)
			b[cmapOff + n * 8 + i] = jset[i];
		return b;
	}

	static Graphics::HiResBitmapFont *load(const Bytes &bytes, bool streamed) {
		Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
		bool ok;
		if (streamed) {
			ok = font->loadStreamed(new Common::MemoryReadStream(bytes.begin(), bytes.size()), DisposeAfterUse::YES);
		} else {
			Common::MemoryReadStream stream(bytes.begin(), bytes.size());
			ok = font->load(stream);
		}
		TS_ASSERT(ok);
		TS_ASSERT_EQUALS(font->isStreamed(), streamed);
		return font;
	}

	/// The rows of @p cp holding ink in column @p x, over every row the source answers, in cell coordinates.
	static Common::Array<int> inkRows(Graphics::UnicodeGlyphSource &src, uint32 cp, int x) {
		Common::Array<int> rows;
		for (int y = -src.inkRowsAbove(); y < src.cellHeight() + src.inkRowsBelow(); ++y) {
			const byte *row = src.row(cp, y);
			if (row && Graphics::TextCompose::expandCoverage(row, x, src.bitsPerPixel()))
				rows.push_back(y);
		}
		return rows;
	}

	static void expectRows(const Common::Array<int> &got, int first, int count) {
		TS_ASSERT_EQUALS(got.size(), (uint)count);
		for (uint i = 0; i < got.size(); ++i)
			TS_ASSERT_EQUALS(got[i], first + (int)i);
	}

	static Common::Array<G> latin() {
		Common::Array<G> g;
		const G a = { 'a', 0, 2, 4, 12 };   // x-height letter, on the baseline (row 12)
		const G p = { 'p', 3, 3, 6, 16 };   // descender to the cell's last row, drawn 3 rows lower
		const G t = { 't', -2, 4, 0, 10 };  // ink from row 0, drawn 2 rows higher
		g.push_back(a);
		g.push_back(p);
		g.push_back(t);
		return g;
	}

public:
	/// Header and metrics are read; bearingY follows the glyph's dy.
	void test_font_reads_the_range_and_each_dy() {
		for (int streamed = 0; streamed < 2; ++streamed) {
			Graphics::HiResBitmapFont *font = load(makeFont(latin(), -2, 3), streamed);
			TS_ASSERT_EQUALS(font->dyMin(), -2);
			TS_ASSERT_EQUALS(font->dyMax(), 3);
			Graphics::GlyphMetrics m;
			TS_ASSERT(font->glyphMetrics(font->glyphIndex('a'), m));
			TS_ASSERT_EQUALS(m.dy, 0);
			TS_ASSERT_EQUALS(m.bearingY, 12);
			TS_ASSERT(font->glyphMetrics(font->glyphIndex('p'), m));
			TS_ASSERT_EQUALS(m.dy, 3);
			TS_ASSERT_EQUALS(m.bearingY, 9);
			TS_ASSERT(font->glyphMetrics(font->glyphIndex('t'), m));
			TS_ASSERT_EQUALS(m.dy, -2);
			TS_ASSERT_EQUALS(m.bearingY, 14);
			delete font;
		}
	}

	/// (a) dy 0 everywhere, the range 0..0: the source answers the cell only, as before.
	void test_no_dy_keeps_the_cell() {
		Common::Array<G> g = latin();
		for (uint i = 0; i < g.size(); ++i)
			g[i].dy = 0;
		Graphics::SvfnGlyphSource src(load(makeFont(g, 0, 0), false), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src.inkRowsAbove(), 0);
		TS_ASSERT_EQUALS(src.inkRowsBelow(), 0);
		TS_ASSERT(src.row('p', -1) == nullptr);
		TS_ASSERT(src.row('p', kCell) == nullptr);
		expectRows(inkRows(src, 'p', 3), 6, 10);
		expectRows(inkRows(src, 't', 4), 0, 10);
	}

	/// (b) (c) (e) dy +3 moves the glyph 3 rows lower, its descender past the cell bottom; dy -2 moves one up
	/// past the cell top; a dy-0 glyph stays; whole and streamed alike.
	void test_glyphs_are_moved_by_their_dy() {
		for (int streamed = 0; streamed < 2; ++streamed) {
			Graphics::SvfnGlyphSource src(load(makeFont(latin(), -2, 3), streamed), DisposeAfterUse::YES);
			TS_ASSERT_EQUALS(src.cellHeight(), kCell);
			TS_ASSERT_EQUALS(src.inkRowsAbove(), 2);
			TS_ASSERT_EQUALS(src.inkRowsBelow(), 3);
			TS_ASSERT_EQUALS(src.baselineRow(), 12);
			expectRows(inkRows(src, 'a', 2), 4, 8);
			expectRows(inkRows(src, 'p', 3), 9, 10);	// rows 9 .. 18: three past the cell
			expectRows(inkRows(src, 't', 4), -2, 10);	// rows -2 .. 7
			// Every row of the range is there (blank where there is no ink), none beyond it.
			for (int y = -2; y < kCell + 3; ++y)
				TS_ASSERT(src.row('a', y) != nullptr);
			TS_ASSERT(src.row('a', -3) == nullptr);
			TS_ASSERT(src.row('a', kCell + 3) == nullptr);
			Graphics::GlyphMetrics m;
			TS_ASSERT(src.metrics('p', m));
			TS_ASSERT_EQUALS(m.dy, 3);
			TS_ASSERT_EQUALS(m.bearingY, 9);
		}
	}

	/// (d) A glyph whose dy lies outside the header's range is drawn at dy 0; so is every glyph of a font whose
	/// range is not valid, or that gives one without a metrics table.
	void test_out_of_range_dy_is_rejected() {
		Common::Array<G> g = latin();
		g[1].dy = 5;	// above dyMax 3
		{
			Graphics::SvfnGlyphSource src(load(makeFont(g, -2, 3), false), DisposeAfterUse::YES);
			expectRows(inkRows(src, 'p', 3), 6, 10);
			expectRows(inkRows(src, 't', 4), -2, 10);
			Graphics::GlyphMetrics m;
			TS_ASSERT(src.metrics('p', m));
			TS_ASSERT_EQUALS(m.dy, 0);
			TS_ASSERT_EQUALS(m.bearingY, 12);
		}
		const int bad[][2] = { { 1, 3 }, { -2, -1 }, { -17, 0 }, { 0, 17 } };
		for (uint k = 0; k < ARRAYSIZE(bad); ++k) {
			Graphics::HiResBitmapFont *font = load(makeFont(latin(), bad[k][0], bad[k][1]), false);
			TS_ASSERT_EQUALS(font->dyMin(), 0);
			TS_ASSERT_EQUALS(font->dyMax(), 0);
			Graphics::SvfnGlyphSource src(font, DisposeAfterUse::YES);
			TS_ASSERT_EQUALS(src.inkRowsBelow(), 0);
			expectRows(inkRows(src, 'p', 3), 6, 10);
		}
		Graphics::HiResBitmapFont *plain = load(makeFont(latin(), -2, 3, Bytes(), false), false);
		TS_ASSERT_EQUALS(plain->dyMin(), 0);
		TS_ASSERT_EQUALS(plain->dyMax(), 0);
		delete plain;
	}

	/// The cache: an entry holds the rows of the range, and a glyph seen again comes back the same.
	void test_cached_rows_hold_the_range() {
		Graphics::SvfnGlyphSource src(load(makeFont(latin(), -2, 3), true), DisposeAfterUse::YES);
		src.setCacheBytes(0, 1);	// one entry: every new code point evicts the last
		expectRows(inkRows(src, 'p', 3), 9, 10);
		expectRows(inkRows(src, 'a', 2), 4, 8);
		expectRows(inkRows(src, 'p', 3), 9, 10);
	}

	/// The rows of a face chain: the most of any face, and a NormalizedGlyphSource moves them with its top row.
	void test_chains_pass_the_range_on() {
		Common::Array<G> plain;
		const G b = { 'b', 0, 1, 0, 12 };
		plain.push_back(b);
		Common::Array<Graphics::UnicodeGlyphSource *> faces;
		faces.push_back(new Graphics::SvfnGlyphSource(load(makeFont(plain, 0, 0), false), DisposeAfterUse::YES));
		faces.push_back(new Graphics::SvfnGlyphSource(load(makeFont(latin(), -2, 3), false), DisposeAfterUse::YES));
		Graphics::FallbackGlyphSource chain(faces, DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(chain.inkRowsAbove(), 2);
		TS_ASSERT_EQUALS(chain.inkRowsBelow(), 3);
		expectRows(inkRows(chain, 'p', 3), 9, 10);

		Common::String err;
		Graphics::NormalizedGlyphSource *n = Graphics::NormalizedGlyphSource::create(
			new Graphics::SvfnGlyphSource(load(makeFont(latin(), -2, 3), false), DisposeAfterUse::YES), 16, 18, 1,
			DisposeAfterUse::YES, err);
		TS_ASSERT(n);
		// The face at row 1 of an 18-row cell: rows -2 .. 18 of it are rows -1 .. 19 here.
		TS_ASSERT_EQUALS(n->inkRowsAbove(), 1);
		TS_ASSERT_EQUALS(n->inkRowsBelow(), 2);
		expectRows(inkRows(*n, 'p', 3), 10, 10);
		expectRows(inkRows(*n, 't', 4), -1, 10);
		delete n;
	}

	/// (f) A jamo-set face whose Latin carries a dy: the Latin is moved, the composed syllables are drawn as with
	/// no dy (their cell rows, blank outside the cell).
	void test_jamo_face_with_latin_dy() {
		// A JSET for the 8x4x4 system (as hires_text_jamo.h): pieces 1 + initial set * 20 + lead, ...
		Bytes jset;
		const byte head[] = { 'J', 'S', 'E', 'T', 1, 0, 1, 1, 1, 1, 1, 0 };
		for (uint i = 0; i < sizeof(head); ++i)
			jset.push_back(head[i]);
		for (int l = 0; l < 19; ++l)
			jset.push_back(0);
		for (int t = 0; t < 28; ++t)
			jset.push_back(0);
		for (int which = 0; which < 3; ++which)
			for (int v = 0; v < 21; ++v)
				jset.push_back(0);
		// One set each: initial piece 0 for every lead, medial piece 1, no final (blank).
		for (int l = 0; l < 19; ++l) {
			jset.push_back(0);
			jset.push_back(0);
		}
		for (int v = 0; v < 21; ++v) {
			jset.push_back(1);
			jset.push_back(0);
		}
		for (int t = 1; t < 28; ++t) {
			jset.push_back(0xFF);
			jset.push_back(0xFF);
		}
		for (int c = 0; c < 51; ++c) {
			jset.push_back(0xFF);
			jset.push_back(0xFF);
		}
		Common::String err;
		Graphics::JamoSetTable table;
		TS_ASSERT(Graphics::parseJamoSetTable(jset.begin(), jset.size(), table, err));

		Common::Array<G> g = latin();
		const G ini = { Graphics::kJamoPieceBase + 0, 0, 5, 0, kCell };	// a bar the whole cell high
		const G med = { Graphics::kJamoPieceBase + 1, 0, 9, 2, 14 };
		g.push_back(ini);
		g.push_back(med);
		Common::Array<G> flat = g;
		for (uint i = 0; i < flat.size(); ++i)
			flat[i].dy = 0;

		for (int streamed = 0; streamed < 2; ++streamed) {
			Graphics::JamoGlyphSource src(load(makeFont(g, -2, 3, jset), streamed), DisposeAfterUse::YES);
			Graphics::JamoGlyphSource ref(load(makeFont(flat, 0, 0, jset), streamed), DisposeAfterUse::YES);
			TS_ASSERT_EQUALS(src.inkRowsAbove(), 2);
			TS_ASSERT_EQUALS(src.inkRowsBelow(), 3);
			TS_ASSERT_EQUALS(src.cells(0xAC00), 2);
			expectRows(inkRows(src, 0xAC00, 5), 0, kCell);
			expectRows(inkRows(src, 0xAC00, 9), 2, 12);
			for (int y = 0; y < kCell; ++y)
				TS_ASSERT_SAME_DATA(src.row(0xAC00, y), ref.row(0xAC00, y), 4);
			TS_ASSERT(src.row(0xAC00, -1) == nullptr);
			TS_ASSERT(src.row(0xAC00, kCell) == nullptr);
			Graphics::GlyphMetrics m;
			TS_ASSERT(src.metrics(0xAC00, m));
			TS_ASSERT_EQUALS(m.dy, 0);
			expectRows(inkRows(src, 'p', 3), 9, 10);
			expectRows(inkRows(ref, 'p', 3), 6, 10);
			TS_ASSERT(src.metrics('p', m));
			TS_ASSERT_EQUALS(m.dy, 3);
		}
	}
};
