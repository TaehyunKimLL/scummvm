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
