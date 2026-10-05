#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_composite.h"
#include "engines/scumm/hires_sinks.h"
#include "graphics/hires_text/keyed_compose.h"

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

	/// The paletted 2x screen's path (no coverage, no decoration): each game
	/// pixel fills an m x m block wherever the text plane is transparent, and
	/// the text wins everywhere else - in rows with no text at all, rows with
	/// some, and rows that are all text.
	void test_scaled_picture_with_keyed_text() {
		for (int m = 1; m <= 3; ++m) {
			const int w = 7, h = 5, srcPad = 3, pad = 2;
			const int outW = w * m, planePitch = outW + pad, srcPitch = w + srcPad;
			Common::Array<byte> src(srcPitch * h), text(planePitch * h * m);
			uint32 seed = 99;
			for (uint i = 0; i < src.size(); ++i)
				src[i] = (byte)((seed = seed * 1103515245 + 12345) >> 16);
			for (int y = 0; y < h * m; ++y)
				for (int x = 0; x < planePitch; ++x) {
					const uint32 r = (seed = seed * 1103515245 + 12345) >> 8;
					// Rows 0 and 1 (of the output) no text, the last row all text.
					const bool none = y < 2, all = y == h * m - 1;
					text[y * planePitch + x] = none ? kNone : all ? (byte)(r | 1) : ((r & 3) ? kNone : (byte)(r >> 4));
				}
			Common::Array<byte> out(outW * h * m);
			Scumm::HiResIndexSink sink(out.begin());
			Scumm::compositeText(sink, src.begin(), srcPad, text.begin(), pad, nullptr, 0, w, h, m);
			int bad = 0;
			for (int y = 0; y < h * m; ++y)
				for (int x = 0; x < outW; ++x) {
					const byte t = text[y * planePitch + x];
					const byte want = (t == kNone) ? src[(y / m) * srcPitch + x / m] : t;
					bad += (out[y * outW + x] != want) ? 1 : 0;
				}
			TS_ASSERT_EQUALS(bad, 0);
		}
	}

	/// Graphics::KeyedCompose (the paletted screen's select per pixel) writes
	/// byte for byte what compositeText() with a HiResIndexSink and no
	/// coverage writes, at every scale, width and padding, over rows with no
	/// text, some, and only text (index 0 included: opaque without coverage).
	static void keyedCase(int m, int w, int h, int srcPad, int pad, uint32 &seed, int &bad, bool useRows) {
		const int outW = w * m, planePitch = outW + pad, srcPitch = w + srcPad;
		Common::Array<byte> src(srcPitch * h), text(planePitch * h * m);
		for (uint i = 0; i < src.size(); ++i)
			src[i] = (byte)((seed = seed * 1103515245 + 12345) >> 16);
		for (int y = 0; y < h * m; ++y)
			for (int x = 0; x < planePitch; ++x) {
				const uint32 r = (seed = seed * 1103515245 + 12345) >> 8;
				const int kind = (y + (int)(r >> 20)) % 4;
				text[y * planePitch + x] = kind == 0 ? kNone : kind == 1 ? (byte)(r >> 4) : ((r & 3) ? kNone : (byte)(r & 0x40));
			}
		Common::Array<byte> want(outW * h * m), got(outW * h * m + 1);
		Scumm::HiResIndexSink sink(want.begin());
		Scumm::compositeText(sink, src.begin(), srcPad, text.begin(), pad, nullptr, 0, w, h, m);
		got[outW * h * m] = 0x5A;	// a canary past the end
		if (useRows)
			Graphics::KeyedCompose::rows(got.begin(), src.begin(), srcPad, text.begin(), pad, w, h, m, kNone);
		else
			Graphics::KeyedCompose::rowsScalar(got.begin(), src.begin(), srcPad, text.begin(), pad, w, h, m, kNone);
		for (int i = 0; i < outW * h * m; ++i)
			bad += (want[i] != got[i]) ? 1 : 0;
		bad += (got[outW * h * m] != 0x5A) ? 1 : 0;
	}

	/// The MMX implementation (where built and the CPU has it) writes byte
	/// for byte what the scalar reference writes: widths around its 8-pixel
	/// step, odd paddings, single rows, keys that are every byte value.
	void test_keyed_compose_mmx_matches_scalar() {
		if (!Graphics::KeyedCompose::rowsMmx || !Graphics::KeyedCompose::haveMmx()) {
			TS_WARN("no MMX here: nothing to compare");
			return;
		}
		uint32 seed = 99991;
		int bad = 0, cases = 0;
		for (int m = 1; m <= 3; ++m)
			for (int w = 1; w <= 41; ++w)
				for (int srcPad = 0; srcPad <= 5; srcPad += 5)
					for (int pad = 0; pad <= 7; pad += 7) {
						const int h = 1 + (w % 3), outW = w * m;
						const byte key = (byte)((seed = seed * 1103515245 + 12345) >> 16);
						Common::Array<byte> src((w + srcPad) * h), text((outW + pad) * h * m);
						for (uint i = 0; i < src.size(); ++i)
							src[i] = (byte)((seed = seed * 1103515245 + 12345) >> 16);
						for (uint i = 0; i < text.size(); ++i) {
							const uint32 r = (seed = seed * 1103515245 + 12345) >> 12;
							text[i] = (r & 1) ? key : (byte)(r >> 4);
						}
						Common::Array<byte> a(outW * h * m + 16, 0x77), b(outW * h * m + 16, 0x77);
						Graphics::KeyedCompose::rowsScalar(a.begin(), src.begin(), srcPad, text.begin(), pad, w, h, m, key);
						Graphics::KeyedCompose::rowsMmx(b.begin(), src.begin(), srcPad, text.begin(), pad, w, h, m, key);
						for (uint i = 0; i < a.size(); ++i)
							bad += (a[i] != b[i]) ? 1 : 0;
						++cases;
					}
		TS_ASSERT_EQUALS(cases, 3 * 41 * 2 * 2);
		TS_ASSERT_EQUALS(bad, 0);
		// The FPU works after it. (x87 on x86_64 only for long double; real
		// MMX/EMMS coverage comes from the DJGPP build and the pentium_mmx gate.)
		volatile long double x = 1.5L;
		TS_ASSERT_EQUALS(x * 2.0L, 3.0L);
	}

	/// With MMX turned off (a DPMI host that emulates the FPU) rows() takes
	/// the scalar path and writes the same bytes.
	void test_keyed_compose_mmx_forced_off() {
		const bool before = Graphics::KeyedCompose::usesMmx();
		Graphics::KeyedCompose::setMmxAllowed(false, "test");
		TS_ASSERT(!Graphics::KeyedCompose::usesMmx());
		int bad = 0;
		uint32 seed = 5;
		for (int w = 1; w <= 20; ++w)
			keyedCase(2, w, 3, 1, 2, seed, bad, true);
		TS_ASSERT_EQUALS(bad, 0);
		Graphics::KeyedCompose::setMmxAllowed(true);
		TS_ASSERT_EQUALS(Graphics::KeyedCompose::usesMmx(), before);
	}

	void test_keyed_compose_matches_the_index_sink() {
		uint32 seed = 2024;
		int bad = 0, cases = 0;
		for (int m = 1; m <= 4; ++m)
			for (int w = 1; w <= 19; ++w)
				for (int pad = 0; pad <= 3; pad += 3) {
					keyedCase(m, w, 3, pad, pad * m, seed, bad, false);
					keyedCase(m, w, 3, pad, pad * m, seed, bad, true);
					cases += 2;
				}
		TS_ASSERT_EQUALS(cases, 4 * 19 * 2 * 2);
		TS_ASSERT_EQUALS(bad, 0);
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
