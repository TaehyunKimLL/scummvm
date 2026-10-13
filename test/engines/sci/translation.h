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

	void test_digits_then_tab_is_a_room_but_digits_alone_are_text() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "0\t7\t12\t\n0\t8\t12\n0\t9\t70000\tx\n"));
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 7, 12), out));
		TS_ASSERT_EQUALS(out, Common::String(""));
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(0, 7), out));
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 8), out));
		TS_ASSERT_EQUALS(out, Common::String("12"));
		// Not a valid room, so not a room: the digits are text.
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(0, 9), out));
		TS_ASSERT_EQUALS(out, Common::String("70000\tx"));
	}

	void test_bom_is_stripped() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "\xEF\xBB\xBF" "995\t4\tx\n"));
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(995, 4), out));
		TS_ASSERT_EQUALS(out, Common::String("x"));
	}

	void test_non_numeric_key_rejects_the_table() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "0\t1\tok\n99a\t4\tx\n"));
		TS_ASSERT(!t.isPresent());
		TS_ASSERT_EQUALS(t.entryCount(), 0u);
		TS_ASSERT(!load(t, "\t4\tx\n"));
		TS_ASSERT(!load(t, "995\t\tx\n"));
		TS_ASSERT(!load(t, "995\t4x\tx\n"));
		TS_ASSERT(!load(t, "-1\t4\tx\n"));

		Sci::ScriptStrings::Key key;
		Common::String text;
		const char *error = nullptr;
		TS_ASSERT_EQUALS(Sci::ScriptStrings::parseLine("99a\t4\tx", key, text, error), Sci::ScriptStrings::kLineError);
		TS_ASSERT_EQUALS(Common::String(error), Common::String("bad script number"));
		TS_ASSERT_EQUALS(Sci::ScriptStrings::parseLine("99\t4a\tx", key, text, error), Sci::ScriptStrings::kLineError);
		TS_ASSERT_EQUALS(Common::String(error), Common::String("bad string id"));
	}

	void test_key_over_65534_rejects_the_table() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "70000\t4\tx\n"));
		TS_ASSERT(!load(t, "1\t65535\tx\n"));
		TS_ASSERT(!load(t, "99999999999\t4\tx\n"));
		TS_ASSERT(load(t, "65534\t65534\tx\n"));
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(65534, 65534), out));
	}

	void test_indented_line_rejects_the_table() {
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, " #c\n"));
		TS_ASSERT(!load(t, "   \n"));
		TS_ASSERT(!load(t, " 995\t4\tx\n"));
		TS_ASSERT(!t.isPresent());
	}

	void test_duplicate_key_later_line_wins() {
		Sci::ScriptStrings t;
		TS_ASSERT(load(t, "995\t4\tfirst\n995\t4\tsecond\n"));
		TS_ASSERT_EQUALS(t.entryCount(), 1u);
		Common::String out;
		TS_ASSERT(t.lookup(Sci::ScriptStrings::Key(995, 4), out));
		TS_ASSERT_EQUALS(out, Common::String("second"));
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

	// The PQ2 inventory: StrCpy "You are carrying..." (997:19) into a
	// buffer, StrCat a script string, StrCat plain text (an item name
	// kFormat already wrote), display the buffer.
	static void buildInventoryLine(Sci::ScriptStrings &t, uint32 buf) {
		using Sci::ScriptStrings;
		t.tagBuffer(buf, ScriptStrings::Key(997, 19, 1), "You are carrying...");
		t.appendToBuffer(buf, "You are carrying...", ScriptStrings::Key(995, 7, 1),
		                 ScriptStrings::Key::kNoBuffer, " a ");
		t.appendToBuffer(buf, "You are carrying... a ", ScriptStrings::Key(),
		                 ScriptStrings::bufferId(0x10, 0x100), "머니 클립.");
	}

	void test_strcat_onto_a_tagged_buffer_translates_each_part() {
		using Sci::ScriptStrings;
		ScriptStrings t;
		TS_ASSERT(load(t,
			"997\t19\t가진 것...\n"
			"995\t7\t: \n"));
		const uint32 buf = ScriptStrings::bufferId(0x10, 0x2c2);
		buildInventoryLine(t, buf);

		ScriptStrings::Key k = t.keyOf(buf, "You are carrying... a 머니 클립.");
		TS_ASSERT(k.isSet());
		TS_ASSERT(k.isBuffer());
		Common::String out;
		TS_ASSERT(t.lookup(k, out));
		TS_ASSERT_EQUALS(out, Common::String("가진 것...: 머니 클립."));

		// Print StrCpy's the sentence into its own buffer: the tag goes along.
		const uint32 print = ScriptStrings::bufferId(0x10, 0x582);
		t.copyBufferTag(print, buf, "You are carrying... a 머니 클립.");
		out.clear();
		TS_ASSERT(t.lookup(t.keyOf(print, "You are carrying... a 머니 클립."), out));
		TS_ASSERT_EQUALS(out, Common::String("가진 것...: 머니 클립."));
		// A copy of other text into it takes no tag.
		t.copyBufferTag(print, ScriptStrings::bufferId(0x10, 0x600), "Other");
		TS_ASSERT(!t.keyOf(print, "Other").isSet());
		// The buffer reused: no key, never a stale translation.
		TS_ASSERT(!t.keyOf(buf, "You are carrying... a ").isSet());
		TS_ASSERT(!t.keyOf(buf, "Something else").isSet());
	}

	void test_strcat_part_without_translation_stays_as_written() {
		using Sci::ScriptStrings;
		ScriptStrings t;
		TS_ASSERT(load(t, "997\t19\t가진 것...\n"));
		const uint32 buf = ScriptStrings::bufferId(0x10, 0x2c2);
		buildInventoryLine(t, buf);
		Common::String out;
		TS_ASSERT(t.lookup(t.keyOf(buf, "You are carrying... a 머니 클립."), out));
		TS_ASSERT_EQUALS(out, Common::String("가진 것... a 머니 클립."));
	}

	void test_strcat_with_no_translation_changes_nothing() {
		using Sci::ScriptStrings;
		// No table: lookup never answers, the display keeps the buffer.
		{
			ScriptStrings t;
			const uint32 buf = ScriptStrings::bufferId(0x10, 0x2c2);
			buildInventoryLine(t, buf);
			Common::String out;
			TS_ASSERT(!t.lookup(t.keyOf(buf, "You are carrying... a 머니 클립."), out));
		}
		// A table without these strings: same.
		{
			ScriptStrings t;
			TS_ASSERT(load(t, "14\t2\t거울이다.\n"));
			const uint32 buf = ScriptStrings::bufferId(0x10, 0x2c2);
			buildInventoryLine(t, buf);
			Common::String out;
			TS_ASSERT(!t.lookup(t.keyOf(buf, "You are carrying... a 머니 클립."), out));
		}
	}

	void test_strcat_onto_an_untagged_buffer_stays_untagged() {
		using Sci::ScriptStrings;
		ScriptStrings t;
		TS_ASSERT(load(t, "995\t7\t: \n"));
		const uint32 buf = ScriptStrings::bufferId(0x10, 0x2c2);
		t.appendToBuffer(buf, "Score: ", ScriptStrings::Key(995, 7), ScriptStrings::Key::kNoBuffer, " a ");
		TS_ASSERT(!t.keyOf(buf, "Score:  a ").isSet());
		TS_ASSERT(!t.keyOf(buf, " a ").isSet());
	}

	void test_strcat_of_a_tagged_buffer_carries_its_parts() {
		using Sci::ScriptStrings;
		ScriptStrings t;
		TS_ASSERT(load(t,
			"997\t19\t가진 것...\n"
			"995\t7\t: \n"));
		const uint32 a = ScriptStrings::bufferId(0x10, 0x2c2);
		const uint32 b = ScriptStrings::bufferId(0x10, 0x300);
		t.tagBuffer(a, ScriptStrings::Key(997, 19), "You are carrying...");
		t.tagBuffer(b, ScriptStrings::Key(995, 7), " a ");
		t.appendToBuffer(a, "You are carrying...", ScriptStrings::Key(), b, " a ");
		Common::String out;
		TS_ASSERT(t.lookup(t.keyOf(a, "You are carrying... a "), out));
		TS_ASSERT_EQUALS(out, Common::String("가진 것...: "));
	}

	void test_unloaded_table_translates_nothing() {
		Sci::ScriptStrings t;
		TS_ASSERT(!t.isLoaded());
		Common::String out;
		TS_ASSERT(!t.lookup(Sci::ScriptStrings::Key(995, 4), out));
	}

	void test_script_strings_comments_only_is_present() {
		// A translation whose scripts need no string still ships a
		// sci-<lang>.str: it is the manifest that says the TEXT resources
		// are UTF-8 (I18N_TEXT_DESIGN.md section 4.1). Present, not loaded.
		Sci::ScriptStrings t;
		TS_ASSERT(!load(t, "# only a comment\n"));
		TS_ASSERT(t.isPresent());
		TS_ASSERT(!t.isLoaded());
		TS_ASSERT_EQUALS(t.entryCount(), 0u);

		// A malformed table is neither.
		Sci::ScriptStrings bad;
		TS_ASSERT(!load(bad, "995\t4\tok\n995\n"));
		TS_ASSERT(!bad.isPresent());
		TS_ASSERT(!bad.isLoaded());

		// A missing file is neither.
		Sci::ScriptStrings missing;
		TS_ASSERT(!missing.load("zz-no-such-language"));
		TS_ASSERT(!missing.isPresent());
		TS_ASSERT(!missing.isLoaded());
	}

	static Common::String particles(const char *in) {
		Common::String s(in);
		Sci::ScriptStrings::resolveParticles(s);
		return s;
	}

	// QFG2's "You can't open %s." with the noun in: 문 ends in a consonant,
	// 상자 does not; 길 ends in ㄹ, which takes 로 like a vowel.
	void test_particles_follow_the_last_syllable() {
		TS_ASSERT_EQUALS(particles("\xEB\xAC\xB8\xEC\x9D\x84(\xEB\xA5\xBC)"), "\xEB\xAC\xB8\xEC\x9D\x84");
		TS_ASSERT_EQUALS(particles("\xEC\x83\x81\xEC\x9E\x90\xEC\x9D\x84(\xEB\xA5\xBC)"), "\xEC\x83\x81\xEC\x9E\x90\xEB\xA5\xBC");
		TS_ASSERT_EQUALS(particles("\xEC\x83\x81\xEC\x9E\x90(\xEC\x9D\xB4)\xEB\x8B\xA4."), "\xEC\x83\x81\xEC\x9E\x90\xEB\x8B\xA4.");
		TS_ASSERT_EQUALS(particles("\xEB\xAC\xB8(\xEC\x9D\xB4)\xEB\x8B\xA4."), "\xEB\xAC\xB8\xEC\x9D\xB4\xEB\x8B\xA4.");
		TS_ASSERT_EQUALS(particles("\xEA\xB8\xB8(\xEC\x9C\xBC)\xEB\xA1\x9C"), "\xEA\xB8\xB8\xEB\xA1\x9C");
		TS_ASSERT_EQUALS(particles("\xEB\xAC\xB8(\xEC\x9C\xBC)\xEB\xA1\x9C"), "\xEB\xAC\xB8\xEC\x9C\xBC\xEB\xA1\x9C");
		TS_ASSERT_EQUALS(particles("\xEA\xB8\xB8\xEC\x9D\xB4(\xEA\xB0\x80)"), "\xEA\xB8\xB8\xEC\x9D\xB4");
	}

	void test_particles_look_through_quotes_and_read_digits() {
		// "WIT"는 / 3을 / 2를
		TS_ASSERT_EQUALS(particles("\"WIT\"\xEC\x9D\x80(\xEB\x8A\x94)"), "\"WIT\"\xEB\x8A\x94");
		TS_ASSERT_EQUALS(particles("3\xEC\x9D\x84(\xEB\xA5\xBC)"), "3\xEC\x9D\x84");
		TS_ASSERT_EQUALS(particles("2\xEC\x9D\x84(\xEB\xA5\xBC)"), "2\xEB\xA5\xBC");
	}

	void test_particles_without_a_word_or_marker_stay() {
		TS_ASSERT_EQUALS(particles("(\xEC\x9D\xB4)"), "(\xEC\x9D\xB4)");
		TS_ASSERT_EQUALS(particles("Score (100)"), "Score (100)");
		TS_ASSERT_EQUALS(particles("no markers"), "no markers");
	}
};
