#include <cxxtest/TestSuite.h>

#include "graphics/pixelformat.h"
#include "graphics/scaler/aspect.h"

/**
 * Tests for the 200 -> 240 aspect-ratio stretch with interpolation.
 *
 * The interpolated stretch used to exist for 2-byte screens only; a 4-byte
 * screen fell back to nearest-neighbour. Both depths are checked here against
 * the same reference: each output row is either a copy of a source row or a
 * floor((w1 * cur + w2 * prev) / 8) blend per channel, with the weights of
 * the 2-byte path (7:1 and 5:3, see stretch200To240Interpolated).
 */
class ScalerAspectTestSuite : public CxxTest::TestSuite {
private:
	enum { kW = 320, kSrcH = 200, kDstH = 240 };

	static byte srcR(int x, int y) { return (byte)(x * 7 + y * 3); }
	static byte srcG(int x, int y) { return (byte)(x ^ (y * 5)); }
	static byte srcB(int x, int y) { return (byte)((y * 255) / (kSrcH - 1) + (x & 1) * 3); }

	/**
	 * Expected channel value of output row y from the source channel of rows
	 * aspect2Real(y) (cur) and aspect2Real(y) - 1 (prev).
	 */
	static uint expected(uint cur, uint prev, int y) {
		switch (y % 6) {
		case 1: return (7 * cur + 1 * prev) / 8;
		case 2: return (5 * cur + 3 * prev) / 8;
		case 3: return (3 * cur + 5 * prev) / 8;
		case 4: return (1 * cur + 7 * prev) / 8;
		default: return cur; // rows 0 and 5 of every six are copies
		}
	}

	/** Fill a kW x kDstH buffer with the source image in its top kSrcH rows. */
	static void fill(byte *buf, uint pitch, const Graphics::PixelFormat &f) {
		for (int y = 0; y < kSrcH; ++y) {
			for (int x = 0; x < kW; ++x) {
				const uint32 c = f.RGBToColor(srcR(x, y), srcG(x, y), srcB(x, y));
				if (f.bytesPerPixel == 2)
					*(uint16 *)(buf + y * pitch + x * 2) = (uint16)c;
				else
					*(uint32 *)(buf + y * pitch + x * 4) = c;
			}
		}
	}

	/**
	 * Stretch in place and compare every output pixel, channel by channel in
	 * the format's own channel width, with the reference.
	 */
	static uint check(const Graphics::PixelFormat &f) {
		const uint pitch = kW * f.bytesPerPixel;
		byte *buf = new byte[pitch * kDstH]();
		fill(buf, pitch, f);

		// Source channels as the format stores them (5/6 bits for 565).
		byte *ref = new byte[pitch * kSrcH];
		memcpy(ref, buf, pitch * kSrcH);

		const int h = stretch200To240(buf, pitch, kW, kSrcH, 0, 0, 0, true, f);
		TS_ASSERT_EQUALS(h, kDstH);

		uint mismatches = 0;
		for (int y = 0; y < kDstH; ++y) {
			const int cy = aspect2Real(y);
			const int py = cy > 0 ? cy - 1 : 0;
			for (int x = 0; x < kW; ++x) {
				uint32 got, cur, prev;
				if (f.bytesPerPixel == 2) {
					got = *(const uint16 *)(buf + y * pitch + x * 2);
					cur = *(const uint16 *)(ref + cy * pitch + x * 2);
					prev = *(const uint16 *)(ref + py * pitch + x * 2);
				} else {
					got = *(const uint32 *)(buf + y * pitch + x * 4);
					cur = *(const uint32 *)(ref + cy * pitch + x * 4);
					prev = *(const uint32 *)(ref + py * pitch + x * 4);
				}
				const uint shifts[3] = { f.rShift, f.gShift, f.bShift };
				const uint masks[3] = { f.rMax(), f.gMax(), f.bMax() };
				for (int c = 0; c < 3; ++c) {
					const uint g = (got >> shifts[c]) & masks[c];
					const uint e = expected((cur >> shifts[c]) & masks[c], (prev >> shifts[c]) & masks[c], y);
					if (g != e) {
						if (mismatches < 5)
							TS_FAIL(Common::String::format("%s: row %d x %d channel %d: got %u want %u",
							                               f.toString().c_str(), y, x, c, g, e).c_str());
						++mismatches;
					}
				}
			}
		}

		delete[] ref;
		delete[] buf;
		return mismatches;
	}

public:
	void test_interpolated_565_matches_reference() {
		TS_ASSERT_EQUALS(check(Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0)), 0u);
	}

	void test_interpolated_xrgb8888_matches_reference() {
		TS_ASSERT_EQUALS(check(Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0)), 0u);
	}

	void test_interpolated_argb8888_matches_reference() {
		TS_ASSERT_EQUALS(check(Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24)), 0u);
	}

	void test_interpolated_rgba8888_matches_reference() {
		TS_ASSERT_EQUALS(check(Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0)), 0u);
	}

	// The 4-byte stretch really interpolates: a row between two differently
	// coloured source rows gets a colour that is neither of them.
	void test_32bit_stretch_is_not_nearest() {
		const Graphics::PixelFormat f(4, 8, 8, 8, 0, 16, 8, 0, 0);
		const uint pitch = 8 * 4;
		uint32 buf[8 * kDstH];
		memset(buf, 0, sizeof(buf));
		for (int y = 0; y < kSrcH; ++y)
			for (int x = 0; x < 8; ++x)
				buf[y * 8 + x] = (y & 1) ? f.RGBToColor(0, 0, 0) : f.RGBToColor(240, 240, 240);
		stretch200To240((byte *)buf, pitch, 8, kSrcH, 0, 0, 0, true, f);
		// Output row 2: cur = source row 2 (240), prev = source row 1 (0):
		// (5 * 240 + 3 * 0) / 8 = 150. Nearest would give 240.
		byte r, g, b;
		f.colorToRGB(buf[2 * 8], r, g, b);
		TS_ASSERT_EQUALS(r, 150);
		TS_ASSERT_EQUALS(g, 150);
		TS_ASSERT_EQUALS(b, 150);
	}
};
