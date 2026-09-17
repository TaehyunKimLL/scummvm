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
