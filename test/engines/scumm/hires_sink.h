#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_sinks.h"
#include "graphics/pixelformat.h"

/**
 * The composite sinks: one decision rule, three destinations.
 *
 * The point of these tests is not that the arithmetic is right - it is that
 * each sink reproduces exactly what the branch it replaces used to write, and
 * that none of them caches a resolved colour. A sink that held on to colours
 * would keep working until a palette change, which no rendering test covers.
 */
class HiResSinkTestSuite : public CxxTest::TestSuite {
public:
	void test_index_sink_copies_indices_through() {
		byte dst[8];
		memset(dst, 0xAA, sizeof(dst));
		Scumm::HiResIndexSink sink(dst);

		const byte bg[] = { 1, 2, 3 };
		const byte fg[] = { 7, 8 };
		sink.writeBackground(bg, 3);
		sink.writeOpaque(fg, 2);

		TS_ASSERT_EQUALS(dst[0], 1);
		TS_ASSERT_EQUALS(dst[1], 2);
		TS_ASSERT_EQUALS(dst[2], 3);
		TS_ASSERT_EQUALS(dst[3], 7);
		TS_ASSERT_EQUALS(dst[4], 8);
		// Nothing written past the runs.
		TS_ASSERT_EQUALS(dst[5], 0xAA);
	}

	void test_index_sink_rounds_partial_coverage() {
		// A palette index cannot hold a mixture, so the sink picks a side.
		// The threshold is 128: below it the background wins.
		byte dst[4];
		Scumm::HiResIndexSink sink(dst);

		const byte fg[] = { 9, 9, 9, 9 };
		const byte bg[] = { 1, 1, 1, 1 };
		const byte cov[] = { 0, 127, 128, 255 };
		sink.writeBlended(fg, bg, cov, 4);

		TS_ASSERT_EQUALS(dst[0], 1);
		TS_ASSERT_EQUALS(dst[1], 1);
		TS_ASSERT_EQUALS(dst[2], 9);
		TS_ASSERT_EQUALS(dst[3], 9);
	}

	void test_palette16_sink_looks_every_index_up() {
		const Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		uint32 pal[256];
		for (int i = 0; i < 256; ++i)
			pal[i] = (uint32)(0x1000 + i);

		byte dst[8];
		Scumm::HiResPalette16Sink sink(dst, pal, fmt);

		const byte bg[] = { 3, 4 };
		const byte fg[] = { 5 };
		sink.writeBackground(bg, 2);
		sink.writeOpaque(fg, 1);

		TS_ASSERT_EQUALS(READ_UINT16(dst + 0), 0x1003);
		TS_ASSERT_EQUALS(READ_UINT16(dst + 2), 0x1004);
		TS_ASSERT_EQUALS(READ_UINT16(dst + 4), 0x1005);
	}

	/**
	 * The 16-bit sink writes two bytes per pixel and no more.
	 *
	 * This is the invariant behind a heap overrun: the composite buffer is
	 * allocated at the screen's bytes per pixel, so a sink that writes four
	 * bytes into it runs off the end by the buffer's own size. Guard the
	 * width by giving the sink exactly the room it should need and checking
	 * the byte after it is untouched.
	 */
	void test_palette16_sink_writes_two_bytes_per_pixel() {
		const Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		uint32 pal[256];
		memset(pal, 0xFF, sizeof(pal));

		byte dst[9];
		memset(dst, 0xAA, sizeof(dst));

		Scumm::HiResPalette16Sink sink(dst, pal, fmt);
		const byte fg[] = { 1, 2, 3, 4 };
		const byte bg[] = { 5, 6, 7, 8 };
		const byte cov[] = { 64, 64, 64, 64 };

		sink.writeOpaque(fg, 2);
		sink.writeBlended(fg, bg, cov, 2);

		// Four pixels, eight bytes. The ninth is not ours.
		TS_ASSERT_EQUALS(dst[8], 0xAA);
	}

