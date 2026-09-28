#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "common/str.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_file.h"

/**
 * Tests for the shared "is this an SVFN file, then open it" helper
 * (graphics/hires_text/glyph_source_file.h), the front door an engine or the
 * font map uses before it knows whether a face is a live TrueType font or a
 * baked bitmap one.
 */
class HiResTextGlyphSourceFileTestSuite : public CxxTest::TestSuite {
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

	static const int kGlyphs = 2;

	/// Code point of glyph index i: U+0041 (narrow) and U+AC00 (wide).
	static uint32 codepointOf(int index) {
		static const uint32 cps[kGlyphs] = { 0xAC00, 0x0041 };
		return cps[index];
	}

	/**
	 * A minimal version 2 SVFN file, built the same way as the fixture in
	 * test/graphics/hires_text_svfn_source.h (36-byte header, 8bpp glyph
	 * data, code point table last, so truncating the file cuts the table).
	 */
	static Common::Array<byte> makeSvfnFile(int cellW, int cellH) {
		const int rowPitch = cellW;	// 8bpp: one byte per pixel
		const uint32 glyphStride = rowPitch * cellH;
		const uint32 dataOff = 36;
		const uint32 dataSize = glyphStride * kGlyphs;
		const uint32 cmapOff = dataOff + dataSize;

		Common::Array<byte> b;
		b.resize(cmapOff + kGlyphs * 8);
		for (uint i = 0; i < b.size(); ++i)
			b[i] = 0;

		b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
		put16(b, 4, 2);		// version
		put16(b, 6, 0);			// flags
		b[8] = 8;			// bpp
		put16(b, 10, 0);		// code page (unused by v2)
		put16(b, 12, kGlyphs);
		b[14] = cellW;
		b[15] = cellH;
		b[16] = cellH - 1;		// ascent
		put32(b, 20, 0);		// no metrics table
		put32(b, 24, dataOff);
		put32(b, 28, dataSize);
		put32(b, 32, cmapOff);

		// A non-zero pixel per glyph, so a real row is never mistaken for a miss.
		for (int i = 0; i < kGlyphs; ++i)
			b[dataOff + i * glyphStride] = (byte)(i + 1);

		for (int i = 0; i < kGlyphs; ++i) {
			put32(b, cmapOff + i * 8, codepointOf(i));
			put32(b, cmapOff + i * 8 + 4, i);
		}
		return b;
	}

public:
	void test_isSvfnFile_recognises_the_magic() {
		const byte head[4] = { 'S', 'V', 'F', 'N' };
		TS_ASSERT(Graphics::isSvfnFile(head, sizeof(head)));
	}

	void test_isSvfnFile_rejects_other_magics_and_short_buffers() {
		const byte other[4] = { 'S', 'C', 'V', 'M' };
		TS_ASSERT(!Graphics::isSvfnFile(other, sizeof(other)));

		const byte svfn[4] = { 'S', 'V', 'F', 'N' };
		TS_ASSERT(!Graphics::isSvfnFile(svfn, 3));
		TS_ASSERT(!Graphics::isSvfnFile(svfn, 0));
	}

	void test_createSvfnSource_opens_a_valid_file() {
		const Common::Array<byte> bytes = makeSvfnFile(6, 5);
		Common::MemoryReadStream stream(bytes.begin(), bytes.size());

		Common::String error;
		Graphics::UnicodeGlyphSource *src = Graphics::createSvfnSource(stream, error);
		TS_ASSERT(src != nullptr);
		if (!src)
			return;

		TS_ASSERT_EQUALS(src->cells(0x0041), 1);
		TS_ASSERT_EQUALS(src->cells(0xAC00), 2);
		TS_ASSERT_EQUALS(src->cells(0x0042), 0);

		delete src;
	}

	void test_createSvfnSource_fails_on_a_truncated_file() {
		const Common::Array<byte> bytes = makeSvfnFile(6, 5);
		// Cut the file inside the code point table.
		Common::MemoryReadStream stream(bytes.begin(), bytes.size() - 1);

		Common::String error;
		Graphics::UnicodeGlyphSource *src = Graphics::createSvfnSource(stream, error);
		TS_ASSERT(src == nullptr);
		TS_ASSERT(!error.empty());
	}
};
