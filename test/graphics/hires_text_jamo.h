#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/file-cache-stats.h"
#include "common/memstream.h"
#include "common/algorithm.h"
#include "graphics/hires_text/bitmap_font.h"
#include "common/log.h"
#include "common/str.h"
#include "graphics/hires_text/glyph_source_file.h"
#include "graphics/hires_text/glyph_source_jamo.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/jamo_set.h"

/**
 * Tests for the JSET jamo-set block of an SVFN (JamoSetTable) and what
 * builds on it. The fixtures are written byte by byte, so every build runs
 * them: a synthetic 8x4x4 set system (the DOS standard, tools/korean/johab844.py)
 * whose pieces are numbered as that script numbers them.
 */
class HiResTextJamoTestSuite : public CxxTest::TestSuite {
public:
	// These fonts are a few KB: streamed only with no threshold.
	void setUp() override { Graphics::HiResBitmapFont::setStreamThreshold(0); }
	void tearDown() override { Graphics::HiResBitmapFont::setStreamThreshold(HIRES_SVF_STREAM_MIN); }

private:
	typedef Common::Array<byte> Bytes;

	static void put16(Bytes &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Bytes &b, uint pos, uint32 v) {
		put16(b, pos, v & 0xffff);
		put16(b, pos + 2, v >> 16);
	}

	static void append16(Bytes &b, uint16 v) {
		b.push_back(v & 0xff);
		b.push_back(v >> 8);
	}

	// Offsets of the JSET header fields.
	enum { kVersion = 4, kLC = 6, kTC = 7, kNI = 8, kNM = 9, kNF = 10, kAdvance = 11, kLeadClass = 12, kTailClass = 31, kTables = 59 };

	// 8x4x4 selection (docs/johab/2026-10-14-johab-font-research.md 3.2).
	static const byte kInitNoFin[21];
	static const byte kInitFin[21];
	static const byte kFinSet[21];

	/// A JSET for the 8x4x4 system: lead classes {ㄱ ㅋ} and the rest, tail
	/// classes {none} and {some}. Pieces are numbered as johab844.py does.
	static Bytes make844Jset(byte advance = 0) {
		const int LC = 2, TC = 2;
		Bytes b;
		const byte head[] = { 'J', 'S', 'E', 'T', 1, 0, (byte)LC, (byte)TC, 8, 4, 4, advance };
		for (uint i = 0; i < sizeof(head); ++i)
			b.push_back(head[i]);
		for (int l = 0; l < 19; ++l)
			b.push_back(l == 0 || l == 15 ? 0 : 1);
		for (int t = 0; t < 28; ++t)
			b.push_back(t == 0 ? 0 : 1);
		// initSet, then medSet, then finSet, each indexed (lc * 21 + v) * TC + tc.
		for (int which = 0; which < 3; ++which)
			for (int lc = 0; lc < LC; ++lc)
				for (int v = 0; v < 21; ++v)
					for (int tc = 0; tc < TC; ++tc) {
						if (which == 0)
							b.push_back(tc ? kInitFin[v] : kInitNoFin[v]);
						else if (which == 1)
							b.push_back(lc + 2 * tc);
						else
							b.push_back(tc ? kFinSet[v] : 0);
					}
		for (int s = 0; s < 8; ++s)
			for (int l = 0; l < 19; ++l)
				append16(b, s * 20 + l + 1);
		for (int s = 0; s < 4; ++s)
			for (int v = 0; v < 21; ++v)
				append16(b, 160 + s * 22 + v + 1);
		for (int s = 0; s < 4; ++s)
			for (int t = 1; t < 28; ++t)
				append16(b, 248 + s * 28 + t);
		for (int c = 0; c < 51; ++c)
			append16(b, c == 0 ? 1 : 0xFFFF);	// U+3131 (ㄱ) is initial piece 1
		return b;
	}

	static bool parse(const Bytes &b, Graphics::JamoSetTable &t, Common::String &err, uint32 size) {
		return Graphics::parseJamoSetTable(b.begin(), size, t, err);
	}

	/// Size of the valid 8x4x4 block.
	static uint32 sizeOf(const Bytes &b) { return b.size(); }

	/// A pixel a glyph is given in place of the default one (makeFont()).
	struct Px {
		uint32 cp;
		byte x, y, level;
	};

	static void setPixel(Bytes &b, uint glyphAt, uint pitch, int bpp, uint x, uint y, byte level) {
		byte *row = &b[glyphAt + y * pitch];
		if (bpp == 8)
			row[x] = level;
		else
			row[x * bpp / 8] |= level << (8 - bpp - (x * bpp & 7));
	}

