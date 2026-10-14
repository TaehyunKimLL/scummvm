#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/bitmap_font.h"
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
	static Bytes make844Jset() {
		const int LC = 2, TC = 2;
		Bytes b;
		const byte head[] = { 'J', 'S', 'E', 'T', 1, 0, (byte)LC, (byte)TC, 8, 4, 4, 0 };
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

	/**
	 * A version 2 SVFN, 16x16 1bpp, one glyph per code point in @p cps (each
	 * with the single pixel (n % 16, n / 16 % 16) for n = cp - U+F0000, else
	 * (0, 0)), the code point table last and @p jset right after it. @p jset
	 * may be empty (no block).
	 */
	static Bytes makeFont(const Common::Array<uint32> &cps, const Bytes &jset, bool flag = true) {
		const uint32 glyphs = cps.size();
		const uint32 stride = 32;
		const uint32 dataOff = 36;
		const uint32 cmapOff = dataOff + glyphs * stride;
		Bytes b;
		b.resize(cmapOff + glyphs * 8);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, flag ? 2 : 0);
		b[8] = 1;
		put16(b, 12, glyphs);
		b[14] = 16;
		b[15] = 16;
		b[16] = 14;
		put32(b, 24, dataOff);
		put32(b, 28, glyphs * stride);
		put32(b, 32, cmapOff);
		for (uint i = 0; i < glyphs; ++i) {
			const uint32 n = cps[i] >= Graphics::kJamoPieceBase ? cps[i] - Graphics::kJamoPieceBase : 0;
			const uint x = n % 16, y = n / 16 % 16;
			b[dataOff + i * stride + y * 2 + x / 8] |= 0x80 >> (x & 7);
			put32(b, cmapOff + i * 8, cps[i]);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		for (uint i = 0; i < jset.size(); ++i)
			b.push_back(jset[i]);
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
};

const byte HiResTextJamoTestSuite::kInitNoFin[21] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 3, 3, 1, 2, 4, 4, 4, 2, 1, 3, 0 };
const byte HiResTextJamoTestSuite::kInitFin[21] = { 5, 5, 5, 5, 5, 5, 5, 5, 6, 7, 7, 7, 6, 6, 7, 7, 7, 6, 6, 7, 5 };
const byte HiResTextJamoTestSuite::kFinSet[21] = { 0, 2, 0, 2, 1, 2, 1, 2, 3, 0, 2, 1, 3, 3, 1, 2, 1, 3, 3, 1, 1 };
