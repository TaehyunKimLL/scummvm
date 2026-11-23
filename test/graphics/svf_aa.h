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
#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/ustr.h"

#include "graphics/fonts/svffont.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/text_compose.h"
#include "graphics/hires_text/text_plane.h"
#include "graphics/pixelformat.h"
#include "graphics/surface.h"
#include "image/png.h"

#include "test/graphics/svf_fixture.h"
#include "test/graphics/svf_fixture_aa.h"
#include "test/graphics/svf_golden_aa_data.h"
#include "test/graphics/svf_test_util.h"
#include "test/system/null_osystem.h"

#define SVF_AA_ARGS(p) p##Text, p##X, p##Y, p##W, p##H, p##ScaleNum, p##ScaleDen, p##Missing, p##Rows

// The anti-aliased (2 bpp, one 8 bpp) faces made from TrueType by tools/korean/gen_aa_faces.py, drawn in chains over the
// golden lines of tools/svfline.py (tools/korean/mkgolden.py): single faces, precomposed + jamo set in both orders,
// a three face chain, 1 bpp mixed with 2 bpp, two cell sizes, 8 bpp; through the C targets, SvfFont and TextPlane + sinks.
class SvfAaTestSuite : public CxxTest::TestSuite {
	enum { kP, kP18, kL, kJA, kP8, kMarks, kJamo1, kFaceCount };

	struct Blob {
		const byte *data;
		uint32 size;
	};

	static Blob blob(int face) {
		static const Blob table[kFaceCount] = {
			{ kSvfAaNG16P, sizeof(kSvfAaNG16P) }, { kSvfAaNG18P, sizeof(kSvfAaNG18P) }, { kSvfAaNG16L, sizeof(kSvfAaNG16L) },
			{ kSvfAaNG16JA, sizeof(kSvfAaNG16JA) }, { kSvfAaNG16P8, sizeof(kSvfAaNG16P8) },
			{ kSvfFixtureMarks, sizeof(kSvfFixtureMarks) }, { kSvfFixtureJamo, sizeof(kSvfFixtureJamo) }
		};
		return table[face];
	}

	// The chain of @p faces (indexes of the face table), mapped or streamed.
	static Graphics::SvfChain *makeChain(const int *faces, int count, uint num = 1, uint den = 1, uint8 missing = 0,
	                                     bool streamed = false, uint16 slots = 8) {
		Graphics::SvfChain *chain = new Graphics::SvfChain();
		for (int i = 0; i < count; ++i) {
			Graphics::SvfFace *f = new Graphics::SvfFace();
			const Blob b = blob(faces[i]);
			if (streamed) {
				TS_ASSERT(f->loadStreamed(new Common::MemoryReadStream(b.data, b.size), DisposeAfterUse::YES, "f", 1024, slots));
			} else {
				TS_ASSERT(f->loadFromMemory(b.data, b.size, "f"));
			}
			TS_ASSERT(chain->addFace(f, DisposeAfterUse::YES));
		}
		chain->setScale(num, den);
		chain->setMissingAdvance(missing);
		return chain;
	}

	static Graphics::SvfChain *single(int face, uint num = 1, uint den = 1) {
		return makeChain(&face, 1, num, den);
	}

	// GRAY8 of @p text through chain->draw.
	static Common::Array<byte> gray(const Graphics::SvfChain &chain, const char *text, int x, int y, int w, int h) {
		Common::Array<byte> buf;
		buf.resize(w * h);
		memset(buf.data(), 0, buf.size());
		svf_target t;
		TS_ASSERT_EQUALS(svf_target_init(&t, SVF_T_GRAY8, buf.data(), buf.size(), w, h, w), SVF_OK);
		TS_ASSERT_EQUALS(chain.draw(text, strlen(text), x, y, t), SVF_OK);
		return buf;
	}

	static Common::Array<byte> expected(const char *const *rows, int w, int h) {
		return SvfTest::goldenBytes(rows, w, h);
	}

	static void histogram(const Common::Array<byte> &px, uint h[256]) {
		memset(h, 0, 256 * sizeof(uint));
		for (uint i = 0; i < px.size(); ++i)
			++h[px[i]];
	}

	static uint inkSum(const Common::Array<byte> &px) {
		uint s = 0;
		for (uint i = 0; i < px.size(); ++i)
			s += px[i];
		return s;
	}

	// Ink rows [top, bottom] of a w x h buffer; false when empty.
	static bool inkRows(const Common::Array<byte> &px, int w, int h, int &top, int &bottom) {
		top = bottom = -1;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				if (px[y * w + x]) {
					if (top < 0)
						top = y;
					bottom = y;
				}
		return top >= 0;
	}

	void checkLine(const char *name, const char *text, const int *faces, int count, int x, int y, int w, int h, int num, int den,
	               int missing, const char *const *rows, bool streamed = false, uint slots = 0) {
		Graphics::SvfChain *chain = makeChain(faces, count, num, den, missing, streamed);
		chain->setSyllableCache(slots);
		Common::Array<byte> got = gray(*chain, text, x, y, w, h);
		if (slots)
			got = gray(*chain, text, x, y, w, h);	// a second, cached draw must not change a pixel
		const Common::Array<byte> want = expected(rows, w, h);
		const bool same = memcmp(got.data(), want.data(), got.size()) == 0;
		TSM_ASSERT(name, same);
		if (!same) {
			TS_TRACE(("got" + SvfTest::asciiArt(got.data(), w, h)).c_str());
			TS_TRACE(("expected" + SvfTest::asciiArt(want.data(), w, h)).c_str());
		}
		delete chain;
	}

#define CHECK_LINE(P) checkLine(#P, P##Text, P##Faces, P##FaceCount, P##X, P##Y, P##W, P##H, P##ScaleNum, P##ScaleDen, \
	                        P##Missing, P##Rows)
#define CHECK_STREAMED(P, SLOTS) checkLine(#P, P##Text, P##Faces, P##FaceCount, P##X, P##Y, P##W, P##H, P##ScaleNum, P##ScaleDen, \
	                        P##Missing, P##Rows, true, SLOTS)

	static void plotToPlane(void *ctx, int16_t x, int16_t y, uint8_t cov) {
		((Graphics::TextPlane *)ctx)->plot(x, y, cov, 1);
	}

	// A plane of the line's size (at hi-res scale @p scale) with @p text drawn into it through the coverage callback,
	// the way HiResFontSet::drawChar does.
	static void drawPlane(Graphics::TextPlane &plane, const int *faces, int count, int num, int den, int missing,
	                      const char *text, int x, int y, int w, int h, int scale) {
		Graphics::SvfChain *chain = makeChain(faces, count, num, den, missing);
		TS_ASSERT(plane.create(w / scale, h / scale, scale));
		TS_ASSERT_EQUALS(chain->drawCoverage(text, strlen(text), x, y, w, h, plotToPlane, &plane), SVF_OK);
		delete chain;
	}

	static uint32 pixelAt(const Graphics::Surface &s, int x, int y) {
		const void *p = s.getBasePtr(x, y);
		return s.format.bytesPerPixel == 1 ? *(const byte *)p : s.format.bytesPerPixel == 2 ? *(const uint16 *)p : *(const uint32 *)p;
	}

	static const byte *palette() {
		// index 0 black, 1 the text colour, 2 the background of the RGB surfaces
		static const byte p[9] = { 0, 0, 0, 240, 224, 64, 16, 32, 96 };
		return p;
	}

public:
	// ---- GRAY8 goldens: every chain ------------------------------------------------------------------------------