	/**
	 * A version 2 SVFN, 16x16 at @p bpp, one glyph per code point in @p cps
	 * (each with the single pixel (n % 16, n / 16 % 16) at the top level for
	 * n = cp - U+F0000, else (0, 0), unless @p px names the glyph's pixel),
	 * the code point table last and @p jset right after it. @p jset may be
	 * empty (no block).
	 */
	static Bytes makeFont(const Common::Array<uint32> &cps, const Bytes &jset, bool flag = true, int bpp = 1,
						  const Common::Array<Px> &px = Common::Array<Px>()) {
		const uint32 glyphs = cps.size();
		const uint32 pitch = (16 * bpp + 7) / 8;
		const uint32 stride = pitch * 16;
		const uint32 dataOff = 36;
		const uint32 cmapOff = dataOff + glyphs * stride;
		Bytes b;
		b.resize(cmapOff + glyphs * 8);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, flag ? 2 : 0);
		b[8] = bpp;
		put16(b, 12, glyphs);
		b[14] = 16;
		b[15] = 16;
		b[16] = 14;
		put32(b, 24, dataOff);
		put32(b, 28, glyphs * stride);
		put32(b, 32, cmapOff);
		for (uint i = 0; i < glyphs; ++i) {
			const uint32 n = cps[i] >= Graphics::kJamoPieceBase ? cps[i] - Graphics::kJamoPieceBase : 0;
			const uint dx = n % 16, dy = n / 16 % 16;
			const byte top = bpp == 8 ? 255 : (1 << bpp) - 1;
			bool named = false;
			for (uint k = 0; k < px.size(); ++k) {
				if (px[k].cp != cps[i])
					continue;
				named = true;
				setPixel(b, dataOff + i * stride, pitch, bpp, px[k].x, px[k].y, px[k].level);
			}
			if (!named)
				setPixel(b, dataOff + i * stride, pitch, bpp, dx, dy, top);
			put32(b, cmapOff + i * 8, cps[i]);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		for (uint i = 0; i < jset.size(); ++i)
			b.push_back(jset[i]);
		return b;
	}

	/**
	 * The font as tools/korean/jamoset.py writes it: header, code point table, the JSET block, then the metrics
	 * table (when @p withMetrics) and the glyph rows, so the block ends where the next table starts.
	 */
	static Bytes makeToolOrderFont(const Common::Array<uint32> &cps, const Bytes &jset, bool withMetrics) {
		const Bytes plain = makeFont(cps, Bytes());
		const uint32 glyphs = cps.size(), stride = 32;
		const uint32 cmapOff = 36, metricsOff = cmapOff + glyphs * 8 + jset.size();
		const uint32 dataOff = metricsOff + (withMetrics ? glyphs * 4 : 0);
		Bytes b;
		for (uint i = 0; i < 36; ++i)
			b.push_back(plain[i]);
		put16(b, 6, withMetrics ? 3 : 2);		// jamo sets, and proportional when there are metrics
		put32(b, 20, withMetrics ? metricsOff : 0);
		put32(b, 24, dataOff);
		put32(b, 32, cmapOff);
		for (uint i = 36 + glyphs * stride; i < plain.size(); ++i)		// the code point table
			b.push_back(plain[i]);
		for (uint i = 0; i < jset.size(); ++i)
			b.push_back(jset[i]);
		if (withMetrics)
			for (uint i = 0; i < glyphs; ++i) {
				const byte w = cps[i] < 0x80 ? 8 : 16;
				b.push_back(w);		// advance
				b.push_back(0);		// bearingX
				b.push_back(w);		// ink width
				b.push_back(0);
			}
		for (uint i = 0; i < glyphs * stride; ++i)
			b.push_back(plain[36 + i]);
		return b;
	}

	/// The code points of a font holding the 360 pieces of the 8x4x4 system and 'A'.
	static Common::Array<uint32> pieceCps() {
		Common::Array<uint32> cps;
		cps.push_back('A');
		for (uint n = 0; n < 360; ++n)
			cps.push_back(Graphics::kJamoPieceBase + n);
		return cps;
	}

	static void expect(const Graphics::JamoSetTable &t, uint32 cp, int count, uint16 a, uint16 b, uint16 c = 0) {
		uint16 out[3] = { 0, 0, 0 };
		int n = 0;
		TS_ASSERT(t.pieces(cp, out, n));
		TS_ASSERT_EQUALS(n, count);
		TS_ASSERT_EQUALS(out[0], a);
		TS_ASSERT_EQUALS(out[1], b);
		if (count == 3)
			TS_ASSERT_EQUALS(out[2], c);
	}

public:
	void test_pieces_of_syllables() {
		Graphics::JamoSetTable t;
		Common::String err;
		const Bytes jset = make844Jset();
		TS_ASSERT(parse(jset, t, err, sizeOf(jset)));
		// johab844.py pieces(): initial set*20+L+1, medial 160+set*22+V+1, final 248+set*28+T.
		expect(t, 0xAC00, 2, 1, 161);			// 가: no final, so two pieces
		expect(t, 0xAC01, 3, 101, 205, 249);	// 각: initial set 5, medial set 2
		expect(t, 0xBDC1, 3, 148, 242, 313);	// 뷁 (ㅂ ㅞ ㄺ)
		expect(t, 0xD759, 3, 139, 245, 341);	// 흙 (ㅎ ㅡ ㄺ)
		uint16 out[3];
		int n = 0;
		TS_ASSERT(!t.pieces(0xABFF, out, n));
		TS_ASSERT(!t.pieces(0xD7A4, out, n));
		TS_ASSERT(!t.pieces('A', out, n));
		TS_ASSERT(t.pieces(0xD7A3, out, n));	// the last syllable
	}

