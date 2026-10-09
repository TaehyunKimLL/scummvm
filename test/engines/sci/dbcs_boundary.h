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

#include "common/fs.h"
#include "common/stream.h"
#include "common/str.h"
#include "common/system.h"

#include "sci/graphics/dbcs.h"

// Character-boundary stepping for the SCI edit control.
//
// This exists because of a crash, not a cosmetic bug. The edit control used
// to move and delete one BYTE at a time. Removing one byte of a double-byte
// character leaves half a character in the buffer, and GfxText16::Box()
// walks the string by decoding: it reads a lead byte, takes the following
// byte as the trail, and steps two. When the trail is the terminator it
// reads past the end of the string. The game therefore died on the next
// REDRAW, some distance from the edit that caused it, which is why it
// presented as "typing Korean and then deleting kills the game".
//
// The reason this needs a parse from the start of the string, and not a
// look at the byte before the cursor, is that EUC-KR trail bytes occupy
// 0xA1..0xFE - exactly the same range as lead bytes. A byte in isolation
// cannot say whether it begins a character or ends one. The tests below
// pin both readings of the same byte value.

class DbcsBoundaryTestSuite : public CxxTest::TestSuite {
	// "문" = B9 AE, "을" = C0 BB in EUC-KR.
	static Common::String mun() {
		return Common::String("\xB9\xAE", 2);
	}
	static Common::String eul() {
		return Common::String("\xC0\xBB", 2);
	}

public:
	// Reading the source needs a filesystem, which needs a g_system.
	void setUp() {
		Common::install_null_g_system();
	}

	void tearDown() {
		Common::uninstall_null_g_system();
	}

	// One Korean character: the cursor goes from 2 to 0, never to 1.
	void test_left_skips_a_whole_korean_character() {
		Common::String t = mun();
		TS_ASSERT_EQUALS(Sci::stepCharLeft(t, 2), 0u);
	}

	// ASCII still moves one byte at a time.
	void test_left_moves_one_byte_through_ascii() {
		Common::String t("abc");
		TS_ASSERT_EQUALS(Sci::stepCharLeft(t, 3), 2u);
		TS_ASSERT_EQUALS(Sci::stepCharLeft(t, 1), 0u);
	}

	// Mixed content is where a byte-local guess fails. In "ab문" the byte
	// at offset 3 is a TRAIL byte whose value (0xAE) is also a legal lead
	// byte; only the parse from offset 0 knows it is not one.
	void test_left_after_ascii_then_korean() {
		Common::String t = Common::String("ab") + mun();
		TS_ASSERT_EQUALS(Sci::stepCharLeft(t, 4), 2u);
	}

	// Two Korean characters: from the end, back to the second one's start.
	void test_left_between_two_korean_characters() {
		Common::String t = mun() + eul();
		TS_ASSERT_EQUALS(Sci::stepCharLeft(t, 4), 2u);
		TS_ASSERT_EQUALS(Sci::stepCharLeft(t, 2), 0u);
	}

	// Rightward stepping has to agree, or Delete splits what Backspace
	// keeps whole.
	void test_right_skips_a_whole_korean_character() {
		Common::String t = mun() + eul();
		TS_ASSERT_EQUALS(Sci::stepCharRight(t, 0), 2u);
		TS_ASSERT_EQUALS(Sci::stepCharRight(t, 2), 4u);
	}

	void test_right_moves_one_byte_through_ascii() {
		Common::String t("abc");
		TS_ASSERT_EQUALS(Sci::stepCharRight(t, 0), 1u);
	}

	// Never past the ends.
	void test_does_not_run_off_either_end() {
		Common::String t = mun();
		TS_ASSERT_EQUALS(Sci::stepCharLeft(t, 0), 0u);
		TS_ASSERT_EQUALS(Sci::stepCharRight(t, 2), 2u);

		Common::String empty;
		TS_ASSERT_EQUALS(Sci::stepCharLeft(empty, 0), 0u);
		TS_ASSERT_EQUALS(Sci::stepCharRight(empty, 0), 0u);
	}

	// The crash reproduction, stated as an invariant: deleting backwards
	// from the end, repeatedly, must never leave an odd remainder inside a
	// double-byte character. Walk a mixed string to empty and check every
	// intermediate state is a whole number of characters.
	void test_repeated_backspace_never_leaves_half_a_character() {
		Common::String t = Common::String("ab") + mun() + eul();
		while (!t.empty()) {
			const uint start = Sci::stepCharLeft(t, t.size());
			t.erase(start, t.size() - start);

			// Re-parse what remains: every character must be complete.
			uint i = 0;
			while (i < t.size()) {
				if (Sci::isEucKrLeadByte((byte)t[i])) {
					// A lead byte must have a trail byte after it.
					TS_ASSERT(i + 1 < t.size());
					i += 2;
				} else {
					i += 1;
				}
			}
			TS_ASSERT_EQUALS(i, t.size());
		}
	}

	// The helpers above are correct in isolation, but the bug was that the
	// EDIT CONTROL did not use them - it stepped by byte. Testing the
	// helper alone passes whether or not the control calls it, so pin the
	// call sites: reverting the fix has to fail a test, not just look
	// wrong.
	void test_edit_control_steps_by_character_not_by_byte() {
		Common::String src = readControls16();

		// Backspace and Delete must go through the boundary helpers.
		TS_ASSERT(contains(src, "stepCharLeft(text, cursorPos)"));
		TS_ASSERT(contains(src, "stepCharRight(text, cursorPos)"));

		// And the byte-at-a-time forms they replaced must be gone. These
		// are what left half a character behind.
		TS_ASSERT(!contains(src, "cursorPos--; text.deleteChar(cursorPos)"));
		TS_ASSERT(!contains(src, "cursorPos--; textChanged = true"));
		TS_ASSERT(!contains(src, "cursorPos++; textChanged = true"));
	}

private:
	static bool contains(const Common::String &hay, const char *needle) {
		return strstr(hay.c_str(), needle) != nullptr;
	}

	static Common::String readControls16() {
		Common::String path =
		    Common::String(SCI_TEST_SRCDIR) + "/engines/sci/graphics/controls16.cpp";
		Common::FSNode node(Common::Path(path, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(("cannot read " + path).c_str());
			return Common::String();
		}
		uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		TS_ASSERT_LESS_THAN(0u, out.size());
		return out;
	}
};
