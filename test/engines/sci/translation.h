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

#include "sci/engine/translation.h"

/**
 * The SCITRS key normalisation and hash are duplicated in the bundle builder
 * (harness/i18n/m5mktrs.py). If the two ever disagree, every lookup silently
 * misses and the game renders untranslated text - a failure with no error
 * message. These tests pin the rule and the hash values.
 */
class SciTranslationTestSuite : public CxxTest::TestSuite {
public:
	void test_normalise_collapses_whitespace() {
		// SCI pads menu items and uses \r for line breaks, so the string the
		// engine holds is not byte-identical to what a translator typed.
		TS_ASSERT_EQUALS(Sci::Translation::normalise("  Save  "), Common::String("Save"));
		TS_ASSERT_EQUALS(Sci::Translation::normalise("a\r\nb"), Common::String("a b"));
		TS_ASSERT_EQUALS(Sci::Translation::normalise("a \t b"), Common::String("a b"));
		TS_ASSERT_EQUALS(Sci::Translation::normalise("a    b"), Common::String("a b"));
		TS_ASSERT_EQUALS(Sci::Translation::normalise("\r\n"), Common::String(""));
		TS_ASSERT_EQUALS(Sci::Translation::normalise(""), Common::String(""));
	}

	void test_normalise_preserves_everything_else() {
		// Deliberately NOT case folding or stripping punctuation: two menu
		// entries differing only in case are different strings.
		TS_ASSERT_EQUALS(Sci::Translation::normalise("Look"), Common::String("Look"));
		TS_ASSERT_EQUALS(Sci::Translation::normalise("look"), Common::String("look"));
		TS_ASSERT_EQUALS(Sci::Translation::normalise("a.b,c!"), Common::String("a.b,c!"));
	}

	void test_normalise_is_idempotent() {
		const char *inputs[] = { "  Save  ", "a\r\nb", "x", "", "  a  b  " };
		for (int i = 0; i < 5; i++) {
			Common::String once = Sci::Translation::normalise(inputs[i]);
			TS_ASSERT_EQUALS(Sci::Translation::normalise(once), once);
		}
	}

	void test_hash_matches_reference_fnv1a() {
		// FNV-1a 32 reference values, independently computable:
		//   python3 -c "h=0x811C9DC5
		//   for b in b'Save': h^=b; h=(h*0x01000193)&0xFFFFFFFF
		//   print(hex(h))"
		TS_ASSERT_EQUALS(Sci::Translation::hash(""), 0x811C9DC5u);
		TS_ASSERT_EQUALS(Sci::Translation::hash("a"), 0xE40C292Cu);
		TS_ASSERT_EQUALS(Sci::Translation::hash("Save"), 0x4D2D5D68u);
		TS_ASSERT_EQUALS(Sci::Translation::hash("look"), 0xE6EF5696u);
	}

	void test_hash_differs_for_near_miss_keys() {
		// A weak hash here would put colliding keys in the same bucket and
		// rely entirely on the string comparison; check the obvious pairs.
		TS_ASSERT_DIFFERS(Sci::Translation::hash("Save"), Sci::Translation::hash("save"));
		TS_ASSERT_DIFFERS(Sci::Translation::hash("ab"), Sci::Translation::hash("ba"));
		TS_ASSERT_DIFFERS(Sci::Translation::hash("a"), Sci::Translation::hash("aa"));
	}

	// ---- In-memory bundle builder -------------------------------------
	//
	// Lays out a SCITRS bundle exactly as m5mktrs.py does, so the lookup
	// can be exercised without a data directory. One language, entries
	// sorted by bucket, pool of NUL-terminated UTF-8.

	struct Pair {
		const char *src;
		const char *dst;
		uint16 res;
		uint16 idx;
		byte kind;	///< Translation::Key::Kind; 0 = written by an older builder
		uint16 room;	///< 0 = any room (what an older builder wrote)
	};

	static void put16(Common::Array<byte> &b, uint16 v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
	static void put32(Common::Array<byte> &b, uint32 v) { for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xFF); }

