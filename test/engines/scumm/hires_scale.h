#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_scale.h"
#include "engines/scumm/hires_sinks.h"
#include "graphics/pixelformat.h"

/**
 * Magnifying a strip of the game's picture for an enlarged screen.
 *
 * The defect these tests describe was in the transition effects, which hand
 * the game's own 320-wide buffer to the backend with a destination rectangle
 * scaled by the text-surface multiplier. The blit was therefore scaled on one
 * side only: the source pitch was declared as `vsPitch * m` when the buffer's
 * real stride is `vsPitch`, so the backend sampled every m-th row and read
 * past the end of the buffer, and the rectangle was scaled inconsistently -
 * one direction scaled height but not width, covering 1/m of a screen m times
 * as wide.
 *
 * What is checked here is the property that was violated, not the arithmetic:
 * every output pixel comes from the source pixel above it under a plain m-fold
 * magnification, and the source is read at its own stride and never beyond its
 * last row. A test that only counted written bytes would have passed against
 * the broken code.
 */
class HiResScaleTestSuite : public CxxTest::TestSuite {

	/// A sink that records what it was handed, so a test can look at pixels.
	class CaptureSink : public Scumm::HiResSink {
	public:
		Common::Array<byte> out;

		void writeBackground(const byte *bg, int count) override {
			for (int i = 0; i < count; ++i)
				out.push_back(bg[i]);
		}
		void writeOpaque(const byte *fg, int count) override {
			writeBackground(fg, count);
		}
		void writeBlended(const byte *fg, const byte *, const byte *,
						  int count) override {
			writeBackground(fg, count);
		}
	};

	/**
	 * A source buffer whose every byte identifies its own row and column, with
	 * @p pad bytes of a distinct poison value after each row.
	 *
	 * The padding is the point: it is what a reader using the wrong stride
	 * lands in, so a stride error shows up as a poison byte in the output
	 * rather than as a subtly wrong picture.
	 */
	static void buildSource(Common::Array<byte> &buf, int w, int h, int pad,
							byte poison = 0xEE) {
		buf.clear();
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x)
				buf.push_back((byte)(1 + y * w + x));
			for (int p = 0; p < pad; ++p)
				buf.push_back(poison);
		}
	}

