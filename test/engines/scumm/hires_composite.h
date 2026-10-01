#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_composite.h"

/**
 * The compositor's choice of what each output pixel is (C19).
 *
 * The sink here writes a letter per pixel naming the call it came from, so a
 * test reads the decision directly: '.' picture, 'T' opaque text, 'b' text
 * blended over the picture, 'U' opaque decoration, 'u' decoration blended
 * over the picture, 'L' text blended over the decoration.
 */
class HiResCompositeTestSuite : public CxxTest::TestSuite {
	class KindSink : public Scumm::HiResSink {
	public:
		Common::String out;
		void writeBackground(const byte *, int count) override { add('.', count); }
		void writeOpaque(const byte *fg, int count) override {
			for (int i = 0; i < count; ++i)
				out += (fg[i] == kUnder) ? 'U' : 'T';
		}
		void writeBlended(const byte *fg, const byte *, const byte *, int count) override {
			for (int i = 0; i < count; ++i)
				out += (fg[i] == kUnder) ? 'u' : 'b';
		}
		void writeLayered(const byte *, const byte *, const byte *, const byte *,
						  const byte *, int count) override { add('L', count); }
	private:
		void add(char c, int count) {
			for (int i = 0; i < count; ++i)
				out += c;
		}
	};

	/// Writes every value the compositor hands over, so two runs compare.
	class ValueSink : public Scumm::HiResSink {
	public:
		Common::Array<byte> out;
		void writeBackground(const byte *bg, int count) override { put('.', bg, count); }
		void writeOpaque(const byte *fg, int count) override { put('T', fg, count); }
		void writeBlended(const byte *fg, const byte *bg, const byte *a, int count) override {
			put('b', fg, count); put(0, bg, count); put(0, a, count);
		}
		void writeLayered(const byte *fg, const byte *a, const byte *u, const byte *ua,
						  const byte *bg, int count) override {
			put('L', fg, count); put(0, a, count); put(0, u, count); put(0, ua, count); put(0, bg, count);
		}
	private:
		void put(byte tag, const byte *p, int count) {
			if (tag)
				out.push_back(tag);
			for (int i = 0; i < count; ++i)
				out.push_back(p[i]);
		}
	};

	static const byte kText = 7;
	static const byte kUnder = 1;
	static const byte kNone = Scumm::kHiResTextTransparent;

public:
	void test_every_combination_of_text_and_decoration() {
		const byte src[] = { 3, 3, 3, 3, 3, 3, 3, 3 };
		const byte text[] = { kNone, kText, kText, kNone, kNone, kText, kText, kText };
		const byte cov[] = { 0, 255, 128, 0, 0, 128, 255, 0 };
		const byte uIdx[] = { 0, 0, 0, kUnder, kUnder, kUnder, kUnder, kUnder };
		const byte uCov[] = { 0, 0, 0, 255, 100, 255, 255, 255 };

		KindSink sink;
		Scumm::compositeText(sink, src, 0, text, 0, cov, 0, 8, 1, 1, uIdx, uCov, 0);
		// Picture, text, text over picture, the decoration alone (solid,
		// then partial), text over the decoration, solid text hiding it,
		// and text drawn with no coverage (a legacy glyph) hiding it too.
		TS_ASSERT_EQUALS(sink.out, Common::String(".TbUuLTT"));
	}

	/// Without under planes, nothing changes from the two-plane compositor.
	void test_no_under_planes_is_the_old_compositor() {
		const byte src[] = { 3, 3, 3, 3 };
		const byte text[] = { kNone, kText, kText, 0 };
		const byte cov[] = { 0, 255, 128, 0 };
		KindSink sink;
		Scumm::compositeText(sink, src, 0, text, 0, cov, 0, 4, 1, 1);
		TS_ASSERT_EQUALS(sink.out, Common::String(".Tb."));
	}

	/// drawStripToScreen() composes a strip in bands of game rows; each band,
	/// started at its own row of every plane, gives the same output as the
	/// matching rows of the whole strip.
	void test_bands_compose_like_the_whole_strip() {
		const int w = 5, h = 7, m = 2, srcPad = 3, pad = 2;
		const int outW = w * m, planePitch = outW + pad, srcPitch = w + srcPad;
		byte src[srcPitch * h], text[planePitch * h * m], cov[planePitch * h * m];
		byte uIdx[planePitch * h * m], uCov[planePitch * h * m];
		uint32 seed = 12345;
		for (uint i = 0; i < sizeof(src); ++i)
			src[i] = (byte)((seed = seed * 1103515245 + 12345) >> 16);
		for (uint i = 0; i < sizeof(text); ++i) {
			const uint32 r = (seed = seed * 1103515245 + 12345) >> 8;
			text[i] = (r & 3) == 0 ? kNone : (byte)(r >> 4);
			cov[i] = (byte)(r >> 12);
			uIdx[i] = (byte)(r >> 3);
			uCov[i] = (r & 0x30) ? (byte)(r >> 20) : 0;
		}

		ValueSink whole;
		Scumm::compositeText(whole, src, srcPitch - w, text, pad, cov, pad, w, h, m, uIdx, uCov, pad);

		for (int bandRows = 1; bandRows <= h; ++bandRows) {
			ValueSink banded;
			for (int band = 0; band < h; band += bandRows) {
				const int rows = MIN(bandRows, h - band);
				const int off = band * m * planePitch;
				Scumm::compositeText(banded, src + band * srcPitch, srcPitch - w,
									 text + off, pad, cov + off, pad, w, rows, m,
									 uIdx + off, uCov + off, pad);
			}
			TS_ASSERT(banded.out == whole.out);
		}
	}

	/// The under planes are read at their own pitch, like the others.
	void test_under_planes_follow_their_pitch() {
		const byte src[] = { 3, 3 };                   // 2x1 game pixels
		const byte text[] = { kNone, kNone, kNone, kNone, 0, 0,
							  kNone, kNone, kNone, kNone, 0, 0 };   // 4 wide + 2 pad, m = 2
		const byte cov[] = { 0, 0, 0, 0, 9, 9, 0, 0, 0, 0, 9, 9 };
		const byte uIdx[] = { 0, 0, 0, kUnder, 9, 9, 9, 0, kUnder, 0, 0, 0, 0 };
		const byte uCov[] = { 0, 0, 0, 255, 9, 9, 9, 0, 255, 0, 0, 0, 0 };  // 3 pad
		KindSink sink;
		Scumm::compositeText(sink, src, 0, text, 2, cov, 2, 2, 1, 2, uIdx, uCov, 3);
		TS_ASSERT_EQUALS(sink.out, Common::String("...U.U.."));
	}
};
