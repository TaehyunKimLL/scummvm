#include <cxxtest/TestSuite.h>

#include "graphics/hires_text/banded_plane.h"
#include "graphics/surface.h"

// A text plane held in 16-row bands: only bands with something non-zero in
// them exist, and a packable plane keeps 4 bits a pixel while that is exact.
class HiResBandedPlaneTestSuite : public CxxTest::TestSuite {
public:
	void test_a_new_plane_holds_no_band_and_reads_zero() {
		Graphics::BandedPlane p;
		p.create(40, 50, true);
		TS_ASSERT(p.exists());
		TS_ASSERT_EQUALS(p.width(), 40);
		TS_ASSERT_EQUALS(p.height(), 50);
		TS_ASSERT_EQUALS(p.bandsHeld(), 0u);
		byte scratch[40];
		for (int y = 0; y < 50; ++y) {
			TS_ASSERT(p.row(y, scratch) == nullptr);
			TS_ASSERT_EQUALS(p.get(39, y), 0);
		}
		p.set(3, 20, 0);	// a zero makes no band
		TS_ASSERT_EQUALS(p.bandsHeld(), 0u);
	}

	void test_a_band_comes_and_goes_with_its_last_value() {
		Graphics::BandedPlane p;
		p.create(40, 50, true);
		p.set(3, 20, 85);
		p.set(4, 21, 255);
		TS_ASSERT_EQUALS(p.bandsHeld(), 1u);
		TS_ASSERT(p.rowHeld(16) && p.rowHeld(31) && !p.rowHeld(15) && !p.rowHeld(32));
		TS_ASSERT_EQUALS(p.bytesHeld(), 16u * 20);	// packed: two pixels a byte
		p.set(3, 20, 0);
		TS_ASSERT_EQUALS(p.bandsHeld(), 1u);
		p.set(4, 21, 0);
		TS_ASSERT_EQUALS(p.bandsHeld(), 0u);
		// The last band is partial (rows 48, 49).
		p.set(39, 49, 170);
		TS_ASSERT_EQUALS(p.get(39, 49), 170);
		p.fill(Common::Rect(0, 48, 40, 50), 0);	// all of it: given back
		TS_ASSERT_EQUALS(p.bandsHeld(), 0u);
	}

	void test_packed_values_read_back_exactly_and_the_first_odd_one_unpacks() {
		Graphics::BandedPlane p;
		p.create(9, 20, true);
		for (int v = 0; v < 256; v += 17)
			p.set(v / 17 % 9, v / 17, (byte)v);
		TS_ASSERT(p.packed());
		for (int v = 0; v < 256; v += 17)
			TS_ASSERT_EQUALS(p.get(v / 17 % 9, v / 17), v);
		byte scratch[9];
		const byte *r = p.row(5, scratch);
		TS_ASSERT(r == scratch);
		TS_ASSERT_EQUALS(r[5], 85);
		p.set(8, 19, 0x60);	// not a multiple of 17
		TS_ASSERT(!p.packed());
		TS_ASSERT_EQUALS(p.get(8, 19), 0x60);
		for (int v = 17; v < 256; v += 17)
			TS_ASSERT_EQUALS(p.get(v / 17 % 9, v / 17), v);
		TS_ASSERT(p.row(5, scratch) != scratch);
		TS_ASSERT_EQUALS(p.row(5, scratch)[5], 85);
	}

	void test_an_unpackable_plane_keeps_a_byte_a_pixel() {
		Graphics::BandedPlane p;
		p.create(10, 10, false);
		p.set(1, 1, 34);
		TS_ASSERT(!p.packed());
		TS_ASSERT_EQUALS(p.bytesHeld(), 16u * 10);
		TS_ASSERT_EQUALS(p.get(1, 1), 34);
	}

	void test_fill_keeps_the_count_and_gives_back_emptied_bands() {
		Graphics::BandedPlane p;
		p.create(30, 40, true);
		p.fill(Common::Rect(2, 2, 12, 30), 0xFF);	// two bands
		TS_ASSERT_EQUALS(p.bandsHeld(), 2u);
		TS_ASSERT_EQUALS(p.get(11, 29), 0xFF);
		TS_ASSERT_EQUALS(p.get(12, 29), 0);
		p.fill(Common::Rect(0, 0, 30, 10), 0);	// part of the first band: some left
		TS_ASSERT_EQUALS(p.bandsHeld(), 2u);
		p.fill(Common::Rect(2, 10, 12, 16), 0);	// its last values
		TS_ASSERT_EQUALS(p.bandsHeld(), 1u);
		p.fill(Common::Rect(-5, -5, 100, 100), 0);	// clipped
		TS_ASSERT_EQUALS(p.bandsHeld(), 0u);
		p.fill(Common::Rect(1, 1, 3, 3), 0x40);	// an odd value unpacks it
		TS_ASSERT(!p.packed());
		TS_ASSERT_EQUALS(p.get(2, 2), 0x40);
	}

