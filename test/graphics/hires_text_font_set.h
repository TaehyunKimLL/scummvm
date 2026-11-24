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

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/id_plan.h"
#include "graphics/hires_text/svfchain.h"
#include "graphics/hires_text/text_plane.h"

#include "test/graphics/hires_support.h"
#include "test/graphics/svf_golden_data.h"
#include "test/graphics/svf_test_util.h"

namespace {

const char kFixtureMap[] =
	"[map]\nversion=3\n"
	"[fonts]\nmarks=data:fixture/MARKS.SVF\njamo=data:fixture/JAMO.SVF\n"
	"[font]\nface=marks,jamo\n"
	"[font.2]\nsize=32\n"
	"[font.3]\nsize=24\n"
	"[font.4]\nface=jamo\nrange.basic-latin=original\n"
	"[font.5]\nface=jamo\nmissing=u+25a1\nadvance=cell\nadvance.basic-latin=game\n";

// Records what the game's own font is asked to draw.
class FakeOriginal : public Graphics::HiResOriginalFont {
public:
	int getAdvance(uint32 cp) const override { return 5; }
	void drawChar(uint32 cp, int x, int yBaseline) const override {
		drawn += Common::String::format("%c@%d,%d ", (char)cp, x, yBaseline);
	}
	mutable Common::String drawn;
};

} // End of anonymous namespace

class HiResTextFontSetTestSuite : public CxxTest::TestSuite {
	// A plane of the golden line's size at scale 1, drawn through @p set.
	bool drawsGolden(const Graphics::HiResFontSet &set, const char *text, int x, int y, int w, int h, const char *const *rows) {
		Graphics::TextPlane plane;
		plane.create(w, h, 1);
		set.drawText(text, strlen(text), x, y, plane, 7);
		const Common::Array<byte> want = SvfTest::goldenBytes(rows, w, h);
		const Common::Array<byte> got = HiResTest::coverageOf(plane);
		for (int i = 0; i < w * h; ++i) {
			if (got[i] != want[i])
				return false;
			if (got[i] && plane.getIndex(i % w, i / w) != 7)
				return false;
		}
		return true;
	}

	bool loadSet(HiResTest::MemoryFiles &files, Graphics::HiResFaceCache &cache, int id, Graphics::HiResFontSet &set) {
		Common::MemoryReadStream s((const byte *)kFixtureMap, strlen(kFixtureMap));
		Graphics::HiResMap map;
		if (!Graphics::HiResFontMap::load(s, "FIXTURE.MAP", Common::Path("game"), Graphics::HiResMapQualifiers(), map))
			return false;
		Common::Array<Common::String> w;
		const Graphics::HiResIdPlan plan = Graphics::compileIdPlan(map, id, w);
		return set.build(plan, map, cache, w) && w.empty();
	}

public:
	// Drawn one code point at a time, the line is byte for byte the renderer's line (marks, U+FFFD rule, missing advance).
	void test_perCodePointIsTheGoldenLine() {
		Graphics::HiResFontSet set;
		set.addChain(SvfTest::makeChain(1, 1, kSvfGoldenLine1xMissing), false, DisposeAfterUse::YES);
		TS_ASSERT(drawsGolden(set, kSvfGoldenLine1xText, kSvfGoldenLine1xX, kSvfGoldenLine1xY, kSvfGoldenLine1xW,
		                      kSvfGoldenLine1xH, kSvfGoldenLine1xRows));
		TS_ASSERT_EQUALS(set.measure(kSvfGoldenLine1xText, strlen(kSvfGoldenLine1xText)), kSvfGoldenLine1xWidth);
	}

