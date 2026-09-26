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
#include "common/endian.h"
#include "common/fs.h"
#include "common/stream.h"
#include "ags/shared/font/wfn_font.h"
#include "graphics/hires_text/codepage_kr.h"

#include "../system/null_osystem.h"

namespace {

// A WFN font with one base glyph, 'A' (3x2), filled in directly: the base
// reader needs the engine's stream and debug output, which the runner does
// not link.
class TestWFNFont : public AGS3::WFNFont {
public:
	void addBaseA(const uint8_t *pixels) {
		AGS3::WFNChar a;
		a.Width = 3;
		a.Height = 2;
		a.Data = pixels;
		_items.push_back(a);
		_refs.resize(0x42);
		for (size_t i = 0; i < _refs.size(); i++)
			_refs[i] = &_emptyChar;
		_refs[0x41] = &_items[0];
	}
};

// The code point of KS X 1001 Hangul index i (the syllables are a sparse,
// ordered subset of U+AC00..U+D7A3).
uint32 ksx(int i) {
	return Graphics::KoreanCodePage::decodeEucKrPair((byte)(0xB0 + i / 94), (byte)(0xA1 + i % 94));
}

void putLE16(Common::Array<byte> &out, uint16 v) {
	out.push_back(v & 0xFF);
	out.push_back(v >> 8);
}

void putLE32(Common::Array<byte> &out, uint32 v) {
	putLE16(out, v & 0xFFFF);
	putLE16(out, v >> 16);
}

/**
 * An extfnt file with `count` offsets: glyph 0 is 8x8 all ink, every other
 * entry a 1x1 dot. Entry `badEntry` (if >= 0) points at a record whose
 * 8x200 pixel data runs past the table address.
 */
Common::Array<byte> makeExt(uint count, int badEntry = -1) {
	Common::Array<byte> f;
	const char *sig = "WGT Font File  ";
	for (int i = 0; i < 15; i++)
		f.push_back(sig[i]);
	putLE32(f, 0); // table address, patched below

	const uint32 glyph0 = f.size();
	putLE16(f, 8);
	putLE16(f, 8);
	for (int i = 0; i < 8; i++)
		f.push_back(0xFF);
	const uint32 dot = f.size();
	putLE16(f, 1);
	putLE16(f, 1);
	f.push_back(0x80);
	const uint32 tall = f.size();
	putLE16(f, 8);
	putLE16(f, 200);
	f.push_back(0xFF);

	const uint32 table = f.size();
	WRITE_LE_UINT32(&f[15], table);
	for (uint i = 0; i < count; i++)
		putLE32(f, i == 0 ? glyph0 : ((int)i == badEntry ? tall : dot));
	return f;
}

} // End of anonymous namespace

/**
 * The Korean fan patches' extfntN.wfn: a WFN with a 32-bit table address and
 * exactly 2350 glyphs, the KS X 1001 Hangul block in code order. Code points
 * below 256 stay on the base font; a KS X 1001 syllable draws its ext glyph.
 */
