#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/path.h"

#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * Whether [render] blend= (design section 7.2, replacing [hires] alpha=)
 * wants blended text (C17).
 *
 * A TrueType face and an 8 bpp SVFN both carry coverage: their edges are
 * partly covered pixels. With blending off, the layer keys them: it draws a
 * pixel where coverage is at least 0x40 (any coverage at all before C17),
 * which throws the anti-aliasing away and gives hard, stepped edges. `auto`
 * (the default, unset blend=) blends whenever some named face has coverage;
 * a map of only 1 bpp stencils stays keyed.
 */
class HiResAlphaDefaultTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	/// blend=auto with a face that has coverage: blending wanted.
	void test_blend_auto_with_coverage_wants_alpha() {
		TS_ASSERT(Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendAuto, true));
	}

	/// blend=auto with only 1 bpp stencils: stays keyed, as before.
	void test_blend_auto_without_coverage_does_not_want_alpha() {
		TS_ASSERT(!Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendAuto, false));
	}

	/// blend=off never blends, whatever the fonts.
	void test_blend_off_never_wants_alpha() {
		TS_ASSERT(!Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendOff, true));
		TS_ASSERT(!Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendOff, false));
	}

	/// blend=on always wants alpha (the resolved screen may still refuse it).
	void test_blend_on_always_wants_alpha() {
		TS_ASSERT(Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendOn, false));
		TS_ASSERT(Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendOn, true));
	}

	/// SCUMM v7/v8 cannot blend at all (the backend palette is SMUSH's).
	void test_a_game_that_cannot_blend_is_named_by_canBlendText() {
		TS_ASSERT(Scumm::ScummHiResText::canBlendText(5));
		TS_ASSERT(Scumm::ScummHiResText::canBlendText(6));
		TS_ASSERT(!Scumm::ScummHiResText::canBlendText(7));
		TS_ASSERT(!Scumm::ScummHiResText::canBlendText(8));
	}

	/**
	 * A game that cannot blend (SCUMM v7 and v8: the backend palette is
	 * SMUSH's) gets no default: blend=auto with a covering face stays keyed,
	 * so the "will not be blended" warning is not printed on every start of
	 * FT, The Dig or COMI (Task 7 review L1(a)). blend=on still asks, and
	 * the caller (scumm.cpp) is still the one that finds out it could not be
	 * done - wantsAlphaFor() itself never silently drops an explicit ask.
	 */
	void test_a_game_that_cannot_blend_gets_no_default() {
		TS_ASSERT(!Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendAuto, true, false));
		TS_ASSERT(Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendOn, true, false));
		TS_ASSERT(Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendOn, false, false));
		TS_ASSERT(!Scumm::ScummHiResText::wantsAlphaFor(Graphics::kHiResBlendOff, true, false));
	}

	/// The phase-1 coverage probe (design 7.1.1, Graphics::HiResCoverageFn):
	/// a 2 bpp SVFN has coverage, a 1 bpp one does not - the C17 rule at the
	/// file level, sniffed by magic and the bpp byte alone.
	void test_svf_coverage_probe_reads_the_bpp_byte() {
		Common::FSNode tmp("/tmp/scummvm-hires-alpha-test");
		tmp.createDirectory();
		const Common::Path dir = tmp.getPath();

		auto writeSvf = [&](const char *name, byte bpp) {
			Common::Array<byte> b(16, (byte)0);
			b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
			b[8] = bpp;
			Common::DumpFile f;
			TS_ASSERT(f.open(dir.join(Common::Path(name, '/'))));
			f.write(b.begin(), b.size());
			f.close();
		};
		writeSvf("one.svf", 1);
		writeSvf("two.svf", 2);
		writeSvf("eight.svf", 8);

		TS_ASSERT(!Scumm::ScummHiResText::faceHasCoverage(dir.join(Common::Path("one.svf", '/')), nullptr));
		TS_ASSERT(Scumm::ScummHiResText::faceHasCoverage(dir.join(Common::Path("two.svf", '/')), nullptr));
		TS_ASSERT(Scumm::ScummHiResText::faceHasCoverage(dir.join(Common::Path("eight.svf", '/')), nullptr));
		// Not SVFN at all (a TrueType face, by elimination): coverage is
		// assumed - there is no cheap way to know without a rasteriser.
		Common::DumpFile f;
		TS_ASSERT(f.open(dir.join(Common::Path("face.ttf", '/'))));
		const byte junk[4] = { 0, 1, 2, 3 };
		f.write(junk, sizeof(junk));
		f.close();
		TS_ASSERT(Scumm::ScummHiResText::faceHasCoverage(dir.join(Common::Path("face.ttf", '/')), nullptr));
		// A path with nothing there at all has no coverage to offer.
		TS_ASSERT(!Scumm::ScummHiResText::faceHasCoverage(dir.join(Common::Path("missing.svf", '/')), nullptr));
	}
};