	void test_goldenLines() {
		CHECK_LINE(kSvfAaGoldenPreOnly);
		CHECK_LINE(kSvfAaGoldenJamoOnly);
		CHECK_LINE(kSvfAaGoldenChainPJ);
		CHECK_LINE(kSvfAaGoldenChainPJ2x);
		CHECK_LINE(kSvfAaGoldenChainPJ3by2);
		CHECK_LINE(kSvfAaGoldenChainJP);
		CHECK_LINE(kSvfAaGoldenChainLPJ);
		CHECK_LINE(kSvfAaGoldenChainMixedBpp);
		CHECK_LINE(kSvfAaGoldenChainSizes);
		CHECK_LINE(kSvfAaGoldenChainSizes2x);
		CHECK_LINE(kSvfAaGoldenChainSizes3by2);
		CHECK_LINE(kSvfAaGoldenBpp8);
	}

	// A streamed face (block cache, with and without the composed-syllable cache) draws as the mapped one.
	void test_streamedEqualsMapped() {
		CHECK_STREAMED(kSvfAaGoldenChainPJ, 0);
		CHECK_STREAMED(kSvfAaGoldenChainPJ, 16);
		CHECK_STREAMED(kSvfAaGoldenChainPJ3by2, 16);
		CHECK_STREAMED(kSvfAaGoldenChainSizes, 16);
		CHECK_STREAMED(kSvfAaGoldenBpp8, 0);
	}

	void test_extents() {
		static const struct { const int *faces; int n; const char *text; int w, a, d, num, den; } c[] = {
			{ kSvfAaGoldenChainPJFaces, 2, kSvfAaGoldenChainPJText, kSvfAaGoldenChainPJWidth, kSvfAaGoldenChainPJAscent, kSvfAaGoldenChainPJDescent, 1, 1 },
			{ kSvfAaGoldenChainJPFaces, 2, kSvfAaGoldenChainJPText, kSvfAaGoldenChainJPWidth, kSvfAaGoldenChainJPAscent, kSvfAaGoldenChainJPDescent, 1, 1 },
			{ kSvfAaGoldenChainSizesFaces, 2, kSvfAaGoldenChainSizesText, kSvfAaGoldenChainSizesWidth, kSvfAaGoldenChainSizesAscent, kSvfAaGoldenChainSizesDescent, 1, 1 },
			{ kSvfAaGoldenChainSizes3by2Faces, 2, kSvfAaGoldenChainSizes3by2Text, kSvfAaGoldenChainSizes3by2Width, kSvfAaGoldenChainSizes3by2Ascent, kSvfAaGoldenChainSizes3by2Descent, 3, 2 },
		};
		for (uint i = 0; i < ARRAYSIZE(c); ++i) {
			Graphics::SvfChain *chain = makeChain(c[i].faces, c[i].n, c[i].num, c[i].den, 6);
			if (i == 3)
				chain->setMissingAdvance(0);
			svf_extent e;
			TS_ASSERT_EQUALS(chain->measure(c[i].text, strlen(c[i].text), e), SVF_OK);
			TS_ASSERT_EQUALS(e.width, c[i].w);
			TS_ASSERT_EQUALS(e.ascent, c[i].a);
			TS_ASSERT_EQUALS(e.descent, c[i].d);
			delete chain;
		}
	}

	// ---- the four levels and the level mapping -------------------------------------------------------------------

	void test_fourLevels() {
		Graphics::SvfChain *chain = makeChain(kSvfAaGoldenChainPJFaces, 2);
		const Common::Array<byte> g = gray(*chain, kSvfAaGoldenChainPJText, kSvfAaGoldenChainPJX, kSvfAaGoldenChainPJY, kSvfAaGoldenChainPJW,
		                                   kSvfAaGoldenChainPJH);
		uint h[256];
		histogram(g, h);
		TS_ASSERT(h[0] > 0 && h[85] > 0 && h[170] > 0 && h[255] > 0);
		uint other = 0;
		for (int i = 1; i < 255; ++i)
			if (i != 85 && i != 170)
				other += h[i];
		TS_ASSERT_EQUALS(other, 0u);	// a 2 bpp face has no other level at 1x
		TS_ASSERT(h[255] > h[85] && h[255] > h[170]);
		delete chain;

		// jamo-set pieces OR as the per-pixel maximum, field-wise at 2 bpp: no composed pixel is above both pieces' max, and
		// the composed syllable keeps every level (the jamo-only line has the four levels too)
		chain = makeChain(kSvfAaGoldenJamoOnlyFaces, 1);
		histogram(gray(*chain, kSvfAaGoldenJamoOnlyText, kSvfAaGoldenJamoOnlyX, kSvfAaGoldenJamoOnlyY, kSvfAaGoldenJamoOnlyW,
		               kSvfAaGoldenJamoOnlyH), h);
		TS_ASSERT(h[85] > 0 && h[170] > 0 && h[255] > 0);
		delete chain;

		// 3/2 gives levels between the four
		chain = makeChain(kSvfAaGoldenChainPJ3by2Faces, 2, 3, 2);
		histogram(gray(*chain, kSvfAaGoldenChainPJ3by2Text, kSvfAaGoldenChainPJ3by2X, kSvfAaGoldenChainPJ3by2Y, kSvfAaGoldenChainPJ3by2W,
		               kSvfAaGoldenChainPJ3by2H), h);
		other = 0;
		for (int i = 1; i < 255; ++i)
			if (i != 85 && i != 170)
				other += h[i];
		TS_ASSERT(other > 0);
		delete chain;

		// 8 bpp: the byte is the coverage
		chain = makeChain(kSvfAaGoldenBpp8Faces, 2);
		histogram(gray(*chain, kSvfAaGoldenBpp8Text, kSvfAaGoldenBpp8X, kSvfAaGoldenBpp8Y, kSvfAaGoldenBpp8W, kSvfAaGoldenBpp8H), h);
		other = 0;
		for (int i = 1; i < 255; ++i)
			if (i != 85 && i != 170)
				other += h[i];
		TS_ASSERT(other > 20);
		TS_ASSERT_EQUALS(chain->getFace(0)->getBpp(), 8u);
		TS_ASSERT_EQUALS(chain->getFace(1)->getBpp(), 2u);
		delete chain;
	}

	void test_faceFacts() {
		const int faces[] = { kP, kP18, kL, kJA, kP8, kMarks, kJamo1 };
		const uint bpp[] = { 2, 2, 2, 2, 8, 1, 1 };
		for (int i = 0; i < kFaceCount; ++i) {
			Graphics::SvfChain *c = single(faces[i]);
			TS_ASSERT_EQUALS(c->getFace(0)->getBpp(), bpp[i]);
			delete c;
		}
		Graphics::SvfChain *c = makeChain(kSvfAaGoldenChainPJFaces, 2);
		const Graphics::SvfFace *p = c->getFace(0), *ja = c->getFace(1);
		TS_ASSERT(!p->isJamo());
		TS_ASSERT(ja->isJamo());
		TS_ASSERT_EQUALS(p->getCellWidth(), 16u);
		TS_ASSERT_EQUALS(ja->getCellWidth(), 16u);
		TS_ASSERT(p->has(0xD55C));
		TS_ASSERT(!p->has(0xB620));		// 똠 is not in the precomposed subset
		TS_ASSERT(!p->has(0x3131));		// nor is a compat jamo
		TS_ASSERT(ja->has(0xAC00) && ja->has(0xD7A3) && ja->has(0xB620) && ja->has(0xBF41));	// every syllable
		TS_ASSERT(ja->has(0x3131) && ja->has(0x3163));
		delete c;
	}

	// ---- which face draws a code point ---------------------------------------------------------------------------

	// Draws one code point through @p chain at a fixed pen; the same through every face of the chain alone.
	// Returns the index of the faces whose own drawing equals the chain's (the first face with the glyph).
	uint drawnBy(const int *faces, int count, const char *utf8, bool &blank) {
		Graphics::SvfChain *chain = makeChain(faces, count);
		const Common::Array<byte> got = gray(*chain, utf8, 4, 18, 48, 24);
		blank = inkSum(got) == 0;
		uint mask = 0;
		for (int i = 0; i < count; ++i) {
			Graphics::SvfChain *one = makeChain(faces + i, 1);
			const Common::Array<byte> own = gray(*one, utf8, 4, 18, 48, 24);
			if (own.size() == got.size() && memcmp(own.data(), got.data(), got.size()) == 0)
				mask |= 1u << i;
			delete one;
		}
		delete chain;
		return mask;
	}

