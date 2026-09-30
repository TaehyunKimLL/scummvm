#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

#include "engines/scumm/charset.h"
#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * [latin] baseline=face: Latin from a bitmap (SVFN) face sits on the
 * baseline baked into the face.
 *
 * A bitmap face's Latin glyph carries its own baseline. printChar() still
 * added the game glyph's offsY on top of it, which is right for a game font
 * whose glyphs are cut to their ink (Monkey Island 2's card fonts: the
 * offsY of a period or a lowercase letter is large) and wrong for the face
 * that replaces it, whose glyph is on a full cell. TrueType faces already
 * dropped the game's offsets (latinStepsByFace); baseline=face asks the
 * same of a bitmap face, and only when the map says so, so no other map
 * changes. printChar() drops the game's offsX/offsY exactly when
 * latinBaselineByFace() is true, through CharsetRendererClassic::
 * latinGlyphOffsets(), which is tested here directly (printChar() itself
 * needs a running engine).
 */
class ScummHiResLatinBaselineTestSuite : public CxxTest::TestSuite {
private:
	static const int kCs = 4;		///< a card charset of MI2
	static const int kCell = 16;

	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		put16(b, pos, v & 0xffff);
		put16(b, pos + 2, v >> 16);
	}

	/// A version 2 SVFN file with the glyphs 'A' and '.', 1 bpp, proportional.
	static Common::Array<byte> makeFont() {
		const int kGlyphs = 2;
		const uint32 cps[kGlyphs] = { 0x41, 0x2E };
		const int rowPitch = (kCell + 7) / 8;
		const uint32 glyphStride = rowPitch * kCell;
		const uint32 metricsOff = 36;
		const uint32 dataOff = metricsOff + kGlyphs * 4;
		const uint32 dataSize = glyphStride * kGlyphs;
		const uint32 cmapOff = dataOff + dataSize;

		Common::Array<byte> b;
		b.resize(cmapOff + kGlyphs * 8);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, 1);
		b[8] = 1;
		put16(b, 12, kGlyphs);
		b[14] = kCell;
		b[15] = kCell;
		b[16] = 12;
		put32(b, 20, metricsOff);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);
		put32(b, 32, cmapOff);
		for (int i = 0; i < kGlyphs; ++i) {
			b[metricsOff + i * 4 + 0] = 9;	// advance
			b[metricsOff + i * 4 + 1] = 1;	// left bearing
			b[metricsOff + i * 4 + 2] = 6;	// ink width
			for (int y = 8; y < 12; ++y)
				b[dataOff + i * glyphStride + y * rowPitch] = 0x7e;
			put32(b, cmapOff + i * 8, cps[i]);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	/// A layer with the face above as charset kCs's font and @p map as the
	/// map (after the [hires] header).
	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *map) {
		const Common::String text = Common::String::format(
			"[hires]\nscale=2\nalpha=false\n%s", map);
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/baseline", '/'), qualifiers, c));
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.noteGameCharset(kCs, 8, 8);
		const Common::Array<byte> bytes = makeFont();
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		return hr.addBitmapFont(kCs, false, ms, "M2U4.SVF");
	}

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

	/// With the key, the bitmap face's Latin is placed by the face: a
	/// letter and a period it has a glyph for, in any charset that uses it.
	void test_key_puts_bitmap_latin_on_the_face_baseline() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[latin]\nmode=proportional\nmetrics=font\nbaseline=face\n"));
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
		TS_ASSERT(hr.latinBaselineByFace('.', kCs));
	}

	/// Without the key nothing changes: the game's own offsets stand, with
	/// the metrics=font of the same map, and with none.
	void test_without_the_key_the_game_offsets_stand() {
		const char *const maps[] = {
			"[latin]\nmode=proportional\nmetrics=font\n",
			"[latin]\nmode=proportional\n",
			"[latin]\nmode=proportional\nmetrics=font\nbaseline=game\n",
		};
		for (uint i = 0; i < ARRAYSIZE(maps); ++i) {
			Scumm::HiResOverlay overlay;
			overlay.create(64, 40, false);
			Scumm::ScummHiResText hr;
			TS_ASSERT(open(hr, overlay, maps[i]));
			TS_ASSERT(!hr.latinBaselineByFace('A', kCs));
			TS_ASSERT(!hr.latinBaselineByFace('.', kCs));
		}
	}

	/// Only what the face draws: a character it has no glyph for, the
	/// space, a code past ASCII, and a glyph the map keeps for the game
	/// (the game draws those, with its own offsets) are not placed by it.
	void test_only_glyphs_the_face_draws() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[latin]\nmode=proportional\nmetrics=font\nbaseline=face\n[glyphs]\n0x2e = keep\n"));
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
		TS_ASSERT(!hr.latinBaselineByFace('.', kCs));		// kept
		TS_ASSERT(!hr.latinBaselineByFace('B', kCs));		// no glyph
		TS_ASSERT(!hr.latinBaselineByFace(' ', kCs));
		TS_ASSERT(!hr.latinBaselineByFace(0x80, kCs));
		TS_ASSERT(!hr.latinBaselineByFace(0x1f, kCs));
		// A charset with no bitmap face has nothing to ask.
		TS_ASSERT(!hr.latinBaselineByFace('A', 2));
	}

	/// [latin] mode=off leaves Latin to the game's font, offsets and all.
	void test_latin_off_keeps_the_game_font() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[latin]\nmode=off\nbaseline=face\n"));
		TS_ASSERT(!hr.latinBaselineByFace('A', kCs));
	}

	/// The layer off: no map, no change.
	void test_layer_off() {
		Scumm::ScummHiResText hr;
		TS_ASSERT(!hr.latinBaselineByFace('A', kCs));
	}

	/// A remapped glyph is drawn from another code point by the map, not by
	/// this rule: the key does not touch it.
	void test_remap_is_unaffected() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[latin]\nmode=proportional\nmetrics=font\nbaseline=face\n[glyphs]\n0x41=u+002E\n"));
		TS_ASSERT(!hr.latinBaselineByFace('A', kCs));		// remapped
		TS_ASSERT(hr.latinBaselineByFace('.', kCs));		// its neighbour still is placed
	}

	/// A renderer that measures Latin with the game's widths and never asks
	/// the layer (FM-Towns, V2) switches the face step off; it ignores this
	/// key too.
	void test_renderer_that_does_not_ask_the_layer_ignores_the_key() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[latin]\nmode=proportional\nmetrics=font\nbaseline=face\n"));
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
		hr.setLatinFaceStepAllowed(false);
		TS_ASSERT(!hr.latinBaselineByFace('A', kCs));
		TS_ASSERT(!hr.latinBaselineByFace('.', kCs));
		hr.setLatinFaceStepAllowed(true);
		TS_ASSERT(hr.latinBaselineByFace('A', kCs));
	}

	/// A TrueType face has its own rule (latinStepsByFace, the charset's
	/// shared line offset): the key is about bitmap faces and does not
	/// apply to it, so a TrueType-only map with the key set changes nothing.
	void test_truetype_face_is_unaffected() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		static const char *const kFonts[] = {
			"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
			"/System/Library/Fonts/AppleSDGothicNeo.ttc",
		};
		const char *ttf = nullptr;
		for (uint i = 0; i < ARRAYSIZE(kFonts) && !ttf; ++i)
			if (Common::FSNode(kFonts[i]).exists())
				ttf = kFonts[i];
		if (!ttf) {
			TS_SKIP("needs a TrueType face");
			return;
		}
		const Common::String text = Common::String::format(
			"[hires]\nscale=2\nalpha=true\nface=%s\n[latin]\nmode=proportional\nbaseline=face\n", ttf);
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/baseline", '/'), qualifiers, c));
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.setTtfFace(c.ttfPath[Graphics::kHiResRoleDefault]);
		hr.noteGameCharset(2, 9, 9);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		// The face is in use, and it is the TrueType path that steps it.
		TS_ASSERT(hr.latinStepsByFace('A', 2));
		TS_ASSERT(!hr.latinBaselineByFace('A', 2));
		TS_ASSERT(!hr.latinBaselineByFace('.', 2));