	void test_mapPipelineDrawsTheGoldenLine() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		TS_ASSERT(loadSet(files, cache, 0, set));
		TS_ASSERT(drawsGolden(set, kSvfGoldenFont1xText, kSvfGoldenFont1xX, kSvfGoldenFont1xY, kSvfGoldenFont1xW,
		                      kSvfGoldenFont1xH, kSvfGoldenFont1xRows));
		TS_ASSERT_EQUALS(set.getAscent(), kSvfGoldenFont1xAscent);
	}

	// size=32 on 16 px cells is 2/1, size=24 is 3/2: the scaled golden lines.
	void test_sizeScales() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet two, threeHalves;
		TS_ASSERT(loadSet(files, cache, 2, two));
		TS_ASSERT(loadSet(files, cache, 3, threeHalves));
		TS_ASSERT_EQUALS(cache.getCount(), 2u);
		TS_ASSERT_EQUALS(files.opens, 2);
		TS_ASSERT(drawsGolden(two, kSvfGoldenLine2xText, kSvfGoldenLine2xX, kSvfGoldenLine2xY, kSvfGoldenLine2xW,
		                      kSvfGoldenLine2xH, kSvfGoldenLine2xRows));
		TS_ASSERT(drawsGolden(threeHalves, kSvfGoldenLine3by2Text, kSvfGoldenLine3by2X, kSvfGoldenLine3by2Y,
		                      kSvfGoldenLine3by2W, kSvfGoldenLine3by2H, kSvfGoldenLine3by2Rows));
	}

	void test_originalRange() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		TS_ASSERT(loadSet(files, cache, 4, set));
		TS_ASSERT_EQUALS(set.route('a'), -1);
		TS_ASSERT_EQUALS(set.route(0xD55C), 0);
		FakeOriginal orig;
		Graphics::TextPlane plane;
		plane.create(64, 20, 1);
		const int end = set.drawText("a\xed\x95\x9c" "b", 5, 2, 14, plane, 1, nullptr, &orig);
		TS_ASSERT_EQUALS(orig.drawn, "a@2,14 b@23,14 ");
		TS_ASSERT_EQUALS(end, 28);
	}

	// missing= draws the box for a code point no face has; advance= picks the cell or the game's advance.
	void test_missingAndAdvance() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet plain, set;
		TS_ASSERT(loadSet(files, cache, 0, plain));
		TS_ASSERT(loadSet(files, cache, 5, set));
		const char snowman[] = "\xe2\x98\x83";
		const char box[] = "\xe2\x96\xa1";
		Graphics::TextPlane a, b;
		a.create(32, 20, 1);
		b.create(32, 20, 1);
		TS_ASSERT_EQUALS(plain.drawText(snowman, 3, 0, 14, a, 1), 0);
		TS_ASSERT(a.isEmpty());
		set.drawText(snowman, 3, 0, 14, a, 1);
		plain.drawText(box, 3, 0, 14, b, 1);
		TS_ASSERT(!a.isEmpty());
		TS_ASSERT(HiResTest::coverageOf(a) == HiResTest::coverageOf(b));
		FakeOriginal orig;
		TS_ASSERT_EQUALS(set.getAdvance(0xD55C, &orig), 16);
		TS_ASSERT_EQUALS(plain.getAdvance('i', &orig), 8);
		TS_ASSERT_EQUALS(set.getAdvance('i', &orig), 5);
		TS_ASSERT_EQUALS(set.getAdvance('i'), 8);
		TS_ASSERT_EQUALS(set.getAdvance(0x0301, &orig), 0);
	}

	// TextLayout breaks Hangul at spaces (hangul=word) with the set's advances.
	void test_layout() {
		HiResTest::MemoryFiles files;
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		TS_ASSERT(loadSet(files, cache, 0, set));
		const char text[] = "\xed\x95\x9c\xea\xb8\x80 \xed\x95\x9c\xea\xb8\x80 ab";	// 한글 한글 ab
		Graphics::TextRun run;
		run.decode((const byte *)text, strlen(text), Graphics::Utf8TextDecoder());
		Graphics::HiResFontMetrics metrics(set);
		Common::Array<Graphics::LineSpan> lines;
		Graphics::TextLayout::breakLines(run, 72, metrics, Graphics::BreakRules(), lines);
		TS_ASSERT_EQUALS(lines.size(), 2u);
		TS_ASSERT_EQUALS(lines[0].byteEnd, 13u);
		TS_ASSERT_EQUALS(lines[0].width, 72);
		TS_ASSERT_EQUALS(lines[1].byteStart, 14u);
		TS_ASSERT_EQUALS(lines[1].width, 16);
	}

	// [preload] lists the characters a game draws with a streamed face: build() reads their glyphs once, so drawing them
	// afterwards seeks nothing (the block cache holds them).
	static const char *preloadMap(bool withPreload) {
		return withPreload ? "[map]\nversion=3\n"
		                     "[fonts]\nmarks=data:fixture/MARKS.SVF\njamo=data:fixture/JAMO.SVF\n"
		                     "[font]\nface=marks,jamo\n"
		                     "[load]\njamo=stream\n"
		                     "[preload]\njamo=data:fixture/JAMO.HOT\n"
		                   : "[map]\nversion=3\n"
		                     "[fonts]\nmarks=data:fixture/MARKS.SVF\njamo=data:fixture/JAMO.SVF\n"
		                     "[font]\nface=marks,jamo\n"
		                     "[load]\njamo=stream\n";
	}
	// Builds font 0 of @p text over @p files; returns the jamo face's stream reads after the build.
	uint32 readsAfterBuild(const char *text, HiResTest::MemoryFiles &files, Graphics::HiResFaceCache &cache,
	                       Graphics::HiResFontSet &set, Common::Array<Common::String> &w) {
		Common::MemoryReadStream s((const byte *)text, strlen(text));
		Graphics::HiResMap map;
		TS_ASSERT(Graphics::HiResFontMap::load(s, "FIXTURE.MAP", Common::Path("game"), Graphics::HiResMapQualifiers(), map));
		const Graphics::HiResIdPlan plan = Graphics::compileIdPlan(map, 0, w);
		TS_ASSERT(set.build(plan, map, cache, w));
		Graphics::SvfFace *face = cache.get(map.faces["jamo"], Graphics::kHiResLoadStream, w);
		TS_ASSERT(face && face->isStreamed());
		return face ? face->getStreamReads() : 0;
	}
	uint32 readsNow(Graphics::HiResFaceCache &cache, Common::Array<Common::String> &w) {
		Graphics::HiResFaceRef ref;
		ref.data = true;
		ref.path = Common::Path("fixture/JAMO.SVF");
		return cache.get(ref, Graphics::kHiResLoadStream, w)->getStreamReads();
	}