	void test_provenance() {
		static const int pj[] = { kP, kJA }, jp[] = { kJA, kP }, lpj[] = { kL, kP, kJA }, mixed[] = { kMarks, kP, kJamo1 }, sizes[] = { kP18, kJA };
		static const struct { const int *faces; int n; const char *text; int face; } c[] = {
			// [precomposed, jamo]: the subset from the precomposed face, the rest composed
			{ pj, 2, "\xea\xb0\x80", 0 },			// 가
			{ pj, 2, "\xed\x95\x9c", 0 },			// 한
			{ pj, 2, "\xeb\x98\xa0", 1 },			// 똠
			{ pj, 2, "\xec\x80\x8d", 1 },			// 쀍 (U+C00D? any syllable outside the subset)
			{ pj, 2, "\xeb\xb7\x81", 1 },			// 뷁
			{ pj, 2, "\xe3\x84\xb1", 1 },			// ㄱ
			{ pj, 2, "\xe3\x85\xa3", 1 },			// ㅣ
			{ pj, 2, "A", 0 }, { pj, 2, "9", 0 }, { pj, 2, ",", 0 },
			// [jamo, precomposed]: the jamo face has every syllable and ASCII, so the second face never draws
			{ jp, 2, "\xea\xb0\x80", 0 }, { jp, 2, "\xed\x95\x9c", 0 }, { jp, 2, "\xeb\x98\xa0", 0 }, { jp, 2, "A", 0 },
			{ jp, 2, "\xe3\x84\xb1", 0 },
			// [Latin, precomposed, jamo]
			{ lpj, 3, "A", 0 }, { lpj, 3, "9", 0 }, { lpj, 3, "\xea\xb0\x80", 1 }, { lpj, 3, "\xeb\x98\xa0", 2 }, { lpj, 3, "\xe3\x84\xb1", 2 },
			// [1 bpp marks, 2 bpp precomposed, 1 bpp jamo]
			{ mixed, 3, "A", 0 }, { mixed, 3, "b", 1 }, { mixed, 3, "\xea\xb0\x80", 1 }, { mixed, 3, "\xeb\x98\xa0", 2 }, { mixed, 3, "\xe3\x84\xb1", 2 },
			// two cell sizes
			{ sizes, 2, "\xea\xb0\x80", 0 }, { sizes, 2, "\xeb\x98\xa0", 1 }, { sizes, 2, "A", 0 },
		};
		for (uint i = 0; i < ARRAYSIZE(c); ++i) {
			bool blank;
			const uint mask = drawnBy(c[i].faces, c[i].n, c[i].text, blank);
			TSM_ASSERT(c[i].text, !blank);
			// the face that should draw it is among those whose own drawing matches
			TSM_ASSERT_DIFFERS(c[i].text, mask & (1u << c[i].face), 0u);
			// and no earlier face drew it
			for (int f = 0; f < c[i].face; ++f)
				TSM_ASSERT_EQUALS(c[i].text, mask & (1u << f), 0u);
		}
		// the order decides: for 한 the two faces draw different pixels (the test above would not tell them apart otherwise)
		Graphics::SvfChain *a = single(kP), *b = single(kJA);
		TS_ASSERT(gray(*a, "\xed\x95\x9c", 4, 18, 48, 24) != gray(*b, "\xed\x95\x9c", 4, 18, 48, 24));
		delete a;
		delete b;
	}

	// ---- coherence of the join of two faces ---------------------------------------------------------------------

	void test_coherence() {
		Graphics::SvfChain *p = single(kP), *ja = single(kJA);
		const char *hangul = "\xed\x95\x9c", *glyphs[] = { "\xed\x95\x9c", "\xea\xb8\x80", "\xea\xb0\x80" };
		// Advance: the syllables step by the same 15 px in both faces, so the chain has no gap or overlap at the join.
		for (uint i = 0; i < 3; ++i)
			TS_ASSERT_EQUALS(p->getWidth(glyphs[i], 3), ja->getWidth(glyphs[i], 3));
		TS_ASSERT_EQUALS(p->getWidth(hangul, 3), 15);
		// Baseline: the ink of a Latin letter ends on the same row in both faces; a syllable's ink ends on the same row
		int pt, pb, jt, jb;
		TS_ASSERT(inkRows(gray(*p, "A", 0, 15, 40, 24), 40, 24, pt, pb));
		TS_ASSERT(inkRows(gray(*ja, "A", 0, 15, 40, 24), 40, 24, jt, jb));
		TS_ASSERT_EQUALS(pb, jb);
		TS_ASSERT_EQUALS(pt, jt);
		for (uint i = 0; i < 3; ++i) {
			TS_ASSERT(inkRows(gray(*p, glyphs[i], 0, 15, 40, 24), 40, 24, pt, pb));
			TS_ASSERT(inkRows(gray(*ja, glyphs[i], 0, 15, 40, 24), 40, 24, jt, jb));
			TS_ASSERT(pb - jb >= -1 && pb - jb <= 1);
			TS_ASSERT(pt - jt >= -1 && pt - jt <= 1);
			// stroke weight: the coverage sum of the same syllable in the two faces (same TrueType, same size)
			const uint sp = inkSum(gray(*p, glyphs[i], 0, 15, 40, 24)), sj = inkSum(gray(*ja, glyphs[i], 0, 15, 40, 24));
			TS_ASSERT(sp * 100 > sj * 90 && sp * 100 < sj * 110);	// measured 0.98 .. 1.05
		}
		// the faces disagree by one row on the ascent (14 and 13), so the chain's ascent is the larger
		TS_ASSERT_EQUALS(p->getAscent(), 14);
		TS_ASSERT_EQUALS(ja->getAscent(), 13);
		Graphics::SvfChain *chain = makeChain(kSvfAaGoldenChainPJFaces, 2);
		TS_ASSERT_EQUALS(chain->getAscent(), 14);
		TS_ASSERT_EQUALS(chain->getDescent(), 3);
		TS_ASSERT_EQUALS(chain->getCellHeight(), 16);
		delete chain;
		delete p;
		delete ja;
	}

	// ---- two cell sizes in one chain ----------------------------------------------------------------------------

