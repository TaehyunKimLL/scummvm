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

#include "graphics/hires_text/id_plan.h"

class HiResTextIdPlanTestSuite : public CxxTest::TestSuite {
	Graphics::HiResMap load(const char *text) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResMap m;
		Graphics::HiResFontMap::load(s, "T.MAP", Common::Path("g"), Graphics::HiResMapQualifiers(), m);
		return m;
	}

	Common::String names(const Graphics::HiResFaceChain &c) {
		Common::String s;
		for (uint i = 0; i < c.faces.size(); ++i)
			s += c.faces[i].name + " ";
		return s + (c.original ? "original" : "-");
	}

public:
	void test_precedence() {
		const Graphics::HiResMap m = load("[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\nc=C.SVF\n"
		                                  "[font]\nface=a,b\nmissing=u+25a1\nsize=16\n"
		                                  "[font.300]\nface=c,same\nsize=24\nshift=1\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p0 = Graphics::compileIdPlan(m, 0, w);
		TS_ASSERT_EQUALS(names(p0.chains[0]), "a b -");
		TS_ASSERT_EQUALS(p0.size, 16);
		TS_ASSERT_EQUALS(p0.missing, 0x25A1u);
		const Graphics::HiResIdPlan p300 = Graphics::compileIdPlan(m, 300, w);
		TS_ASSERT_EQUALS(names(p300.chains[0]), "c a b -");
		TS_ASSERT_EQUALS(p300.size, 24);
		TS_ASSERT_EQUALS(p300.shift, 1);
		TS_ASSERT_EQUALS(p300.missing, 0x25A1u);
		TS_ASSERT(w.empty());
	}

	void test_ranges() {
		const Graphics::HiResMap m = load("[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\n"
		                                  "[font]\nface=a\nrange.basic-latin=original\nadvance.basic-latin=game\n"
		                                  "[font.1]\nrange.u+0041-005a=b,same\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p = Graphics::compileIdPlan(m, 1, w);
		TS_ASSERT_EQUALS(p.chains.size(), 3u);
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor('A')]), "b a -");	// [font.1]'s narrower rule
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor('a')]), "original");
		TS_ASSERT_EQUALS(p.chainFor(0xAC00), 0u);
		TS_ASSERT_EQUALS(p.advanceFor('a'), Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(p.advanceFor(0xAC00), Graphics::kHiResAdvanceFont);
	}

	void test_noFace() {
		const Graphics::HiResMap m = load("[map]\nversion=3\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p = Graphics::compileIdPlan(m, 7, w);
		TS_ASSERT(p.chains[0].empty());
		TS_ASSERT_EQUALS(w.size(), 1u);
		TS_ASSERT_EQUALS(w[0], "font 7: no face= names a face");
	}
};