public:
	void test_preloadWarmsAStreamedFace() {
		const char hot[] = "# the characters of the game's first scenes\n\xed\x95\x9c\xea\xb8\x80 \xea\xb0\x80\xea\xb0\x81\n";
		const char text[] = "\xed\x95\x9c\xea\xb8\x80 \xea\xb0\x80\xea\xb0\x81";
		Common::Array<Common::String> w;

		HiResTest::MemoryFiles coldFiles;
		Graphics::HiResFaceCache coldCache(coldFiles);
		Graphics::HiResFontSet cold;
		const uint32 coldBuild = readsAfterBuild(preloadMap(false), coldFiles, coldCache, cold, w);

		HiResTest::MemoryFiles files;
		files.addText("data/fixture/JAMO.HOT", hot);
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		const uint32 warmBuild = readsAfterBuild(preloadMap(true), files, cache, set, w);
		TS_ASSERT(w.empty());
		TS_ASSERT_LESS_THAN(coldBuild, warmBuild);

		Graphics::TextPlane plane;
		plane.create(200, 40, 1);
		set.drawText(text, strlen(text), 0, 14, plane, 1);
		TS_ASSERT_EQUALS(readsNow(cache, w), warmBuild);	// the glyphs were all in the block cache

		cold.drawText(text, strlen(text), 0, 14, plane, 1);
		TS_ASSERT_LESS_THAN(coldBuild, readsNow(coldCache, w));	// without the list the first line seeks
	}

	void test_preloadFileMissingIsAWarning() {
		HiResTest::MemoryFiles files;	// no JAMO.HOT
		Graphics::HiResFaceCache cache(files);
		Graphics::HiResFontSet set;
		Common::Array<Common::String> w;
		readsAfterBuild(preloadMap(true), files, cache, set, w);
		TS_ASSERT_EQUALS(w.size(), 1u);
		TS_ASSERT(w.size() && w[0].contains("preload"));
	}
};
