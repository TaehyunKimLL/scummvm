#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/surface.h"

#include "engines/scumm/hires_overlay.h"
#include "engines/scumm/hires_text.h"

/**
 * Mirrored charsets (C27).
 *
 * MI1, MI2 and Loom CD have a charset 3 whose glyphs are the normal ones
 * turned half a turn; MI1 draws the dazed dialogue choices in the Fettucini
 * brothers' tent with it ("?rehtom ym uoy erA  .nibboB m'I", stored
 * reversed). By default such a charset keeps the game's own font; a map
 * asks for a replacement with [font.N] face= or mirror=, and mirror= says
 * how the replacement's glyphs are flipped.
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

	static Graphics::HiResTextConfig parse(const char *text) {
		Graphics::HiResTextConfig c;
		Common::Array<Common::String> qualifiers;
		Common::MemoryReadStream stream((const byte *)text, strlen(text));
		TS_ASSERT(Graphics::HiResFontMap::loadFromStream(stream, Common::Path("/tmp/c27", '/'), qualifiers, c));
		return c;
	}

	static bool addFont(Scumm::ScummHiResText &hr, int cs, bool latin, const Common::Array<byte> &bytes) {
		Common::MemoryReadStream s(bytes.begin(), bytes.size());
		return hr.addBitmapFont(cs, latin, s, latin ? "lat.fnt" : "cjk.fnt");
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

	static const Graphics::HiResFontIdSettings *font3(const Graphics::HiResTextConfig &c) {
		return c.fontIdSettings(3);
	}

public:
	/// The table: charset 3 of MI1 (EGA/VGA/CD), MI2 and Loom CD, nothing else.
	void test_game_table() {
		using Scumm::ScummHiResText;
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 5, 3), Graphics::kHiResMirrorBoth);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 4, 3), Graphics::kHiResMirrorBoth);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey2", 5, 3), Graphics::kHiResMirrorBoth);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("loom", 4, 3), Graphics::kHiResMirrorBoth);
		// Loom EGA (v3) has a different charset set.
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("loom", 3, 3), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 5, 2), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("monkey", 5, 4), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("tentacle", 6, 3), Graphics::kHiResMirrorNone);
		TS_ASSERT_EQUALS(ScummHiResText::gameMirror("atlantis", 5, 3), Graphics::kHiResMirrorNone);
	}

	/// mirror= wins; true means "as the game's font does", or horizontal
	/// for a charset the table does not know; unset follows the table.
	void test_resolve_mirror() {
		using Scumm::ScummHiResText;
		const Graphics::HiResMirror both = Graphics::kHiResMirrorBoth, none = Graphics::kHiResMirrorNone;
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(nullptr, both), both);
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(nullptr, none), none);

		Graphics::HiResTextConfig t = parse("[font.3]\nmirror=true\n");
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(font3(t), both), both);
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(font3(t), none), Graphics::kHiResMirrorHorizontal);

		Graphics::HiResTextConfig f = parse("[font.3]\nmirror=false\n");
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(font3(f), both), none);

		Graphics::HiResTextConfig v = parse("[font.3]\nmirror=vertical\n");
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(font3(v), both), Graphics::kHiResMirrorVertical);

		// face= alone replaces the font and keeps the game's orientation.
		Graphics::HiResTextConfig face = parse("[font.3]\nface=/tmp/c27/x.ttf\n");
		TS_ASSERT_EQUALS(ScummHiResText::resolveMirror(font3(face), both), both);
	}

	/// The default keeps the game's font, for every character it can draw.
	void test_keeps_game_font() {
		using Scumm::ScummHiResText;
		const Graphics::HiResMirror both = Graphics::kHiResMirrorBoth, none = Graphics::kHiResMirrorNone;
		TS_ASSERT(ScummHiResText::keepsGameFont(nullptr, both, false, 'a'));
		// A code-page pair: the Korean patch's own font draws it.
		TS_ASSERT(ScummHiResText::keepsGameFont(nullptr, both, false, 0xA1B0));
		TS_ASSERT(ScummHiResText::keepsGameFont(nullptr, both, true, 'a'));
		// UTF-8 text beyond ASCII has no game glyph: the replacement draws it.
		TS_ASSERT(!ScummHiResText::keepsGameFont(nullptr, both, true, 0xAC00));
		TS_ASSERT(!ScummHiResText::keepsGameFont(nullptr, none, false, 'a'));

		Graphics::HiResTextConfig t = parse("[font.3]\nmirror=true\n");
		Graphics::HiResTextConfig f = parse("[font.3]\nmirror=false\n");
		Graphics::HiResTextConfig face = parse("[font.3]\nface=/tmp/c27/x.ttf\n");
		Graphics::HiResTextConfig size = parse("[font.3]\nsize=20\n");
		TS_ASSERT(!ScummHiResText::keepsGameFont(font3(t), both, false, 'a'));
		TS_ASSERT(!ScummHiResText::keepsGameFont(font3(f), both, false, 'a'));
		TS_ASSERT(!ScummHiResText::keepsGameFont(font3(face), both, false, 'a'));
		// A key that names no face and no mirror does not ask for one.
		TS_ASSERT(ScummHiResText::keepsGameFont(font3(size), both, false, 'a'));
	}

	/// mirror= alone does not switch per-glyph placement on for the whole map.
	void test_mirror_only_section_keeps_legacy_placement() {
		Scumm::ScummHiResText a, b;
		a.adoptConfig(parse("[hires]\nscale=2\n[font.3]\nmirror=true\n"));
		b.adoptConfig(parse("[hires]\nscale=2\n[font.3]\nsize=20\nmirror=true\n"));
		TS_ASSERT(!a.perGlyphMetrics());
		TS_ASSERT(b.perGlyphMetrics());
	}

	/// With no map key the table's charset declines: the game draws it and
	/// lays it out, and the other charsets are unchanged.
	void test_default_declines_mirrored_charset() {
		const G a[] = { { 0x41, 12, 0, 10, 2, 12, 2, 14 } };
		Scumm::HiResOverlay overlay;
		overlay.create(64, 40, true);
		Scumm::ScummHiResText hr;
		hr.useOverlay(&overlay);
		hr.adoptConfig(parse("[hires]\nscale=2\nalpha=true\n[latin]\nmode=proportional\nmetrics=font\n"));
		hr.setGameMirror("monkey", 5);
		TS_ASSERT(addFont(hr, 0, true, svfn(a, 1)));

		Graphics::Surface dest;
		clear(dest);
		TS_ASSERT(!hr.drawChar(dest, 'A', 3, 2, 2, kInk, 0, 1));
		TS_ASSERT_EQUALS(inkCount(dest), 0);
		TS_ASSERT_EQUALS(hr.advanceFor('A', 3, 5), 5);
		// Charset 0 is not mirrored and still takes the replacement.
		TS_ASSERT(hr.drawChar(dest, 'A', 0, 2, 2, kInk, 0, 1));
		TS_ASSERT_EQUALS(hr.advanceFor('A', 0, 5), 6);
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
			hr.adoptConfig(parse(pass ? "[hires]\nscale=2\nalpha=true\n[font.3]\nmirror=both\nmetrics=font\n"
									  : "[hires]\nscale=2\nalpha=true\n[font.3]\nmirror=false\nmetrics=font\n"));
			hr.setGameMirror("monkey", 5);
			hr.useUtf8Text();
			TS_ASSERT(addFont(hr, 3, false, svfn(g, 2)));
			TS_ASSERT(addFont(hr, 3, true, svfn(g, 2)));
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

	/// mirror=true on a charset the table does not know flips across only;
	/// a section holding only mirror= keeps the map's legacy placement.
	void test_mirror_horizontal() {
		const G g[] = { { 0x41, 12, 0, 10, 2, 12, 2, 14 } };
		Graphics::Surface normal, mirrored;
		for (int pass = 0; pass < 2; ++pass) {
			Scumm::HiResOverlay overlay;
			overlay.create(64, 40, true);
			Scumm::ScummHiResText hr;
			hr.useOverlay(&overlay);
			hr.adoptConfig(parse(pass ? "[hires]\nscale=2\nalpha=true\n[font.5]\nmirror=true\n"
									  : "[hires]\nscale=2\nalpha=true\n"));
			TS_ASSERT(!hr.perGlyphMetrics());
			TS_ASSERT(addFont(hr, 5, true, svfn(g, 1)));
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
