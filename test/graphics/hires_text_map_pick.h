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

#include "graphics/hires_text/map_pick.h"

#include "test/graphics/hires_support.h"

namespace {

const char kSvfMap[] = "[map]\nversion=3\n[fonts]\nko=data:fixture/JAMO.SVF\n[font]\nface=ko\n";
const char kBitMap[] = "[map]\nversion=3\n[fonts]\nko=data:fixture/MARKS.SVF\n[font]\nface=ko\n";
const char kGoneMap[] = "[map]\nversion=3\n[fonts]\nko=data:fonts/GONE.SVF\n[font]\nface=ko\n";
const char kOldMap[] = "[map]\nversion=2\n";

} // End of anonymous namespace

class HiResTextMapPickTestSuite : public CxxTest::TestSuite {
	bool pick(HiResTest::MemoryFiles &files, const Graphics::HiResOptions &o, Graphics::HiResPick &out,
	          Common::Array<Common::String> &notes) {
		return Graphics::pickHiResMap(o, Common::Path("game"), "ko", Graphics::HiResMapQualifiers(), files, out, notes);
	}

public:
	void test_order() {
		HiResTest::MemoryFiles files;
		files.addText("game/ko/HIRESBIT.MAP", kBitMap);
		files.addText("game/HiResVF.map", kSvfMap);
		Graphics::HiResOptions o;
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.kind, Graphics::kHiResFontSvf);
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/HiResVF.map");
		o.font = Graphics::kHiResFontBitmap;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.kind, Graphics::kHiResFontBitmap);
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/ko/HIRESBIT.MAP");
	}

	void test_languageFolderFirst() {
		HiResTest::MemoryFiles files;
		files.addText("game/HIRESVF.MAP", kOldMap);
		files.addText("game/KO/HIRESVF.MAP", kSvfMap);
		Graphics::HiResOptions o;
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/KO/HIRESVF.MAP");
	}

	void test_skips() {
		HiResTest::MemoryFiles files;
		files.addText("game/HIRESVF.MAP", kGoneMap);
		files.addText("game/HIRESBIT.MAP", kOldMap);
		Graphics::HiResOptions o;
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(!pick(files, o, p, notes));
		TS_ASSERT_EQUALS(notes.size(), 2u);
		TS_ASSERT_EQUALS(notes[0], "svf map HIRESVF.MAP: face ko is missing");
		TS_ASSERT(notes[1].hasPrefix("bitmap map HIRESBIT.MAP: does not load: HIRESBIT.MAP:1: map version 2 is not read"));
	}

	void test_namedMap() {
		HiResTest::MemoryFiles files;
		files.addText("game/HIRESVF.MAP", kSvfMap);
		files.addText("game/ko/MINE.MAP", kBitMap);
		Graphics::HiResOptions o;
		o.mapSet = true;
		o.map = "mine.map";
		Graphics::HiResPick p;
		Common::Array<Common::String> notes;
		TS_ASSERT(pick(files, o, p, notes));
		TS_ASSERT_EQUALS(p.path.toString('/'), "game/ko/MINE.MAP");
		TS_ASSERT_EQUALS(p.kind, Graphics::kHiResFontAuto);
		o.map.clear();
		TS_ASSERT(!pick(files, o, p, notes));
		o = Graphics::HiResOptions();
		o.enabled = false;
		TS_ASSERT(!pick(files, o, p, notes));
	}
};