	void test_compat_jamo() {
		Graphics::JamoSetTable t;
		Common::String err;
		const Bytes jset = make844Jset();
		TS_ASSERT(parse(jset, t, err, sizeOf(jset)));
		TS_ASSERT_EQUALS(t.compat(0x3131), 1);
		TS_ASSERT_EQUALS(t.compat(0x3132), 0xFFFF);
		TS_ASSERT_EQUALS(t.compat(0x3163), 0xFFFF);
		TS_ASSERT_EQUALS(t.compat(0x3130), 0xFFFF);
		TS_ASSERT_EQUALS(t.compat(0x3164), 0xFFFF);
		TS_ASSERT_EQUALS(t.advance, 0);
	}

	void test_garbage_final_set_of_the_no_final_class_is_ignored() {
		Graphics::JamoSetTable t;
		Common::String err;
		Bytes jset = make844Jset();
		// finSet is the third table; tail class 0 (no tail) entries hold 200.
		const uint tableLen = 2 * 21 * 2;
		for (uint i = 0; i < tableLen; i += 2)
			jset[kTables + 2 * tableLen + i] = 200;
		TS_ASSERT(parse(jset, t, err, sizeOf(jset)));
		expect(t, 0xAC00, 2, 1, 161);
		expect(t, 0xAC01, 3, 101, 205, 249);
	}

	void test_malformed_blocks() {
		Graphics::JamoSetTable t;
		Common::String err;
		const Bytes good = make844Jset();

		Bytes b = good;
		b[0] = 'X';
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("magic"));