class AgsWfnExtTestSuite : public CxxTest::TestSuite {
public:
	void test_ext_glyphs_by_code_point() {
		static const uint8_t pixelsA[2] = { 0xA0, 0x40 };
		TestWFNFont font;
		font.addBaseA(pixelsA);
		const Common::Array<byte> ext = makeExt(2350);
		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), ext.size()), AGS3::kWFNErr_NoError);
		TS_ASSERT(font.HasExt());
		TS_ASSERT_EQUALS(font.GetExtHeight(), 8);

		const AGS3::WFNChar &ga = font.GetChar(0xAC00);
		TS_ASSERT_EQUALS(ga.Width, 8);
		TS_ASSERT_EQUALS(ga.Height, 8);
		TS_ASSERT(ga.Data != nullptr);
		if (ga.Data)
			TS_ASSERT_EQUALS(ga.Data[7], 0xFF);

		const AGS3::WFNChar &last = font.GetChar(0xD79D); // KS X 1001 index 2349
		TS_ASSERT_EQUALS(last.Width, 1);
		TS_ASSERT_EQUALS(last.Height, 1);

		const AGS3::WFNChar &a = font.GetChar(0x41);
		TS_ASSERT_EQUALS(a.Width, 3);
		TS_ASSERT_EQUALS(a.Data, pixelsA);

		// Not in the base table, not a KS X 1001 syllable: empty.
		TS_ASSERT_EQUALS(font.GetChar(0xE9).Width, 0);
		TS_ASSERT_EQUALS(font.GetChar(0xD7A3).Width, 0); // CP949 extension only
		TS_ASSERT_EQUALS(font.GetChar(0x3131).Width, 0); // a jamo, not a syllable
	}

	void test_without_ext_hangul_is_empty() {
		static const uint8_t pixelsA[2] = { 0xA0, 0x40 };
		TestWFNFont font;
		font.addBaseA(pixelsA);
		TS_ASSERT(!font.HasExt());
		TS_ASSERT_EQUALS(font.GetExtHeight(), 0);
		TS_ASSERT_EQUALS(font.GetChar(0xAC00).Width, 0);
		TS_ASSERT_EQUALS(font.GetChar(0x41).Width, 3);
	}

	void test_wrong_glyph_count_is_refused() {
		TestWFNFont font;
		const Common::Array<byte> ext = makeExt(2349);
		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), ext.size()), AGS3::kWFNErr_BadCharCount);
		TS_ASSERT(!font.HasExt());
		TS_ASSERT_EQUALS(font.GetChar(0xAC00).Width, 0);

		const Common::Array<byte> more = makeExt(2351);
		TS_ASSERT_EQUALS(font.ReadExtFromData(more.data(), more.size()), AGS3::kWFNErr_BadCharCount);
		TS_ASSERT(!font.HasExt());
	}

	void test_truncated_offset_table_is_refused() {
		TestWFNFont font;
		const Common::Array<byte> ext = makeExt(2350);
		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), ext.size() - 2), AGS3::kWFNErr_BadCharCount);
		TS_ASSERT(!font.HasExt());
	}

	void test_bad_header_is_refused() {
		TestWFNFont font;
		Common::Array<byte> ext = makeExt(2350);
		ext[0] = 'X';
		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), ext.size()), AGS3::kWFNErr_BadSignature);
		TS_ASSERT(!font.HasExt());

		ext = makeExt(2350);
		WRITE_LE_UINT32(&ext[15], ext.size() + 4); // table past the end
		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), ext.size()), AGS3::kWFNErr_BadTableAddress);
		TS_ASSERT(!font.HasExt());

		WRITE_LE_UINT32(&ext[15], 3); // table inside the header
		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), ext.size()), AGS3::kWFNErr_BadTableAddress);

		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), 10), AGS3::kWFNErr_BadSignature);
		TS_ASSERT_EQUALS(font.ReadExtFromData(nullptr, 0), AGS3::kWFNErr_BadSignature);
	}

	void test_bad_offset_empties_that_entry_only() {
		TestWFNFont font;
		Common::Array<byte> ext = makeExt(2350, 6);
		const uint32 table = READ_LE_UINT32(&ext[15]);
		WRITE_LE_UINT32(&ext[table + 5 * 4], 0xFFFFFFF0); // header past the data
		WRITE_LE_UINT32(&ext[table + 7 * 4], 2);          // inside the signature
		TS_ASSERT_EQUALS(font.ReadExtFromData(ext.data(), ext.size()), AGS3::kWFNErr_HasBadCharacters);
		TS_ASSERT(font.HasExt());
		TS_ASSERT_EQUALS(font.GetChar(ksx(0)).Width, 8);
		for (int i = 5; i <= 7; i++) {
			TS_ASSERT_EQUALS(font.GetChar(ksx(i)).Width, 0);
			TS_ASSERT_EQUALS(font.GetChar(ksx(i)).Height, 0);
		}
		TS_ASSERT_EQUALS(font.GetChar(ksx(4)).Width, 1);
		TS_ASSERT_EQUALS(font.GetChar(ksx(8)).Width, 1);
	}

	// The 5 Days a Stranger patch's own file, when the harness data is here.
	void test_real_5days_extfnt0() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
		Common::FSNode node("/Users/juami/work/scummvm/kortrs/5 Days a Stranger (Windows)/extfnt0.wfn");
		if (!node.exists()) {
			Common::uninstall_null_g_system();
			TS_SKIP("the 5 Days a Stranger Korean patch is not present on this machine");
			return;
		}
		Common::SeekableReadStream *s = node.createReadStream();
		TS_ASSERT(s != nullptr);
		if (s) {
			Common::Array<byte> data;
			data.resize(s->size());
			s->read(data.data(), data.size());
			delete s;
			TS_ASSERT_EQUALS(data.size(), 65819u);
			TestWFNFont font;
			TS_ASSERT_EQUALS(font.ReadExtFromData(data.data(), data.size()), AGS3::kWFNErr_NoError);
			TS_ASSERT(font.HasExt());
			TS_ASSERT_EQUALS(font.GetExtCharCount(), 2350u);
			TS_ASSERT_EQUALS(font.GetExtHeight(), 10);
			for (uint32 cp = 0xAC00; cp <= 0xD7A3; cp++) {
				const AGS3::WFNChar &c = font.GetChar(cp);
				if (c.Width == 0)
					continue;
				TS_ASSERT_EQUALS(c.Width, 10);
				TS_ASSERT_EQUALS(c.Height, 10);
			}
			TS_ASSERT_EQUALS(font.GetChar(0xAC00).Width, 10);  // '가'
			TS_ASSERT_EQUALS(font.GetChar(0xD79D).Width, 10);  // '힝'
		}
		Common::uninstall_null_g_system();
#else
		TS_SKIP("no real filesystem access in this test environment");
#endif
	}
};
