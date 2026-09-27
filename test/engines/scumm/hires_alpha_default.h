#include <cxxtest/TestSuite.h>

#include "engines/scumm/hires_text.h"

/**
 * Whether a map that says nothing about alpha= gets blended text (C17).
 *
 * A TrueType face and an 8 bpp SVFN both carry coverage: their edges are
 * partly covered pixels. With blending off, the layer draws a pixel where
 * coverage is at least half, which throws the anti-aliasing away and gives
 * hard, stepped edges. AGS already blends unless the map says alpha=false;
 * SCUMM did not, so the fork's own universal map ([hires] face=ko, ja, th,
 * no alpha= line) drew Monkey Island 2's Korean on a paletted screen with
 * jagged edges. The map-less forms already blended (a face named in the
 * ini, an 8 bpp set found by name); a map now does too.
 */
class HiResAlphaDefaultTestSuite : public CxxTest::TestSuite {
public:
	/** A map naming a face and no alpha= blends. */
	void test_face_without_alpha_key_blends() {
		Graphics::HiResTextConfig c;
		TS_ASSERT(Scumm::ScummHiResText::mapWantsAlpha(c, true, false));
	}

	/** A map naming an anti-aliased (8 bpp) SVFN and no alpha= blends. */
	void test_coverage_bitmap_without_alpha_key_blends() {
		Graphics::HiResTextConfig c;
		TS_ASSERT(Scumm::ScummHiResText::mapWantsAlpha(c, false, true));
	}

	/**
	 * A map with only 1 bpp stencils has nothing to blend: it stays keyed,
	 * on a paletted screen, exactly as before.
	 */
	void test_stencils_only_do_not_blend() {
		Graphics::HiResTextConfig c;
		TS_ASSERT(!Scumm::ScummHiResText::mapWantsAlpha(c, false, false));
	}

	/** What the map says always wins, both ways. */
	void test_explicit_alpha_key_wins() {
		Graphics::HiResTextConfig off;
		off.alpha = false;
		off.alphaFromMap = true;
		TS_ASSERT(!Scumm::ScummHiResText::mapWantsAlpha(off, true, true));

		Graphics::HiResTextConfig on;
		on.alpha = true;
		on.alphaFromMap = true;
		TS_ASSERT(Scumm::ScummHiResText::mapWantsAlpha(on, false, false));
	}

	/**
	 * A face named only through a [hires] face= chain, or a [font.N]
	 * face= chain, is a font the layer uses: the "names no [bitmap] fonts"
	 * warning must not fire for it (it did for the universal map).
	 */
	void test_face_chain_is_a_named_font() {
		const Common::Path none;

		Graphics::HiResTextConfig h;
		h.hiresFaceSet = true;
		h.hiresFaceChain.push_back(Common::Path("/System/Library/Fonts/AppleSDGothicNeo.ttc", '/'));
		TS_ASSERT(!Scumm::ScummHiResText::mapNamesNoFonts(h, none));

		Graphics::HiResTextConfig f;
		Graphics::HiResFontIdSettings s;
		s.faceSet = true;
		s.faceChain.push_back(Common::Path("/System/Library/Fonts/AppleSDGothicNeo.ttc", '/'));
		f.fontIds[2] = s;
		TS_ASSERT(!Scumm::ScummHiResText::mapNamesNoFonts(f, none));

		// Set but empty (every entry dropped) is still nothing named.
		Graphics::HiResTextConfig e;
		e.hiresFaceSet = true;
		TS_ASSERT(Scumm::ScummHiResText::mapNamesNoFonts(e, none));
	}
};
