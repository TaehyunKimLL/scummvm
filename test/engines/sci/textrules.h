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
#include "common/str.h"
#include "graphics/hires_text/font_map.h"
#include "sci/graphics/hirestextsettings.h"
#include "sci/graphics/textlatin.h"

/**
 * The old hires_text_latin modes, each now a map recipe (design section 6.6),
 * asserted through the same TextCompose::glyphCode()/goesToUnicodeFace()
 * routing hirestextsettings.h's suite exercises: this file only pins that
 * each recipe reproduces the mode it replaces.
 */
class SciTextRulesTestSuite : public CxxTest::TestSuite {
private:
	static Sci::FontSettings settings(const char *text) {
		Common::String full = "[map]\nversion=2\n";
		full += text;
		Common::MemoryReadStream stream((const byte *)full.c_str(), full.size());
		Graphics::HiResMap map;
		Common::Array<Common::String> qualifiers;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(stream, Common::Path("/maps"), qualifiers, Graphics::kHiResKeysSci, map));
		return Sci::resolveFontSettings(map, true, 0, Graphics::HiResIniOverrides(), Common::Path("/maps"),
										Common::Path("/games/kq1"));
	}

public:
	// ---- [latin] mode=off -> the default (no range rule at all) ----------

	void test_off_recipe_keeps_ascii_on_the_resource_font() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\n");
		const uint32 code = Sci::TextCompose::glyphCode(s.plan, 'A');
		TS_ASSERT_EQUALS(code, Graphics::kHiResGameCodeBase + (uint32)'A');
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, code));
	}

	// ---- [latin] mode=half -> range.basic-latin=same + advance=cell -------

	void test_half_recipe_routes_ascii_to_the_face_at_cell_advance() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nadvance.basic-latin=cell\n");
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		TS_ASSERT_EQUALS(s.plan.advanceFor('A'), Graphics::kHiResAdvanceCell);
	}

	// ---- [latin] mode=proportional + metrics=game|font ---------------------

	void test_proportional_recipe_game_metrics() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nadvance.basic-latin=game\n");
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		TS_ASSERT_EQUALS(s.plan.advanceFor('A'), Graphics::kHiResAdvanceGame);
	}

	void test_proportional_recipe_font_metrics() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nadvance.basic-latin=font\n");
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 'A'));
		TS_ASSERT_EQUALS(s.plan.advanceFor('A'), Graphics::kHiResAdvanceFont);
	}

	// ---- [latin] font=original / mode=off with an explicit face -----------
	// beats even a face= that would otherwise route the range.

	void test_latin_font_original_recipe_forces_the_resource_font() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=original\n");
		const uint32 code = Sci::TextCompose::glyphCode(s.plan, 'A');
		TS_ASSERT_EQUALS(code, Graphics::kHiResGameCodeBase + (uint32)'A');
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, code));
	}

	// ---- [latin] mode=fullwidth (+ space=fullwidth) -> [glyphs] ------------

	void test_fullwidth_recipe_remaps_the_printable_range() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n");
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 0x21), 0xFF01u);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 0x7E), 0xFF5Eu);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 'A'), 0xFF21u);
		// '|', '\', '@': the protocol characters the old split existed for -
		// their classification (readChar()) is untouched; only the glyph
		// step remaps them.
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, '|'), 0xFF5Cu);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, '\\'), 0xFF3Cu);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, '@'), 0xFF20u);
	}

	void test_fullwidth_recipe_boundaries_stay_on_the_resource_font() {
		// 0x1F and 0x7F are outside the remapped range and outside
		// basic-latin's own span (U+0020-007E), but the SCI engine scope's
		// own built-in rules keep C0 controls and DEL on the resource font
		// regardless (sciEngineScope(); a map may still override either
		// span explicitly) - unlike a code point genuinely in no block at
		// all, they are not left to the id's plain chain.
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n");
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 0x1F), Graphics::kHiResGameCodeBase + 0x1Fu);
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 0x7F), Graphics::kHiResGameCodeBase + 0x7Fu);
	}

	// A C0/DEL scenario: even with a face routing basic-latin away and
	// missing= set, tab (0x09, a C0 control) and DEL (0x7F) still reach the
	// game's own font - sciEngineScope()'s built-in
	// range.U+0000-001F/U+007F=original rules decline them before
	// Graphics::pickGlyph() (and its missing= box step) is ever reached, so
	// the box never stands in for either.
	void test_c0_and_del_reach_the_game_font_even_with_missing_set() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\nmissing=u+25a1\n");
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 0x09), Graphics::kHiResGameCodeBase + 0x09u); // tab
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 0x7F), Graphics::kHiResGameCodeBase + 0x7Fu); // DEL
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, Sci::TextCompose::glyphCode(s.plan, 0x09)));
		TS_ASSERT(!Sci::TextCompose::goesToUnicodeFace(s.plan, Sci::TextCompose::glyphCode(s.plan, 0x7F)));
	}

	void test_fullwidth_recipe_space_flag() {
		const Sci::FontSettings without = settings("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n");
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(without.plan, ' '),
						 Graphics::kHiResGameCodeBase + (uint32)' ');

		const Sci::FontSettings with =
			settings("[font]\nface=KO.SVF\n[glyphs]\n0x21-0x7E=+0xFEE0\n0x20=u+3000\n");
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(with.plan, ' '), 0x3000u);
	}

	void test_fullwidth_recipe_leaves_non_ascii_untouched() {
		const Sci::FontSettings s = settings("[font]\nface=KO.SVF\nrange.basic-latin=same\n[glyphs]\n0x21-0x7E=+0xFEE0\n");
		TS_ASSERT_EQUALS(Sci::TextCompose::glyphCode(s.plan, 0xAC00), 0xAC00u);
		TS_ASSERT(Sci::TextCompose::goesToUnicodeFace(s.plan, 0xAC00));
	}
};