	void test_cellSizes() {
		Graphics::SvfChain *chain = makeChain(kSvfAaGoldenChainSizesFaces, 2);
		TS_ASSERT_EQUALS(chain->getFace(0)->getCellHeight(), 18u);
		TS_ASSERT_EQUALS(chain->getFace(1)->getCellHeight(), 16u);
		TS_ASSERT_EQUALS(chain->getAscent(), 16);
		TS_ASSERT_EQUALS(chain->getDescent(), 3);
		TS_ASSERT_EQUALS(chain->getCellHeight(), 18);
		TS_ASSERT_EQUALS(chain->getMaxCellWidth(), 18);
		// a syllable of the 18 px face steps 17 px, one of the 16 px jamo face 15 px
		TS_ASSERT_EQUALS(chain->getWidth("\xea\xb0\x80", 3), 17);		// 가: face 0
		TS_ASSERT_EQUALS(chain->getWidth("\xeb\x98\xa0", 3), 15);		// 똠: face 1
		delete chain;
		for (int den = 1; den <= 2; ++den) {
			const int num = den == 1 ? 2 : 3;
			chain = makeChain(kSvfAaGoldenChainSizesFaces, 2, num, den);
			TS_ASSERT_EQUALS(chain->getAscent(), Graphics::SvfChain::scaleLength(16, num, den));
			TS_ASSERT_EQUALS(chain->getCellHeight(), Graphics::SvfChain::scaleDimension(18, num, den));
			delete chain;
		}
		// SvfFont reports the same
		Graphics::SvfFont font(makeChain(kSvfAaGoldenChainSizesFaces, 2), DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(font.getFontAscent(), 16);
		TS_ASSERT_EQUALS(font.getFontDescent(), 3);
		TS_ASSERT_EQUALS(font.getFontHeight(), 18);
		TS_ASSERT_EQUALS(font.getCharWidth(0xAC00), 17);
		TS_ASSERT_EQUALS(font.getCharWidth(0xB620), 15);
		// Clipping: a line drawn into a smaller target writes only inside it (the 18 px cell sticks out above and below)
		Graphics::SvfChain *c2 = makeChain(kSvfAaGoldenChainSizesFaces, 2);
		byte big[40 * 40];
		memset(big, 7, sizeof(big));
		svf_target t;
		svf_target_init(&t, SVF_T_GRAY8, big, sizeof(big), 40, 40, 40);
		t.clip_x0 = 10;
		t.clip_y0 = 12;
		t.clip_x1 = 30;
		t.clip_y1 = 22;
		TS_ASSERT_EQUALS(c2->draw("\xea\xb0\x80\xeb\x98\xa0", 6, 8, 26, t), SVF_OK);
		int outside = 0, inside = 0;
		for (int y = 0; y < 40; ++y)
			for (int x = 0; x < 40; ++x) {
				const bool in = x >= 10 && x < 30 && y >= 12 && y < 22;
				outside += !in && big[y * 40 + x] != 7;
				inside += in && big[y * 40 + x] != 7;
			}
		TS_ASSERT_EQUALS(outside, 0);
		TS_ASSERT(inside > 0);
		delete c2;
	}

	// ---- the C targets ------------------------------------------------------------------------------------------

	enum TargetKind { kT565, kT555, kT888 };

	static uint16 pack565(uint32 c) {
		return (c >> 8 & 0xF800) | (c >> 5 & 0x07E0) | (c >> 3 & 0x1F);
	}
	static uint16 pack555(uint32 c) {
		return (c >> 9 & 0x7C00) | (c >> 6 & 0x03E0) | (c >> 3 & 0x1F);
	}

	void checkTarget(const int *faces, int n, const char *text, int x, int y, int w, int h, int num, int den, int missing,
	                 const char *const *grayRows, const char *const *rgbRows, TargetKind kind, bool big = false, uint16 bit15 = 0) {
		Graphics::SvfChain *chain = makeChain(faces, n, num, den, missing);
		const bool is888 = kind == kT888;
		const int bpp = is888 ? 3 : 2;
		Common::Array<byte> buf;
		buf.resize(w * h * bpp);
		const uint32 bg = 0x102060, fg = 0xF0E040;
		const uint16 bg16 = kind == kT555 ? pack555(bg) : pack565(bg), fg16 = (kind == kT555 ? pack555(fg) : pack565(fg)) | bit15;
		for (int i = 0; i < w * h; ++i) {
			if (is888) {
				buf[3 * i] = bg & 255;
				buf[3 * i + 1] = bg >> 8 & 255;
				buf[3 * i + 2] = bg >> 16;
			} else {
				buf[2 * i] = big ? bg16 >> 8 : bg16 & 255;
				buf[2 * i + 1] = big ? bg16 & 255 : bg16 >> 8;
			}
		}
		svf_target t;
		TS_ASSERT_EQUALS(svf_target_init(&t, is888 ? SVF_T_RGB888 : kind == kT555 ? SVF_T_RGB555 : SVF_T_RGB565, buf.data(), buf.size(), w, h,
		                                 w * bpp), SVF_OK);
		t.fg565 = fg16;
		t.bg565 = bg16;
		t.fg888 = fg;
		t.bg888 = bg;
		t.flags = big ? SVF_TF_RGB16_BIGENDIAN : 0;
		TS_ASSERT_EQUALS(chain->draw(text, strlen(text), x, y, t), SVF_OK);
		int bad = 0, between = 0;
		const Common::Array<byte> cov = expected(grayRows, w, h);
		for (int yy = 0; yy < h; ++yy) {
			for (int xx = 0; xx < w; ++xx) {
				for (int k = 0; k < bpp; ++k) {
					const byte want = SvfTest::hexNibble(rgbRows[yy][2 * (xx * bpp + k)]) << 4 | SvfTest::hexNibble(rgbRows[yy][2 * (xx * bpp + k) + 1]);
					bad += buf[(yy * w + xx) * bpp + k] != want;
				}
				const byte c = cov[yy * w + xx];
				if (c == 85 || c == 170) {
					// strictly between the background and the text colour in the red channel
					byte r;
					if (is888)
						r = buf[(yy * w + xx) * 3 + 2];
					else {
						const uint16 v = big ? (buf[(yy * w + xx) * 2] << 8 | buf[(yy * w + xx) * 2 + 1])
						                     : (buf[(yy * w + xx) * 2 + 1] << 8 | buf[(yy * w + xx) * 2]);
						r = kind == kT555 ? ((v >> 10 & 31) << 3) : ((v >> 11 & 31) << 3);
					}
					between += r > 0x10 && r < 0xF0;
				}
				if (kind == kT555 && bit15 && c) {	// bit 15 of fg565 is copied to every drawn pixel
					const uint16 v = big ? (buf[(yy * w + xx) * 2] << 8 | buf[(yy * w + xx) * 2 + 1])
					                     : (buf[(yy * w + xx) * 2 + 1] << 8 | buf[(yy * w + xx) * 2]);
					bad += !(v & 0x8000);
				}
			}
		}
		TS_ASSERT_EQUALS(bad, 0);
		TS_ASSERT(between > 100);
		delete chain;
	}

#define CHECK_TARGET(P, ...) checkTarget(P##Faces, P##FaceCount, P##Text, P##X, P##Y, P##W, P##H, P##ScaleNum, P##ScaleDen, P##Missing, \
	                                     P##Rows, __VA_ARGS__)

	// The C targets at 16 bit (565 and 555, both byte orders, bit 15) and 32 bit against tools/svfline.py's arithmetic.
	void test_rgbTargets() {
		CHECK_TARGET(kSvfAaGoldenChainPJ, kSvfAaGoldenChainPJRgb565Rows, kT565);
		CHECK_TARGET(kSvfAaGoldenChainPJ, kSvfAaGoldenChainPJRgb565BeRows, kT565, true);
		CHECK_TARGET(kSvfAaGoldenChainPJ, kSvfAaGoldenChainPJRgb555Rows, kT555, false, 0x8000);
		CHECK_TARGET(kSvfAaGoldenChainPJ, kSvfAaGoldenChainPJRgb555BeRows, kT555, true, 0);
		CHECK_TARGET(kSvfAaGoldenChainPJ, kSvfAaGoldenChainPJRgb888Rows, kT888);
		CHECK_TARGET(kSvfAaGoldenChainPJ3by2, kSvfAaGoldenChainPJ3by2Rgb565Rows, kT565);
		CHECK_TARGET(kSvfAaGoldenChainPJ3by2, kSvfAaGoldenChainPJ3by2Rgb555Rows, kT555, false, 0x8000);
		CHECK_TARGET(kSvfAaGoldenChainPJ3by2, kSvfAaGoldenChainPJ3by2Rgb888Rows, kT888);
	}

	// ---- the plane keeps the coverage; the sinks --------------------------------------------------------------

	void checkPlane(const char *name, const int *faces, int n, const char *text, int x, int y, int w, int h, int num, int den,
	                int missing, const char *const *rows, int scale) {
		Graphics::TextPlane plane;
		drawPlane(plane, faces, n, num, den, missing, text, x, y, w, h, scale);
		const Common::Array<byte> want = expected(rows, w, h);
		int bad = 0;
		uint level[256];
		memset(level, 0, sizeof(level));
		for (int yy = 0; yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx) {
				const byte c = plane.getCoverage(xx, yy);
				bad += c != want[yy * w + xx];
				++level[c];
			}
		TSM_ASSERT_EQUALS(name, bad, 0);
		if (num == den) {	// the 85 and 170 of a 2 bpp face are in the plane, not thresholded away
			TSM_ASSERT(name, level[85] > 0 && level[170] > 0 && level[255] > 0);
		}
	}

	void test_planeKeepsCoverage() {
		checkPlane("PJ", kSvfAaGoldenChainPJFaces, 2, SVF_AA_ARGS(kSvfAaGoldenChainPJ), 1);
		checkPlane("PJ2x", kSvfAaGoldenChainPJ2xFaces, 2, SVF_AA_ARGS(kSvfAaGoldenChainPJ2x), 2);
		checkPlane("JP", kSvfAaGoldenChainJPFaces, 2, SVF_AA_ARGS(kSvfAaGoldenChainJP), 1);
		checkPlane("LPJ", kSvfAaGoldenChainLPJFaces, 3, SVF_AA_ARGS(kSvfAaGoldenChainLPJ), 1);
		checkPlane("Mixed", kSvfAaGoldenChainMixedBppFaces, 3, SVF_AA_ARGS(kSvfAaGoldenChainMixedBpp), 1);
		checkPlane("Sizes", kSvfAaGoldenChainSizesFaces, 2, SVF_AA_ARGS(kSvfAaGoldenChainSizes), 1);
		checkPlane("Sizes2x", kSvfAaGoldenChainSizes2xFaces, 2, SVF_AA_ARGS(kSvfAaGoldenChainSizes2x), 2);
		checkPlane("PJ3by2", kSvfAaGoldenChainPJ3by2Faces, 2, SVF_AA_ARGS(kSvfAaGoldenChainPJ3by2), 2);
		checkPlane("Bpp8", kSvfAaGoldenBpp8Faces, 2, SVF_AA_ARGS(kSvfAaGoldenBpp8), 1);
	}

	// CLUT8: the plane is stamped where coverage >= 128, so the levels 170 and 255 survive and 85 does not.
	void test_clut8Stamp() {
		const int w = kSvfAaGoldenChainPJW, h = kSvfAaGoldenChainPJH;
		Graphics::TextPlane plane;
		drawPlane(plane, kSvfAaGoldenChainPJFaces, 2, 1, 1, kSvfAaGoldenChainPJMissing, kSvfAaGoldenChainPJText, kSvfAaGoldenChainPJX,
		          kSvfAaGoldenChainPJY, w, h, 1);
		Graphics::Surface s;
		s.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
		s.fillRect(Common::Rect(w, h), 9);
		TS_ASSERT(Graphics::TextCompose::compositeRect(plane, Common::Rect(w, h), s, nullptr, Graphics::TextCompose::kStamp));
		const Common::Array<byte> cov = expected(kSvfAaGoldenChainPJRows, w, h);
		uint stamped = 0, ink = 0, lost85 = 0, kept170 = 0, kept255 = 0, wrong = 0;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const byte c = cov[y * w + x], v = *(const byte *)s.getBasePtr(x, y);
				ink += c > 0;
				stamped += v == 1;
				wrong += v != (c >= 128 ? 1 : 9);
				lost85 += c == 85 && v == 9;
				kept170 += c == 170 && v == 1;
				kept255 += c == 255 && v == 1;
			}
		TS_ASSERT_EQUALS(wrong, 0u);
		TS_ASSERT(lost85 > 0 && kept170 > 0 && kept255 > 0);
		TS_ASSERT(stamped < ink);
		s.free();
	}