	/**
	 * Sixteen bits is a colour, not an index, so partial coverage blends
	 * rather than picking a side.
	 *
	 * The assertion is monotonicity between the two endpoints, not exact
	 * values: 5:6:5 quantisation makes the midpoints format-dependent, but a
	 * threshold implementation produces exactly two distinct outputs, which
	 * this fails.
	 */
	void test_palette16_sink_blends_between_the_two_indices() {
		const Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		uint32 pal[256];
		memset(pal, 0, sizeof(pal));
		pal[1] = fmt.RGBToColor(0, 0, 0);         // background: black
		pal[2] = fmt.RGBToColor(248, 252, 248);   // text: white at 5:6:5

		const byte cov[] = { 0, 64, 128, 192, 255 };
		byte dst[10];
		Scumm::HiResPalette16Sink sink(dst, pal, fmt);

		const byte fg[] = { 2, 2, 2, 2, 2 };
		const byte bg[] = { 1, 1, 1, 1, 1 };
		sink.writeBlended(fg, bg, cov, 5);

		uint8 r[5], g, b;
		for (int i = 0; i < 5; ++i)
			fmt.colorToRGB(READ_UINT16(dst + i * 2), r[i], g, b);

		// The endpoints are whatever the format round-trips the palette
		// entries to, not the values handed to RGBToColor: 5:6:5 expands a
		// 5-bit channel by bit replication, so 248 comes back as 255.
		uint8 endR, endG, endB;
		fmt.colorToRGB(pal[1], endR, endG, endB);
		TS_ASSERT_EQUALS(r[0], endR);   // no coverage: pure background
		fmt.colorToRGB(pal[2], endR, endG, endB);
		TS_ASSERT_EQUALS(r[4], endR);   // full coverage: pure text

		// Strictly increasing: a threshold gives two values and fails here.
		for (int i = 1; i < 5; ++i)
			TS_ASSERT(r[i] > r[i - 1]);
	}

	void test_truecolor_sink_blends_between_the_two_indices() {
		const Graphics::PixelFormat fmt(4, 8, 8, 8, 8, 16, 8, 0, 24);
		uint32 pal[256];
		memset(pal, 0, sizeof(pal));
		pal[1] = fmt.RGBToColor(0, 0, 0);        // background: black
		pal[2] = fmt.RGBToColor(200, 100, 50);   // text

		uint32 dst[3];
		Scumm::HiResTrueColorSink sink(dst, pal, fmt);

		const byte fg[] = { 2, 2, 2 };
		const byte bg[] = { 1, 1, 1 };
		const byte cov[] = { 0, 128, 255 };
		sink.writeBlended(fg, bg, cov, 3);

		uint8 r, g, b;
		fmt.colorToRGB(dst[0], r, g, b);
		TS_ASSERT_EQUALS(r, 0);       // no coverage: pure background

		fmt.colorToRGB(dst[1], r, g, b);
		TS_ASSERT(r > 90 && r < 110); // half coverage: about half

		fmt.colorToRGB(dst[2], r, g, b);
		TS_ASSERT_EQUALS(r, 200);     // full coverage: pure text
	}

	/**
	 * The invariant that motivates the whole interface.
	 *
	 * The overlay stores indices so that a palette change recolours text that
	 * was drawn long before. A sink must therefore resolve through the
	 * palette on every call. Writing the same run twice across an edited
	 * palette has to produce two different colours.
	 */
	void test_truecolor_sink_follows_a_palette_change() {
		const Graphics::PixelFormat fmt(4, 8, 8, 8, 8, 16, 8, 0, 24);
		uint32 pal[256];
		memset(pal, 0, sizeof(pal));
		pal[5] = fmt.RGBToColor(10, 20, 30);

		uint32 first = 0, second = 0;
		const byte fg[] = { 5 };

		{
			Scumm::HiResTrueColorSink sink(&first, pal, fmt);
			sink.writeOpaque(fg, 1);
		}

		// The game cycles or fades the palette; nothing is redrawn.
		pal[5] = fmt.RGBToColor(240, 230, 220);

		{
			Scumm::HiResTrueColorSink sink(&second, pal, fmt);
			sink.writeOpaque(fg, 1);
		}

		TS_ASSERT_DIFFERS(first, second);

		uint8 r, g, b;
		fmt.colorToRGB(second, r, g, b);
		TS_ASSERT_EQUALS(r, 240);
	}

	/// The same, for the 16-bit table: it must not be snapshotted either.
	void test_palette16_sink_follows_a_palette_change() {
		const Graphics::PixelFormat fmt(2, 5, 6, 5, 0, 11, 5, 0, 0);
		uint32 pal[256];
		memset(pal, 0, sizeof(pal));
		pal[7] = 0x1234;

		byte first[2], second[2];
		const byte fg[] = { 7 };

		{
			Scumm::HiResPalette16Sink sink(first, pal, fmt);
			sink.writeOpaque(fg, 1);
		}

		pal[7] = 0x4321;

		{
			Scumm::HiResPalette16Sink sink(second, pal, fmt);
			sink.writeOpaque(fg, 1);
		}

		TS_ASSERT_EQUALS(READ_UINT16(first), 0x1234);
		TS_ASSERT_EQUALS(READ_UINT16(second), 0x4321);
	}

	/// A zero-length run must not move the write pointer.
	void test_empty_runs_write_nothing() {
		byte dst[2] = { 0x11, 0x22 };
		Scumm::HiResIndexSink sink(dst);

		const byte none[] = { 0 };
		sink.writeBackground(none, 0);
		sink.writeOpaque(none, 0);
		sink.writeBlended(none, none, none, 0);

		TS_ASSERT_EQUALS(dst[0], 0x11);
		TS_ASSERT_EQUALS(dst[1], 0x22);
	}
};
