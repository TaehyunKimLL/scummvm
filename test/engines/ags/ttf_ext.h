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
#include "common/str.h"

#include "ags/lib/allegro/unicode_euckr.h"
#include "ags/shared/font/ttf_ext_text.h"

namespace {

/** A face without Hangul (6 px a character, a run of n kerned to 6n - (n - 1)),
 *  an extension with every Hangul syllable at 10 px; EUC-KR text. */
struct AgsFakeTtfExt {
	struct Call {
		Common::String run;
		int cp, x;
	};
	Common::Array<Call> calls;

	int getxc(const char **s) {
		char *p = const_cast<char *>(*s);
		const int c = AGS3::euckr_getx(&p);
		*s = p;
		return c;
	}
	bool useExt(int cp) { return cp >= 0xAC00 && cp <= 0xD7A3; }
	int faceWidth(const char *run) {
		const int n = (int)strlen(run);
		return n ? 6 * n - (n - 1) : 0;
	}
	int extWidth(int cp) { return 10; }
	void drawFace(const char *run, int x) {
		Call c; c.run = run; c.cp = 0; c.x = x;
		calls.push_back(c);
	}
	void drawExt(int cp, int x) {
		Call c; c.cp = cp; c.x = x;
		calls.push_back(c);
	}
};

} // End of anonymous namespace

/** TTF font N with a Korean patch's extfntN.wfn behind it (5 Days a Stranger fonts 0, 1). */
class AgsTtfExtTestSuite : public CxxTest::TestSuite {
public:
	void test_text_without_ext_chars_takes_the_old_path() {
		AgsFakeTtfExt ops;
		TS_ASSERT(!AGS3::ttf_ext_has_chars("Hello.", ops));
		TS_ASSERT(AGS3::ttf_ext_has_chars("A\xb0\xa1", ops));
	}

	void test_mixed_width_measures_face_runs_whole() {
		// "AB" 가 "CD": face runs 11 + 11 (kerned), the syllable 10.
		AgsFakeTtfExt ops;
		TS_ASSERT_EQUALS(AGS3::ttf_ext_text_width("AB\xb0\xa1" "CD", ops), 32);
		TS_ASSERT_EQUALS(AGS3::ttf_ext_text_width("\xb0\xa1\xb3\xaa", ops), 20);
		TS_ASSERT_EQUALS(AGS3::ttf_ext_text_width("", ops), 0);
	}

	void test_mixed_render_places_runs_and_glyphs() {
		AgsFakeTtfExt ops;
		AGS3::ttf_ext_render_text("AB\xb0\xa1" "CD", 5, ops);
		TS_ASSERT_EQUALS(ops.calls.size(), 3u);
		if (ops.calls.size() != 3)
			return;
		TS_ASSERT_EQUALS(ops.calls[0].run, "AB");
		TS_ASSERT_EQUALS(ops.calls[0].x, 5);
		TS_ASSERT_EQUALS(ops.calls[1].cp, 0xAC00);
		TS_ASSERT_EQUALS(ops.calls[1].x, 16);
		TS_ASSERT_EQUALS(ops.calls[2].run, "CD");
		TS_ASSERT_EQUALS(ops.calls[2].x, 26);
	}
};
