/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/memstream.h"
#include "graphics/korfont.h"

/**
 * korean.fnt (SCVMSJIS) as FontKoreanSVM reads it: format v3, the one the
 * Korean SCI/SCUMM fan patches have always shipped, and v4, which adds a
 * 128-byte per-ASCII advance table after the header and stores the Latin
 * glyphs 16 px wide (the Leisure Suit Larry 1 VGA Korean patch).
 *
 * The files are synthetic and minimal: 12 Hangul glyphs (the least the
 * loader's bounds assertion for glyph 0 allows) and 128 Latin ones.
 */
class KoreanFontSvmTestSuite : public CxxTest::TestSuite {
	enum { kNum16 = 12, kNum8x16 = 128 };

	static void put32(Common::Array<byte> &d, uint32 v) {
		d.push_back(v >> 24); d.push_back(v >> 16); d.push_back(v >> 8); d.push_back(v);
	}
	static void put16(Common::Array<byte> &d, uint16 v) {
		d.push_back(v >> 8); d.push_back(v);
	}

	// Hangul glyph 0 ('ga'): row 0 = 0xFF 0x00 (left half), every other row empty.
	// Latin 'A': row 0 = 0x81 0x81 - the second byte is what only v4 keeps.
	static Common::Array<byte> makeFont(uint32 version) {
		Common::Array<byte> d;
		put32(d, MKTAG('S', 'C', 'V', 'M'));
		put32(d, MKTAG('S', 'J', 'I', 'S'));
		put32(d, version);
		put16(d, kNum16);
		put16(d, kNum8x16);
		put16(d, 0);
		if (version >= 4) {
			for (int c = 0; c < 128; c++)
				d.push_back(c == 'A' ? 14 : (c == ' ' ? 6 : (c < 0x20 ? 0 : 10)));
		}
		for (int g = 0; g < kNum16; g++)
			for (int b = 0; b < 32; b++)
				d.push_back((g == 0 && b == 0) ? 0xFF : 0x00);
		for (int c = 0; c < kNum8x16; c++)
			for (int b = 0; b < 32; b++)
				d.push_back((c == 'A' && b < 2) ? 0x81 : 0x00);
		return d;
	}

	static bool load(Graphics::FontKoreanSVM &font, const Common::Array<byte> &d) {
		Common::MemoryReadStream s(d.data(), d.size());
		return font.loadFromStream(s);
	}

	// Draws ch into a 16x16 8-bit buffer (0 = empty, 1 = ink); returns row 0 as a bit string.
	static Common::String row0(Graphics::FontKoreanSVM &font, uint16 ch) {
		byte buf[16 * 16];
		memset(buf, 0, sizeof(buf));
		font.drawChar(buf, ch, 16, 1, 1, 0, -1, -1);
		Common::String s;
		for (int x = 0; x < 16; x++)
			s += buf[x] ? '#' : '.';
		return s;
	}

public:
	void test_v3_loads_unchanged() {
		Graphics::FontKoreanSVM font;
		TS_ASSERT(load(font, makeFont(3)));
		TS_ASSERT_EQUALS(font.getVersion(), 3u);
		TS_ASSERT(!font.hasProportionalLatin());
		// Fixed half-cell Latin, full-cell Hangul, as always.
		TS_ASSERT_EQUALS(font.getCharWidth('A'), 8u);
		TS_ASSERT_EQUALS(font.getCharWidth(' '), 8u);
		TS_ASSERT_EQUALS(font.getCharWidth(0xA1B0), 16u);
		// Only the first byte of a Latin row is used: 8 px.
		TS_ASSERT_EQUALS(row0(font, 'A'), Common::String("#......#........"));
		TS_ASSERT_EQUALS(row0(font, 0xA1B0), Common::String("########........"));
	}

	void test_v4_loads_with_proportional_latin() {
		Graphics::FontKoreanSVM font;
		TS_ASSERT(load(font, makeFont(4)));
		TS_ASSERT_EQUALS(font.getVersion(), 4u);
		TS_ASSERT(font.hasProportionalLatin());
		// Latin advances come from the table.
		TS_ASSERT_EQUALS(font.getCharWidth('A'), 14u);
		TS_ASSERT_EQUALS(font.getCharWidth(' '), 6u);
		TS_ASSERT_EQUALS(font.getCharWidth('a'), 10u);
		TS_ASSERT_EQUALS(font.getLatinAdvance('A'), 14);
		// Hangul is untouched by the table - and found after it.
		TS_ASSERT_EQUALS(font.getCharWidth(0xA1B0), 16u);
		TS_ASSERT_EQUALS(row0(font, 0xA1B0), Common::String("########........"));
		// Both bytes of a Latin row are drawn: 16 px.
		TS_ASSERT_EQUALS(row0(font, 'A'), Common::String("#......##......#"));
	}

	void test_other_versions_rejected() {
		Graphics::FontKoreanSVM f2, f5;
		TS_ASSERT(!load(f2, makeFont(2)));
		TS_ASSERT(!load(f5, makeFont(5)));
	}

	void test_truncated_v4_fails() {
		Common::Array<byte> d = makeFont(4);
		d.resize(18 + 64);	// cut inside the width table
		Graphics::FontKoreanSVM font;
		TS_ASSERT(!load(font, d));
	}
};