	void checkBlend(const Graphics::PixelFormat &f, Graphics::TextCompose::Mode mode, const char *name) {
		const int w = kSvfAaGoldenChainPJW, h = kSvfAaGoldenChainPJH;
		Graphics::TextPlane plane;
		drawPlane(plane, kSvfAaGoldenChainPJFaces, 2, 1, 1, kSvfAaGoldenChainPJMissing, kSvfAaGoldenChainPJText, kSvfAaGoldenChainPJX,
		          kSvfAaGoldenChainPJY, w, h, 1);
		Graphics::Surface s;
		s.create(w, h, f);
		const uint32 bg = f.RGBToColor(palette()[6], palette()[7], palette()[8]);
		s.fillRect(Common::Rect(w, h), bg);
		TS_ASSERT(Graphics::TextCompose::compositeRect(plane, Common::Rect(w, h), s, palette(), mode));
		const Common::Array<byte> cov = expected(kSvfAaGoldenChainPJRows, w, h);
		byte br, bgc, bb;
		f.colorToRGB(bg, br, bgc, bb);
		int bad = 0, strictly = 0, mid = 0;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const byte c = cov[y * w + x];
				uint32 want = bg;
				if (c) {
					byte r = br, g = bgc, b = bb;
					const byte cc = mode == Graphics::TextCompose::kBlend ? c : (c >= 128 ? 255 : 0);
#define MIX(d, t) (byte)(((uint)(d) * (255 - cc) + (uint)(t) * cc + 127) / 255)
					r = MIX(br, palette()[3]);
					g = MIX(bgc, palette()[4]);
					b = MIX(bb, palette()[5]);
#undef MIX
					want = f.RGBToColor(r, g, b);
				}
				const uint32 got = pixelAt(s, x, y);
				bad += got != want;
				if (mode == Graphics::TextCompose::kBlend && (c == 85 || c == 170)) {
					++mid;
					byte r, g, b;
					f.colorToRGB(got, r, g, b);
					byte fr, fg, fb;
					f.colorToRGB(f.RGBToColor(palette()[3], palette()[4], palette()[5]), fr, fg, fb);
					// strictly between the background and the text colour in every channel
					strictly += (r > br && r < fr) && (g > bgc && g < fg) && (b < bb && b > fb);
				}
			}
		TSM_ASSERT_EQUALS(name, bad, 0);
		if (mode == Graphics::TextCompose::kBlend) {
			TSM_ASSERT(name, mid > 100);
			TSM_ASSERT_EQUALS(name, strictly, mid);
		}
		s.free();
	}

	void test_rgbSinks() {
		// formats: the usual and others (RGB555, BGR565, ARGB8888, ABGR8888, RGBA8888)
		const Graphics::PixelFormat formats[] = {
			Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0),
			Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0),
			Graphics::PixelFormat(2, 5, 6, 5, 0, 0, 5, 11, 0),
			Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24),
			Graphics::PixelFormat(4, 8, 8, 8, 8, 0, 8, 16, 24),
			Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0),
			Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0),
		};
		for (uint i = 0; i < ARRAYSIZE(formats); ++i) {
			checkBlend(formats[i], Graphics::TextCompose::kBlend, "blend");
			checkBlend(formats[i], Graphics::TextCompose::kThreshold, "threshold");
		}
	}

	// A frame of the screen (CLUT8, palette) presented through every sink: kStamp, kThreshold, kBlend.
	void test_presentThroughScreen() {
		const int w = kSvfAaGoldenChainPJW, h = kSvfAaGoldenChainPJH;
		Graphics::TextPlane plane;
		drawPlane(plane, kSvfAaGoldenChainPJFaces, 2, 1, 1, kSvfAaGoldenChainPJMissing, kSvfAaGoldenChainPJText, kSvfAaGoldenChainPJX,
		          kSvfAaGoldenChainPJY, w, h, 1);
		Graphics::Surface frame;
		frame.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
		frame.fillRect(Common::Rect(w, h), 2);
		const Graphics::PixelFormat f565(2, 5, 6, 5, 0, 11, 5, 0, 0);
		const Graphics::PixelFormat x8888(4, 8, 8, 8, 0, 16, 8, 0, 0);
		const Common::Array<byte> cov = expected(kSvfAaGoldenChainPJRows, w, h);
		const Graphics::PixelFormat formats[] = { f565, x8888 };
		for (uint k = 0; k < 2; ++k) {
			Graphics::Surface blended, hard;
			blended.create(w, h, formats[k]);
			hard.create(w, h, formats[k]);
			TS_ASSERT(Graphics::TextCompose::presentRect(plane, frame, Common::Rect(w, h), blended, palette(), Graphics::TextCompose::kBlend));
			TS_ASSERT(Graphics::TextCompose::presentRect(plane, frame, Common::Rect(w, h), hard, palette(), Graphics::TextCompose::kThreshold));
			const uint32 bg = formats[k].RGBToColor(palette()[6], palette()[7], palette()[8]), fg = formats[k].RGBToColor(palette()[3], palette()[4], palette()[5]);
			int strictly = 0, partial = 0, hardBad = 0;
			for (int y = 0; y < h; ++y)
				for (int x = 0; x < w; ++x) {
					const byte c = cov[y * w + x];
					const uint32 b = pixelAt(blended, x, y), t = pixelAt(hard, x, y);
					hardBad += t != (c >= 128 ? fg : bg);
					if (c == 85 || c == 170) {
						++partial;
						strictly += b != bg && b != fg;
					}
				}
			TS_ASSERT_EQUALS(hardBad, 0);
			TS_ASSERT(partial > 100);
			TS_ASSERT_EQUALS(strictly, partial);
			blended.free();
			hard.free();
		}
		frame.free();
	}

	// SvfFont (the Graphics::Font the engines use) over the chain: CLUT8 thresholds, RGB blends.
	void checkSvfFont(const Graphics::PixelFormat &format, bool scaled) {
		const char *text = scaled ? kSvfAaGoldenChainPJ3by2Text : kSvfAaGoldenChainPJText;
		const int w = scaled ? kSvfAaGoldenChainPJ3by2W : kSvfAaGoldenChainPJW, h = scaled ? kSvfAaGoldenChainPJ3by2H : kSvfAaGoldenChainPJH;
		const int x = scaled ? kSvfAaGoldenChainPJ3by2X : kSvfAaGoldenChainPJX, y = scaled ? kSvfAaGoldenChainPJ3by2Y : kSvfAaGoldenChainPJY;
		const char *const *rows = scaled ? kSvfAaGoldenChainPJ3by2Rows : kSvfAaGoldenChainPJRows;
		Graphics::SvfFont font(makeChain(kSvfAaGoldenChainPJFaces, 2, scaled ? 3 : 1, scaled ? 2 : 1, scaled ? 0 : kSvfAaGoldenChainPJMissing),
		                       DisposeAfterUse::YES);
		Graphics::Surface s;
		s.create(w, h, format);
		const uint32 bg = format.isCLUT8() ? 3 : format.RGBToColor(16, 32, 96), fg = format.isCLUT8() ? 7 : format.RGBToColor(240, 224, 64);
		s.fillRect(Common::Rect(w, h), bg);
		font.drawString(&s, Common::U32String(text), x, y - font.getFontAscent(), w, fg);
		const Common::Array<byte> cov = expected(rows, w, h);
		int mismatch = 0, partial = 0, partialHit = 0;
		for (int yy = 0; yy < h; ++yy)
			for (int xx = 0; xx < w; ++xx) {
				const byte c = cov[yy * w + xx];
				const uint32 v = pixelAt(s, xx, yy);
				if (format.isCLUT8())
					mismatch += v != (c >= 128 ? fg : bg);
				else if (c == 0)
					mismatch += v != bg;
				else if (c == 255)
					mismatch += v != fg;
				else {
					++partial;
					partialHit += v != bg && v != fg;
				}
			}
		TSM_ASSERT_EQUALS(Common::String::format("bpp %d clut8 %d scaled %d", format.bytesPerPixel, format.isCLUT8(), scaled).c_str(), mismatch, 0);
		if (!format.isCLUT8())
			TS_ASSERT(partialHit * 10 >= partial * 9);	// a blend, not a threshold (565 may round a faint one onto bg)
		s.free();
	}

	void test_svfFontSurfaces() {
		for (int scaled = 0; scaled < 2; ++scaled) {
			checkSvfFont(Graphics::PixelFormat::createFormatCLUT8(), scaled);
			checkSvfFont(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0), scaled);
			checkSvfFont(Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0), scaled);
			checkSvfFont(Graphics::PixelFormat::createFormatARGB32(), scaled);
		}
	}

	// Mixed bpp in one line: the 1 bpp glyphs are 255, the 2 bpp glyphs have levels, both reach the plane and the sinks.
	void test_mixedBppLine() {
		Graphics::SvfChain *chain = makeChain(kSvfAaGoldenChainMixedBppFaces, 3, 1, 1, 6);
		// the marks face has 'A', the precomposed 'b': each alone has the levels of its bpp
		uint h1[256], h2[256];
		histogram(gray(*chain, "A", 4, 18, 24, 24), h1);
		histogram(gray(*chain, "b", 4, 18, 24, 24), h2);
		TS_ASSERT(h1[255] > 0 && h1[85] == 0 && h1[170] == 0);
		TS_ASSERT(h2[255] > 0 && (h2[85] > 0 || h2[170] > 0));
		Graphics::TextPlane plane;
		drawPlane(plane, kSvfAaGoldenChainMixedBppFaces, 3, 1, 1, 6, kSvfAaGoldenChainMixedBppText, kSvfAaGoldenChainMixedBppX,
		          kSvfAaGoldenChainMixedBppY, kSvfAaGoldenChainMixedBppW, kSvfAaGoldenChainMixedBppH, 1);
		uint lv[256];
		memset(lv, 0, sizeof(lv));
		for (int y = 0; y < kSvfAaGoldenChainMixedBppH; ++y)
			for (int x = 0; x < kSvfAaGoldenChainMixedBppW; ++x)
				++lv[plane.getCoverage(x, y)];
		TS_ASSERT(lv[85] > 0 && lv[170] > 0 && lv[255] > 0);
		delete chain;
	}

	// 8 bpp coverage reaches the plane unchanged and the sinks blend by it; CLUT8 stamps at 128.
	void test_eightBppSinks() {
		const int w = kSvfAaGoldenBpp8W, h = kSvfAaGoldenBpp8H;
		Graphics::TextPlane plane;
		drawPlane(plane, kSvfAaGoldenBpp8Faces, 2, 1, 1, kSvfAaGoldenBpp8Missing, kSvfAaGoldenBpp8Text, kSvfAaGoldenBpp8X,
		          kSvfAaGoldenBpp8Y, w, h, 1);
		const Common::Array<byte> cov = expected(kSvfAaGoldenBpp8Rows, w, h);
		int bad = 0, odd = 0;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				bad += plane.getCoverage(x, y) != cov[y * w + x];
				odd += plane.getCoverage(x, y) % 85 != 0;
			}
		TS_ASSERT_EQUALS(bad, 0);
		TS_ASSERT(odd > 20);
		Graphics::Surface s;
		s.create(w, h, Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0));
		s.fillRect(Common::Rect(w, h), 0);
		TS_ASSERT(Graphics::TextCompose::compositeRect(plane, Common::Rect(w, h), s, palette(), Graphics::TextCompose::kBlend));
		int strictly = 0, partial = 0;
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				const byte c = cov[y * w + x];
				if (c >= 60 && c <= 200) {
					++partial;
					strictly += pixelAt(s, x, y) != 0 && pixelAt(s, x, y) != s.format.RGBToColor(240, 224, 64);
				}
			}
		TS_ASSERT(partial > 10);
		TS_ASSERT_EQUALS(strictly, partial);
		s.free();
	}

	// Developer aid: with a directory "aa-dump" in the working directory, writes strips of the same text in the 1 bpp
	// fallback face and the 2 bpp faces as CLUT8 would show them (stamped at 128, at 64) and blended on RGB, each pixel
	// drawn 3x3 (docs/korean/plans/AA-tests.md, "CLUT8 policy").
	void test_dumpWhenAsked() {
		Common::install_null_g_system();
		if (Common::FSNode(Common::Path("aa-dump")).isDirectory()) {
			static const char *text = "\xed\x95\x9c\xea\xb8\x80 \xea\xb2\x8c\xec\x9e\x84\xec\x9d\x84 \xec\x8b\x9c\xec\x9e\x91\xed\x95\xa9\xeb\x8b\x88\xeb\x8b\xa4. Hello 123";
			static const struct { const char *name; int faces[2]; int n; } rows[] = {
				{ "1bpp", { kJamo1, 0 }, 1 }, { "2bpp-precomposed", { kP, 0 }, 1 }, { "2bpp-jamo", { kJA, 0 }, 1 }, { "2bpp-chain", { kP, kJA }, 2 },
			};
			const int w = 264, h = 20, zoom = 3, gap = 4;
			const int kModes = 3;	// stamp 128, stamp 64, blend
			Graphics::Surface out;
			out.create(w * zoom, (int)ARRAYSIZE(rows) * kModes * (h * zoom + gap), Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0));
			out.fillRect(Common::Rect(out.w, out.h), 0x102060);
			for (uint r = 0; r < ARRAYSIZE(rows); ++r) {
				Graphics::SvfChain *chain = makeChain(rows[r].faces, rows[r].n, 1, 1, 6);
				const Common::Array<byte> cov = gray(*chain, text, 2, 15, w, h);
				for (int m = 0; m < kModes; ++m)
					for (int y = 0; y < h; ++y)
						for (int x = 0; x < w; ++x) {
							const byte c = cov[y * w + x];
							byte shade = 0;
							if (m == 0)
								shade = c >= 128 ? 255 : 0;
							else if (m == 1)
								shade = c >= 64 ? 255 : 0;
							else
								shade = c;
							const uint32 col = Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0).RGBToColor(
								Graphics::TextCompose::blend(0x10, 0xF0, shade), Graphics::TextCompose::blend(0x20, 0xE0, shade),
								Graphics::TextCompose::blend(0x60, 0x40, shade));
							const int oy = ((int)r * kModes + m) * (h * zoom + gap) + y * zoom;
							out.fillRect(Common::Rect(x * zoom, oy, (x + 1) * zoom, oy + zoom), col);
						}
				delete chain;
			}
			Common::DumpFile f;
			TS_ASSERT(f.open(Common::Path("aa-dump/clut8-policy.png")));
			TS_ASSERT(Image::writePNG(f, out));
			out.free();
		}
		Common::uninstall_null_g_system();
	}

	// ---- 15 and 16 bit: every AA case, the sinks against the renderer's arithmetic --------------------------

	// The blend of the 5/6/5 or 5/5/5 channels the C targets use: (bg * (255 - cov) + fg * cov + 127) / 255 per channel.
	static uint16 blend16(uint16 bg, uint16 fg, byte cov, bool is555) {
		const int shift[3] = { is555 ? 10 : 11, 5, 0 }, mask[3] = { 31, is555 ? 31 : 63, 31 };
		uint16 out = 0;
		for (int i = 0; i < 3; ++i) {
			const int b = bg >> shift[i] & mask[i], f = fg >> shift[i] & mask[i];
			out |= ((b * (255 - cov) + f * cov + 127) / 255) << shift[i];
		}
		return out;
	}

	struct AaCase {
		const char *name;
		const int *faces;
		int n;
		const char *text;
		int x, y, w, h, num, den, missing;
		const char *const *rows;
	};