	static Common::Array<byte> buildBundle(const Pair *pairs, uint count, uint32 bucketCount = 8) {
		// Pool first, remembering offsets.
		Common::Array<byte> pool;
		Common::Array<uint32> srcOff, dstOff;
		for (uint i = 0; i < count; i++) {
			srcOff.push_back(pool.size());
			for (const char *p = pairs[i].src; *p; p++) pool.push_back(*p);
			pool.push_back(0);
			dstOff.push_back(pool.size());
			for (const char *p = pairs[i].dst; *p; p++) pool.push_back(*p);
			pool.push_back(0);
		}

		// Order entries by bucket, as the file format requires.
		Common::Array<uint> order;
		Common::Array<uint32> hashes;
		for (uint i = 0; i < count; i++)
			hashes.push_back(Sci::Translation::hash(Sci::Translation::normalise(pairs[i].src)));
		for (uint32 b = 0; b < bucketCount; b++)
			for (uint i = 0; i < count; i++)
				if ((hashes[i] & (bucketCount - 1)) == b)
					order.push_back(i);

		const uint32 headerSize = 0x18, langEntrySize = 32, entrySize = 20;
		const uint32 entriesOff = headerSize + langEntrySize;
		const uint32 bucketsOff = entriesOff + count * entrySize;
		const uint32 poolOff = bucketsOff + (bucketCount + 1) * 4;

		Common::Array<byte> out;
		const char magic[8] = { 'S', 'C', 'I', 'T', 'R', 'S', 0, 0 };
		for (int i = 0; i < 8; i++) out.push_back(magic[i]);
		put16(out, 1);                    // version
		put16(out, 1);                    // langCount
		put32(out, poolOff);
		put32(out, pool.size());
		while (out.size() < headerSize) out.push_back(0);

		const char code[8] = { 'k', 'o', 0, 0, 0, 0, 0, 0 };
		for (int i = 0; i < 8; i++) out.push_back(code[i]);
		put32(out, count);
		put32(out, entriesOff);
		put32(out, bucketCount);
		put32(out, bucketsOff);
		while (out.size() < headerSize + langEntrySize) out.push_back(0);

		for (uint k = 0; k < order.size(); k++) {
			const uint i = order[k];
			put32(out, hashes[i]);
			put32(out, srcOff[i]);
			put32(out, dstOff[i]);
			put16(out, pairs[i].res);
			put16(out, pairs[i].idx);
			out.push_back(pairs[i].kind); // byte 16: Key::Kind, 0 in old bundles
			put16(out, pairs[i].room);    // bytes 17-18: room, 0 in old bundles
			out.push_back(0);
		}

		// Bucket table: bucket b spans [lo[b], lo[b+1]).
		uint32 pos = 0;
		for (uint32 b = 0; b < bucketCount; b++) {
			put32(out, pos);
			for (uint k = 0; k < order.size(); k++)
				if ((hashes[order[k]] & (bucketCount - 1)) == b)
					pos++;
		}
		put32(out, pos);

		for (uint i = 0; i < pool.size(); i++) out.push_back(pool[i]);
		return out;
	}

