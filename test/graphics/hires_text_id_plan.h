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

	Graphics::HiResMap loadFor(const char *text, const char *engine, Graphics::HiResRenderTarget target) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResMapQualifiers q;
		q.engine = engine;
		q.target = target;
		Graphics::HiResMap m;
		Graphics::HiResFontMap::load(s, "T.MAP", Common::Path("g"), q, m);
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

	// A range or advance rule of a qualified section replaces the base section's rule for the same span.
	void test_qualifiedRangesOverride() {
		const char *text = "[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\nc=C.SVF\n"
		                   "[font]\nface=a\nrange.hangul-syllables=b\nadvance.hangul-syllables=font\n"
		                   "[font:sci]\nrange.hangul-syllables=c\nadvance.u+ac00-d7a3=cell\n";
		Common::Array<Common::String> w;
		Graphics::HiResMap m = loadFor(text, "sci", Graphics::kHiResTargetAuto);
		Graphics::HiResIdPlan p = Graphics::compileIdPlan(m, 0, w);
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor(0xAC00)]), "c -");
		TS_ASSERT_EQUALS(p.advanceFor(0xAC00), Graphics::kHiResAdvanceCell);
		TS_ASSERT(m.warnings.empty());
		m = loadFor(text, "scumm", Graphics::kHiResTargetAuto);	// the qualified section does not apply
		p = Graphics::compileIdPlan(m, 0, w);
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor(0xAC00)]), "b -");
		TS_ASSERT_EQUALS(p.advanceFor(0xAC00), Graphics::kHiResAdvanceFont);
		// [font.N] and its qualified sections: the same rule.
		m = loadFor("[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\nc=C.SVF\n[font]\nface=a\n"
		            "[font.1]\nrange.hangul-syllables=b\n[font.1:sci]\nrange.hangul-syllables=c\n", "sci", Graphics::kHiResTargetAuto);
		p = Graphics::compileIdPlan(m, 1, w);
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor(0xAC00)]), "c -");
	}

	// Least to most specific: [S], [S:clut8|rgb], [S:engine], [S:engine:target].
	void test_qualifierOrder() {
		const char *text = "[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\nc=C.SVF\nd=D.SVF\ne=E.SVF\n[font]\nface=a\n"
		                   "range.hangul-syllables=b\n"
		                   "[font:sci:clut8]\nrange.hangul-syllables=e\n"
		                   "[font:sci]\nrange.hangul-syllables=d\n"
		                   "[font:clut8]\nrange.hangul-syllables=c\n";
		Common::Array<Common::String> w;
		Graphics::HiResMap m = loadFor(text, "sci", Graphics::kHiResTargetClut8);
		TS_ASSERT_EQUALS(names(Graphics::compileIdPlan(m, 0, w).chains[Graphics::compileIdPlan(m, 0, w).chainFor(0xAC00)]), "e -");
		m = loadFor(text, "sci", Graphics::kHiResTargetRgb565);	// no :rgb section: [S:sci] over [S]
		TS_ASSERT_EQUALS(names(Graphics::compileIdPlan(m, 0, w).chains[Graphics::compileIdPlan(m, 0, w).chainFor(0xAC00)]), "d -");
		m = loadFor(text, "scumm", Graphics::kHiResTargetClut8);	// [S:clut8] over [S]
		TS_ASSERT_EQUALS(names(Graphics::compileIdPlan(m, 0, w).chains[Graphics::compileIdPlan(m, 0, w).chainFor(0xAC00)]), "c -");
		m = loadFor(text, "scumm", Graphics::kHiResTargetRgb565);
		TS_ASSERT_EQUALS(names(Graphics::compileIdPlan(m, 0, w).chains[Graphics::compileIdPlan(m, 0, w).chainFor(0xAC00)]), "b -");
	}

	// rgb555 is an rgb target for the qualifiers: [S:rgb] applies to it, [S:clut8] does not.
	void test_rgb555QualifiesAsRgb() {
		const char *text = "[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\nc=C.SVF\n[font]\nface=a\n[font:rgb]\nface=b\n[font:clut8]\nface=c\n";
		Common::Array<Common::String> w;
		const Graphics::HiResRenderTarget targets[] = { Graphics::kHiResTargetRgb565, Graphics::kHiResTargetRgb555, Graphics::kHiResTargetRgb888,
		                                                Graphics::kHiResTargetClut8 };
		const char *want[] = { "b -", "b -", "b -", "c -" };
		for (uint i = 0; i < 4; ++i) {
			const Graphics::HiResMap m = loadFor(text, "sci", targets[i]);
			const Graphics::HiResIdPlan p = Graphics::compileIdPlan(m, 0, w);
			TS_ASSERT_EQUALS(names(p.chains[p.chainFor(0xAC00)]), want[i]);
		}
	}

	// Two rules for one span in one section: the later wins, with a warning (as scalars do).
	void test_duplicateSpanInOneSection() {
		const Graphics::HiResMap m = load("[map]\nversion=3\n[fonts]\na=A.SVF\nb=B.SVF\nc=C.SVF\n[font]\nface=a\n"
		                                  "range.hangul-syllables=b\nrange.u+ac00-d7a3=c\nsize=16\nsize=20\n");
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan p = Graphics::compileIdPlan(m, 0, w);
		TS_ASSERT_EQUALS(names(p.chains[p.chainFor(0xAC00)]), "c -");
		TS_ASSERT_EQUALS(p.size, 20);
		TS_ASSERT_EQUALS(m.warnings.size(), 1u);
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
