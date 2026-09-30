#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/font_map.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * [latin] font=same (or [font.N] latin_font=same, alias face=/latin_face=):
 * Latin comes from the same font as the charset's own Hangul, never from a
 * Latin companion, a TTF and never - on a glyph neither has, with
 * [hires] missing= set - the game's internal font either.
 *
 * [font.N] face=original / bitmap=original ("the game's own font, always")
 * lives beside it here: both are value sentinels the parser stores as plain
 * strings (graphics/hires_text is untouched) and only the SCUMM engine
 * interprets, in resolveCharsetFonts() and faceForCodePoint().
 *
 * faceForCodePoint() is private; tests reach it through the narrow
 * perGlyphSourceFor() accessor (for tests only, see hires_text.h), which
 * hands back the Graphics::UnicodeGlyphSource* it resolves, exactly as
 * printChar()'s own callers (drawChar, advanceFor, latinBaselineByFace,
 * latinFaceStep, drawsCode) see it - and the code point as it was updated
 * (the missing= mark).
 */
class ScummHiResLatinSameTestSuite : public CxxTest::TestSuite {
private:
	static const int kCs = 4;      ///< a card charset, as the baseline suite uses
	static const int kOtherCs = 0; ///< a charset with no bitmap of its own
	static const int kCell = 16;

	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		put16(b, pos, v & 0xffff);
		put16(b, pos + 2, v >> 16);
	}

	/// A version 2 SVFN file, 1bpp proportional, holding one glyph per code
	/// point in @p cps, each with some ink so cells()/glyphInk() see it.
	static Common::Array<byte> makeFont(const Common::Array<uint32> &cps) {
		const int n = cps.size();
		const int rowPitch = (kCell + 7) / 8;
		const uint32 glyphStride = rowPitch * kCell;
		const uint32 metricsOff = 36;
		const uint32 dataOff = metricsOff + n * 4;
		const uint32 dataSize = glyphStride * n;
		const uint32 cmapOff = dataOff + dataSize;

		Common::Array<byte> b;
		b.resize(cmapOff + n * 8);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, 1);
		b[8] = 1;
		put16(b, 12, n);
		b[14] = kCell;
		b[15] = kCell;
		b[16] = 12;
		put32(b, 20, metricsOff);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);
		put32(b, 32, cmapOff);
		for (int i = 0; i < n; ++i) {
			b[metricsOff + i * 4 + 0] = 9;  // advance
			b[metricsOff + i * 4 + 1] = 1;  // left bearing
			b[metricsOff + i * 4 + 2] = 6;  // ink width
			for (int y = 8; y < 12; ++y)
				b[dataOff + i * glyphStride + y * rowPitch] = 0x7e;
			put32(b, cmapOff + i * 8, cps[i]);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	static bool addFont(Scumm::ScummHiResText &hr, int charsetId, bool latin,
						const Common::Array<uint32> &cps, const char *name) {
		const Common::Array<byte> bytes = makeFont(cps);
		Common::MemoryReadStream ms(bytes.begin(), bytes.size());
		return hr.addBitmapFont(charsetId, latin, ms, name);
	}

	/// A layer with @p map (after the [hires] header) parsed and adopted;
	/// no bitmap font added yet.
	bool open(Scumm::ScummHiResText &hr, Scumm::HiResOverlay &overlay, const char *map) {
		const Common::String text = Common::String::format(
			"[hires]\nscale=2\nalpha=false\nmissing=u+25a1\n%s", map);
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		if (!Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/same", '/'), qualifiers, c))
			return false;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.noteGameCharset(kCs, 8, 8);
		hr.noteGameCharset(kOtherCs, 8, 8);
		// nearestFont() matches by _charsetWidths (setCharsetGrid()), not the
		// TTF-sizing cell noteGameCharset() records: kCell (16) at scale 2 is
		// 8 game px, so a charset with no font of its own (want 8) finds one
		// sized for another exactly.
		hr.setCharsetGrid(kCs, 8, 8);
		hr.setCharsetGrid(kOtherCs, 8, 8);
		return true;
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

	/// Map-wide font=same, with a [font.N] bitmap= (the charset's own SVF)
	/// and a [latin] bitmap= companion both holding 'A': same resolves it
	/// to the own SVF.
	void test_map_wide_same_prefers_own_font_over_companion() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nbitmap=OWN.SVF\n[latin]\nmode=proportional\nfont=same\n"));
		Common::Array<uint32> own, companion;
		own.push_back('A');
		companion.push_back('A');
		TS_ASSERT(addFont(hr, kCs, false, own, "OWN.SVF"));
		TS_ASSERT(addFont(hr, kCs, true, companion, "LAT.SVF"));

		Graphics::UnicodeGlyphSource *ownSrc = hr.sourceFor(kCs, false);
		Graphics::UnicodeGlyphSource *companionSrc = hr.sourceFor(kCs, true);
		TS_ASSERT(ownSrc && companionSrc && ownSrc != companionSrc);

		uint32 cp = 'A';
		Graphics::UnicodeGlyphSource *resolved = hr.perGlyphSourceFor(kCs, cp);
		TS_ASSERT_EQUALS(resolved, ownSrc);
		TS_ASSERT_EQUALS(cp, (uint32)'A');
	}

	/// The same map without font=same: the companion wins (regression guard).
	void test_without_same_the_companion_wins() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nbitmap=OWN.SVF\n[latin]\nmode=proportional\n"));
		Common::Array<uint32> own, companion;
		own.push_back('A');
		companion.push_back('A');
		TS_ASSERT(addFont(hr, kCs, false, own, "OWN.SVF"));
		TS_ASSERT(addFont(hr, kCs, true, companion, "LAT.SVF"));

		Graphics::UnicodeGlyphSource *companionSrc = hr.sourceFor(kCs, true);
		uint32 cp = 'A';
		Graphics::UnicodeGlyphSource *resolved = hr.perGlyphSourceFor(kCs, cp);
		TS_ASSERT_EQUALS(resolved, companionSrc);
	}

	/// A charset with no bitmap of its own resolves same to the nearest
	/// charset's SVF.
	void test_same_borrows_the_nearest_charset_when_this_one_has_none() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		// kOtherCs (0) names no bitmap; kCs (4) does, and is the only
		// charset with one, so it is what nearestFont() finds.
		TS_ASSERT(open(hr, overlay, "[font.4]\nbitmap=OWN.SVF\n[latin]\nmode=proportional\nfont=same\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		TS_ASSERT(addFont(hr, kCs, false, own, "OWN.SVF"));

		Graphics::UnicodeGlyphSource *ownSrc = hr.sourceFor(kCs, false);
		uint32 cp = 'A';
		Graphics::UnicodeGlyphSource *resolved = hr.perGlyphSourceFor(kOtherCs, cp);
		TS_ASSERT_EQUALS(resolved, ownSrc);
	}

	/// A glyph absent from the own SVF: with missing=u+25a1 and the SVF
	/// holding U+25A1, same resolves to the own SVF with cp == 0x25A1;
	/// without missing= (a map that never sets it) it is declined.
	void test_missing_glyph_draws_the_mark_from_the_same_font() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nbitmap=OWN.SVF\n[latin]\nmode=proportional\nfont=same\n"));
		Common::Array<uint32> own;
		own.push_back(0x25a1); // no 'A': the missing mark only
		TS_ASSERT(addFont(hr, kCs, false, own, "OWN.SVF"));

		Graphics::UnicodeGlyphSource *ownSrc = hr.sourceFor(kCs, false);
		uint32 cp = 'A';
		Graphics::UnicodeGlyphSource *resolved = hr.perGlyphSourceFor(kCs, cp);
		TS_ASSERT_EQUALS(resolved, ownSrc);
		TS_ASSERT_EQUALS(cp, (uint32)0x25a1);
	}

	/// Without missing= in effect (nulled out via a per-charset SVF that
	/// simply has neither glyph and a map with no [hires] missing=), 'A' is
	/// declined outright: nullptr, and cp is left alone.
	void test_no_missing_key_declines() {
		const Common::String text =
			"[hires]\nscale=2\nalpha=false\n"
			"[font.4]\nbitmap=OWN.SVF\n[latin]\nmode=proportional\nfont=same\n";
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/same", '/'), qualifiers, c));
		TS_ASSERT_EQUALS(c.missing, (uint32)0);

		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.noteGameCharset(kCs, 8, 8);
		Common::Array<uint32> own;
		own.push_back(0x25a1);
		TS_ASSERT(addFont(hr, kCs, false, own, "OWN.SVF"));

		uint32 cp = 'A';
		Graphics::UnicodeGlyphSource *resolved = hr.perGlyphSourceFor(kCs, cp);
		TS_ASSERT(!resolved);
		TS_ASSERT_EQUALS(cp, (uint32)'A');
	}

	/// Per-charset [font.4] latin_font=same affects only that charset: a
	/// second charset with a companion of its own keeps using it.
	void test_per_charset_same_affects_only_that_charset() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nbitmap=OWN4.SVF\nlatin_font=same\n"
					   "[font.0]\nbitmap=OWN0.SVF\n[latin]\nmode=proportional\n"));
		Common::Array<uint32> own4, own0, companion0;
		own4.push_back('A');
		own0.push_back('X');
		companion0.push_back('A');
		TS_ASSERT(addFont(hr, kCs, false, own4, "OWN4.SVF"));
		TS_ASSERT(addFont(hr, kOtherCs, false, own0, "OWN0.SVF"));
		TS_ASSERT(addFont(hr, kOtherCs, true, companion0, "LAT0.SVF"));

		Graphics::UnicodeGlyphSource *own4Src = hr.sourceFor(kCs, false);
		Graphics::UnicodeGlyphSource *companion0Src = hr.sourceFor(kOtherCs, true);
		TS_ASSERT(own4Src && companion0Src);

		uint32 cp4 = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cp4), own4Src);
		uint32 cp0 = 'A';
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kOtherCs, cp0), companion0Src);
	}

	// ---- [font.N] face=original / bitmap=original --------------------

	/// bitmap=original: the whole charset, Hangul included, is the game's
	/// own to draw - faceForCodePoint() declines outright, for ASCII and
	/// for a double-byte code point alike, and does not borrow a neighbour's
	/// font (nearestFont()) into it either.
	void test_bitmap_original_declines_the_whole_charset() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		// kOtherCs has a real font, so it would otherwise be the nearest
		// donor for kCs.
		TS_ASSERT(open(hr, overlay,
					   "[font.0]\nbitmap=OWN0.SVF\n[font.4]\nbitmap=original\n[latin]\nmode=proportional\n"));
		Common::Array<uint32> own0;
		own0.push_back('A');
		own0.push_back(0xAC00);
		TS_ASSERT(addFont(hr, kOtherCs, false, own0, "OWN0.SVF"));

		uint32 cpA = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpA));
		uint32 cpHangul = 0xAC00;
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpHangul));
	}

	/// face=original does the same as bitmap=original.
	void test_face_original_declines_the_whole_charset() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay, "[font.4]\nface=original\n[latin]\nmode=proportional\n"));
		uint32 cp = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}

	/// latin_font=original leaves Latin (only) to the game's font, exactly
	/// as [latin] mode=off does; Hangul on the same charset is unaffected.
	void test_latin_font_original_is_mode_off_for_ascii_only() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nbitmap=OWN.SVF\nlatin_font=original\n[latin]\nmode=proportional\n"));
		Common::Array<uint32> own;
		own.push_back('A');
		own.push_back(0xAC00);
		TS_ASSERT(addFont(hr, kCs, false, own, "OWN.SVF"));

		uint32 cpA = 'A';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cpA));
		Graphics::UnicodeGlyphSource *ownSrc = hr.sourceFor(kCs, false);
		uint32 cpHangul = 0xAC00;
		TS_ASSERT_EQUALS(hr.perGlyphSourceFor(kCs, cpHangul), ownSrc);
	}

	/// original beats a [glyphs] remap: a remapped ASCII code in an
	/// original charset is still the game's to draw, because
	/// faceForCodePoint() itself declines before any candidate is tried,
	/// whatever code point the caller looked up.
	void test_original_beats_glyph_remap() {
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		TS_ASSERT(open(hr, overlay,
					   "[font.4]\nbitmap=original\n[latin]\nmode=proportional\n[glyphs:cs4]\n0x41=u+0042\n"));
		// Even asked for directly by the remapped code point, it is declined.
		uint32 cp = 'B';
		TS_ASSERT(!hr.perGlyphSourceFor(kCs, cp));
	}

	/// loadFonts() itself, on disk: [font.0] bitmap=original is skipped with
	/// no attempt to open a file named "original" (and so no "not found"
	/// warning either), while [font.4]'s real bitmap= still loads - proving
	/// the skip in the bitmap-loading loop does not disturb an ordinary
	/// charset beside it. latin_font=same is covered end to end by the
	/// perGlyphSourceFor() tests above: it leaves latinFace empty, so
	/// latinTtfFaceFor() (loadFonts()'s only path that opens a TrueType
	/// face by name) never sees "same" as a path at all.
	void test_bitmap_original_is_skipped_by_loadfonts_without_opening_it() {
		Common::FSNode tmp("/tmp/scummvm-hires-same-test");
		tmp.createDirectory();
		const Common::Path gameDir = tmp.getPath();

		{
			const Common::Array<byte> bytes = makeFont(Common::Array<uint32>(1, (uint32)'A'));
			Common::DumpFile f;
			TS_ASSERT(f.open(gameDir.join(Common::Path("OWN4.SVF", '/'))));
			f.write(bytes.begin(), bytes.size());
			f.close();
		}

		const Common::String text =
			"[hires]\nscale=2\nalpha=false\n"
			"[font.0]\nbitmap=original\n[font.4]\nbitmap=OWN4.SVF\n[latin]\nmode=proportional\n";
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, gameDir, qualifiers, c));

		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, false);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.noteGameCharset(kCs, 8, 8);
		hr.noteGameCharset(kOtherCs, 8, 8);
		TS_ASSERT(hr.loadFonts(gameDir));

		// font.4's real SVF loaded; font.0's "original" opened nothing.
		TS_ASSERT(hr.sourceFor(kCs, false) != nullptr);
		TS_ASSERT(hr.sourceFor(kOtherCs, false) == nullptr);
		TS_ASSERT_EQUALS(hr.sourceCount(), 1);
	}
};
