#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/text_compose.h"

// Blend16 is blendPixel by table: every 2-byte pixel, every partial coverage, colours at the channel edges and between.
class HiResTextBlend16TestSuite : public CxxTest::TestSuite {
	static const Graphics::PixelFormat *formats(uint &n) {
		static const Graphics::PixelFormat f[] = {
			Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0),  // RGB565
			Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0),  // RGB555
			Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15), // ARGB1555
			Graphics::PixelFormat(2, 5, 5, 5, 1, 11, 6, 1, 0),  // RGBA5551
			Graphics::PixelFormat(2, 5, 6, 5, 0, 0, 5, 11, 0)   // BGR565
		};
		n = ARRAYSIZE(f);
		return f;
	}

public:
	void test_everyPixelEveryCoverage() {
		static const byte fg[][3] = { { 0, 0, 0 }, { 255, 255, 255 }, { 255, 255, 85 }, { 0, 170, 170 }, { 83, 7, 201 }, { 170, 0, 0 } };
		uint nf;
		const Graphics::PixelFormat *f = formats(nf);
		uint32 bad = 0;
		for (uint i = 0; i < nf; ++i) {
			Graphics::TextCompose::Blend16 table(f[i]);
			for (uint c = 0; c < ARRAYSIZE(fg); ++c)
				for (uint cov = 1; cov < 255; ++cov)
					for (uint32 d = 0; d < 65536; ++d)
						if (table.blend((uint16)d, fg[c][0], fg[c][1], fg[c][2], (byte)cov) !=
						    (uint16)Graphics::TextCompose::blendPixel(f[i], d, fg[c][0], fg[c][1], fg[c][2], (byte)cov))
							++bad;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	void test_composeSpanEqualsBlendPixel() {
		// Random rows through composeSpan, with a table across rows and without one; coverage 0, 255 and partial.
		uint nf;
		const Graphics::PixelFormat *f = formats(nf);
		byte pal[256 * 3];
		uint32 s = 7;
		for (int i = 0; i < 768; ++i) {
			s = s * 1103515245 + 12345;
			pal[i] = (byte)(s >> 16);
		}
		for (uint i = 0; i < nf; ++i) {
			Graphics::TextCompose::Blend16 table(f[i]);
			for (int row = 0; row < 16; ++row) {
				uint16 dst[640], viaTable[640], ref[640];
				byte idx[640], cov[640];
				for (int x = 0; x < 640; ++x) {
					s = s * 1103515245 + 12345;
					dst[x] = viaTable[x] = ref[x] = (uint16)(s >> 8);
					idx[x] = (byte)(s >> 3);
					cov[x] = (x % 7 == 0) ? 0 : (x % 5 == 0) ? 255 : (byte)(s >> 13);
				}
				Graphics::TextCompose::composeSpan((byte *)dst, f[i], idx, cov, 640, pal, Graphics::TextCompose::kBlend);
				Graphics::TextCompose::composeSpan((byte *)viaTable, f[i], idx, cov, 640, pal, Graphics::TextCompose::kBlend, &table);
				for (int x = 0; x < 640; ++x) {
					const byte *c = pal + idx[x] * 3;
					const uint16 want = cov[x] ? (uint16)Graphics::TextCompose::blendPixel(f[i], ref[x], c[0], c[1], c[2], cov[x]) : ref[x];
					TS_ASSERT_EQUALS(dst[x], want);
					TS_ASSERT_EQUALS(viaTable[x], want);
				}
			}
		}
	}

	void test_convertSpanEqualsBlendPixel() {
		// The same through convertSpan (a CLUT8 frame row converted with text over it), with and without a table.
		uint nf;
		const Graphics::PixelFormat *f = formats(nf);
		byte pal[256 * 3];
		uint32 s = 99;
		for (int i = 0; i < 768; ++i) {
			s = s * 1103515245 + 12345;
			pal[i] = (byte)(s >> 16);
		}
		for (uint i = 0; i < nf; ++i) {
			Graphics::TextCompose::Blend16 table(f[i]);
			for (int row = 0; row < 16; ++row) {
				uint16 viaTable[320], plain[320];
				byte frame[320], idx[320], cov[320];
				for (int x = 0; x < 320; ++x) {
					s = s * 1103515245 + 12345;
					frame[x] = (byte)(s >> 5);
					idx[x] = (byte)(s >> 3);
					cov[x] = (x % 6 == 0) ? 0 : (x % 4 == 0) ? 255 : (byte)(s >> 13);
				}
				Graphics::TextCompose::convertSpan((byte *)viaTable, f[i], frame, idx, cov, 320, pal, Graphics::TextCompose::kBlend, &table);
				Graphics::TextCompose::convertSpan((byte *)plain, f[i], frame, idx, cov, 320, pal, Graphics::TextCompose::kBlend);
				for (int x = 0; x < 320; ++x) {
					const byte *b = pal + frame[x] * 3, *c = pal + idx[x] * 3;
					const uint32 under = f[i].RGBToColor(b[0], b[1], b[2]);
					const uint16 want = cov[x] ? (uint16)Graphics::TextCompose::blendPixel(f[i], under, c[0], c[1], c[2], cov[x]) : (uint16)under;
					TS_ASSERT_EQUALS(viaTable[x], want);
					TS_ASSERT_EQUALS(plain[x], want);
				}
			}
		}
	}
};
