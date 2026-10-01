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

	/// A strip starting on an odd pixel of a packed plane reads its own
	/// pixels from the right nibbles.
	void test_a_packed_span_from_an_odd_pixel() {
		const int w = 3, h = 2, m = 2, ow = w * m, oh = h * m;
		Common::Array<byte> src(w * h), text(ow * oh), cov(ow * oh);
		Graphics::BandedPlane pCov;
		pCov.create(ow + 5, oh, true);
		for (int i = 0; i < w * h; ++i)
			src[i] = 2;
		for (int y = 0; y < oh; ++y)
			for (int x = 0; x < ow; ++x) {
				const int i = y * ow + x;
				text[i] = kText;
				cov[i] = (byte)(((x + y) % 4) * 85);
				pCov.set(x + 3, y, cov[i]);
			}
		pCov.set(2, 0, 255);		// next to the span: not read
		pCov.set(ow + 3, 1, 170);
		TS_ASSERT(pCov.packed());
		ValueSink whole, span;
		Scumm::compositeText(whole, src.begin(), 0, text.begin(), 0, cov.begin(), 0, w, h, m);
		Scumm::compositeTextRows(span, src.begin(), 0, text.begin(), 0, Scumm::CompositeRows(&pCov, 3, 0, ow), w, h, m);
		TS_ASSERT(whole.out == span.out);
	}

	/// Planes held in bands (Graphics::BandedPlane) compose as the same
	/// bytes held whole: a band not there is zeros, a packed one unpacked.
	void test_banded_planes_compose_like_whole_ones() {
		const int w = 6, h = 20, m = 2, ow = w * m, oh = h * m;
		Common::Array<byte> src(w * h), text(ow * oh), cov(ow * oh), uIdx(ow * oh), uCov(ow * oh);
		Graphics::BandedPlane pCov, pUIdx, pUCov;
		pCov.create(ow + 4, oh + 4, true);
		pUIdx.create(ow + 4, oh + 4, false);
		pUCov.create(ow + 4, oh + 4, true);
		uint32 seed = 12345;
		for (int i = 0; i < w * h; ++i)
			src[i] = (byte)(i % 5);
		for (int y = 0; y < oh; ++y)
			for (int x = 0; x < ow; ++x) {
				const int i = y * ow + x;
				seed = seed * 1103515245u + 12345u;
				// Text only in some rows (whole bands without any), coverage
				// of 2 bpp first, then any value once the halfway row is past.
				const bool rowHasText = (y >= 4 && y < 12) || y >= 33;
				const uint r = seed >> 16;
				text[i] = (rowHasText && (r & 1)) ? kText : kNone;
				cov[i] = text[i] == kText ? (byte)((r >> 1) % 4 * 85) : 0;
				uCov[i] = (rowHasText && (r & 8)) ? (byte)(y >= oh / 2 ? (r >> 4) & 0xFF : 255) : 0;
				uIdx[i] = uCov[i] ? kUnder : 0;
				pCov.set(x + 2, y + 3, cov[i]);
				pUIdx.set(x + 2, y + 3, uIdx[i]);
				pUCov.set(x + 2, y + 3, uCov[i]);
			}
		TS_ASSERT(pCov.packed());
		TS_ASSERT(pCov.bandsHeld() < pCov.height() / 16 + 1);

		ValueSink whole, banded, span;
		Scumm::compositeText(whole, src.begin(), 0, text.begin(), 0, cov.begin(), 0, w, h, m,
							 uIdx.begin(), uCov.begin(), 0);
		Scumm::compositeTextRows(banded, src.begin(), 0, text.begin(), 0, Scumm::CompositeRows(&pCov, 2, 3),
								 w, h, m, Scumm::CompositeRows(&pUIdx, 2, 3), Scumm::CompositeRows(&pUCov, 2, 3));
		TS_ASSERT(whole.out == banded.out);
		// The strip's span only, as drawStripToScreen() asks for it.
		Scumm::compositeTextRows(span, src.begin(), 0, text.begin(), 0, Scumm::CompositeRows(&pCov, 2, 3, ow),
								 w, h, m, Scumm::CompositeRows(&pUIdx, 2, 3, ow), Scumm::CompositeRows(&pUCov, 2, 3, ow));
		TS_ASSERT(whole.out == span.out);
	}
};