	void test_in_memory_bundle_round_trips() {
		static const Pair pairs[] = {
			{ "Save", "저장", 0, 0, 0 },
			{ "look", "보다", 1, 3, 0 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 2), "ko"));
		TS_ASSERT(t.isLoaded());
		TS_ASSERT_EQUALS(t.entryCount(), 2u);
		Common::String out;
		TS_ASSERT(t.translate("Save", out, Sci::Translation::Key::text(0, 0)));
		TS_ASSERT_EQUALS(out, Common::String("저장"));
		TS_ASSERT(!t.translate("absent", out, Sci::Translation::Key::text(0, 0)));
	}

	void test_same_source_resolves_by_hint() {
		// The case the review found: one English line, three places, three
		// translations. LB1's bundle has 86 such groups. The (resource,
		// index) hint must pick the right one, not the first one.
		static const Pair pairs[] = {
			{ "You see a mirror.", "거울 A", 32, 15, 0 },
			{ "You see a mirror.", "거울 B", 34, 33, 0 },
			{ "You see a mirror.", "거울 C", 73, 47, 0 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 3), "ko"));
		Common::String out;
		TS_ASSERT(t.translate("You see a mirror.", out, Sci::Translation::Key::text(34, 33)));
		TS_ASSERT_EQUALS(out, Common::String("거울 B"));
		TS_ASSERT(t.translate("You see a mirror.", out, Sci::Translation::Key::text(73, 47)));
		TS_ASSERT_EQUALS(out, Common::String("거울 C"));
		TS_ASSERT(t.translate("You see a mirror.", out, Sci::Translation::Key::text(32, 15)));
		TS_ASSERT_EQUALS(out, Common::String("거울 A"));
	}

	void test_unmatched_hint_falls_back_to_first_source_match() {
		// A bundle built from another release: the source is present but
		// under different numbers. Returning the first match is right far
		// more often than returning nothing, and is what the engine does;
		// the warning it emits is the visibility, not a refusal.
		static const Pair pairs[] = {
			{ "Two lamps.", "램프 둘", 43, 3, 0 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 1), "ko"));
		Common::String out;
		TS_ASSERT(t.translate("Two lamps.", out, Sci::Translation::Key::text(999, 999)));
		TS_ASSERT_EQUALS(out, Common::String("램프 둘"));
	}

	void test_hint_is_matched_by_resource_and_index_together() {
		// Same resource, different index must not be treated as a match.
		static const Pair pairs[] = {
			{ "Yes.", "네", 10, 1, 0 },
			{ "Yes.", "예", 10, 2, 0 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 2), "ko"));
		Common::String out;
		TS_ASSERT(t.translate("Yes.", out, Sci::Translation::Key::text(10, 2)));
		TS_ASSERT_EQUALS(out, Common::String("예"));
	}

	void test_old_bundle_kind_zero_reads_as_text() {
		// Bundles written before the kind byte existed have a zero there.
		// They must keep matching a Key::text hint, not become unhintable.
		static const Pair pairs[] = {
			{ "Save", "저장 A", 5, 1, 0 },
			{ "Save", "저장 B", 5, 2, 0 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 2), "ko"));
		Common::String out;
		TS_ASSERT(t.translate("Save", out, Sci::Translation::Key::text(5, 2)));
		TS_ASSERT_EQUALS(out, Common::String("저장 B"));
	}

	void test_script_and_text_keys_are_distinct_namespaces() {
		// Script 300 string 4 and text resource 300 index 4 are different
		// places. A script key must not pick up the text entry and vice
		// versa, even though the numbers coincide.
		static const Pair pairs[] = {
			{ "Look", "보다 (text)",   300, 4, Sci::Translation::Key::kText },
			{ "Look", "보다 (script)", 300, 4, Sci::Translation::Key::kScript },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 2), "ko"));
		Common::String out;
		TS_ASSERT(t.translate("Look", out, Sci::Translation::Key::script(300, 4)));
		TS_ASSERT_EQUALS(out, Common::String("보다 (script)"));
		TS_ASSERT(t.translate("Look", out, Sci::Translation::Key::text(300, 4)));
		TS_ASSERT_EQUALS(out, Common::String("보다 (text)"));
	}

	void test_translation_is_returned_as_utf8_bytes() {
		// The pool is UTF-8 and the caller gets those bytes untouched: no
		// transcoding to a code page, which is where non-cp949 characters
		// used to vanish. Thai ko kai is 3 bytes and has no cp949 form.
		static const Pair pairs[] = {
			{ "Begin", "\xE0\xB8\x81", 1, 1, 0 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 1), "ko"));
		Common::String out;
		TS_ASSERT(t.translate("Begin", out));
		TS_ASSERT_EQUALS(out.size(), 3u);
		TS_ASSERT_EQUALS((byte)out[0], 0xE0);
		TS_ASSERT_EQUALS((byte)out[1], 0xB8);
		TS_ASSERT_EQUALS((byte)out[2], 0x81);
	}

	void test_room_specific_entry_wins_in_its_room_only() {
		// "The mirror" is one thing in room 5 and another in room 9; the
		// entry that names the room wins there, the any-room entry elsewhere.
		using Sci::Translation;
		static const Pair pairs[] = {
			{ "The mirror.", "거울이다.",        14, 2, Translation::Key::kText, 0 },
			{ "The mirror.", "마법의 거울이다.", 14, 2, Translation::Key::kText, 5 },
		};
		Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 2), "ko"));
		Common::String out;
		TS_ASSERT(t.translate("The mirror.", out, Translation::Key::text(14, 2, 5)));
		TS_ASSERT_EQUALS(out, Common::String("마법의 거울이다."));
		TS_ASSERT(t.translate("The mirror.", out, Translation::Key::text(14, 2, 9)));
		TS_ASSERT_EQUALS(out, Common::String("거울이다."));
		TS_ASSERT(t.translate("The mirror.", out, Translation::Key::text(14, 2)));
		TS_ASSERT_EQUALS(out, Common::String("거울이다."));
	}

	void test_buffer_tag_survives_until_the_buffer_is_reused() {
		// A script string copied into a stack buffer keeps its key as long
		// as the buffer still holds it; once something else is written
		// there the key is gone, never a wrong one.
		using Sci::Translation;
		Translation t;
		const uint32 buf = Translation::bufferId(0x10, 0x2c2);
		t.tagBuffer(buf, Translation::Key::script(995, 4, 1), "You are carrying nothing!");

		Translation::Key k = t.keyOf(buf, "You are carrying nothing!");
		TS_ASSERT(k.isSet());
		TS_ASSERT_EQUALS(k.kind, Translation::Key::kScript);
		TS_ASSERT_EQUALS(k.number, 995);
		TS_ASSERT_EQUALS(k.index, 4);
		TS_ASSERT_EQUALS(k.room, 1);

		TS_ASSERT(!t.keyOf(buf, "Something else now").isSet());
		TS_ASSERT(!t.keyOf(Translation::bufferId(0x10, 0x2c4), "You are carrying nothing!").isSet());
	}

	void test_unloaded_bundle_translates_nothing() {
		// The engine must keep the original text when no bundle is present,
		// rather than returning an empty string.
		Sci::Translation t;
		TS_ASSERT(!t.isLoaded());
		Common::String out("sentinel");
		TS_ASSERT(!t.translate("Save", out));
		TS_ASSERT_EQUALS(out, Common::String("sentinel"));
		TS_ASSERT_EQUALS(t.entryCount(), 0u);
	}
};