		b = good;
		b[kVersion] = 2;
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("version"));

		b = good;
		b[kLC] = 5;
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("lead classes"));

		b = good;
		b[kTC] = 0;
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("tail classes"));

		b = good;
		b[kNI] = 17;
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("initial sets"));

		b = good;
		b[kLeadClass + 3] = 2;	// LC is 2
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("leadClass"));

		b = good;
		b[kTailClass + 27] = 7;
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("tailClass"));

		b = good;
		b[kTables + 5] = 8;		// initSet entry, NI is 8
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("initSet"));

		b = good;
		b[kTables + 2 * 21 * 2 + 9] = 4;	// medSet entry, NM is 4
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("medSet"));

		b = good;
		b[kTables + 2 * 2 * 21 * 2 + 3] = 4;	// finSet entry of a used class, NF is 4
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("finSet"));

		// Truncated by one byte, and one byte too many: the length is exact.
		TS_ASSERT(!parse(good, t, err, sizeOf(good) - 1));
		TS_ASSERT(err.contains("length"));
		TS_ASSERT(!parse(good, t, err, 5));
		TS_ASSERT(err.contains("length"));
		b = good;
		b.push_back(0);
		TS_ASSERT(!parse(b, t, err, sizeOf(b)));
		TS_ASSERT(err.contains("length"));
	}

	void test_whole_font_exposes_the_table() {
		Graphics::HiResBitmapFont::setStreamThreshold(HIRES_SVF_STREAM_MIN);
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.load(stream));
		TS_ASSERT(!font.isStreamed());
		TS_ASSERT(font.jamoSets() != nullptr);
		TS_ASSERT(font.jamoSetError().empty());
		if (font.jamoSets())
			expect(*font.jamoSets(), 0xAC01, 3, 101, 205, 249);
	}

	void test_streamed_font_exposes_the_table() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.loadStreamed(new Common::MemoryReadStream(bytes.begin(), bytes.size()), DisposeAfterUse::YES));
		TS_ASSERT(font.isStreamed());
		TS_ASSERT(font.jamoSets() != nullptr);
		TS_ASSERT(font.jamoSetError().empty());
		if (font.jamoSets())
			expect(*font.jamoSets(), 0xBDC1, 3, 148, 242, 313);
		// The pieces themselves still come from the file.
		const int idx = font.glyphIndex(Graphics::kJamoPieceBase + 161);
		TS_ASSERT(idx >= 0);
		TS_ASSERT(font.glyphData(idx) != nullptr);
	}

	void test_truncated_block_loads_as_plain_font() {
		const Bytes good = make844Jset();
		Bytes jset = good;
		jset.pop_back();	// the file ends one byte early
		const Bytes bytes = makeFont(pieceCps(), jset);
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.load(stream));
		TS_ASSERT(font.jamoSets() == nullptr);
		TS_ASSERT(font.jamoSetError().contains("length"));
		TS_ASSERT(font.hasGlyph('A'));

		TS_ASSERT(font.loadStreamed(new Common::MemoryReadStream(bytes.begin(), bytes.size()), DisposeAfterUse::YES));
		TS_ASSERT(font.isStreamed());
		TS_ASSERT(font.jamoSets() == nullptr);
		TS_ASSERT(font.jamoSetError().contains("length"));
		TS_ASSERT(font.hasGlyph('A'));
	}

	void test_flag_without_block_and_block_without_flag() {
		// Flag set, no JSET at all: plain font, an error says why.
		Bytes bytes = makeFont(pieceCps(), Bytes());
		Common::MemoryReadStream s1(bytes.begin(), bytes.size());
		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.load(s1));
		TS_ASSERT(font.jamoSets() == nullptr);
		TS_ASSERT(!font.jamoSetError().empty());
		// A block with the flag clear is not looked at.
		bytes = makeFont(pieceCps(), make844Jset(), false);
		Common::MemoryReadStream s2(bytes.begin(), bytes.size());
		TS_ASSERT(font.load(s2));
		TS_ASSERT(font.jamoSets() == nullptr);
		TS_ASSERT(font.jamoSetError().empty());
	}
	// ---- JamoGlyphSource -------------------------------------------------

	/// Every pixel with coverage in a glyph's rows (both cells), as x | y << 8 | level << 16, in reading order.
	static Common::Array<uint32> inkOf(Graphics::UnicodeGlyphSource &src, uint32 cp) {
		Common::Array<uint32> out;
		const int bpp = src.bitsPerPixel();
		for (int y = 0; y < src.cellHeight(); ++y) {
			const byte *row = src.row(cp, y);
			if (!row)
				continue;
			for (int x = 0; x < src.cellWidth() * 2; ++x) {
				int level;
				if (bpp == 8)
					level = row[x];
				else
					level = (row[x * bpp / 8] >> (8 - bpp - (x * bpp & 7))) & ((1 << bpp) - 1);
				if (level)
					out.push_back(x | y << 8 | level << 16);
			}
		}
		return out;
	}

	/// The pixels of pieces @p a, @p b[, @p c] of the default fixture font at the top level.
	static Common::Array<uint32> pieceInk(int count, uint16 a, uint16 b, uint16 c = 0) {
		Common::Array<uint32> out;
		const uint16 n[3] = { a, b, c };
		for (int i = 0; i < count; ++i)
			out.push_back(n[i] % 16 | (n[i] / 16 % 16) << 8 | 1 << 16);
		Common::sort(out.begin(), out.end(), [](uint32 p, uint32 q) { return (p & 0xFFFF) < (q & 0xFFFF) || ((p & 0xFFFF) == (q & 0xFFFF) && p < q); });
		return out;
	}

	/// Row-major order (y first), as inkOf() lists them.
	static void sortReading(Common::Array<uint32> &v) {
		Common::sort(v.begin(), v.end(), [](uint32 p, uint32 q) {
			const uint32 py = p >> 8 & 0xFF, qy = q >> 8 & 0xFF;
			return py < qy || (py == qy && (p & 0xFF) < (q & 0xFF));
		});
	}

	static void expectInk(const Common::Array<uint32> &got, Common::Array<uint32> want) {
		sortReading(want);
		TS_ASSERT_EQUALS(got.size(), want.size());
		for (uint i = 0; i < got.size() && i < want.size(); ++i)
			TS_ASSERT_EQUALS(got[i], want[i]);
	}

	static Graphics::HiResBitmapFont *loadFont(const Bytes &bytes, bool streamed) {
		Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
		bool ok;
		if (streamed)
			ok = font->loadStreamed(new Common::MemoryReadStream(bytes.begin(), bytes.size()), DisposeAfterUse::YES);
		else {
			Common::MemoryReadStream stream(bytes.begin(), bytes.size());
			ok = font->load(stream);
		}
		TS_ASSERT(ok);
		TS_ASSERT_EQUALS(font->isStreamed(), streamed);
		return font;
	}

	void checkComposition(bool streamed) {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, streamed), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src.cellWidth(), 16);
		TS_ASSERT_EQUALS(src.bitsPerPixel(), 1);
		TS_ASSERT_EQUALS(src.cells(0xAC00), 2);
		TS_ASSERT_EQUALS(src.cells(0xD7A3), 2);
		// 가 is pieces 1 and 161: two pixels. The second cell stays blank.
		expectInk(inkOf(src, 0xAC00), pieceInk(2, 1, 161));
		expectInk(inkOf(src, 0xAC01), pieceInk(3, 101, 205, 249));	// 각
		expectInk(inkOf(src, 0xBDC1), pieceInk(3, 148, 242, 313));	// 뷁
		expectInk(inkOf(src, 0xD759), pieceInk(3, 139, 245, 341));	// 흙
		TS_ASSERT_EQUALS(src.composed(), 4u);
		// Asked again: from the cache.
		expectInk(inkOf(src, 0xAC01), pieceInk(3, 101, 205, 249));
		TS_ASSERT_EQUALS(src.composed(), 4u);
	}