public:
	/**
	 * Every output pixel is the source pixel above it: m columns across and
	 * m rows down, for each of them.
	 */
	void test_each_source_pixel_covers_m_by_m_output_pixels() {
		const int w = 4, h = 3, m = 3;
		Common::Array<byte> src;
		buildSource(src, w, h, 0);

		CaptureSink sink;
		Scumm::expandStrip(sink, src.begin(), w, w, h, m);

		TS_ASSERT_EQUALS(sink.out.size(), (uint)(w * m * h * m));
		for (int y = 0; y < h * m; ++y) {
			for (int x = 0; x < w * m; ++x) {
				const byte expect = (byte)(1 + (y / m) * w + (x / m));
				TS_ASSERT_EQUALS(sink.out[y * w * m + x], expect);
			}
		}
	}

	/**
	 * The source is read at the stride it was given, not at its width.
	 *
	 * A VirtScreen's pitch is not always its width, and the broken code
	 * multiplied the pitch by the scale on top of that. Padding the rows with
	 * a poison value turns either mistake into a visible wrong pixel.
	 */
	void test_the_source_is_read_at_its_own_pitch() {
		const int w = 4, h = 3, m = 2, pad = 5;
		Common::Array<byte> src;
		buildSource(src, w, h, pad);

		CaptureSink sink;
		Scumm::expandStrip(sink, src.begin(), w + pad, w, h, m);

		for (uint i = 0; i < sink.out.size(); ++i)
			TSM_ASSERT_DIFFERS("padding leaked into the output - wrong stride",
							   sink.out[i], (byte)0xEE);

		// And the rows are the right ones, not merely non-poison.
		for (int y = 0; y < h * m; ++y)
			for (int x = 0; x < w * m; ++x)
				TS_ASSERT_EQUALS(sink.out[y * w * m + x],
								 (byte)(1 + (y / m) * w + (x / m)));
	}

	/**
	 * Nothing is read past the last source row.
	 *
	 * This is the overrun half of the original defect: declaring the pitch as
	 * `vsPitch * m` makes the last rows of a tall strip come from beyond the
	 * buffer. Poison placed immediately after the strip must not appear.
	 */
	void test_nothing_is_read_past_the_last_row() {
		const int w = 8, h = 8, m = 3;
		Common::Array<byte> src;
		buildSource(src, w, h, 0);
		for (int i = 0; i < w * 16; ++i)
			src.push_back(0xEE);   // whatever follows the strip in memory

		CaptureSink sink;
		Scumm::expandStrip(sink, src.begin(), w, w, h, m);

		TS_ASSERT_EQUALS(sink.out.size(), (uint)(w * m * h * m));
		for (uint i = 0; i < sink.out.size(); ++i)
			TS_ASSERT_DIFFERS(sink.out[i], (byte)0xEE);
	}

	/**
	 * At m == 1 this is a plain copy.
	 *
	 * Every game we do not touch runs at m == 1, so the scaled path must be
	 * indistinguishable from the original blit there.
	 */
	void test_scale_one_is_a_plain_copy() {
		const int w = 5, h = 4;
		Common::Array<byte> src;
		buildSource(src, w, h, 3);

		CaptureSink sink;
		Scumm::expandStrip(sink, src.begin(), w + 3, w, h, 1);

		TS_ASSERT_EQUALS(sink.out.size(), (uint)(w * h));
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				TS_ASSERT_EQUALS(sink.out[y * w + x], (byte)(1 + y * w + x));
	}

	/// An empty or nonsensical strip writes nothing rather than guessing.
	void test_a_degenerate_strip_writes_nothing() {
		Common::Array<byte> src;
		buildSource(src, 4, 4, 0);

		CaptureSink a, b, c;
		Scumm::expandStrip(a, src.begin(), 4, 0, 4, 2);
		Scumm::expandStrip(b, src.begin(), 4, 4, 0, 2);
		Scumm::expandStrip(c, src.begin(), 4, 4, 4, 0);

		TS_ASSERT_EQUALS(a.out.size(), 0u);
		TS_ASSERT_EQUALS(b.out.size(), 0u);
		TS_ASSERT_EQUALS(c.out.size(), 0u);
	}

	/**
	 * Through the true-colour sink, the strip comes out as colours - which is
	 * the conversion the raw blit could not do at all.
	 *
	 * The composite buffer is sized by the screen's bytes per pixel, so the
	 * byte count matters as much as the values: this is the same overrun
	 * shape as the 32bpp-sink-into-a-16bpp-buffer bug.
	 */
	void test_expanding_into_a_true_colour_sink_resolves_the_palette() {
		const Graphics::PixelFormat fmt(4, 8, 8, 8, 8, 16, 8, 0, 24);
		uint32 pal[256];
		memset(pal, 0, sizeof(pal));
		pal[1] = fmt.RGBToColor(10, 20, 30);
		pal[2] = fmt.RGBToColor(200, 100, 50);

		const byte src[] = { 1, 2 };
		const int m = 2;

		uint32 dst[2 * 2 * 1 * 2];   // w*m x h*m, w=2 h=1
		memset(dst, 0xAB, sizeof(dst));

		Scumm::HiResTrueColorSink sink(dst, pal, fmt);
		Scumm::expandStrip(sink, src, 2, 2, 1, m);

		// Row 0: 1 1 2 2 - and row 1 the same, the row being repeated.
		TS_ASSERT_EQUALS(dst[0], pal[1]);
		TS_ASSERT_EQUALS(dst[1], pal[1]);
		TS_ASSERT_EQUALS(dst[2], pal[2]);
		TS_ASSERT_EQUALS(dst[3], pal[2]);
		TS_ASSERT_EQUALS(dst[4], pal[1]);
		TS_ASSERT_EQUALS(dst[7], pal[2]);
	}

	/**
	 * A tall strip, which is where the original overrun actually bit: the
	 * scroll effect's left/right steps are the full screen height.
	 */
	void test_a_full_height_strip_stays_inside_the_buffer() {
		const int w = 8, h = 200, m = 3;
		Common::Array<byte> src;
		src.resize((uint)(w * h));
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x)
				src[y * w + x] = (byte)(y & 0x7F);
		for (int i = 0; i < w * 32; ++i)
			src.push_back(0xEE);

		CaptureSink sink;
		Scumm::expandStrip(sink, src.begin(), w, w, h, m);

		TS_ASSERT_EQUALS(sink.out.size(), (uint)(w * m * h * m));
		for (int y = 0; y < h * m; ++y)
			TS_ASSERT_EQUALS(sink.out[y * w * m], (byte)((y / m) & 0x7F));
	}
};
