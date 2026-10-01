#include <cxxtest/TestSuite.h>

#include "common/memstream.h"
#include "common/str-enc.h"
#include "graphics/hires_text/bitmap_font.h"
#include "graphics/hires_text/text_compose.h"

/**
 * Tests for the hi-res text bitmap font reader.
 *
 * The fixtures are built here rather than shipped, so every field a real font
 * file carries is stated in the test and a header change cannot pass unnoticed.
 */
class HiResBitmapFontTestSuite : public CxxTest::TestSuite {
private:
	static void put16(Common::Array<byte> &b, uint pos, uint16 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
	}

	static void put32(Common::Array<byte> &b, uint pos, uint32 v) {
		b[pos] = v & 0xff;
		b[pos + 1] = (v >> 8) & 0xff;
		b[pos + 2] = (v >> 16) & 0xff;
		b[pos + 3] = (v >> 24) & 0xff;
	}

	/**
	 * A font file in the shipped (version 1) layout.
	 *
	 * @param proportional add a metrics table, i.e. per-glyph advances
	 */
	static Common::Array<byte> makeFont(int bpp, uint16 codePage, int glyphs,
										int cellW, int cellH, int ascent,
										bool proportional) {
		const int rowPitch = (cellW * bpp + 7) / 8;
		const uint32 glyphStride = rowPitch * cellH;
		const uint32 metricsSize = proportional ? glyphs * 4 : 0;
		const uint32 metricsOff = 32;
		const uint32 dataOff = metricsOff + metricsSize;
		const uint32 dataSize = glyphStride * glyphs;

		Common::Array<byte> b;
		b.resize(dataOff + dataSize);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 1);
		put16(b, 6, proportional ? 1 : 0);
		b[8] = bpp;
		b[9] = 0;
		put16(b, 10, codePage);
		put16(b, 12, glyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = ascent;
		put32(b, 20, proportional ? metricsOff : 0);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);

		if (proportional) {
			for (int i = 0; i < glyphs; ++i) {
				b[metricsOff + i * 4 + 0] = (i % 20) + 4;  // advance
				b[metricsOff + i * 4 + 1] = i % 3;         // left bearing
				b[metricsOff + i * 4 + 2] = (i % 20) + 2;  // ink width
			}
		}

		// Make each glyph's first pixel its own index, so a test can tell
		// which glyph it was handed.
		for (int i = 0; i < glyphs; ++i)
			b[dataOff + i * glyphStride] = (byte)(i & 0xff);

		return b;
	}

	static bool loadFont(Graphics::HiResBitmapFont &font, Common::Array<byte> &bytes,
						 uint32 sizeLimit = 64 * 1024 * 1024) {
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());
		return font.load(stream, sizeLimit);
	}