public:
	void test_composes_from_pieces() { checkComposition(false); }
	void test_composes_from_streamed_pieces() { checkComposition(true); }

	void test_row_pointer_survives_asking_the_same_glyph_again() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, true), DisposeAfterUse::YES);
		const byte *r0 = src.row(0xAC01, 0);
		TS_ASSERT(r0 != nullptr);
		TS_ASSERT(src.row(0xAC01, 15) != nullptr);
		TS_ASSERT(src.row(0xAC01, 16) == nullptr);
		TS_ASSERT_EQUALS(r0, src.row(0xAC01, 0));
	}

	void test_other_glyphs_come_from_the_file() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src.cells('A'), 1);
		Common::Array<uint32> want;
		want.push_back(0 | 0 << 8 | 1 << 16);
		expectInk(inkOf(src, 'A'), want);
		TS_ASSERT_EQUALS(src.cells('B'), 0);
		TS_ASSERT_EQUALS(src.cells(0x3132), 0);		// ㄲ is not by piece, and the font has no glyph for it
		TS_ASSERT_EQUALS(src.glyphCount(), 361u);
		TS_ASSERT_EQUALS(src.baselineRow(), 14);
		Graphics::GlyphMetrics m;
		TS_ASSERT(src.metrics('A', m));
		TS_ASSERT(!m.wide);
		TS_ASSERT(!src.metrics('B', m));
	}

	void test_compat_jamo_is_its_piece() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src.cells(0x3131), 2);
		expectInk(inkOf(src, 0x3131), pieceInk(1, 1, 0));
	}

	void test_advance_and_metrics_of_syllables() {
		Bytes bytes = makeFont(pieceCps(), make844Jset());
		{
			Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
			TS_ASSERT_EQUALS(src.advance(0xAC00), 16);	// 0: the cell width
			Graphics::GlyphMetrics m;
			TS_ASSERT(src.metrics(0xAC00, m));
			TS_ASSERT(m.wide);
			TS_ASSERT_EQUALS(m.advance, 16);
			TS_ASSERT_EQUALS(m.originX, 0);
			TS_ASSERT_EQUALS(m.height, 16);
		}
		bytes = makeFont(pieceCps(), make844Jset(14));
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src.advance(0xAC00), 14);
		TS_ASSERT_EQUALS(src.advance(0x3131), 14);
		TS_ASSERT_EQUALS(src.advance('A'), 0);		// the inner source: no metrics table
	}

	void test_blank_pieces_add_nothing() {
		Bytes jset = make844Jset();
		// initPiece of set 0, initial ㄱ (the first of the piece tables): blank.
		const uint pieceTables = kTables + 3 * 2 * 21 * 2;
		put16(jset, pieceTables, 0xFFFF);
		const Bytes bytes = makeFont(pieceCps(), jset);
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		Common::String why;
		Graphics::HiResBitmapFont *again = loadFont(bytes, false);
		TS_ASSERT(Graphics::JamoGlyphSource::usable(*again, why));
		delete again;
		expectInk(inkOf(src, 0xAC00), pieceInk(1, 161, 0));	// 가 is its medial alone
	}

	void test_coverage_is_the_maximum_not_the_or() {
		// 가 is pieces 1 and 161; both are put at (5, 5).
		Common::Array<Px> px;
		const Px a = { Graphics::kJamoPieceBase + 1, 5, 5, 0 }, b = { Graphics::kJamoPieceBase + 161, 5, 5, 0 };
		const byte levels[][2] = { { 1, 2 }, { 2, 1 }, { 1, 3 }, { 3, 2 }, { 2, 2 } };
		for (int i = 0; i < 5; ++i) {
			px.clear();
			Px pa = a, pb = b;
			pa.level = levels[i][0];
			pb.level = levels[i][1];
			px.push_back(pa);
			px.push_back(pb);
			const Bytes bytes = makeFont(pieceCps(), make844Jset(), true, 2, px);
			Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
			TS_ASSERT_EQUALS(src.bitsPerPixel(), 2);
			Common::Array<uint32> want;
			want.push_back(5 | 5 << 8 | MAX(levels[i][0], levels[i][1]) << 16);
			expectInk(inkOf(src, 0xAC00), want);
		}
		// 8bpp: 100 | 200 would be 236.
		px.clear();
		Px pa = a, pb = b;
		pa.level = 100;
		pb.level = 200;
		px.push_back(pa);
		px.push_back(pb);
		const Bytes bytes = makeFont(pieceCps(), make844Jset(), true, 8, px);
		Graphics::JamoGlyphSource src(loadFont(bytes, true), DisposeAfterUse::YES);
		Common::Array<uint32> want;
		want.push_back(5 | 5 << 8 | 200 << 16);
		expectInk(inkOf(src, 0xAC00), want);
	}

	void test_2bpp_maximum_is_per_pixel_when_two_pixels_share_a_byte() {
		// Pixels 4 and 5 of a row share byte 1 at 2bpp. Piece A: (4,5)=3, (5,5)=1; piece B: (5,5)=2. The per-pixel
		// maximum is {3, 2}; a byte-wise maximum would give A's byte {3, 1}, and an OR 3 and 3.
		Common::Array<Px> px;
		const Px list[] = { { Graphics::kJamoPieceBase + 1, 4, 5, 3 }, { Graphics::kJamoPieceBase + 1, 5, 5, 1 },
							{ Graphics::kJamoPieceBase + 161, 5, 5, 2 } };
		for (int i = 0; i < 3; ++i)
			px.push_back(list[i]);
		const Bytes bytes = makeFont(pieceCps(), make844Jset(), true, 2, px);
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		Common::Array<uint32> want;
		want.push_back(4 | 5 << 8 | 3 << 16);
		want.push_back(5 | 5 << 8 | 2 << 16);
		expectInk(inkOf(src, 0xAC00), want);
		// The same at 8bpp, where a byte is a pixel and the maximum is plain.
		px.clear();
		for (int i = 0; i < 3; ++i) {
			Px q = list[i];
			q.level = q.level * 60;
			px.push_back(q);
		}
		const Bytes bytes8 = makeFont(pieceCps(), make844Jset(), true, 8, px);
		Graphics::JamoGlyphSource src8(loadFont(bytes8, false), DisposeAfterUse::YES);
		want.clear();
		want.push_back(4 | 5 << 8 | 180 << 16);
		want.push_back(5 | 5 << 8 | 120 << 16);
		expectInk(inkOf(src8, 0xAC00), want);
	}

	void test_compose_does_not_hold_a_piece_row_across_the_next_piece() {
		// With room for one piece in the inner cache, a pointer to the first piece's row is overwritten by the
		// second piece's: compose() must use each row before asking for the next piece.
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		for (int streamed = 0; streamed < 2; ++streamed) {
			Graphics::JamoGlyphSource src(loadFont(bytes, streamed != 0), DisposeAfterUse::YES);
			src.setCacheBytes(0, 1);
			TS_ASSERT_EQUALS(src.pieceCacheEntries(), 1u);
			expectInk(inkOf(src, 0xAC01), pieceInk(3, 101, 205, 249));
			expectInk(inkOf(src, 0xBDC1), pieceInk(3, 148, 242, 313));
			expectInk(inkOf(src, 0xAC00), pieceInk(2, 1, 161));
		}
	}

	void test_one_budget_covers_the_composed_and_the_piece_cache() {
		// A face costs HIRES_SVF_CACHE_KB in all, as an SvfnGlyphSource does.
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		const uint32 each = 4 * 16 + 64;	// a 16x16 1bpp glyph in the wide rows, and the entry's overhead
		TS_ASSERT((src.cacheEntries() + src.pieceCacheEntries()) * each <= (uint32)HIRES_SVF_CACHE_KB * 1024);
		TS_ASSERT(src.cacheEntries() >= 32);
		TS_ASSERT(src.pieceCacheEntries() >= 32);
	}

	void test_block_before_the_metrics_and_glyph_tables() {
		// The layout jamoset.py writes: the block ends where the metrics table (or, with none, the glyph rows) starts.
		for (int withMetrics = 0; withMetrics < 2; ++withMetrics)
			for (int streamed = 0; streamed < 2; ++streamed) {
				const Bytes bytes = makeToolOrderFont(pieceCps(), make844Jset(), withMetrics != 0);
				Graphics::HiResBitmapFont *font = loadFont(bytes, streamed != 0);
				TS_ASSERT(font->jamoSetError().empty());
				TS_ASSERT(font->jamoSets() != nullptr);
				Common::String why;
				TS_ASSERT(Graphics::JamoGlyphSource::usable(*font, why));
				Graphics::JamoGlyphSource src(font, DisposeAfterUse::YES);
				expectInk(inkOf(src, 0xAC01), pieceInk(3, 101, 205, 249));
				expectInk(inkOf(src, 0xBDC1), pieceInk(3, 148, 242, 313));
				TS_ASSERT_EQUALS(src.cells('A'), 1);
			}
	}

	void test_syllable_with_a_missing_piece_is_not_usable() {
		Common::Array<uint32> cps = pieceCps();
		Common::Array<uint32> fewer;
		for (uint i = 0; i < cps.size(); ++i)
			if (cps[i] != Graphics::kJamoPieceBase + 249)	// 각's final
				fewer.push_back(cps[i]);
		Common::String why;
		const Bytes bytes = makeFont(fewer, make844Jset());
		Graphics::HiResBitmapFont *font = loadFont(bytes, false);
		TS_ASSERT(!Graphics::JamoGlyphSource::usable(*font, why));
		TS_ASSERT(why.contains("249"));
		delete font;

		const Bytes good = makeFont(cps, make844Jset());
		font = loadFont(good, false);
		TS_ASSERT(Graphics::JamoGlyphSource::usable(*font, why));
		delete font;

		const Bytes plain = makeFont(cps, Bytes(), false);
		font = loadFont(plain, false);
		TS_ASSERT(!Graphics::JamoGlyphSource::usable(*font, why));
		TS_ASSERT(why.contains("JSET"));
		delete font;
	}

	void test_font_without_a_table_draws_the_file_only() {
		const Bytes bytes = makeFont(pieceCps(), Bytes(), false);
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(src.cells(0xAC00), 0);
		TS_ASSERT_EQUALS(src.cells('A'), 1);
	}

	void test_cache_survives_more_syllables_than_it_holds() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, true), DisposeAfterUse::YES);
		const uint32 n = src.cacheEntries() + 44;
		TS_ASSERT(n >= 100);
		Graphics::JamoSetTable t;
		Common::String err;
		const Bytes jset = make844Jset();
		TS_ASSERT(parse(jset, t, err, jset.size()));
		for (uint32 i = 0; i < n; ++i) {
			const uint32 cp = 0xAC00 + i * 37;
			uint16 p[3];
			int count;
			TS_ASSERT(t.pieces(cp, p, count));
			expectInk(inkOf(src, cp), pieceInk(count, p[0], p[1], count == 3 ? p[2] : 0));
		}
		TS_ASSERT_EQUALS(src.composed(), n);
		expectInk(inkOf(src, 0xAC00), pieceInk(2, 1, 161));
		TS_ASSERT_EQUALS(src.composed(), n + 1);
		// The newest are still kept.
		const uint32 last = 0xAC00 + (n - 1) * 37;
		inkOf(src, last);
		TS_ASSERT_EQUALS(src.composed(), n + 1);
	}

	// A list of syllables is expanded to the pieces the file holds: those are
	// sized for, read in file order, and then draw without a read.
	void test_preload_expands_syllables_to_pieces() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		const uint before = Common::FileCacheRegistry::all().size();
		Graphics::JamoGlyphSource src(loadFont(bytes, true), DisposeAfterUse::YES);
		src.setName("JP.SVF");
		const Common::FileCacheStats *stats = nullptr;
		for (uint i = before; i < Common::FileCacheRegistry::all().size(); ++i)
			if (Common::FileCacheRegistry::all()[i]->name == "JP.SVF")
				stats = Common::FileCacheRegistry::all()[i];
		TS_ASSERT(stats != nullptr);
		if (!stats)
			return;
		Common::Array<uint32> list;
		list.push_back(0xAC00);	// pieces 1, 161
		list.push_back(0xAC01);	// pieces 101, 205, 249
		list.push_back('A');
		list.push_back(0xAC00);
		TS_ASSERT_EQUALS(src.preload(list), 6u);
		// Five pieces and 'A', in glyph slots 0, 2, 102, 162, 206, 250: two 128-glyph blocks.
		TS_ASSERT_EQUALS(stats->listed, 6u);
		TS_ASSERT_EQUALS(stats->preloaded, 6u);
		TS_ASSERT_EQUALS(stats->reads, 2u);
		expectInk(inkOf(src, 0xAC00), pieceInk(2, 1, 161));
		expectInk(inkOf(src, 0xAC01), pieceInk(3, 101, 205, 249));
		TS_ASSERT_EQUALS(stats->reads, 2u);
		// A syllable not listed reads its pieces on demand.
		expectInk(inkOf(src, 0xD759), pieceInk(3, 139, 245, 341));
		TS_ASSERT(stats->reads > 2u);
	}

	// A face held whole ignores the list.
	void test_preload_is_ignored_by_a_whole_jamo_face() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, false), DisposeAfterUse::YES);
		Common::Array<uint32> list;
		list.push_back(0xAC00);
		TS_ASSERT_EQUALS(src.preload(list), 0u);
		expectInk(inkOf(src, 0xAC00), pieceInk(2, 1, 161));
	}

	void test_prefetch_takes_out_what_it_composes() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::JamoGlyphSource src(loadFont(bytes, true), DisposeAfterUse::YES);
		Common::Array<uint32> cps;
		cps.push_back(0xAC00);
		cps.push_back('A');
		cps.push_back(0x3131);
		cps.push_back(0x1234);	// not in the font
		cps.push_back(0xAC01);
		cps.push_back('Z');		// not in the font
		src.prefetch(cps);
		TS_ASSERT_EQUALS(cps.size(), 2u);
		if (cps.size() == 2) {
			TS_ASSERT_EQUALS(cps[0], 0x1234u);
			TS_ASSERT_EQUALS(cps[1], (uint32)'Z');
		}
		// And what was prefetched still draws right.
		expectInk(inkOf(src, 0xAC01), pieceInk(3, 101, 205, 249));
	}
	// ---- createSvfnSource() ----------------------------------------------

	static Common::Array<Common::String> &warned() {
		static Common::Array<Common::String> messages;
		return messages;
	}

	static void watch(LogMessageType::Type type, int level, uint32 channel, const char *message) {
		if (type == LogMessageType::kWarning)
			warned().push_back(message);
	}

	/// Warnings of the kind this layer gives when it drops a font's jamo sets.
	static uint ignoredWarnings(const char *needle) {
		uint n = 0;
		for (uint i = 0; i < warned().size(); ++i)
			if (warned()[i].contains("jamo sets ignored") && warned()[i].contains(needle))
				++n;
		return n;
	}

	Graphics::UnicodeGlyphSource *open(const Bytes &bytes, bool streamed, const char *name = "TEST.SVF") {
		Common::String error;
		if (streamed)
			return Graphics::createSvfnSource(new Common::MemoryReadStream(bytes.begin(), bytes.size()),
											  DisposeAfterUse::YES, error, name);
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		return Graphics::createSvfnSource(stream, error);
	}

