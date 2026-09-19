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
			put32(out, 0);                // reserved, pads to 20
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
			{ "Save", "저장", 0, 0 },
			{ "look", "보다", 1, 3 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 2), "ko"));
		TS_ASSERT(t.isLoaded());
		TS_ASSERT_EQUALS(t.entryCount(), 2u);
		Common::U32String out;
		TS_ASSERT(t.translate("Save", out, 0, 0));
		TS_ASSERT_EQUALS(out, Common::U32String("저장"));
		TS_ASSERT(!t.translate("absent", out, 0, 0));
	}

	void test_same_source_resolves_by_hint() {
		// The case the review found: one English line, three places, three
		// translations. LB1's bundle has 86 such groups. The (resource,
		// index) hint must pick the right one, not the first one.
		static const Pair pairs[] = {
			{ "You see a mirror.", "거울 A", 32, 15 },
			{ "You see a mirror.", "거울 B", 34, 33 },
			{ "You see a mirror.", "거울 C", 73, 47 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 3), "ko"));
		Common::U32String out;
		TS_ASSERT(t.translate("You see a mirror.", out, 34, 33));
		TS_ASSERT_EQUALS(out, Common::U32String("거울 B"));
		TS_ASSERT(t.translate("You see a mirror.", out, 73, 47));
		TS_ASSERT_EQUALS(out, Common::U32String("거울 C"));
		TS_ASSERT(t.translate("You see a mirror.", out, 32, 15));
		TS_ASSERT_EQUALS(out, Common::U32String("거울 A"));
	}

	void test_unmatched_hint_falls_back_to_first_source_match() {
		// A bundle built from another release: the source is present but
		// under different numbers. Returning the first match is right far
		// more often than returning nothing, and is what the engine does;
		// the warning it emits is the visibility, not a refusal.
		static const Pair pairs[] = {
			{ "Two lamps.", "램프 둘", 43, 3 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 1), "ko"));
		Common::U32String out;
		TS_ASSERT(t.translate("Two lamps.", out, 999, 999));
		TS_ASSERT_EQUALS(out, Common::U32String("램프 둘"));
	}

	void test_hint_is_matched_by_resource_and_index_together() {
		// Same resource, different index must not be treated as a match.
		static const Pair pairs[] = {
			{ "Yes.", "네", 10, 1 },
			{ "Yes.", "예", 10, 2 },
		};
		Sci::Translation t;
		TS_ASSERT(t.loadFromMemory(buildBundle(pairs, 2), "ko"));
		Common::U32String out;
		TS_ASSERT(t.translate("Yes.", out, 10, 2));
		TS_ASSERT_EQUALS(out, Common::U32String("예"));
	}

	void test_unloaded_bundle_translates_nothing() {
		// The engine must keep the original text when no bundle is present,
		// rather than returning an empty string.
		Sci::Translation t;
		TS_ASSERT(!t.isLoaded());
		Common::U32String out("sentinel");
		TS_ASSERT(!t.translate("Save", out));
		TS_ASSERT_EQUALS(out, Common::U32String("sentinel"));
		TS_ASSERT_EQUALS(t.entryCount(), 0u);
	}
};
