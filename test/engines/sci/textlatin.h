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

#include "sci/graphics/textlatin.h"

using Sci::kLatinOff;
using Sci::kLatinHalf;
using Sci::kLatinFullwidth;
using Sci::TextCompose::latinFullwidth;
using Sci::TextCompose::asciiGoesToUnicodeFace;

class SciTextLatinTestSuite : public CxxTest::TestSuite {
public:
	// kLatinOff never touches the code point, at every boundary this feature
	// cares about, regardless of fullwidthSpace.
	void test_off_is_identity() {
		const uint32 probes[] = { 0x0000, 0x001F, 0x0020, 0x0021, 0x007E, 0x007F, 0xAC00 };
		for (uint i = 0; i < ARRAYSIZE(probes); i++) {
			TS_ASSERT_EQUALS(latinFullwidth(probes[i], kLatinOff, false), probes[i]);
			TS_ASSERT_EQUALS(latinFullwidth(probes[i], kLatinOff, true), probes[i]);
		}
	}

	// kLatinHalf never remaps the code point either - ASCII is routed to a
	// different face by asciiGoesToUnicodeFace() instead of being remapped
	// here, so it must keep its ordinary value.
	void test_half_is_identity_for_ascii() {
		const uint32 probes[] = { 0x0000, 0x001F, 0x0020, 0x0021, 0x007E, 0x007F, 0xAC00 };
		for (uint i = 0; i < ARRAYSIZE(probes); i++) {
			TS_ASSERT_EQUALS(latinFullwidth(probes[i], kLatinHalf, false), probes[i]);
			TS_ASSERT_EQUALS(latinFullwidth(probes[i], kLatinHalf, true), probes[i]);
		}
	}

	// kLatinFullwidth: the printable range U+0021..U+007E remaps to
	// U+FF01..U+FF5E (+0xFEE0), regardless of fullwidthSpace.
	void test_fullwidth_remaps_printable_range() {
		TS_ASSERT_EQUALS(latinFullwidth(0x0021, kLatinFullwidth, false), (uint32)0xFF01);
		TS_ASSERT_EQUALS(latinFullwidth(0x007E, kLatinFullwidth, false), (uint32)0xFF5E);
		TS_ASSERT_EQUALS(latinFullwidth(0x0041, kLatinFullwidth, false), (uint32)0xFF21); // 'A'
		TS_ASSERT_EQUALS(latinFullwidth(0x0021, kLatinFullwidth, true), (uint32)0xFF01);
		TS_ASSERT_EQUALS(latinFullwidth(0x007E, kLatinFullwidth, true), (uint32)0xFF5E);
	}

	// Boundaries just outside the printable range are left alone even in
	// fullwidth mode: 0x1F and 0x7F are not ASCII printable, and 0x20 (space)
	// is handled by the separate fullwidthSpace flag, not this range.
	void test_fullwidth_boundaries() {
		TS_ASSERT_EQUALS(latinFullwidth(0x001F, kLatinFullwidth, false), (uint32)0x001F);
		TS_ASSERT_EQUALS(latinFullwidth(0x007F, kLatinFullwidth, false), (uint32)0x007F);
		TS_ASSERT_EQUALS(latinFullwidth(0x001F, kLatinFullwidth, true), (uint32)0x001F);
		TS_ASSERT_EQUALS(latinFullwidth(0x007F, kLatinFullwidth, true), (uint32)0x007F);
	}

	// The space is remapped to U+3000 (IDEOGRAPHIC SPACE) only when
	// fullwidthSpace is requested; otherwise it stays a normal ASCII space.
	void test_fullwidth_space_flag() {
		TS_ASSERT_EQUALS(latinFullwidth(0x0020, kLatinFullwidth, false), (uint32)0x0020);
		TS_ASSERT_EQUALS(latinFullwidth(0x0020, kLatinFullwidth, true), (uint32)0x3000);
	}

	// The glyph character is remapped for the characters the text protocol
	// is classified by, which is exactly why GfxText16 classifies the RAW
	// character (readChar()) and maps only where it measures or draws
	// (glyphChar()): '@' would otherwise hit the 0xFF20 line-break case,
	// '|' would never reach the SCI1.1 code case, '\\' would not start the
	// PQ2 newline escape, and ' ' would not be a word break with
	// hires_text_latin_space=fullwidth. The raw value itself is never
	// altered - latinFullwidth() returns a new value.
	void test_protocol_characters_remap_only_as_glyphs() {
		const uint32 raw[] = { '|', '@', '\\', ' ' };
		const uint32 glyph[] = { 0xFF5C, 0xFF20, 0xFF3C, 0x3000 };
		for (uint i = 0; i < ARRAYSIZE(raw); i++) {
			uint32 classified = raw[i];
			const uint32 g = latinFullwidth(classified, kLatinFullwidth, true);
			TS_ASSERT_EQUALS(g, glyph[i]);
			TS_ASSERT_EQUALS(classified, raw[i]);
			// Off mode: glyph == raw, so the split is invisible.
			TS_ASSERT_EQUALS(latinFullwidth(raw[i], kLatinOff, true), raw[i]);
		}
		// The collision the split avoids: a remapped '@' IS the fullwidth-@
		// line break GetLongest/Width/Draw switch on.
		TS_ASSERT_EQUALS(latinFullwidth('@', kLatinFullwidth, false), (uint32)0xFF20);
	}

	// A code point outside ASCII entirely (a Hangul syllable) is never
	// touched by any mode.
	void test_non_ascii_is_untouched_in_every_mode() {
		TS_ASSERT_EQUALS(latinFullwidth(0xAC00, kLatinOff, true), (uint32)0xAC00);
		TS_ASSERT_EQUALS(latinFullwidth(0xAC00, kLatinHalf, true), (uint32)0xAC00);
		TS_ASSERT_EQUALS(latinFullwidth(0xAC00, kLatinFullwidth, true), (uint32)0xAC00);
	}

	// asciiGoesToUnicodeFace: only kLatinHalf ever redirects, and only for
	// the printable ASCII range U+0020..U+007E - the boundary values just
	// outside it must stay with the resource face.
	void test_ascii_goes_to_unicode_face_only_in_half_mode() {
		TS_ASSERT(!asciiGoesToUnicodeFace(0x0041, kLatinOff));
		TS_ASSERT(!asciiGoesToUnicodeFace(0x0041, kLatinFullwidth));
		TS_ASSERT(asciiGoesToUnicodeFace(0x0041, kLatinHalf));
	}

	// Until proportional advances exist, proportional routes as half does.
	void test_ascii_goes_to_unicode_face_in_proportional_mode() {
		TS_ASSERT(asciiGoesToUnicodeFace(0x0041, Sci::kLatinProportional));
		TS_ASSERT(asciiGoesToUnicodeFace(0x0020, Sci::kLatinProportional));
		TS_ASSERT(!asciiGoesToUnicodeFace(0x007F, Sci::kLatinProportional));
		TS_ASSERT_EQUALS(latinFullwidth(0x0041, Sci::kLatinProportional, true), (uint32)0x0041);
	}

	void test_ascii_goes_to_unicode_face_boundaries() {
		TS_ASSERT(!asciiGoesToUnicodeFace(0x001F, kLatinHalf));
		TS_ASSERT(asciiGoesToUnicodeFace(0x0020, kLatinHalf));
		TS_ASSERT(asciiGoesToUnicodeFace(0x007E, kLatinHalf));
		TS_ASSERT(!asciiGoesToUnicodeFace(0x007F, kLatinHalf));
	}
};