public:
	void test_file_with_jamo_sets_composes() {
		for (int streamed = 0; streamed < 2; ++streamed) {
			const Bytes bytes = makeFont(pieceCps(), make844Jset());
			Graphics::UnicodeGlyphSource *src = open(bytes, streamed != 0);
			TS_ASSERT(src != nullptr);
			if (!src)
				return;
			TS_ASSERT_EQUALS(src->cells(0xAC01), 2);
			expectInk(inkOf(*src, 0xAC01), pieceInk(3, 101, 205, 249));
			TS_ASSERT_EQUALS(src->cells('A'), 1);
			delete src;
		}
	}

	void test_invalid_jamo_block_opens_as_plain_font_with_one_warning() {
		Bytes jset = make844Jset();
		jset.pop_back();
		const Bytes bytes = makeFont(pieceCps(), jset);
		const Common::LogWatcher before = Common::getLogWatcher();
		for (int streamed = 0; streamed < 2; ++streamed) {
			warned().clear();
			Common::setLogWatcher(watch);
			Graphics::UnicodeGlyphSource *src = open(bytes, streamed != 0, "BADJ.SVF");
			Common::setLogWatcher(before);
			TS_ASSERT(src != nullptr);
			if (!src)
				return;
			TS_ASSERT_EQUALS(ignoredWarnings("length"), 1u);
			if (streamed)
				TS_ASSERT_EQUALS(ignoredWarnings("BADJ.SVF"), 1u);
			// Plain: the file's own glyphs only, a syllable falls through.
			TS_ASSERT_EQUALS(src->cells(0xAC01), 0);
			TS_ASSERT_EQUALS(src->cells('A'), 1);
			// Used a lot: no further warnings.
			inkOf(*src, 'A');
			TS_ASSERT_EQUALS(ignoredWarnings("length"), 1u);
			delete src;
		}
	}

	void test_piece_without_a_glyph_opens_as_plain_font_with_one_warning() {
		Common::Array<uint32> cps = pieceCps();
		cps.pop_back();	// piece 359
		const Bytes bytes = makeFont(cps, make844Jset());
		warned().clear();
		const Common::LogWatcher before = Common::getLogWatcher();
		Common::setLogWatcher(watch);
		Graphics::UnicodeGlyphSource *src = open(bytes, false);
		Common::setLogWatcher(before);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;
		TS_ASSERT_EQUALS(ignoredWarnings("359"), 1u);
		TS_ASSERT_EQUALS(src->cells(0xAC00), 0);
		delete src;
	}

	void test_whole_file_overload_names_the_file_in_the_warning() {
		Bytes jset = make844Jset();
		jset.pop_back();
		const Bytes bytes = makeFont(pieceCps(), jset);
		warned().clear();
		const Common::LogWatcher before = Common::getLogWatcher();
		Common::setLogWatcher(watch);
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		Common::String error;
		Graphics::UnicodeGlyphSource *src = Graphics::createSvfnSource(stream, error, "WHOLE.SVF");
		Common::setLogWatcher(before);
		TS_ASSERT(src != nullptr);
		TS_ASSERT_EQUALS(ignoredWarnings("WHOLE.SVF"), 1u);
		delete src;
	}

	void test_font_without_the_flag_is_unchanged() {
		const Bytes bytes = makeFont(pieceCps(), make844Jset(), false);
		warned().clear();
		const Common::LogWatcher before = Common::getLogWatcher();
		Common::setLogWatcher(watch);
		Graphics::UnicodeGlyphSource *src = open(bytes, false);
		Common::setLogWatcher(before);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;
		TS_ASSERT_EQUALS(ignoredWarnings(""), 0u);
		TS_ASSERT_EQUALS(src->cells(0xAC00), 0);	// no syllables: a KO2350-like plain font's gap
		TS_ASSERT_EQUALS(src->cells('A'), 1);
		TS_ASSERT_EQUALS(src->cells(Graphics::kJamoPieceBase + 5), 1);	// pieces are plain glyphs
		delete src;
	}
	void test_source_for_a_font_loaded_by_the_caller() {
		// Engines that load the font themselves (SCUMM's faces) hand it over.
		const Bytes bytes = makeFont(pieceCps(), make844Jset());
		Graphics::HiResBitmapFont *font = loadFont(bytes, true);
		Graphics::UnicodeGlyphSource *src = Graphics::createSvfnSource(font, "OWN.SVF");
		TS_ASSERT(src != nullptr);
		if (!src)
			return;
		expectInk(inkOf(*src, 0xAC01), pieceInk(3, 101, 205, 249));
		delete src;	// and with it the font

		warned().clear();
		Bytes jset = make844Jset();
		jset.pop_back();
		const Bytes bad = makeFont(pieceCps(), jset);
		const Common::LogWatcher before = Common::getLogWatcher();
		Common::setLogWatcher(watch);
		src = Graphics::createSvfnSource(loadFont(bad, false), "OWNBAD.SVF");
		Common::setLogWatcher(before);
		TS_ASSERT_EQUALS(ignoredWarnings("OWNBAD.SVF"), 1u);
		TS_ASSERT_EQUALS(src->cells(0xAC01), 0);
		delete src;
	}
};

const byte HiResTextJamoTestSuite::kInitNoFin[21] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0 };
const byte HiResTextJamoTestSuite::kInitFin[21] = { 5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5 };
const byte HiResTextJamoTestSuite::kFinSet[21] = { 0, 2, 0, 2, 1, 2, 1, 2, 3, 0, 2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1 };
