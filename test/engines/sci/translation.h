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

#include "common/memstream.h"
#include "sci/engine/translation.h"

/**
 * The sci-<lang>.str table: the run-time translations for strings a fan
 * patch cannot replace because they live inside scripts. The format is
 * small enough that these tests are its specification.
 */
class SciScriptStringsTestSuite : public CxxTest::TestSuite {
public:
	static bool load(Sci::ScriptStrings &t, const char *text) {
		Common::MemoryReadStream in((const byte *)text, strlen(text));
		return t.loadFromStream(in);
	}

	void test_three_fields_is_script_id_text() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "995\t4\t아무것도 가지고 있지 않다!\n"));
		TS_ASSERT_EQUALS(t.entryCount(), 1u);
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(995, 4), out));
		TS_ASSERT_EQUALS(out, Common::String("아무것도 가지고 있지 않다!"));
	}

	void test_lookup_misses_a_different_place() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "995\t4\tx\n"));
		Common::String out;
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(995, 5), out));
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(996, 4), out));
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(), out));
	}

	void test_four_fields_is_room_specific_and_wins_in_its_room_only() {
		// "The mirror." means one thing in room 5 and another elsewhere.
		Sci::ScriptStrings t;
		TS_ASSERT(load(t,
			"14\t2\t거울이다.\n"
			"14\t2\t5\t마법의 거울이다.\n"));
		TS_ASSERT_EQUALS(t.entryCount(), 2u);
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(14, 2, 5), out));
		TS_ASSERT_EQUALS(out, Common::String("마법의 거울이다."));
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(14, 2, 9), out));
		TS_ASSERT_EQUALS(out, Common::String("거울이다."));
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(14, 2), out));
		TS_ASSERT_EQUALS(out, Common::String("거울이다."));
	}

	void test_room_only_entry_does_not_answer_elsewhere() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "14\t2\t5\t마법의 거울이다.\n"));
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(14, 2, 5), out));
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(14, 2, 9), out));
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(14, 2), out));
	}

	void test_text_that_is_all_digits_is_still_text() {
		// A third field of digits with no fourth field is the text, not a room.
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "0\t7\t1234\n"));
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 7), out));
		TS_ASSERT_EQUALS(out, Common::String("1234"));
	}

	void test_comments_blank_lines_and_escaped_newlines() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t,
			"# a comment\n"
			"\n"
			"0\t1\t첫째 줄\\n둘째 줄\n"));
		TS_ASSERT_EQUALS(t.entryCount(), 1u);
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 1), out));
		TS_ASSERT_EQUALS(out, Common::String("첫째 줄\n둘째 줄"));
	}

	void test_text_is_returned_as_utf8_bytes() {
		// Nothing transcodes: the bytes in the file are the bytes in the heap.
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "0\t1\t\xe0\xb8\x81\xf0\x9f\x98\x80\n"));	// U+0E01, U+1F600
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 1), out));
		TS_ASSERT_EQUALS(out.size(), 7u);
		TS_ASSERT_EQUALS((byte)out[0], 0xe0);
		TS_ASSERT_EQUALS((byte)out[3], 0xf0);
	}

	void test_a_line_with_too_few_fields_rejects_the_table() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "995\t4\tok\n995\n"));
		TS_ASSERT(!t.isLoaded());
	}

	void test_buffer_tag_survives_until_the_buffer_is_reused() {
		// A script string copied into a stack buffer keeps its key as long
		// as the buffer still holds it; once something else is written
		// there the key is gone, never a wrong one.
		using Sci::ScriptStrings;
		ScriptStrings t;
		const uint32 buf = ScriptStrings::bufferId(0x10, 0x2c2);
		t.tagBuffer(buf, ScriptStrings::Key(995, 4, 1), "You are carrying nothing!");

		ScriptStrings::Key k = t.keyOf(buf, "You are carrying nothing!");
		TS_ASSERT(k.isSet());
		TS_ASSERT_EQUALS(k.script, 995);
		TS_ASSERT_EQUALS(k.id, 4);
		TS_ASSERT_EQUALS(k.room, 1);

		TS_ASSERT(!t.keyOf(buf, "Something else now").isSet());
		TS_ASSERT(!t.keyOf(ScriptStrings::bufferId(0x10, 0x2c4), "You are carrying nothing!").isSet());
	}

	void test_unloaded_table_translates_nothing() {
		Sci::ScriptStrings t;
		TS_ASSERT(!t.isLoaded());
		Common::String out;
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(995, 4), out));
	}
};
