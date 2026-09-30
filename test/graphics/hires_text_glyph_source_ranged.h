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
#include "common/hashmap.h"
#include "common/memstream.h"
#include "graphics/hires_text/font_map.h"
#include "graphics/hires_text/glyph_source_ranged.h"
#include "graphics/hires_text/id_plan.h"

class HiResRangedSourceTestSuite : public CxxTest::TestSuite {
	class Fake : public Graphics::UnicodeGlyphSource {
	public:
		Common::HashMap<uint32, bool> has;
		byte cellWidth() const override { return 16; }
		byte cellHeight() const override { return 16; }
		byte advanceNarrow() const override { return 8; }
		byte advanceWide() const override { return 16; }
		int bitsPerPixel() const override { return 1; }
		int cells(uint32 cp) override { return has.contains(cp) ? 1 : 0; }
		const byte *row(uint32 cp, int y) override { static byte r[2] = { 0xff, 0xff }; return has.contains(cp) ? r : nullptr; }
		uint32 glyphCount() const override { return has.size(); }
	};

	Graphics::HiResIdPlan compile(const char *text, int id) {
		const Common::String full = Common::String("[map]\nversion=2\n") + text;
		Common::MemoryReadStream s((const byte *)full.c_str(), full.size());
		Common::Array<Common::String> q, w;
		Graphics::HiResMap map;
		TS_ASSERT(Graphics::HiResFontMap::loadMap(s, Common::Path("/m", '/'), q, Graphics::kHiResKeysScumm, map));
		return Graphics::compileIdPlan(map, true, id, Graphics::HiResIniOverrides(), Graphics::HiResFontScope(),
									   Common::Path("/m", '/'), Common::Path("/g", '/'), w);
	}

public:
	void test_rule_chain_then_id_chain_by_coverage() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nrange.basic-latin=LAT.SVF\n", 0);
		Fake ko, lat;
		ko.has[0xAC00] = true;
		ko.has['B'] = true;
		lat.has['A'] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(2);
		chains[0].push_back(&ko);                         // idChain = [KO]
		chains[1].push_back(&lat);                        // rule chain = [LAT, KO]
		chains[1].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		Graphics::HiResPick a = Graphics::pickGlyph(p, chains, targets, 'A');
		TS_ASSERT_EQUALS(a.kind, Graphics::HiResPick::kFace);
		TS_ASSERT_EQUALS(a.chain, 1);
		TS_ASSERT_EQUALS(a.face, 0);
		Graphics::HiResPick b = Graphics::pickGlyph(p, chains, targets, 'B');   // LAT lacks B: falls to KO
		TS_ASSERT_EQUALS(b.chain, 1);
		TS_ASSERT_EQUALS(b.face, 1);
		Graphics::HiResPick h = Graphics::pickGlyph(p, chains, targets, 0xAC00);
		TS_ASSERT_EQUALS(h.chain, 0);
	}

	void test_missing_box_from_the_same_chain_else_game() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nmissing=u+25a1\n", 0);
		Fake ko;
		ko.has[0x25A1] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		Graphics::HiResPick m = Graphics::pickGlyph(p, chains, targets, 0xD7A3);
		TS_ASSERT_EQUALS(m.kind, Graphics::HiResPick::kFace);
		TS_ASSERT(m.missingBox);
		TS_ASSERT_EQUALS(m.cp, 0x25A1u);
		ko.has.erase(0x25A1);
		TS_ASSERT_EQUALS(Graphics::pickGlyph(p, chains, targets, 0xD7A3).kind, Graphics::HiResPick::kGame);
	}

	void test_original_rule_and_no_missing_mean_game() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nmissing=u+25a1\nrange.basic-latin=original\n", 0);
		Fake ko;
		ko.has['A'] = true;
		ko.has[0x25A1] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		TS_ASSERT_EQUALS(Graphics::pickGlyph(p, chains, targets, 'A').kind, Graphics::HiResPick::kGame);
	}

	void test_targets_bypass_rules_and_fall_back_to_game() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\n[glyphs]\n0x07=SYM.SVF:u+2620\n0x08=same:u+2620\n", 0);
		Fake ko, sym;
		sym.has[0x2620] = true;
		ko.has[0x2620] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko);
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		targets.push_back(&sym);      // target 0: SYM.SVF
		targets.push_back(nullptr);   // target 1: same
		uint32 cp = 0;
		p.glyphFor(0x07, 0x07, cp);
		Graphics::HiResPick t = Graphics::pickGlyph(p, chains, targets, cp);
		TS_ASSERT_EQUALS(t.chain, -1);
		TS_ASSERT_EQUALS(t.cp, 0x2620u);
		p.glyphFor(0x08, 0x08, cp);
		Graphics::HiResPick s = Graphics::pickGlyph(p, chains, targets, cp);
		TS_ASSERT_EQUALS(s.chain, 0);             // same: the id chain
		TS_ASSERT_EQUALS(s.cp, 0x2620u);
		sym.has.erase(0x2620);
		p.glyphFor(0x07, 0x07, cp);
		TS_ASSERT_EQUALS(Graphics::pickGlyph(p, chains, targets, cp).kind, Graphics::HiResPick::kGame);  // not the box
	}

	void test_source_answers_for_the_pick() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nrange.basic-latin=LAT.SVF\n", 0);
		Fake ko, lat;
		ko.has[0xAC00] = true;
		lat.has['A'] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(2);
		chains[0].push_back(&ko);
		chains[1].push_back(&lat);
		chains[1].push_back(&ko);
		Graphics::RangeRoutedGlyphSource r(p, chains, Common::Array<Graphics::UnicodeGlyphSource *>());
		TS_ASSERT_EQUALS(r.cells('A'), 1);
		TS_ASSERT_EQUALS(r.cells(0xAC00), 1);
		TS_ASSERT_EQUALS(r.cells('Z'), 0);
		TS_ASSERT_EQUALS(r.cellHeight(), 16);
	}

	// B6: a chain lacking cp plus a borrowed source that has it gives the
	// borrowed face even with `missing` set.
	void test_borrowed_source_answers_before_the_missing_box() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF\nmissing=u+25a1\n", 0);
		Fake ko, box, borrowedFace;
		box.has[0x25A1] = true;
		borrowedFace.has[0xAC01] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko); // idChain = [KO]; KO has neither the box nor 0xAC01
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		Common::Array<Graphics::UnicodeGlyphSource *> borrowed;
		borrowed.push_back(&borrowedFace);

		Graphics::HiResPick withoutBorrow = Graphics::pickGlyph(p, chains, targets, 0xAC01);
		TS_ASSERT_EQUALS(withoutBorrow.kind, Graphics::HiResPick::kGame);

		Graphics::HiResPick withBorrow = Graphics::pickGlyph(p, chains, targets, 0xAC01, &borrowed);
		TS_ASSERT_EQUALS(withBorrow.kind, Graphics::HiResPick::kFace);
		TS_ASSERT(withBorrow.borrowed);
		TS_ASSERT(!withBorrow.missingBox);
		TS_ASSERT_EQUALS(withBorrow.cp, 0xAC01u);

		// The chain also lacks the box, so with no borrowed source having
		// the real cp, the box would come from `borrowed` too.
		Graphics::HiResPick boxFromBorrowed = Graphics::pickGlyph(p, chains, targets, 0x3042, &borrowed);
		TS_ASSERT_EQUALS(boxFromBorrowed.kind, Graphics::HiResPick::kGame); // borrowedFace lacks the box too

		borrowed.push_back(&box);
		Graphics::HiResPick boxFromBorrowed2 = Graphics::pickGlyph(p, chains, targets, 0x3042, &borrowed);
		TS_ASSERT_EQUALS(boxFromBorrowed2.kind, Graphics::HiResPick::kFace);
		TS_ASSERT(boxFromBorrowed2.borrowed);
		TS_ASSERT(boxFromBorrowed2.missingBox);
		TS_ASSERT_EQUALS(boxFromBorrowed2.cp, 0x25A1u);
	}

	// B6: `original` stops the search and never borrows.
	void test_original_rule_never_borrows() {
		Graphics::HiResIdPlan p = compile("[font]\nface=KO.SVF,original\n", 0);
		Fake ko, borrowedFace;
		borrowedFace.has['A'] = true;
		Common::Array<Common::Array<Graphics::UnicodeGlyphSource *> > chains;
		chains.resize(1);
		chains[0].push_back(&ko); // KO lacks 'A'; the id chain ends in `original`
		Common::Array<Graphics::UnicodeGlyphSource *> targets;
		Common::Array<Graphics::UnicodeGlyphSource *> borrowed;
		borrowed.push_back(&borrowedFace);

		TS_ASSERT_EQUALS(Graphics::pickGlyph(p, chains, targets, 'A', &borrowed).kind, Graphics::HiResPick::kGame);
	}
};