#define AA_CASE(P) { #P, P##Faces, P##FaceCount, P##Text, P##X, P##Y, P##W, P##H, P##ScaleNum, P##ScaleDen, P##Missing, P##Rows }
	static const AaCase *aaCases(uint &count) {
		static const AaCase cases[] = {
			AA_CASE(kSvfAaGoldenPreOnly), AA_CASE(kSvfAaGoldenJamoOnly), AA_CASE(kSvfAaGoldenChainPJ), AA_CASE(kSvfAaGoldenChainPJ2x),
			AA_CASE(kSvfAaGoldenChainPJ3by2), AA_CASE(kSvfAaGoldenChainJP), AA_CASE(kSvfAaGoldenChainLPJ), AA_CASE(kSvfAaGoldenChainMixedBpp),
			AA_CASE(kSvfAaGoldenChainSizes), AA_CASE(kSvfAaGoldenChainSizes2x), AA_CASE(kSvfAaGoldenChainSizes3by2), AA_CASE(kSvfAaGoldenBpp8),
		};
		count = ARRAYSIZE(cases);
		return cases;
	}

	// Every case (single faces, jamo set, precomposed, mixed chains; 1x, 2x, 3/2; 1, 2 and 8 bpp; mapped and streamed) drawn
	// by the C 16-bit targets and by the SvfFont / TextPlane sinks is the blend of the golden coverage over the background.
	void test_every_case_at_15_and_16_bit() {
		uint count;
		const AaCase *cases = aaCases(count);
		const uint32 bg = 0x102060, fg = 0xF0E040;
		for (uint c = 0; c < count; ++c) {
			const AaCase &k = cases[c];
			const Common::Array<byte> cov = expected(k.rows, k.w, k.h);
			for (int is555 = 0; is555 < 2; ++is555)
				for (int streamed = 0; streamed < 2; ++streamed) {
					Graphics::SvfChain *chain = makeChain(k.faces, k.n, k.num, k.den, k.missing, streamed != 0);
					const uint16 bg16 = is555 ? pack555(bg) : pack565(bg), fg16 = is555 ? pack555(fg) : pack565(fg);
					Common::Array<byte> buf;
					buf.resize(k.w * k.h * 2);
					for (int i = 0; i < k.w * k.h; ++i) {
						buf[2 * i] = bg16 & 255;
						buf[2 * i + 1] = bg16 >> 8;
					}
					svf_target t;
					TS_ASSERT_EQUALS(svf_target_init(&t, is555 ? SVF_T_RGB555 : SVF_T_RGB565, buf.data(), buf.size(), k.w, k.h, k.w * 2), SVF_OK);
					t.fg565 = fg16;
					t.bg565 = bg16;
					TS_ASSERT_EQUALS(chain->draw(k.text, strlen(k.text), k.x, k.y, t), SVF_OK);
					int bad = 0;
					for (int i = 0; i < k.w * k.h; ++i) {
						const uint16 v = buf[2 * i + 1] << 8 | buf[2 * i];
						bad += v != (cov[i] ? blend16(bg16, fg16, cov[i], is555) : bg16);
					}
					TSM_ASSERT_EQUALS(Common::String::format("%s 555=%d streamed=%d", k.name, is555, streamed).c_str(), bad, 0);
					delete chain;
				}

			// the plane and the TextCompose sink on the PixelFormat of the screen
			const Graphics::PixelFormat formats[] = { Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0), Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0),
			                                          Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15) };
			for (uint f = 0; f < ARRAYSIZE(formats); ++f) {
				const int scale = (k.num == 2 && k.den == 1) ? 2 : (k.num == 3 && k.den == 2) ? 2 : 1;
				Graphics::TextPlane plane;
				drawPlane(plane, k.faces, k.n, k.num, k.den, k.missing, k.text, k.x, k.y, k.w, k.h, scale);
				Graphics::Surface s;
				s.create(k.w, k.h, formats[f]);
				// a background that changes from pixel to pixel (a gradient): the blend is over the local colour
				for (int y = 0; y < k.h; ++y)
					for (int x = 0; x < k.w; ++x)
						*(uint16 *)s.getBasePtr(x, y) = formats[f].RGBToColor(16 + x % 64, 32 + y * 3, 96 - x % 32);
				Graphics::Surface before;
				before.copyFrom(s);
				TS_ASSERT(Graphics::TextCompose::compositeRect(plane, Common::Rect(k.w, k.h), s, palette(), Graphics::TextCompose::kBlend));
				int bad = 0;
				for (int y = 0; y < k.h; ++y)
					for (int x = 0; x < k.w; ++x) {
						const byte cv = cov[y * k.w + x];
						byte r, g, b;
						formats[f].colorToRGB(pixelAt(before, x, y), r, g, b);
#define MIX(d, t) (byte)(((uint)(d) * (255 - cv) + (uint)(t) * cv + 127) / 255)
						const uint32 want = cv ? formats[f].RGBToColor(MIX(r, palette()[3]), MIX(g, palette()[4]), MIX(b, palette()[5])) : pixelAt(before, x, y);
#undef MIX
						bad += pixelAt(s, x, y) != want;
					}
				TSM_ASSERT_EQUALS(Common::String::format("%s sink format %u", k.name, f).c_str(), bad, 0);
				s.free();
				before.free();
			}
		}
	}

	// Quantising to 5 or 6 bits keeps the four levels apart for ordinary colour pairs, and never inverts the order.
	void checkLevels(const Graphics::PixelFormat &f, uint32 bg, uint32 fg, const char *name) {
		byte idx[4] = { 1, 1, 1, 1 }, cov[4] = { 0, 85, 170, 255 };
		byte pal[6] = { (byte)(bg >> 16), (byte)(bg >> 8), (byte)bg, (byte)(fg >> 16), (byte)(fg >> 8), (byte)fg };
		// palette index 1 is the text colour; the pixels start as the background
		byte full[9] = { 0, 0, 0, pal[3], pal[4], pal[5], 0, 0, 0 };
		uint16 px[4];
		for (int i = 0; i < 4; ++i)
			px[i] = (uint16)f.RGBToColor(pal[0], pal[1], pal[2]);
		Graphics::TextCompose::composeSpan((byte *)px, f, idx, cov, 4, full, Graphics::TextCompose::kBlend);
		int distinct = 1;
		for (int i = 1; i < 4; ++i)
			distinct += px[i] != px[i - 1];
		TSM_ASSERT_EQUALS(name, distinct, 4);	// all four levels give another colour
		for (int c = 0; c < 3; ++c) {
			int prev = -1;
			for (int i = 0; i < 4; ++i) {
				byte rgb[3];
				f.colorToRGB(px[i], rgb[0], rgb[1], rgb[2]);
				const int v = rgb[c];
				if (prev >= 0) {
					const bool up = pal[3 + c] >= pal[c];
					TSM_ASSERT(name, up ? v >= prev : v <= prev);	// moves toward the text colour, never back
				}
				prev = v;
			}
		}
	}

	void test_levelsSurviveQuantisation() {
		const Graphics::PixelFormat formats[] = { Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0), Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0),
		                                          Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15), Graphics::PixelFormat(2, 5, 6, 5, 0, 0, 5, 11, 0) };
		static const uint32 pairs[][2] = {
			{ 0x000080, 0xFFFFFF },	// white on navy
			{ 0xFFFFFF, 0x000000 },	// black on white
			{ 0x0000AA, 0xFFFF00 },	// yellow on blue
			{ 0x102060, 0xF0E040 },	// the golden pair
			{ 0x404040, 0x606060 },	// a low contrast pair (grey on grey): still four colours
		};
		for (uint f = 0; f < ARRAYSIZE(formats); ++f)
			for (uint p = 0; p < ARRAYSIZE(pairs); ++p)
				checkLevels(formats[f], pairs[p][0], pairs[p][1], Common::String::format("format %u pair %u", f, p).c_str());
	}

	// The 8 bpp ramp at 5 bits: the blend never goes backwards, at most 32 (5-bit) or 64 (6-bit green) values appear, and
	// no value is further than half a channel step from the exact blend.
	void test_eightBitRampAtFiveBits() {
		const Graphics::PixelFormat formats[] = { Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0), Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0) };
		byte idx[256], cov[256];
		for (int i = 0; i < 256; ++i) {
			idx[i] = 1;
			cov[i] = (byte)i;
		}
		const byte pal[6] = { 0, 0, 0, 255, 255, 255 };	// white on black: the full range
		for (uint f = 0; f < ARRAYSIZE(formats); ++f) {
			uint16 px[256];
			for (int i = 0; i < 256; ++i)
				px[i] = (uint16)formats[f].RGBToColor(0, 0, 0);
			Graphics::TextCompose::composeSpan((byte *)px, formats[f], idx, cov, 256, pal, Graphics::TextCompose::kBlend);
			int distinct[3] = { 1, 1, 1 }, prev[3] = { 0, 0, 0 };
			int maxErr = 0;
			for (int i = 1; i < 256; ++i) {
				byte rgb[3];
				formats[f].colorToRGB(px[i], rgb[0], rgb[1], rgb[2]);
				for (int c = 0; c < 3; ++c) {
					TS_ASSERT(rgb[c] >= prev[c]);	// monotonic: no banding inversion
					distinct[c] += rgb[c] != prev[c];
					prev[c] = rgb[c];
					maxErr = MAX<int>(maxErr, ABS<int>(rgb[c] - i));
				}
			}
			const int bitsG = formats[f].gBits();
			TS_ASSERT_EQUALS(distinct[0], 32);
			TS_ASSERT_EQUALS(distinct[1], 1 << bitsG);
			TS_ASSERT_EQUALS(distinct[2], 32);
			// The sink blends at 8 bits and PixelFormat::RGBToColor truncates to 5 bits (down to 7 below the exact value).
			TS_ASSERT(maxErr <= 7);
			// The C renderer's targets blend the 5-bit channels with rounding: within 4 of the exact blend.
			const bool is555 = formats[f].gBits() == 5;
			int cErr = 0;
			for (int i = 0; i < 256; ++i) {
				const uint16 v = blend16(0, is555 ? 0x7FFF : 0xFFFF, (byte)i, is555);
				const int r5 = v >> (is555 ? 10 : 11) & 31;
				cErr = MAX<int>(cErr, ABS<int>((r5 << 3 | r5 >> 2) - i));
			}
			TS_ASSERT(cErr <= 4);
		}
	}

	// Alpha: a format with an alpha bit (XRGB1555) gets the opaque bit on every pixel the sink writes, and the pixels it
	// does not touch keep theirs; the 565 and 555 formats have none.
	void test_alphaBitOf1555() {
		const Graphics::PixelFormat f1555(2, 5, 5, 5, 1, 10, 5, 0, 15);
		byte idx[3] = { 1, 1, 1 }, cov[3] = { 0, 85, 255 };
		const byte pal[6] = { 0, 0, 0, 255, 255, 255 };
		uint16 px[3];
		for (int i = 0; i < 3; ++i)
			px[i] = (uint16)f1555.ARGBToColor(255, 16, 32, 96);
		Graphics::TextCompose::composeSpan((byte *)px, f1555, idx, cov, 3, pal, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(px[0], (uint16)f1555.ARGBToColor(255, 16, 32, 96));	// no coverage: untouched, alpha kept
		TS_ASSERT(px[1] & 0x8000);
		TS_ASSERT(px[2] & 0x8000);
		TS_ASSERT_EQUALS(px[2], (uint16)f1555.ARGBToColor(255, 255, 255, 255));
		TS_ASSERT(px[1] != px[0] && px[1] != px[2]);
		// a transparent background pixel (alpha 0) becomes opaque where text is drawn
		px[1] = (uint16)f1555.ARGBToColor(0, 16, 32, 96);
		Graphics::TextCompose::composeSpan((byte *)px, f1555, idx, cov, 3, pal, Graphics::TextCompose::kBlend);
		TS_ASSERT(px[1] & 0x8000);
	}
};
