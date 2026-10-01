#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

#include "support/hires_fixture.h"

/**
 * Mirrored charsets (C27).
 *
 * MI1, MI2 and Loom CD have a charset 3 whose glyphs are the normal ones
 * turned half a turn; MI1 draws the dazed dialogue choices in the Fettucini
 * brothers' tent with it ("?rehtom ym uoy erA  .nibboB m'I", stored
 * reversed). By default such a charset keeps the game's own font; a map
 * asks for a replacement with [font.N] face= or mirror=, and mirror= says
 * how the replacement's glyphs are flipped.
 *
 * A version-2 map always uses per-glyph placement, so mirror= alone needs
 * nothing more to take effect.
 */
class ScummHiResMirrorTestSuite : public CxxTest::TestSuite {
private:
	static const byte kInk = 15;
	static const int kCell = 16;

	struct G {
		uint32 cp;
		int advance, bearing, inkWidth;
		int x0, x1, y0, y1;
	};

	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		for (int i = 0; i < 4; i++)
			b[pos + i] = (v >> (8 * i)) & 0xff;
	}

	/// A proportional 8bpp SVFN; each glyph's ink is a rectangle plus, to
	/// make it asymmetric both ways, a bar along its bottom-left.
	static Common::Array<byte> svfn(const G *g, int n) {
		const int cell = kCell;
		const uint32 stride = cell * cell;
		const uint32 metricsOff = 36, dataOff = metricsOff + n * 4;
		const uint32 cmapOff = dataOff + stride * n;
		Common::Array<byte> b;
		b.resize(cmapOff + n * 8, 0);
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		put16(b, 6, 1 | 4);	// proportional, marks hold the pen at -bearing
		b[8] = 8;
		put16(b, 12, n);
		b[14] = cell;
		b[15] = cell;
		b[16] = 12;
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
					if (x < g[i].x0 + 2 || y == g[i].y1 - 1)
						b[dataOff + i * stride + y * cell + x] = 255;
			put32(b, cmapOff + i * 8, g[i].cp);
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

	static Graphics::HiResMap parse(const char *body) {
		Graphics::HiResMap m;
		const Common::String text = Common::String("[map]\nversion=2\n") + body;
		Common::Array<Common::String> q;
		Common::MemoryReadStream stream((const byte *)text.c_str(), text.size());
		TS_ASSERT(Graphics::HiResFontMap::loadMap(stream, Common::Path("/tmp/c27", '/'), q, Graphics::kHiResKeysScumm, m));
		return m;
	}

	static Graphics::HiResIdPlan plan3(const Graphics::HiResMap &m) {
		Common::Array<Common::String> w;
		return Graphics::compileIdPlan(m, true, 3, Graphics::HiResIniOverrides(), Scumm::ScummHiResText::engineScope(),
									   Common::Path("/tmp/c27", '/'), Common::Path("/games/g", '/'), w);
	}

	static bool addFont(Scumm::ScummHiResText &hr, const char *path, const Common::Array<byte> &bytes) {
		Common::MemoryReadStream s(bytes.begin(), bytes.size());
		return hr.addFace(path, s);
	}

	static void clear(Graphics::Surface &s) {
		s.create(64, 40, Graphics::PixelFormat::createFormatCLUT8());
		memset(s.getPixels(), 0, 64 * 40);
	}

	static bool ink(const Graphics::Surface &s, int x, int y) {
		if (x < 0 || y < 0 || x >= s.w || y >= s.h)
			return false;
		return *(const byte *)s.getBasePtr(x, y) != 0;
	}

	static int inkCount(const Graphics::Surface &s) {
		int n = 0;
		for (int y = 0; y < s.h; ++y)
			for (int x = 0; x < s.w; ++x)
				n += ink(s, x, y);
		return n;
	}

public:
	void setUp() { ScummHiResFixture::setUp(); }
	void tearDown() { ScummHiResFixture::tearDown(); }

	/// The table: charset 3 of MI1 (EGA/VGA/CD), MI2 and Loom CD, nothing else.
	void test_game_table() {
		using Scumm::ScummHiResText;
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 5, 3), Graphics::kHiResMirrorBoth);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 4, 3), Graphics::kHiResMirrorBoth);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey2", 5, 3), Graphics::kHiResMirrorBoth);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("loom", 4, 3), Graphics::kHiResMirrorBoth);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("loom", 3, 3), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 5, 2), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 5, 4), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("tentacle", 6, 3), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("atlantis", 5, 3), Graphics::kHiResMirrorNone);
	}

	/// mirror= wins; kHiResMirrorGame means "as the game's font does", or
	/// horizontal for a charset the table does not know; unset follows the
	/// table. No map can produce kHiResMirrorGame any more (the map's
	/// grammar has no `true`); it is a dead value kept until a cleanup
	/// removes it with resolveMirror()'s branch for it.
	void test_resolve_mirror() {
		using Scumm::ScummHiResText;
		const Graphics::HiResMirror both = Graphics::kHiResMirrorBoth, none = Graphics::kHiResMirrorNone;
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(false, Graphics::kHiResMirrorNone, both), both);
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(false, Graphics::kHiResMirrorNone, none), none);

		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(true, Graphics::kHiResMirrorGame, both), both);
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(true, Graphics::kHiResMirrorGame, none), Graphics::kHiResMirrorHorizontal);

		const Graphics::HiResIdPlan f = plan3(parse("[font.3]\nmirror=off\n"));
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(f.mirrorSet, f.mirror, both), none);

		const Graphics::HiResIdPlan v = plan3(parse("[font.3]\nmirror=vertical\n"));
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(v.mirrorSet, v.mirror, both), Graphics::kHiResMirrorVertical);
	}

	/// The default keeps the game's font, for every character it can draw.
	void test_keeps_game_font() {
		using Scumm::ScummHiResText;
		const Graphics::HiResMirror both = Graphics::kHiResMirrorBoth, none = Graphics::kHiResMirrorNone;
		TS_ASSERT(ScummHiResText::keepsGameFont(false, both, false, 'a'));
		// A code-page pair: the Korean patch's own font draws it.
		TS_ASSERT(ScummHiResText::keepsGameFont(false, both, false, 0xA1B0));
		TS_ASSERT(ScummHiResText::keepsGameFont(false, both, true, 'a'));
		// UTF-8 text beyond ASCII has no game glyph: the replacement draws it.
		TS_ASSERT(!ScummHiResText::keepsGameFont(false, both, true, 0xAC00));
		TS_ASSERT(!ScummHiResText::keepsGameFont(false, none, false, 'a'));

		const Graphics::HiResIdPlan t = plan3(parse("[font.3]\nmirror=horizontal\n"));
		const Graphics::HiResIdPlan face = plan3(parse("[font.3]\nface=own.ttf\n"));
		const Graphics::HiResIdPlan size = plan3(parse("[font.3]\nsize=20\n"));
		TS_ASSERT(!ScummHiResText::keepsGameFont(t.mirrorSet, both, false, 'a'));
		TS_ASSERT(!ScummHiResText::keepsGameFont(!face.idChain.faces.empty(), both, false, 'a'));
		// A key that names no face and no mirror does not ask for one.
		TS_ASSERT(ScummHiResText::keepsGameFont(size.mirrorSet || !size.idChain.faces.empty(), both, false, 'a'));
	}

	/// With no map key the table's charset declines: the game draws it and
	/// lays it out, and other charsets are unchanged.
	void test_default_declines_mirrored_charset() {
		const G a[] = { { 0x41, 12, 0, 10, 2, 12, 2, 14 } };
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptMap(parse("[render]\nblend=off\n[font.0]\nface=/tmp/c27/a.svf\n[font]\nadvance.basic-latin=font\n"));
		hr.setGameMirror("monkey", 5);
		hr.noteGameCharset(0, 8, 8);
		hr.setCharsetGrid(0, 8, 8);
		TS_ASSERT(addFont(hr, "/tmp/c27/a.svf", svfn(a, 1)));

		Graphics::Surface dest;
		clear(dest);
		TS_ASSERT(!hr.drawChar(dest, 'A', 3, 2, 2, kInk, 0, 1));
		TS_ASSERT_EQUALS(inkCount(dest), 0);
		TS_ASSERT_EQUALS(hr.advanceFor('A', 3, 5), 5);
		// Charset 0 is not mirrored and still takes the replacement.
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 2, 2, kInk, 0, 1));
		dest.free();
	}

	/**
	 * mirror=both: each glyph turned half a turn inside its own box (the
	 * pen and advance across, the cell down), a combining mark about its
	 * base's box so it stays on it; the string's order is kept.
	 */
	void test_mirror_both_turns_glyph_and_mark_in_place() {
		const G g[] = {
			{ 0x41, 12, 0, 10, 2, 12, 2, 14 },
			// A Thai above mark, drawn left of the pen (marks hold the pen
			// at column -bearing = 8).
			{ 0x0E31, 0, -8, 4, 4, 8, 0, 3 },
		};
		Graphics::Surface normal, mirrored;
		for (int pass = 0; pass < 2; ++pass) {
			Scumm::HiResOverlay overlay;
			overlay.create(64, 40, true);
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptMap(parse(pass ? "[render]\nblend=off\n[font.3]\nface=/tmp/c27/g.svf\nmirror=both\n[font]\nadvance.basic-latin=font\n"
									: "[render]\nblend=off\n[font.3]\nface=/tmp/c27/g.svf\nmirror=off\n[font]\nadvance.basic-latin=font\n"));
			hr.setGameMirror("monkey", 5);
			hr.useUtf8Text();
			hr.noteGameCharset(3, 8, 8);
			hr.setCharsetGrid(3, 8, 8);
			TS_ASSERT(addFont(hr, "/tmp/c27/g.svf", svfn(g, 2)));
			Graphics::Surface &dest = pass ? mirrored : normal;
			clear(dest);
			hr.beginString();
			TS_ASSERT(hr.drawChar(dest, 'A', 3, 10, 2, kInk, 0, 1));
			TS_ASSERT(hr.drawChar(dest, 0x0E31, 3, 16, 2, kInk, 0, 1));
		}
		TS_ASSERT(inkCount(normal) > 0);
		TS_ASSERT_EQUALS(inkCount(normal), inkCount(mirrored));
		// The box: pen 10, advance 12 across; rows 2 .. 2 + 16 down.
		for (int y = 0; y < 40; ++y)
			for (int x = 0; x < 64; ++x)
				if (ink(normal, x, y))
					TS_ASSERT(ink(mirrored, 10 + 22 - 1 - x, 2 + 2 + kCell - 1 - y));
		normal.free();
		mirrored.free();
	}

	/// mirror=horizontal on a charset the table does not know flips across only.
	void test_mirror_horizontal() {
		const G g[] = { { 0x41, 12, 0, 10, 2, 12, 2, 14 } };
		Graphics::Surface normal, mirrored;
		for (int pass = 0; pass < 2; ++pass) {
			Scumm::HiResOverlay overlay;
			overlay.create(64, 40, true);
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptMap(parse(pass ? "[render]\nblend=off\n[font.5]\nface=/tmp/c27/h.svf\nmirror=horizontal\n[font]\nadvance.basic-latin=font\n"
									: "[render]\nblend=off\n[font.5]\nface=/tmp/c27/h.svf\n[font]\nadvance.basic-latin=font\n"));
			hr.noteGameCharset(5, 8, 8);
			hr.setCharsetGrid(5, 8, 8);
			TS_ASSERT(addFont(hr, "/tmp/c27/h.svf", svfn(g, 1)));
			Graphics::Surface &dest = pass ? mirrored : normal;
			clear(dest);
			TS_ASSERT(hr.drawChar(dest, 'A', 5, 10, 2, kInk, 0, 1));
		}
		TS_ASSERT(inkCount(normal) > 0);
		TS_ASSERT_EQUALS(inkCount(normal), inkCount(mirrored));
		for (int y = 0; y < 40; ++y)
			for (int x = 0; x < 64; ++x)
				if (ink(normal, x, y))
					TS_ASSERT(ink(mirrored, 10 + 22 - 1 - x, y));
		normal.free();
		mirrored.free();
	}
};
