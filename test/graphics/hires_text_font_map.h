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

#include "graphics/hires_text/font_map.h"

class HiResTextFontMapTestSuite : public CxxTest::TestSuite {
	bool load(const char *text, Graphics::HiResMap &map, const char *engine = "", Graphics::HiResRenderTarget t = Graphics::kHiResTargetAuto) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResMapQualifiers q;
		q.engine = engine;
		q.target = t;
		return Graphics::HiResFontMap::load(s, "T.MAP", Common::Path("game/ko"), q, map);
	}

	bool warned(const Graphics::HiResMap &map, const char *part) {
		for (uint i = 0; i < map.warnings.size(); ++i)
			if (map.warnings[i].contains(part))
				return true;
		return false;
	}

public:
	void test_versionGate() {
		Graphics::HiResMap m;
		TS_ASSERT(!load("[map]\nversion=2\n[font]\nface=a.svf\n", m));
		TS_ASSERT(warned(m, "T.MAP:1: map version 2 is not read: set [map] version=3"));
		TS_ASSERT(!m.font.faceSet);
		TS_ASSERT(!load("[fonts]\na=a.svf\n", m));
		TS_ASSERT(warned(m, "no [map] version=3"));
		TS_ASSERT(!load("version=3\n", m));
		TS_ASSERT(load("\xEF\xBB\xBF[map]\nversion=3\n", m));
		TS_ASSERT(m.warnings.empty());
	}

	void test_crlfLineEnds() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\r\nversion=3\r\n[fonts]\r\na=A.SVF\r\n[font]\r\nface=a\r\nsize=16\r\n", m));
		TS_ASSERT(m.warnings.empty());
		TS_ASSERT(m.font.faceSet);
		TS_ASSERT_EQUALS(m.font.face.faces[0].path.toString('/'), "game/ko/A.SVF");
		TS_ASSERT_EQUALS(m.font.size, 16);
	}

	void test_fontIdSpellings() {
		Graphics::HiResMap m;
		// [font.01] is the id 1, with a warning; it merges with [font.1] instead of being dropped.
		TS_ASSERT(load("[map]\nversion=3\n[fonts]\na=A.SVF\n[font.1]\nsize=16\n[font.01]\nshift=2\n", m));
		TS_ASSERT(warned(m, "font.01"));
		const Graphics::HiResFontScope *s = m.fontIdScope(1);
		TS_ASSERT(s && s->sizeSet && s->shiftSet);
		TS_ASSERT_EQUALS(m.fontIds.size(), 1u);
		// A blank in the id is no id.
		TS_ASSERT(load("[map]\nversion=3\n[font. 2]\nsize=16\n", m));
		TS_ASSERT(warned(m, "font id"));
		TS_ASSERT(!m.fontIdScope(2));
	}

	void test_unknownMapKeyAndSurrogateMissing() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\nvresion=4\n", m));
		TS_ASSERT(warned(m, "unknown key 'vresion'"));
		uint32 cp;
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+d800", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("0xDFFF", cp));
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("u+e000", cp));
		TS_ASSERT(load("[map]\nversion=3\n[font]\nmissing=u+d800\n", m));
		TS_ASSERT(warned(m, "missing"));
		TS_ASSERT(!m.font.missingSet);
	}

	void test_fontsAndFaces() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n"
		               "[fonts]\nko=NEODG16A.SVF ; the jamo face\nfb=data:hires_text/NEODG16A.SVF\n"
		               "[font]\nface=ko,fb\nmissing=u+25a1\n"
		               "[font.300]\nface=x/BIG.SVF,same,original,ko\nsize=24\nshift=-2\ncell=glyph\nalign=font\n", m));
		TS_ASSERT_EQUALS(m.version, 3);
		TS_ASSERT_EQUALS(m.faces["KO"].path.toString('/'), "game/ko/NEODG16A.SVF");
		TS_ASSERT(m.faces["fb"].data);
		TS_ASSERT_EQUALS(m.faces["fb"].key(), "data:hires_text/NEODG16A.SVF");
		TS_ASSERT_EQUALS(m.font.face.faces.size(), 2u);
		TS_ASSERT_EQUALS(m.font.missing, 0x25A1u);
		const Graphics::HiResFontScope *s = m.fontIdScope(300);
		TS_ASSERT(s);
		TS_ASSERT_EQUALS(s->face.faces.size(), 1u);
		TS_ASSERT_EQUALS(s->face.faces[0].path.toString('/'), "game/ko/x/BIG.SVF");
		TS_ASSERT(s->face.same);
		TS_ASSERT_EQUALS(s->face.sameAt, 1u);
		TS_ASSERT(s->face.endsInOriginal);
		TS_ASSERT(warned(m, "entries after 'original' are ignored: 'ko'"));
		TS_ASSERT_EQUALS(s->size, 24);
		TS_ASSERT_EQUALS(s->shift, -2);
		TS_ASSERT_EQUALS(s->cell, Graphics::kHiResCellGlyph);
		TS_ASSERT_EQUALS(s->align, Graphics::kHiResAlignFont);
		TS_ASSERT(!m.fontIdScope(4));
	}

	void test_ranges() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n[fonts]\nko=K.SVF\n[font]\nface=ko\nrange.basic-latin=original\n"
		               "range.u+ac00-d7a3=same\nadvance.basic-latin=game\nrange.nonsense=ko\nadvance=cell\n", m));
		TS_ASSERT_EQUALS(m.font.rangeSpecs.size(), 2u);
		TS_ASSERT(m.font.rangeValues[0].endsInOriginal);
		TS_ASSERT(m.font.rangeValues[1].same);
		TS_ASSERT_EQUALS(m.font.advanceValues[0], Graphics::kHiResAdvanceGame);
		TS_ASSERT_EQUALS(m.font.advance, Graphics::kHiResAdvanceCell);
		TS_ASSERT(warned(m, "unknown range 'nonsense'"));
	}

	void test_qualifiers() {
		const char *text = "[map]\nversion=3\n[render]\nblend=auto\nscale=2\n[render:clut8]\nblend=off\n"
		                   "[render:sci]\ntarget=rgb565\n[render:sci:rgb]\nblend=on\n[render:scumm]\nscale=1\n";
		Graphics::HiResMap m;
		TS_ASSERT(load(text, m));
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendAuto);
		TS_ASSERT(!m.targetSet);
		TS_ASSERT_EQUALS(m.scale, 2);
		TS_ASSERT(load(text, m, "sci", Graphics::kHiResTargetClut8));
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendOff);
		TS_ASSERT_EQUALS(m.target, Graphics::kHiResTargetRgb565);
		TS_ASSERT(load(text, m, "sci", Graphics::kHiResTargetRgb888));
		TS_ASSERT_EQUALS(m.blend, Graphics::kHiResBlendOn);
		TS_ASSERT(load(text, m, "scumm", Graphics::kHiResTargetRgb565));
		TS_ASSERT_EQUALS(m.scale, 1);
		TS_ASSERT(m.warnings.empty());
		TS_ASSERT(load("[map]\nversion=3\n[render:dos]\nscale=1\n", m));
		TS_ASSERT(warned(m, "unknown qualifier 'dos'"));
		TS_ASSERT(!m.scaleSet);
	}

	void test_removedAndUnknown() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n[render]\ngamma=1.2\n[text]\nencoding=cp949\n[shadow]\nmode=drop\n"
		               "[glyphs]\n0x41=original\n[font]\npixel=16\nmirror=off\ncolour=3\n[bogus]\n", m));
		TS_ASSERT(warned(m, "T.MAP:4: 'gamma' was removed in map version 3; delete it"));
		TS_ASSERT(warned(m, "[text] was removed"));
		TS_ASSERT(warned(m, "[shadow] was removed"));
		TS_ASSERT(warned(m, "[glyphs] was removed"));
		TS_ASSERT(warned(m, "'pixel' was removed"));
		TS_ASSERT(warned(m, "'mirror' was removed"));
		TS_ASSERT(warned(m, "unknown key 'colour'"));
		TS_ASSERT(warned(m, "unknown section [bogus]"));
	}

	void test_loadPreloadLayout() {
		Graphics::HiResMap m;
		TS_ASSERT(load("[map]\nversion=3\n[fonts]\nko=K.SVF\n[load]\nko=stream\nzz=whole\n[preload]\nko=K.HOT\n"
		               "[layout]\nhangul=any\nkinsoku=off\n", m));
		TS_ASSERT_EQUALS(m.loadModeFor(m.faces["ko"]), Graphics::kHiResLoadStream);
		TS_ASSERT(warned(m, "[load] names 'zz'"));
		TS_ASSERT_EQUALS(m.preloads["ko"].path.toString('/'), "game/ko/K.HOT");
		const Graphics::BreakRules r = m.layout.apply(Graphics::BreakRules());
		TS_ASSERT_EQUALS(r.hangul, Graphics::kHangulBreakAny);
		TS_ASSERT(!r.kinsoku);
		TS_ASSERT(r.thaiFallback);
	}

	void test_codePoints() {
		uint32 cp;
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("U+25A1", cp));
		TS_ASSERT_EQUALS(cp, 0x25A1u);
		TS_ASSERT(Graphics::HiResFontMap::parseCodePoint("0x10ffff", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+110000", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("25A1", cp));
		TS_ASSERT(!Graphics::HiResFontMap::parseCodePoint("u+", cp));
	}

	void test_resolveFace() {
		Graphics::HiResFaceRef r = Graphics::HiResFontMap::resolveFace("data:hires_text/X.SVF", Common::Path("game/ko"));
		TS_ASSERT(r.data);
		TS_ASSERT_EQUALS(r.path.toString('/'), "hires_text/X.SVF");
		TS_ASSERT_EQUALS(r.key(), "data:hires_text/X.SVF");
		r = Graphics::HiResFontMap::resolveFace("A.SVF", Common::Path("game/ko"));
		TS_ASSERT(!r.data);
		TS_ASSERT_EQUALS(r.key(), "game/ko/A.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("/abs/B.SVF", Common::Path("game")).key(), "/abs/B.SVF");
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("C:\\fonts\\C.SVF", Common::Path("game")).path.toString('/'), "C:/fonts/C.SVF");
		// a bare "C:" is a drive here as it is when a game's map is looked up (findGameMapFile): not under the map's folder.
		TS_ASSERT_EQUALS(Graphics::HiResFontMap::resolveFace("C:", Common::Path("game")).key(), "C:");
	}
};
