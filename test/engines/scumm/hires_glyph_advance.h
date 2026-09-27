#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/fonts/ttf.h"
#include "graphics/font.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/font_baker.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source_svfn.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "../../system/null_osystem.h"

/**
 * SCUMM hi-res text placed per glyph (C11 T6, I18N_TEXT_DESIGN.md section
 * 4.2): per-charset faces and chains from hires_text.map ([font.N] with N the
 * charset id), the [latin] modes with SCI's meanings, the advance of each
 * glyph from its own metrics (a wide glyph keeps the cell rule, a combining
 * mark advances 0 and is drawn against the previous base), and the SVFN
 * convention that lets a baked font carry such marks.
 */
class ScummHiResGlyphAdvanceTestSuite : public CxxTest::TestSuite {
private:
	static const int kScale = 2;
	static const byte kInk = 15;

	/// One glyph of a synthetic SVFN font: metrics plus a rectangle of ink.
	struct G {
		uint32 cp;
		int advance, bearing, inkWidth;
		int x0, x1, y0, y1;	///< stored ink columns [x0, x1), rows [y0, y1)
	};

	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		for (int i = 0; i < 4; i++)
			b[pos + i] = (v >> (8 * i)) & 0xff;
	}

	/**
	 * A proportional 8bpp version 2 SVFN of square cells. @p marksAtOrigin
	 * sets flags bit 2 (FONT_FORMAT.md section 3): a combining mark's row
	 * holds the pen at column -bearingX, so ink left of the pen survives.
	 */
	static Common::Array<byte> svfn(const G *g, int n, int cell, int ascent, bool marksAtOrigin) {
		const uint32 stride = cell * cell;
		const uint32 metricsOff = 36, dataOff = metricsOff + n * 4;
		const uint32 cmapOff = dataOff + stride * n;
		Common::Array<byte> b;
		b.resize(cmapOff + n * 8, 0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, 1 | (marksAtOrigin ? 4 : 0));
		b[8] = 8;
		put16(b, 12, n);
		b[14] = cell;
		b[15] = cell;
		b[16] = ascent;
		put32(b, 20, metricsOff);
		put32(b, 24, dataOff);
		put32(b, 28, stride * n);
		put32(b, 32, cmapOff);
		for (int i = 0; i < n; i++) {
			b[metricsOff + i * 4 + 0] = g[i].advance;
			b[metricsOff + i * 4 + 1] = (byte)(int8)g[i].bearing;
			b[metricsOff + i * 4 + 2] = g[i].inkWidth;
			for (int y = g[i].y0; y < g[i].y1; y++)
				for (int x = g[i].x0; x < g[i].x1; x++)
					b[dataOff + i * stride + y * cell + x] = 255;
			put32(b, cmapOff + i * 8, g[i].cp);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	static bool addFont(Scumm::ScummHiResText &hr, int cs, bool latin, const Common::Array<byte> &bytes,
						const char *name) {
		Common::MemoryReadStream s(bytes.begin(), bytes.size());
		return hr.addBitmapFont(cs, latin, s, name);
	}

	static Graphics::HiResTextConfig parse(const char *text) {
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/c11t6", '/'), qualifiers, c));
		return c;
	}


	static void clear(Graphics::Surface &s, int w, int h) {
		s.create(w, h, Graphics::PixelFormat::createFormatCLUT8());
		memset(s.getPixels(), 0, w * h);
	}

	static bool inked(const Graphics::Surface &s, int x, int y) {
		return *(const byte *)s.getBasePtr(x, y) != 0;
	}

	/// Leftmost and rightmost inked column in the whole surface; false if none.
	static bool inkSpan(const Graphics::Surface &s, int &left, int &right) {
		left = s.w;
		right = -1;
		for (int y = 0; y < s.h; y++)
			for (int x = 0; x < s.w; x++)
				if (inked(s, x, y)) {
					left = MIN(left, x);
					right = MAX(right, x);
				}
		return right >= 0;
	}

	static int firstInkRow(const Graphics::Surface &s) {
		for (int y = 0; y < s.h; y++)
			for (int x = 0; x < s.w; x++)
				if (inked(s, x, y))
					return y;
		return -1;
	}

	static const char *appleGothic() {
		static const char *const kPath = "/System/Library/Fonts/AppleSDGothicNeo.ttc";
		return Common::FSNode(kPath).exists() ? kPath : nullptr;
	}

	static const char *sukhumvit() {
		static const char *const kTtc = "/System/Library/Fonts/Supplemental/SukhumvitSet.ttc";
		static const char *const kTtf = "/Users/juami/work/scummvm/runs/c11/data/fonts/sukhumvit-text.ttf";
		if (Common::FSNode(kTtc).exists())
			return kTtc;
		return Common::FSNode(kTtf).exists() ? kTtf : nullptr;
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

	/// [font.2] size=24 opens charset 2's face at 24 px; charsets 0 and 1
	/// stay on the game cell times the scale.
	void test_font_n_is_charset_id() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("Apple SD Gothic Neo not present on this machine");
			return;
		}
		const Common::String map = Common::String::format(
			"[hires]\nscale=2\nalpha=true\nface=%s\n[font.2]\nsize=24\n", apple);
		Graphics::HiResTextConfig c = parse(map.c_str());
		TS_ASSERT(c.fontIdSettings(2) && c.fontIdSettings(2)->sizeSet);

		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		for (int cs = 0; cs < 3; cs++)
			hr.setGameFontCell(cs, 8, 8);
		TS_ASSERT(hr.loadFonts(Common::Path()));

		Graphics::UnicodeGlyphSource *s0 = hr.sourceFor(0, false);
		Graphics::UnicodeGlyphSource *s1 = hr.sourceFor(1, false);
		Graphics::UnicodeGlyphSource *s2 = hr.sourceFor(2, false);
		TS_ASSERT(s0 && s1 && s2);
		if (!s0 || !s1 || !s2)
			return;
		TS_ASSERT_EQUALS(s0, s1);
		TS_ASSERT_EQUALS((int)s0->cellHeight(), 16);
		TS_ASSERT_EQUALS((int)s2->cellHeight(), 24);
		TS_ASSERT(s2 != s0);
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// ASCII under [latin] mode=proportional metrics=font: SCI's rule,
	/// latinAdvanceGamePx() (test/graphics/hires_text_latin_advance.h).
	void test_metrics_font_advance() {
		const G glyphs[] = {
			{ 0x41, 13, 0, 12, 0, 12, 2, 14 },
			{ 0x42, 9, 0, 8, 0, 8, 2, 14 },
			{ 0x43, 0, 0, 6, 0, 6, 2, 14 },	// the face gives no advance
		};
		const Common::Array<byte> font = svfn(glyphs, 3, 16, 12, false);

		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);

		{
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=proportional\nmetrics=font\n"));
			TS_ASSERT(addFont(hr, 0, true, font, "lat.fnt"));
			TS_ASSERT(hr.perGlyphMetrics());
			TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 4), 7);
			TS_ASSERT_EQUALS(hr.advanceFor('B', 0, 4), 5);
			TS_ASSERT_EQUALS(hr.advanceFor('C', 0, 4), 4);
		}
		{
			// metrics=game: the game's width, whatever the face says.
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=proportional\nmetrics=game\n"));
			TS_ASSERT(addFont(hr, 0, true, font, "lat.fnt"));
			TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 4), 4);
			TS_ASSERT_EQUALS(hr.advanceFor('B', 0, 4), 4);
		}
		{
			// mode=off: the game's font draws ASCII and sets its advance.
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=off\nmetrics=font\n"));
			TS_ASSERT(addFont(hr, 0, true, font, "lat.fnt"));
			TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 4), 4);
			Graphics::Surface dest;
			clear(dest, 96, 40);
			TS_ASSERT(!hr.drawChar(dest, 'A', 0, 2, 2, kInk, 0, 1));
			dest.free();
		}
		{
			// A non-ASCII, non-wide glyph (U+2026) advances by the face by
			// default, with no metrics key at all.
			const G ell[] = { { 0x2026, 13, 0, 12, 0, 12, 10, 12 } };
			Graphics::HiResTextConfig c = parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=off\n");
			c.glyphOverrides[0xA6A1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x2026);
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(c);
			TS_ASSERT(addFont(hr, 0, false, svfn(ell, 1, 16, 12, false), "ell.fnt"));
			TS_ASSERT_EQUALS(hr.advanceFor(0xA6A1, 0, 8), 7);
		}
	}

	/// A wide glyph keeps today's cell rule: the same advance as a map with
	/// none of the new keys, for every game width, both metrics, with carry.
	void test_wide_keeps_cell() {
		const G ga[] = { { 0xAC00, 9, 1, 12, 1, 13, 2, 14 } };
		const Common::Array<byte> font = svfn(ga, 1, 16, 13, false);
		const int kGa = 0xA1B0;

		for (int metrics = 0; metrics < 2; metrics++) {
			Graphics::HiResTextConfig legacy;
			legacy.scale = 2;
			legacy.alpha = true;
			legacy.encoding = Common::kWindows949;
			legacy.metricsSource = metrics ? Graphics::kHiResMetricsFont : Graphics::kHiResMetricsGame;
			legacy.glyphOverrides[kGa] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0xAC00);
			Graphics::HiResTextConfig perGlyph = legacy;
			perGlyph.latinMode = Graphics::kHiResLatinProportional;
			perGlyph.latinModeSet = true;

			Scumm::HiResOverlay overlay;
			overlay.create(96, 40, true);
			Scumm::ScummHiResText a, b;
			a.useOverlay(&overlay);
			b.useOverlay(&overlay);
			a.adoptConfig(legacy);
			b.adoptConfig(perGlyph);
			TS_ASSERT(addFont(a, 0, false, font, "k.fnt"));
			TS_ASSERT(addFont(b, 0, false, font, "k.fnt"));
			TS_ASSERT(!a.perGlyphMetrics());
			TS_ASSERT(b.perGlyphMetrics());

			for (int gw = 1; gw <= 12; gw++) {
				TS_ASSERT_EQUALS(a.advanceFor(kGa, 0, gw), b.advanceFor(kGa, 0, gw));
				int ca = 1, cb = 1;
				for (int i = 0; i < 5; i++)
					TS_ASSERT_EQUALS(a.advanceFor(kGa, 0, gw, &ca), b.advanceFor(kGa, 0, gw, &cb));
				TS_ASSERT_EQUALS(ca, cb);
			}
			// Today's value, pinned: the ink reach 13 at scale 2 is 7.
			TS_ASSERT_EQUALS(b.advanceFor(kGa, 0, 4), 7);
			TS_ASSERT_EQUALS(b.advanceFor(kGa, 0, 8), metrics ? 7 : 8);
		}
	}

	/// U+0E48 advances 0 and is drawn against the base before it: its ink,
	/// stored with the pen at column 6, lands 6 px left of the anchor.
	void test_combining_zero_advance() {
		const G thai[] = {
			{ 0x0E01, 12, 1, 10, 1, 11, 6, 14 },
			{ 0x0E48, 0, -6, 4, 0, 4, 0, 4 },
		};
		Graphics::HiResTextConfig c = parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=proportional\n");
		c.glyphOverrides[0xA1B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E01);
		c.glyphOverrides[0xA2B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E48);

		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		TS_ASSERT(addFont(hr, 0, false, svfn(thai, 2, 16, 12, true), "th.fnt"));

		Graphics::UnicodeGlyphSource *src = hr.sourceFor(0, false);
		Graphics::GlyphMetrics m;
		TS_ASSERT(src && src->metrics(0x0E48, m));
		TS_ASSERT_EQUALS(m.originX, 6);
		TS_ASSERT(m.combining);

		TS_ASSERT_EQUALS(hr.advanceFor(0xA2B1, 0, 6), 0);
		int carry = 1;
		TS_ASSERT_EQUALS(hr.advanceFor(0xA2B1, 0, 6, &carry), 0);
		TS_ASSERT_EQUALS(carry, 1);	// the pen, carry included, is unchanged
		TS_ASSERT_EQUALS(hr.advanceFor(0xA1B1, 0, 6), 6);

		Graphics::Surface base, mark;
		clear(base, 96, 40);
		clear(mark, 96, 40);
		TS_ASSERT(hr.drawChar(base, 0xA1B1, 0, 20, 4, kInk, 0, 1));
		// The pen the engine hands over is its own, rounded in game px; the
		// mark is placed from the base's hi-res anchor (20 + 12) regardless.
		TS_ASSERT(hr.drawChar(mark, 0xA2B1, 0, 34, 4, kInk, 0, 1));
		int l, r;
		TS_ASSERT(inkSpan(mark, l, r));
		TS_ASSERT_EQUALS(l, 26);
		TS_ASSERT_EQUALS(r, 29);
		TS_ASSERT(inkSpan(base, l, r));
		TS_ASSERT_EQUALS(l, 21);
		TS_ASSERT_EQUALS(r, 30);

		// The same file without flags bit 2 is an old one: its rows start at
		// the pen, so originX stays 0 there (test/graphics/hires_text_glyph_metrics.h).
		Graphics::HiResBitmapFont *old = new Graphics::HiResBitmapFont();
		const Common::Array<byte> oldBytes = svfn(thai, 2, 16, 12, false);
		Common::MemoryReadStream os(oldBytes.begin(), oldBytes.size());
		TS_ASSERT(old->load(os));
		TS_ASSERT(!old->marksAtOrigin());
		Graphics::SvfnGlyphSource oldSrc(old, DisposeAfterUse::YES);
		TS_ASSERT(oldSrc.metrics(0x0E48, m));
		TS_ASSERT_EQUALS(m.originX, 0);

		base.free();
		mark.free();
	}

	/// The anchor a mark attaches to is the base just drawn by this layer on
	/// this line and in this string - not an older one.
	void test_mark_anchor_is_reset() {
		const G thai[] = {
			{ 0x0E01, 12, 1, 10, 1, 11, 6, 14 },
			{ 0x0E48, 0, -6, 4, 0, 4, 0, 4 },
		};
		Graphics::HiResTextConfig c = parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=proportional\n");
		c.glyphOverrides[0xA1B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E01);
		c.glyphOverrides[0xA2B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E48);
		c.glyphOverrides[0xA3B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphKeep, 0);

		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		TS_ASSERT(addFont(hr, 0, false, svfn(thai, 2, 16, 12, true), "th.fnt"));

		Graphics::Surface scratch, mark;
		clear(scratch, 96, 40);
		int l, r;

		// A base the layer declined (the game drew it): the mark stays at
		// the engine's pen, not on the base before that one.
		TS_ASSERT(hr.drawChar(scratch, 0xA1B1, 0, 4, 4, kInk, 0, 1));
		TS_ASSERT(!hr.drawChar(scratch, 0xA3B1, 0, 30, 4, kInk, 0, 1));
		TS_ASSERT(!hr.drawChar(scratch, 'Z', 0, 44, 4, kInk, 0, 1));	// no glyph: declined
		clear(mark, 96, 40);
		TS_ASSERT(hr.drawChar(mark, 0xA2B1, 0, 60, 4, kInk, 0, 1));
		TS_ASSERT(inkSpan(mark, l, r));
		TS_ASSERT_EQUALS(l, 54);
		mark.free();

		// A new string on the same line starts with no base.
		TS_ASSERT(hr.drawChar(scratch, 0xA1B1, 0, 4, 4, kInk, 0, 1));
		hr.beginString();
		clear(mark, 96, 40);
		TS_ASSERT(hr.drawChar(mark, 0xA2B1, 0, 60, 4, kInk, 0, 1));
		TS_ASSERT(inkSpan(mark, l, r));
		TS_ASSERT_EQUALS(l, 54);
		mark.free();

		// A base on another line is not this mark's base.
		TS_ASSERT(hr.drawChar(scratch, 0xA1B1, 0, 4, 4, kInk, 0, 1));
		clear(mark, 96, 40);
		TS_ASSERT(hr.drawChar(mark, 0xA2B1, 0, 60, 20, kInk, 0, 1));
		TS_ASSERT(inkSpan(mark, l, r));
		TS_ASSERT_EQUALS(l, 54);
		mark.free();
		scratch.free();
	}

	/// Under per-glyph placement ASCII is drawn by the replacement unless the
	/// map says off: SCUMM's own behaviour is [latin] mode=proportional.
	void test_latin_default_is_proportional() {
		const G a[] = { { 0x41, 13, 0, 12, 0, 12, 2, 14 } };
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[font.3]\nsize=20\n"));
		TS_ASSERT(hr.perGlyphMetrics());
		TS_ASSERT(addFont(hr, 0, true, svfn(a, 1, 16, 12, false), "lat.fnt"));
		Graphics::Surface dest;
		clear(dest, 96, 40);
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 2, 2, kInk, 0, 1));
		dest.free();
		// metrics follow [render] (game): the game's width.
		TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 4), 4);

		// [latin] enabled=false is off.
		Scumm::ScummHiResText off;
		off.useOverlay(&overlay);
		off.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[font.3]\nsize=20\n[latin]\nenabled=false\n"));
		TS_ASSERT(addFont(off, 0, true, svfn(a, 1, 16, 12, false), "lat.fnt"));
		clear(dest, 96, 40);
		TS_ASSERT(!off.drawChar(dest, 'A', 0, 2, 2, kInk, 0, 1));
		dest.free();
	}

	/// [latin] space= alone switches per-glyph placement on, as mode= does.
	void test_latin_space_turns_per_glyph_on() {
		Scumm::ScummHiResText a, b, none;
		a.adoptConfig(parse("[hires]\nscale=2\n[latin]\nspace=fullwidth\n"));
		b.adoptConfig(parse("[hires]\nscale=2\n[latin]\nmode=half\n"));
		none.adoptConfig(parse("[hires]\nscale=2\n[latin]\nmetrics=font\n"));
		TS_ASSERT(a.perGlyphMetrics());
		TS_ASSERT(b.perGlyphMetrics());
		TS_ASSERT(!none.perGlyphMetrics());
	}

	/// [latin] mode=half: ASCII from the face at its narrow cell (half the
	/// SVFN cell), whatever the game's width or the glyph's own advance.
	void test_latin_half_advances_by_narrow_cell() {
		const G a[] = { { 0x41, 13, 0, 12, 0, 12, 2, 14 } };
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=half\nmetrics=game\n"));
		TS_ASSERT(addFont(hr, 0, true, svfn(a, 1, 16, 12, false), "lat.fnt"));
		TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 6), 4);
		TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 2), 4);
		Graphics::Surface dest;
		clear(dest, 96, 40);
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 2, 2, kInk, 0, 1));
		dest.free();
	}

	/// [latin] mode=fullwidth: ASCII drawn as U+FF01..U+FF5E; a space stays
	/// the game's unless space=fullwidth makes it U+3000.
	void test_latin_fullwidth_remaps() {
		const G fw[] = {
			{ 0xFF21, 16, 0, 14, 1, 15, 2, 14 },
			{ 0x3000, 16, 0, 0, 0, 0, 0, 0 },
		};
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Graphics::Surface dest;
		{
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=fullwidth\n"));
			TS_ASSERT(addFont(hr, 0, false, svfn(fw, 2, 16, 12, false), "fw.fnt"));
			clear(dest, 96, 40);
			TS_ASSERT(hr.drawChar(dest, 'A', 0, 2, 2, kInk, 0, 1));	// the font has only U+FF21
			TS_ASSERT(!hr.drawChar(dest, 'B', 0, 40, 2, kInk, 0, 1));	// no U+FF22: the game's
			dest.free();
			TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 3), 8);		// the wide cell rule
			TS_ASSERT_EQUALS(hr.advanceFor(' ', 0, 3), 3);		// space=keep
		}
		{
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=fullwidth\nspace=fullwidth\n"));
			TS_ASSERT(addFont(hr, 0, false, svfn(fw, 2, 16, 12, false), "fw.fnt"));
			TS_ASSERT_EQUALS(hr.advanceFor(' ', 0, 3), 8);		// U+3000
		}
	}

	/// metrics=game: a glyph narrower than its game cell is centred in it.
	void test_metrics_game_centres_narrow_glyph() {
		const G a[] = { { 0x41, 10, 0, 10, 0, 10, 2, 14 } };
		const Common::Array<byte> font = svfn(a, 1, 16, 12, false);

		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Graphics::Surface dest;
		int l, r;

		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=proportional\nmetrics=game\n"));
		TS_ASSERT(addFont(hr, 0, true, font, "lat.fnt"));
		clear(dest, 96, 40);
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 40, 0, kInk, 0, 1, nullptr, true, 8));
		TS_ASSERT(inkSpan(dest, l, r));
		TS_ASSERT_EQUALS(l, 43);
		dest.free();

		// A map with none of the new keys draws where it always did.
		Graphics::HiResTextConfig legacy = parse("[hires]\nscale=2\nalpha=true\n[latin]\nbitmap=lat.fnt\n");
		Scumm::ScummHiResText old;
		old.useOverlay(&overlay);
		old.adoptConfig(legacy);
		TS_ASSERT(!old.perGlyphMetrics());
		TS_ASSERT(addFont(old, 0, true, font, "lat.fnt"));
		clear(dest, 96, 40);
		TS_ASSERT(old.drawChar(dest, 'A', 0, 40, 0, kInk, 0, 1, nullptr, true, 8));
		TS_ASSERT(inkSpan(dest, l, r));
		TS_ASSERT_EQUALS(l, 40);
		dest.free();
	}

	/// The MI2 example map in engines/scumm/HIRES_TEXT.md, verbatim.
	void test_mi2_keeps_5c_60() {
		static const char *const kMi2Map =
			"; Monkey Island 2 (DOS) with the Korean korean.trs patch\n"
			"[hires]\n"
			"scale=2\n"
			"alpha=true\n"
			"\n"
			"[encoding]\n"
			"codepage=cp949\n"
			"\n"
			"[fonts]\n"
			"default=/System/Library/Fonts/AppleSDGothicNeo.ttc\n"
			"\n"
			"; MI2's own pictograms: the skull bullet at 0x07 and the ellipsis at 0x5e.\n"
			"; The Korean patch draws \"!\" at 0x5c and a quote mark at 0x60 from its\n"
			"; own charset, so a replacement face must not draw '\\' and '`' there.\n"
			"[glyphs]\n"
			"0x07=keep\n"
			"0x5e=keep\n"
			"0x5c=keep\n"
			"0x60=keep\n";
		Graphics::HiResTextConfig c = parse(kMi2Map);
		Graphics::HiResGlyphOverride o;
		TS_ASSERT(c.glyphOverride(0x5c, o));
		TS_ASSERT_EQUALS((int)o.action, (int)Graphics::kHiResGlyphKeep);
		TS_ASSERT(c.glyphOverride(0x60, o));
		TS_ASSERT_EQUALS((int)o.action, (int)Graphics::kHiResGlyphKeep);
		TS_ASSERT(c.glyphOverride(0x07, o));
		TS_ASSERT(c.glyphOverride(0x5e, o));
		TS_ASSERT(!c.glyphOverride(0x41, o));
		TS_ASSERT_EQUALS(c.encoding, Common::kWindows949);
	}

	/// face=A, B: U+0E01, which A lacks, is drawn from B.
	void test_chain_answers_by_coverage() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		const char *thai = sukhumvit();
		if (!apple || !thai) {
			TS_SKIP("needs Apple SD Gothic Neo and a Sukhumvit face");
			return;
		}
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Graphics::Surface dest;

		// Apple SD Gothic Neo alone declines U+0E01.
		{
			Graphics::HiResTextConfig c = parse(Common::String::format(
				"[hires]\nscale=2\nalpha=true\nface=%s\n", apple).c_str());
			c.glyphOverrides[0xA1B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E01);
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(c);
			hr.setGameFontCell(0, 8, 8);
			TS_ASSERT(hr.loadFonts(Common::Path()));
			clear(dest, 96, 40);
			TS_ASSERT(!hr.drawChar(dest, 0xA1B1, 0, 2, 2, kInk, 0, 1));
			dest.free();
		}
		Graphics::HiResTextConfig c = parse(Common::String::format(
			"[hires]\nscale=2\nalpha=true\nface=%s, %s\n", apple, thai).c_str());
		TS_ASSERT_EQUALS(c.hiresFaceChain.size(), 2U);
		c.glyphOverrides[0xA1B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E01);
		c.glyphOverrides[0xA1B0] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0xAC00);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.setGameFontCell(0, 8, 8);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		Graphics::UnicodeGlyphSource *src = hr.sourceFor(0, false);
		TS_ASSERT(src && src->cells(0x0E01) == 1);
		TS_ASSERT(src && src->cells(0xAC00) == 2);
		clear(dest, 96, 40);
		TS_ASSERT(hr.drawChar(dest, 0xA1B1, 0, 2, 2, kInk, 0, 1));
		int l, r;
		TS_ASSERT(inkSpan(dest, l, r));
		TS_ASSERT(hr.drawChar(dest, 0xA1B0, 0, 40, 2, kInk, 0, 1));
		dest.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// HiResFontBaker writes a combining mark with the pen at max(0, -left)
	/// (FONT_FORMAT.md section 3, flags bit 2), so a baked Thai font keeps the
	/// ink left of the pen and SCUMM draws the mark over its base.
	void test_baked_thai_marks_sit_on_their_bases() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *thai = sukhumvit();
		if (!thai) {
			TS_SKIP("no Sukhumvit face on this machine");
			return;
		}
		Common::FSNode node(thai);
		Common::SeekableReadStream *stream = node.createReadStream();
		TS_ASSERT(stream);
		if (!stream)
			return;
		Graphics::Font *face = Graphics::loadTTFFont(stream, DisposeAfterUse::YES, 24);
		TS_ASSERT(face);
		if (!face)
			return;
		const Common::Rect markBox = face->getBoundingBox(0x0E48);
		TS_ASSERT(markBox.left < 0);

		Common::Array<uint32> cps;
		cps.push_back(0x0E01);
		cps.push_back(0x0E48);
		Common::Array<byte> bytes;
		TS_ASSERT(Graphics::HiResFontBaker::bake(*face, cps, 32, 32, true, bytes));
		delete face;

		Graphics::HiResBitmapFont *font = new Graphics::HiResBitmapFont();
		{
			Common::MemoryReadStream s(bytes.begin(), bytes.size());
			TS_ASSERT(font->load(s));
		}
		TS_ASSERT(font->marksAtOrigin());
		{
			Graphics::SvfnGlyphSource src(font, DisposeAfterUse::NO);
			Graphics::GlyphMetrics m;
			TS_ASSERT(src.metrics(0x0E48, m));
			TS_ASSERT_EQUALS(m.originX, -markBox.left);
			TS_ASSERT_EQUALS(m.advance, 0);
			// Nothing of the mark was clipped: its ink is as wide as the box.
			int lo = 64, hi = -1;
			for (int y = 0; y < src.cellHeight(); y++) {
				const byte *row = src.row(0x0E48, y);
				for (int x = 0; row && x < src.cellWidth(); x++)
					if (row[x] >= 128) {
						lo = MIN(lo, x);
						hi = MAX(hi, x);
					}
			}
			TS_ASSERT(hi >= 0);
			TS_ASSERT(hi - lo + 1 >= markBox.width() - 2);
			TS_ASSERT(lo <= 1);
			// Latin-style glyphs are unchanged: the base keeps originX 0.
			TS_ASSERT(src.metrics(0x0E01, m));
			TS_ASSERT_EQUALS(m.originX, 0);
		}
		delete font;

		Graphics::HiResTextConfig c = parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=proportional\n");
		c.glyphOverrides[0xA1B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E01);
		c.glyphOverrides[0xA2B1] = Graphics::HiResGlyphOverride(Graphics::kHiResGlyphRemap, 0x0E48);
		Scumm::HiResOverlay overlay;
		overlay.create(128, 48, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		TS_ASSERT(addFont(hr, 0, false, bytes, "thai24.fnt"));
		Graphics::Surface base, mark;
		clear(base, 128, 48);
		clear(mark, 128, 48);
		const int adv = hr.advanceFor(0xA1B1, 0, 8);
		TS_ASSERT(hr.drawChar(base, 0xA1B1, 0, 30, 4, kInk, 0, 1));
		TS_ASSERT(hr.drawChar(mark, 0xA2B1, 0, 30 + adv * kScale, 4, kInk, 0, 1));
		int bl, br, ml, mr;
		TS_ASSERT(inkSpan(base, bl, br));
		TS_ASSERT(inkSpan(mark, ml, mr));
		// The mark is over the base, not after it.
		TS_ASSERT(ml >= bl - 1);
		TS_ASSERT(mr <= br + 1);
		base.free();
		mark.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// A Latin SVFN beside a TrueType double-byte face sits on the face's
	/// baseline, as it sat on the start-up bake's before the bake went.
	void test_latin_svfn_on_ttf_baseline() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		if (!apple) {
			TS_SKIP("Apple SD Gothic Neo not present on this machine");
			return;
		}
		Graphics::HiResTextConfig c;
		c.scale = 2;
		c.alpha = true;
		c.encoding = Common::kWindows949;
		const G a[] = { { 0x41, 10, 0, 10, 0, 10, 2, 5 } };
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.setTtfFace(Common::Path(apple, '/'));
		hr.setGameFontCell(0, 8, 8);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		// After loadFonts(), which starts from no fonts at all.
		TS_ASSERT(addFont(hr, 0, true, svfn(a, 1, 16, 5, false), "lat.fnt"));
		const Graphics::TtfGlyphSource *ttf =
			static_cast<const Graphics::TtfGlyphSource *>(hr.sourceFor(0, false));
		TS_ASSERT(ttf);
		if (!ttf)
			return;
		TS_ASSERT(ttf->baseline() > 5);
		Graphics::Surface dest;
		clear(dest, 96, 40);
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 2, 0, kInk, 0, 1));
		TS_ASSERT_EQUALS(firstInkRow(dest), 2 + ttf->baseline() - 5);
		dest.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	/// With a translation noted, each face of the chain is checked against
	/// its code points; a face lacking some is warned about, once.
	// C11-T3c: the translation's code points are read past every escape by
	// the shared rule (escapeArgBytes()): codes 4-7 and 9 take two argument
	// bytes too, and an argument of 0 does not end the string.
	void test_note_translated_string_skips_every_escape_argument() {
		Graphics::HiResTextConfig c = parse("[hires]\nscale=2\n");
		c.encoding = Common::kUtf8;
		Scumm::ScummHiResText hr;
		hr.adoptConfig(c);
		static const byte kText[] = {
			0xFF, 0x04, 0x00, 0x00, 0xEA, 0xB0, 0x80, 'a',	// FF 04 00 00, U+AC00 a
			0xFF, 0x05, 'Q', 'R', 'b',			// FF 05 Q R, b
			0xFF, 0x06, 'S', 'T', 0xFF, 0x07, 'U', 'V', 'c',	// FF 06 S T, FF 07 U V, c
			0xFF, 0x09, 'W', 0x00, 'd',			// FF 09 W 00, d
			0xFF, 0x01, 'e',				// FF 01 (no arguments), e
			0xFF, 0x0E, 'X', 'Y', '@', 'f', 0x00, 'Z'
		};
		hr.noteTranslatedString(kText, sizeof(kText));
		const Graphics::CodePointSet &cps = hr.translationCodePoints();
		const uint32 want[] = { 0xAC00, 'a', 'b', 'c', 'd', 'e', 'f' };
		for (uint i = 0; i < ARRAYSIZE(want); i++)
			TS_ASSERT(cps.contains(want[i]));
		TS_ASSERT_EQUALS(cps.size(), (uint32)ARRAYSIZE(want));
	}

	void test_coverage_warning_per_face() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *apple = appleGothic();
		const char *thai = sukhumvit();
		if (!apple || !thai) {
			TS_SKIP("needs Apple SD Gothic Neo and a Sukhumvit face");
			return;
		}
		static const char kText[] = "\xE0\xB8\x81\xE0\xB8\xB5\xE0\xB9\x88 \xEA\xB0\x80 abc";
		Scumm::HiResOverlay overlay;
		overlay.create(96, 40, true);

		Graphics::HiResTextConfig c = parse(Common::String::format(
			"[hires]\nscale=2\nalpha=true\nface=%s, %s\n", apple, thai).c_str());
		c.encoding = Common::kUtf8;
		{
			// No translation: no check, no warning.
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(c);
			hr.setGameFontCell(0, 8, 8);
			TS_ASSERT(hr.loadFonts(Common::Path()));
			TS_ASSERT(hr.coverageWarnings().empty());
		}
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(c);
		hr.noteTranslatedString((const byte *)kText, sizeof(kText) - 1);
		hr.setGameFontCell(0, 8, 8);
		TS_ASSERT(hr.loadFonts(Common::Path()));
		const Common::Array<Common::String> &w = hr.coverageWarnings();
		TS_ASSERT_EQUALS(w.size(), 1U);
		if (w.size() == 1) {
			TS_ASSERT(w[0].contains("AppleSDGothicNeo"));
			TS_ASSERT(w[0].contains("lacks 3 of"));
			TS_ASSERT(w[0].contains("U+0E01"));
		}
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}
};