public:
	void test_rejects_the_engines_own_font_format() {
		// The engine's bitmap fonts have no signature and start with a byte
		// that is always 2. Reading one must fail cleanly, because the caller
		// tries this loader first and falls back on a false.
		const byte legacy[] = { 2, 1, 8, 8, 0, 0, 0, 0 };
		Common::MemoryReadStream stream(legacy, sizeof(legacy));

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!font.load(stream));
		TS_ASSERT(!font.isLoaded());
	}

	void test_rejects_a_truncated_file() {
		const byte tiny[] = { 'S', 'V', 'F', 'N', 1, 0 };
		Common::MemoryReadStream stream(tiny, sizeof(tiny));

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!font.load(stream));
	}

	void test_loads_a_fixed_width_font() {
		// The geometry of the Korean fonts shipped today.
		Common::Array<byte> bytes = makeFont(8, 949, 2350, 24, 24, 17, false);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		TS_ASSERT(font.isLoaded());
		TS_ASSERT_EQUALS(font.bpp(), 8);
		TS_ASSERT_EQUALS(font.cellWidth(), 24);
		TS_ASSERT_EQUALS(font.cellHeight(), 24);
		TS_ASSERT_EQUALS(font.ascent(), 17);
		TS_ASSERT_EQUALS(font.glyphCount(), 2350);
		TS_ASSERT(!font.isProportional());
		TS_ASSERT_EQUALS(font.codePage(), Common::kWindows949);
		TS_ASSERT_EQUALS(font.glyphPitch(), 24);
	}

	void test_fixed_width_metrics_come_from_the_cell() {
		Common::Array<byte> bytes = makeFont(8, 949, 16, 24, 24, 17, false);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		Graphics::GlyphMetrics m;
		TS_ASSERT(font.glyphMetrics(3, m));

		// A caller must never have to ask whether the font is proportional:
		// a fixed font answers with its cell.
		TS_ASSERT_EQUALS(m.advance, 24);
		TS_ASSERT_EQUALS(m.bearingX, 0);
		TS_ASSERT_EQUALS(m.width, 24);
		TS_ASSERT_EQUALS(m.height, 24);
		TS_ASSERT_EQUALS(m.bearingY, 17);
	}

	void test_proportional_metrics_are_read_per_glyph() {
		Common::Array<byte> bytes = makeFont(8, 0, 256, 24, 24, 17, true);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		TS_ASSERT(font.isProportional());

		Graphics::GlyphMetrics a, b;
		TS_ASSERT(font.glyphMetrics(1, a));
		TS_ASSERT(font.glyphMetrics(2, b));

		TS_ASSERT_EQUALS(a.advance, 5);
		TS_ASSERT_EQUALS(a.bearingX, 1);
		TS_ASSERT_EQUALS(a.width, 3);

		// Different glyphs really do get different advances - that is the
		// whole point of a proportional font.
		TS_ASSERT_EQUALS(b.advance, 6);
		TS_ASSERT_DIFFERS(a.advance, b.advance);
	}

	void test_1bpp_and_8bpp_row_pitch() {
		Common::Array<byte> mono = makeFont(1, 949, 8, 24, 24, 20, false);
		Graphics::HiResBitmapFont a;
		TS_ASSERT(loadFont(a, mono));
		TS_ASSERT_EQUALS(a.bpp(), 1);
		TS_ASSERT_EQUALS(a.glyphPitch(), 3);   // 24 bits packed into 3 bytes

		Common::Array<byte> gray = makeFont(8, 949, 8, 24, 24, 20, false);
		Graphics::HiResBitmapFont b;
		TS_ASSERT(loadFont(b, gray));
		TS_ASSERT_EQUALS(b.bpp(), 8);
		TS_ASSERT_EQUALS(b.glyphPitch(), 24);  // one byte of coverage per pixel
	}

	void test_2bpp_row_pitch() {
		// 18 px at 2 bits is 36 bits: four whole bytes and a half one.
		Common::Array<byte> bytes = makeFont(2, 949, 8, 18, 18, 15, false);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));
		TS_ASSERT_EQUALS(font.bpp(), 2);
		TS_ASSERT_EQUALS(font.glyphPitch(), 5);
		TS_ASSERT_EQUALS(font.cellWidth(), 18);
		TS_ASSERT_EQUALS(font.glyphCount(), 8);

		Common::Array<byte> even = makeFont(2, 949, 8, 24, 24, 20, false);
		Graphics::HiResBitmapFont b;
		TS_ASSERT(loadFont(b, even));
		TS_ASSERT_EQUALS(b.glyphPitch(), 6);
	}

	// A 2bpp glyph holds four coverage levels, packed four pixels a byte with
	// the leftmost in the top two bits (TextCompose::expandCoverage()'s order).
	void test_2bpp_glyph_has_four_coverage_levels() {
		Common::Array<byte> bytes = makeFont(2, 949, 2, 18, 18, 15, false);
		const uint32 dataOff = 32;
		const uint32 stride = 5 * 18;
		// Glyph 1, row 0: pixels 0..3 at levels 0, 1, 2, 3; pixel 17 at 3.
		bytes[dataOff + stride + 0] = 0x1B;               // 00 01 10 11
		bytes[dataOff + stride + 4] = 0x30;               // pixels 16..17: 00 11
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));
		const byte *g = font.glyphData(1);
		TS_ASSERT(g != nullptr);
		if (!g)
			return;
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(g, 0, 2), 0);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(g, 1, 2), 85);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(g, 2, 2), 170);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(g, 3, 2), 255);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(g, 16, 2), 0);
		TS_ASSERT_EQUALS(Graphics::TextCompose::expandCoverage(g, 17, 2), 255);
		// The glyph is where its index says: the next row starts a pitch on.
		TS_ASSERT_EQUALS(font.glyphData(1) - font.glyphData(0), (int)stride);
	}

	void test_glyph_data_is_addressed_by_index() {
		Common::Array<byte> bytes = makeFont(8, 949, 32, 24, 24, 17, false);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		const byte *g5 = font.glyphData(5);
		TS_ASSERT(g5 != nullptr);
		TS_ASSERT_EQUALS(*g5, 5);

		TS_ASSERT(font.glyphData(-1) == nullptr);
		TS_ASSERT(font.glyphData(32) == nullptr);
	}

	void test_legacy_korean_font_is_indexed_by_code_point() {
		Common::Array<byte> bytes = makeFont(8, 949, 2350, 24, 24, 17, false);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		// U+AC00 is the first syllable of KS X 1001, at 0xB0A1 in CP949, so
		// it must land on glyph 0. This is what keeps existing Korean font
		// files working now that lookup is by code point.
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC00), 0);
		TS_ASSERT(font.hasGlyph(0xAC00));

		// U+AC01 follows it in the same order.
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC01), 1);

		// Latin has no place in a font ordered by KS X 1001's syllables.
		TS_ASSERT_EQUALS(font.glyphIndex('A'), -1);
		TS_ASSERT(!font.hasGlyph('A'));

		// Neither has a character from another script.
		TS_ASSERT_EQUALS(font.glyphIndex(0x3042), -1);
	}

	void test_legacy_single_byte_font_is_indexed_by_byte_value() {
		// The Latin fonts shipped today say nothing about a code page and
		// place each glyph at its own byte value.
		Common::Array<byte> bytes = makeFont(8, 0, 256, 24, 24, 17, true);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		TS_ASSERT_EQUALS(font.codePage(), Common::kISO8859_1);
		TS_ASSERT_EQUALS(font.glyphIndex('A'), 65);
		TS_ASSERT_EQUALS(font.glyphIndex(' '), 32);

		// Latin-1 accented letters are in range, and are as much "Latin" as
		// the ASCII ones - the byte width of the source text is irrelevant.
		TS_ASSERT_EQUALS(font.glyphIndex(0xE9), 0xE9);   // e acute

		// Anything above the range simply is not there.
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC00), -1);
	}

	void test_font_with_its_own_code_point_table() {
		// A version 2 font maps code points to glyphs itself, so it can hold
		// any mix of scripts in any order.
		const int glyphs = 3;
		const int cellW = 16, cellH = 16;
		// The version 2 header is 36 bytes: the table offset sits after
		// the version 1 fields, not on top of the ascent.
		const uint32 cmapOff = 36;
		const uint32 dataOff = cmapOff + glyphs * 8;
		const uint32 dataSize = cellW * cellH * glyphs;

		Common::Array<byte> b;
		b.resize(dataOff + dataSize);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		b[8] = 8;
		put16(b, 12, glyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = 12;
		put32(b, 32, cmapOff);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);

		// Deliberately out of order, and mixing scripts.
		const uint32 points[glyphs] = { 0x41, 0xAC00, 0x20AC };
		for (int i = 0; i < glyphs; ++i) {
			put32(b, cmapOff + i * 8, points[i]);
			put32(b, cmapOff + i * 8 + 4, glyphs - 1 - i);
		}

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, b));

		TS_ASSERT_EQUALS(font.glyphIndex(0x41), 2);
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC00), 1);
		TS_ASSERT_EQUALS(font.glyphIndex(0x20AC), 0);   // euro sign
		TS_ASSERT_EQUALS(font.glyphIndex(0x42), -1);

		// The ascent survives, which it could not while the table offset
		// was written over it.
		TS_ASSERT_EQUALS(font.ascent(), 12);
	}

	void test_rejects_glyph_data_past_the_end_of_the_file() {
		Common::Array<byte> bytes = makeFont(8, 949, 64, 24, 24, 17, false);
		// Claim far more glyphs than the file has room for.
		put16(bytes, 12, 4096);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!loadFont(font, bytes));
		TS_ASSERT(!font.isLoaded());
	}

	void test_rejects_a_data_offset_outside_the_file() {
		Common::Array<byte> bytes = makeFont(8, 949, 8, 24, 24, 17, false);
		put32(bytes, 24, 0xfffff000);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!loadFont(font, bytes));
	}

	void test_rejects_an_unsupported_depth() {
		Common::Array<byte> bytes = makeFont(8, 949, 8, 24, 24, 17, false);
		bytes[8] = 4;

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!loadFont(font, bytes));
	}

	void test_rejects_an_empty_cell() {
		Common::Array<byte> bytes = makeFont(8, 949, 8, 24, 24, 17, false);
		bytes[14] = 0;

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!loadFont(font, bytes));
	}

	void test_rejects_a_future_version() {
		Common::Array<byte> bytes = makeFont(8, 949, 8, 24, 24, 17, false);
		put16(bytes, 4, 99);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!loadFont(font, bytes));
	}

	void test_rejects_a_file_over_the_size_limit() {
		Common::Array<byte> bytes = makeFont(8, 949, 64, 24, 24, 17, false);

		// A caller bounds the allocation this loader may make, so that a
		// hand-edited font cannot ask for an unreasonable amount of memory.
		Graphics::HiResBitmapFont font;
		TS_ASSERT(!loadFont(font, bytes, 1024));
		TS_ASSERT(!font.isLoaded());
	}

	void test_a_broken_metrics_table_keeps_the_glyphs() {
		Common::Array<byte> bytes = makeFont(8, 0, 64, 24, 24, 17, true);
		put32(bytes, 20, 0xfffff000);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		// Losing the advances is not worth losing the font over: the cell is
		// a usable stand-in.
		TS_ASSERT(font.isLoaded());
		TS_ASSERT(!font.isProportional());

		Graphics::GlyphMetrics m;
		TS_ASSERT(font.glyphMetrics(0, m));
		TS_ASSERT_EQUALS(m.advance, 24);
	}

	void test_rejects_a_code_point_table_pointing_outside_the_font() {
		const int glyphs = 2;
		const uint32 cmapOff = 36;
		const uint32 dataOff = cmapOff + glyphs * 8;
		const uint32 dataSize = 16 * 16 * glyphs;

		Common::Array<byte> b;
		b.resize(dataOff + dataSize);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		b[8] = 8;
		put16(b, 12, glyphs);
		b[14] = 16;
		b[15] = 16;
		put32(b, 32, cmapOff);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);

		put32(b, cmapOff, 0x41);
		put32(b, cmapOff + 4, 999);      // no such glyph

		Graphics::HiResBitmapFont font;
		TS_ASSERT(!loadFont(font, b));
	}

	void test_korean_lookup_does_not_need_encoding_dat() {
		// The glyph map is worked out by decoding the block the font holds,
		// so a build or install without the shared conversion tables still
		// finds its glyphs. Nothing here loads encoding.dat.
		Common::Array<byte> bytes = makeFont(8, 949, 2350, 24, 24, 17, false);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));

		TS_ASSERT_EQUALS(font.glyphIndex(0xAC00), 0);
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC01), 1);

		// The last glyph of the block is reachable too. KS X 1001 holds 2350
		// syllables in its own order, which is not Unicode's: the last one is
		// U+D79D, and U+D7A3 - the last syllable in Unicode - is not in the
		// set at all.
		TS_ASSERT_EQUALS(font.glyphIndex(0xD79D), 2349);
		TS_ASSERT_EQUALS(font.glyphIndex(0xD7A3), -1);
	}

	/// A version 2 font whose table maps points[i] to glyph indices[i].
	static Common::Array<byte> makeV2Font(const Common::Array<uint32> &points, const Common::Array<uint32> &indices, int glyphs) {
		const int cellW = 8, cellH = 8;
		const uint32 cmapOff = 36;
		const uint32 dataOff = cmapOff + points.size() * 8;
		const uint32 dataSize = cellW * cellH * glyphs;
		Common::Array<byte> b;
		b.resize(dataOff + dataSize);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;
		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);
		b[8] = 8;
		put16(b, 12, glyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = 6;
		put32(b, 32, cmapOff);
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);
		for (uint i = 0; i < points.size(); ++i) {
			put32(b, cmapOff + i * 8, points[i]);
			put32(b, cmapOff + i * 8 + 4, indices[i]);
		}
		return b;
	}

	void test_sorted_code_point_table_is_searched_in_place() {
		// The baker writes the table sorted; the font then looks code points
		// up in the file's own table instead of building a hash map.
		Common::Array<uint32> points, indices;
		const int glyphs = 700;
		for (int i = 0; i < glyphs; ++i) {
			points.push_back(0x20 + i * 37);
			indices.push_back((i * 7) % glyphs);
		}
		Common::Array<byte> b = makeV2Font(points, indices, glyphs);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, b));
		TS_ASSERT(font.searchesCodePointTable());
		TS_ASSERT(!font.codePointTableNeedsOrder());

		for (int pass = 0; pass < 2; ++pass) {	// the second pass comes from the cache
			for (int i = 0; i < glyphs; ++i) {
				TS_ASSERT_EQUALS(font.glyphIndex(points[i]), (int)indices[i]);
				TS_ASSERT_EQUALS(font.glyphIndex(points[i] + 1), -1);
			}
		}
		TS_ASSERT_EQUALS(font.glyphIndex(0), -1);
		TS_ASSERT_EQUALS(font.glyphIndex(0x1F), -1);
		TS_ASSERT_EQUALS(font.glyphIndex(0x10FFFF), -1);
		TS_ASSERT_EQUALS(font.glyphIndex(0xFFFFFFFF), -1);
	}

	void test_unsorted_or_repeated_code_points_keep_the_last_entry() {
		// A table that is not strictly ascending is searched through an order
		// of its own: a code point listed twice maps to its last entry, as
		// it always did.
		Common::Array<uint32> points, indices;
		points.push_back(0x41); indices.push_back(0);
		points.push_back(0x42); indices.push_back(1);
		points.push_back(0x42); indices.push_back(2);
		points.push_back(0x43); indices.push_back(3);
		Common::Array<byte> b = makeV2Font(points, indices, 4);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, b));
		TS_ASSERT(font.searchesCodePointTable());
		TS_ASSERT(font.codePointTableNeedsOrder());
		TS_ASSERT_EQUALS(font.glyphIndex(0x41), 0);
		TS_ASSERT_EQUALS(font.glyphIndex(0x42), 2);
		TS_ASSERT_EQUALS(font.glyphIndex(0x43), 3);
		TS_ASSERT_EQUALS(font.glyphIndex(0x44), -1);
	}

	void test_a_table_out_of_order_finds_every_code_point() {
		// The shipped fonts list glyphs in glyph order, not by code point.
		Common::Array<uint32> points, indices;
		const int glyphs = 500;
		for (int i = 0; i < glyphs; ++i) {
			points.push_back(0xAC00 + ((i * 263) % 997) * 3);	// distinct, scattered
			indices.push_back(i);
		}
		points.push_back(0xAC00 + ((7 * 263) % 997) * 3);	// one listed twice: the later one wins
		indices.push_back(3);
		Common::Array<byte> b = makeV2Font(points, indices, glyphs + 1);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, b));
		TS_ASSERT(font.codePointTableNeedsOrder());
		for (int i = 0; i < glyphs; ++i)
			TS_ASSERT_EQUALS(font.glyphIndex(points[i]), i == 7 ? 3 : i);
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC01), -1);
		TS_ASSERT_EQUALS(font.glyphIndex(0x41), -1);
	}

	void test_lookups_follow_a_reloaded_font() {
		// Nothing of the first font's lookups may answer for the second.
		Common::Array<uint32> p1, i1, p2, i2;
		p1.push_back(0x41); i1.push_back(1);
		p1.push_back(0xAC00); i1.push_back(0);
		p2.push_back(0x42); i2.push_back(0);
		p2.push_back(0xAC00); i2.push_back(1);
		Common::Array<byte> a = makeV2Font(p1, i1, 2), b = makeV2Font(p2, i2, 2);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, a));
		TS_ASSERT_EQUALS(font.glyphIndex(0x41), 1);
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC00), 0);
		TS_ASSERT(loadFont(font, b));
		TS_ASSERT_EQUALS(font.glyphIndex(0x41), -1);
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC00), 1);
		TS_ASSERT_EQUALS(font.glyphIndex(0x42), 0);
		font.free();
		TS_ASSERT_EQUALS(font.glyphIndex(0x42), -1);
	}

	void test_free_resets_the_font() {
		Common::Array<byte> bytes = makeFont(8, 949, 16, 24, 24, 17, false);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, bytes));
		TS_ASSERT(font.isLoaded());

		font.free();

		TS_ASSERT(!font.isLoaded());
		TS_ASSERT_EQUALS(font.glyphCount(), 0);
		TS_ASSERT_EQUALS(font.glyphIndex(0xAC00), -1);
		TS_ASSERT(font.glyphData(0) == nullptr);
	}

	void test_loading_twice_replaces_the_first_font() {
		Common::Array<byte> korean = makeFont(8, 949, 2350, 36, 36, 24, false);
		Common::Array<byte> latin = makeFont(8, 0, 256, 24, 24, 17, true);

		Graphics::HiResBitmapFont font;
		TS_ASSERT(loadFont(font, korean));
		TS_ASSERT_EQUALS(font.cellWidth(), 36);

		TS_ASSERT(loadFont(font, latin));
		TS_ASSERT_EQUALS(font.cellWidth(), 24);
		TS_ASSERT_EQUALS(font.glyphCount(), 256);
		TS_ASSERT(font.isProportional());
		TS_ASSERT_EQUALS(font.glyphIndex('A'), 65);
	}
	/// A stream over a copy of @p bytes that says when it is deleted, and
	/// can be made to fail every read from some point on.
	class WatchedStream : public Common::MemoryReadStream {
	public:
		WatchedStream(const Common::Array<byte> &bytes, bool *deleted)
			: Common::MemoryReadStream(copyOf(bytes), bytes.size(), DisposeAfterUse::YES), _deleted(deleted), _fail(false) {}
		~WatchedStream() override {
			if (_deleted)
				*_deleted = true;
		}
		uint32 read(void *dataPtr, uint32 dataSize) override {
			if (_fail)
				return 0;
			return Common::MemoryReadStream::read(dataPtr, dataSize);
		}
		void failReads() { _fail = true; }

	private:
		static byte *copyOf(const Common::Array<byte> &bytes) {
			byte *p = (byte *)malloc(bytes.size());
			memcpy(p, bytes.begin(), bytes.size());
			return p;
		}
		bool *_deleted;
		bool _fail;
	};

	/// Everything a reader asks of a font, the same from load() and loadStreamed().
	static void checkStreamedLikeLoaded(Common::Array<byte> &bytes, const Common::Array<uint32> &probes) {
		Graphics::HiResBitmapFont whole, streamed;
		TS_ASSERT(loadFont(whole, bytes));
		bool deleted = false;
		TS_ASSERT(streamed.loadStreamed(new WatchedStream(bytes, &deleted), DisposeAfterUse::YES));
		TS_ASSERT(streamed.isStreamed());
		TS_ASSERT(!whole.isStreamed());
		TS_ASSERT_EQUALS(streamed.bpp(), whole.bpp());
		TS_ASSERT_EQUALS(streamed.cellWidth(), whole.cellWidth());
		TS_ASSERT_EQUALS(streamed.cellHeight(), whole.cellHeight());
		TS_ASSERT_EQUALS(streamed.ascent(), whole.ascent());
		TS_ASSERT_EQUALS(streamed.glyphCount(), whole.glyphCount());
		TS_ASSERT_EQUALS(streamed.isProportional(), whole.isProportional());
		TS_ASSERT_EQUALS(streamed.codePage(), whole.codePage());
		TS_ASSERT_EQUALS(streamed.glyphPitch(), whole.glyphPitch());
		TS_ASSERT_EQUALS(streamed.searchesCodePointTable(), whole.searchesCodePointTable());
		TS_ASSERT_EQUALS(streamed.codePointTableNeedsOrder(), whole.codePointTableNeedsOrder());
		for (uint i = 0; i < probes.size(); ++i)
			TS_ASSERT_EQUALS(streamed.glyphIndex(probes[i]), whole.glyphIndex(probes[i]));
		const uint32 stride = whole.glyphPitch() * whole.cellHeight();
		// Backwards, then the same glyph twice, so the one buffer is reused.
		for (int i = whole.glyphCount() - 1; i >= 0; --i) {
			Graphics::GlyphMetrics a, b;
			TS_ASSERT_EQUALS(streamed.glyphMetrics(i, a), whole.glyphMetrics(i, b));
			TS_ASSERT_EQUALS(a.advance, b.advance);
			TS_ASSERT_EQUALS(a.bearingX, b.bearingX);
			TS_ASSERT_EQUALS(a.width, b.width);
			for (int twice = 0; twice < 2; ++twice) {
				const byte *p = streamed.glyphData(i);
				TS_ASSERT(p != nullptr);
				if (p)
					TS_ASSERT_EQUALS(memcmp(p, whole.glyphData(i), stride), 0);
			}
		}
		TS_ASSERT(streamed.glyphData(-1) == nullptr);
		TS_ASSERT(streamed.glyphData(whole.glyphCount()) == nullptr);
		TS_ASSERT(!deleted);
		streamed.free();
		TS_ASSERT(deleted);
		TS_ASSERT(!streamed.isLoaded());
	}

	void test_a_streamed_font_reads_like_a_loaded_one() {
		Common::Array<uint32> probes;
		probes.push_back(0x41);
		probes.push_back(0xAC00);
		probes.push_back(0xAC01);
		probes.push_back(0xD79D);
		probes.push_back(0x20 + 5 * 37);
		probes.push_back(0x10FFFF);

		Common::Array<byte> korean = makeFont(8, 949, 2350, 24, 24, 17, false);
		checkStreamedLikeLoaded(korean, probes);
		Common::Array<byte> latin = makeFont(8, 0, 256, 12, 16, 12, true);
		checkStreamedLikeLoaded(latin, probes);
		Common::Array<byte> aa = makeFont(2, 0, 200, 18, 18, 14, true);
		checkStreamedLikeLoaded(aa, probes);
		Common::Array<byte> stencil = makeFont(1, 949, 300, 16, 16, 13, false);
		checkStreamedLikeLoaded(stencil, probes);

		Common::Array<uint32> points, indices;
		for (int i = 0; i < 40; ++i) {
			points.push_back(0x20 + i * 37);
			indices.push_back((i * 7) % 40);
		}
		Common::Array<byte> sorted = makeV2Font(points, indices, 40);
		checkStreamedLikeLoaded(sorted, points);
		{ const uint32 t = points[3]; points[3] = points[30]; points[30] = t; }
		Common::Array<byte> unsorted = makeV2Font(points, indices, 40);
		checkStreamedLikeLoaded(unsorted, points);
	}

	void test_a_streamed_font_refuses_what_load_refuses_and_drops_the_stream() {
		Common::Array<byte> bytes = makeFont(8, 949, 16, 24, 24, 17, true);
		Common::Array<byte> cut = bytes;
		cut.resize(bytes.size() - 1);
		Common::Array<byte> head = bytes;
		head.resize(20);
		Common::Array<byte> bad = bytes;
		bad[8] = 4;	// depth

		Common::Array<byte> *cases[] = { &cut, &head, &bad };
		for (int c = 0; c < 3; ++c) {
			Graphics::HiResBitmapFont whole, streamed;
			TS_ASSERT(!loadFont(whole, *cases[c]));
			bool deleted = false;
			TS_ASSERT(!streamed.loadStreamed(new WatchedStream(*cases[c], &deleted), DisposeAfterUse::YES));
			TS_ASSERT(deleted);
			TS_ASSERT(!streamed.isLoaded());
		}

		// Not the font's to delete.
		bool deleted = false;
		WatchedStream *kept = new WatchedStream(bytes, &deleted);
		{
			Graphics::HiResBitmapFont font;
			TS_ASSERT(font.loadStreamed(kept, DisposeAfterUse::NO));
		}
		TS_ASSERT(!deleted);
		delete kept;
		TS_ASSERT(deleted);
	}

	void test_a_streamed_glyph_that_cannot_be_read_is_null() {
		Common::Array<byte> bytes = makeFont(8, 0, 256, 12, 16, 12, true);
		WatchedStream *stream = new WatchedStream(bytes, nullptr);
		Graphics::HiResBitmapFont font;
		TS_ASSERT(font.loadStreamed(stream, DisposeAfterUse::YES));
		TS_ASSERT(font.glyphData('A') != nullptr);
		TS_ASSERT_EQUALS(font.readCount(), 1u);
		TS_ASSERT(font.glyphData('B') != nullptr);	// read with 'A'
		TS_ASSERT_EQUALS(font.readCount(), 1u);
		stream->failReads();
		TS_ASSERT(font.glyphData('A') != nullptr);	// still in what it read
		TS_ASSERT(font.glyphData(250) == nullptr);
		TS_ASSERT_EQUALS(font.glyphIndex(250), 250);	// the tables are in memory
	}
};