#else
		TS_SKIP("needs FreeType");
#endif
	}

	/// What printChar() does with the game glyph's offsets. A glyph placed by
	/// the baseline rule loses both; one stepped by a TrueType face loses offsX
	/// and takes the charset's shared line offset; otherwise the game's stay.
	void test_glyph_offsets() {
		int x = 3, y = 9;		// a trimmed card font's '.'
		Scumm::CharsetRendererClassic::latinGlyphOffsets(false, true, 4, x, y);
		TS_ASSERT_EQUALS(x, 0);
		TS_ASSERT_EQUALS(y, 0);		// not the line offset 4: 'A' is 0 in that font

		x = -1; y = 1;
		Scumm::CharsetRendererClassic::latinGlyphOffsets(true, false, 4, x, y);
		TS_ASSERT_EQUALS(x, 0);
		TS_ASSERT_EQUALS(y, 4);

		x = -1; y = 1;
		Scumm::CharsetRendererClassic::latinGlyphOffsets(false, false, 4, x, y);
		TS_ASSERT_EQUALS(x, -1);
		TS_ASSERT_EQUALS(y, 1);

		// A TrueType step wins if both are somehow asked for.
		x = -1; y = 1;
		Scumm::CharsetRendererClassic::latinGlyphOffsets(true, true, 4, x, y);
		TS_ASSERT_EQUALS(x, 0);
		TS_ASSERT_EQUALS(y, 4);
	}
};