	void test_copy_bytes_and_surface() {
		Graphics::BandedPlane p, q;
		p.create(8, 20, true);
		p.set(7, 17, 170);
		q.copyFrom(p);
		TS_ASSERT_EQUALS(q.get(7, 17), 170);
		TS_ASSERT_EQUALS(q.bandsHeld(), 1u);
		q.set(7, 17, 0);
		TS_ASSERT_EQUALS(p.get(7, 17), 170);	// a copy, not shared

		// As one array of 8 x 20 bytes.
		byte b[3];
		p.readBytes(17 * 8 + 6, b, 3);
		TS_ASSERT_EQUALS(b[0], 0);
		TS_ASSERT_EQUALS(b[1], 170);
		TS_ASSERT_EQUALS(b[2], 0);
		const byte w[2] = { 0x21, 0 };
		p.writeBytes(2 * 8 + 1, w, 2);
		TS_ASSERT_EQUALS(p.get(1, 2), 0x21);

		Graphics::Surface s;
		p.toSurface(s);
		TS_ASSERT_EQUALS(s.w, 8);
		TS_ASSERT_EQUALS(s.h, 20);
		for (int y = 0; y < 20; ++y)
			for (int x = 0; x < 8; ++x)
				TS_ASSERT_EQUALS(*(const byte *)s.getBasePtr(x, y), p.get(x, y));
		s.free();
	}

	// A span of a row: only its pixels are unpacked, from either nibble.
	void test_a_span_of_a_row_reads_as_the_pixels() {
		Graphics::BandedPlane packed, wide;
		packed.create(13, 20, true);
		wide.create(13, 20, false);
		for (int x = 0; x < 13; ++x) {
			packed.set(x, 2, (byte)((x % 16) * 17));
			wide.set(x, 2, (byte)(x * 7));
		}
		TS_ASSERT(packed.packed());
		for (int x0 = 0; x0 < 13; ++x0) {
			for (int n = 1; x0 + n <= 13; ++n) {
				byte scratch[13];
				memset(scratch, 0xEE, sizeof(scratch));
				const byte *r = packed.row(2, scratch, x0, n);
				for (int i = 0; i < n; ++i)
					TS_ASSERT_EQUALS(r[i], packed.get(x0 + i, 2));
				if (n < 13)
					TS_ASSERT_EQUALS(scratch[n], 0xEE);	// nothing past the span
				r = wide.row(2, scratch, x0, n);
				for (int i = 0; i < n; ++i)
					TS_ASSERT_EQUALS(r[i], wide.get(x0 + i, 2));
			}
		}
		byte scratch[13];
		TS_ASSERT(packed.row(18, scratch, 3, 4) == nullptr);	// another band: none
	}

	// Bytes across rows and bands, read and written as one array.
	void test_bytes_across_rows_and_bands() {
		Graphics::BandedPlane p;
		p.create(7, 40, true);
		Common::Array<byte> in(7 * 20);
		for (uint i = 0; i < in.size(); ++i)
			in[i] = (byte)(((i * 5) % 16) * 17);
		p.writeBytes(7 * 10 + 3, in.begin(), in.size());	// rows 10..30, bands 0..1
		TS_ASSERT(p.packed());
		Common::Array<byte> out(in.size() + 4);
		p.readBytes(7 * 10 + 1, out.begin(), out.size());
		TS_ASSERT_EQUALS(out[0], 0);
		TS_ASSERT_EQUALS(out[1], 0);
		for (uint i = 0; i < in.size(); ++i)
			TS_ASSERT_EQUALS(out[i + 2], in[i]);
		TS_ASSERT_EQUALS(out[in.size() + 2], 0);
		for (uint i = 0; i < in.size(); ++i)
			TS_ASSERT_EQUALS(p.get((7 * 10 + 3 + i) % 7, (7 * 10 + 3 + i) / 7), in[i]);
	}
};
