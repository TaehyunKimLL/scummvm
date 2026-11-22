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

#include "common/file.h"
#include "common/fs.h"
#include "common/memstream.h"

#include "graphics/hires_text/font_set.h"
#include "graphics/hires_text/id_plan.h"
#include "image/png.h"

#include "test/graphics/hires_screen.h"
#include "test/graphics/hires_support.h"
#include "test/graphics/svf_golden_data.h"
#include "test/system/null_osystem.h"

namespace {

const char kScreenMap[] = "[map]\nversion=3\n[fonts]\nmarks=data:fixture/MARKS.SVF\njamo=data:fixture/JAMO.SVF\n"
                          "[font]\nface=marks,jamo\n[font.1]\nsize=24\n";

const Graphics::PixelFormat kScreenRgb565(2, 5, 6, 5, 0, 11, 5, 0, 0);
const Graphics::PixelFormat kScreenXrgb8888(4, 8, 8, 8, 0, 16, 8, 0, 0);

// A 160x50 game screen: navy (0) with a white (2) bar under the text, yellow (1) text from the fixture faces.
struct Scene {
	Scene() : screen(160, 50, 2), cache(files) {
		screen.setColor(0, 0, 0, 128);
		screen.setColor(1, 255, 255, 0);
		screen.setColor(2, 255, 255, 255);
		screen.frame.fillRect(Common::Rect(160, 50), 0);
		screen.frame.fillRect(Common::Rect(0, 30, 160, 40), 2);
		Common::MemoryReadStream s((const byte *)kScreenMap, strlen(kScreenMap));
		Graphics::HiResFontMap::load(s, "SCREEN.MAP", Common::Path("game"), Graphics::HiResMapQualifiers(), map);
		Common::Array<Common::String> w;
		set0.build(Graphics::compileIdPlan(map, 0, w), map, cache, w);
		set1.build(Graphics::compileIdPlan(map, 1, w), map, cache, w);
		set0.drawText(kSvfGoldenFont1xText, strlen(kSvfGoldenFont1xText), 8, 20, screen.plane, 1);
		set1.drawText(kSvfGoldenFont1xText, strlen(kSvfGoldenFont1xText), 8, 76, screen.plane, 1);
	}
	HiResTest::FakeScreen screen;
	HiResTest::MemoryFiles files;
	Graphics::HiResFaceCache cache;
	Graphics::HiResMap map;
	Graphics::HiResFontSet set0, set1;
};

} // End of anonymous namespace

class HiResTextScreenTestSuite : public CxxTest::TestSuite {
public:
	// A 1 bpp face looks the same on CLUT8 and on RGB without blending.
	void test_clut8EqualsThreshold() {
		Scene s;
		Graphics::Surface a, b;
		TS_ASSERT(s.screen.present(a, Graphics::PixelFormat::createFormatCLUT8(), Graphics::TextCompose::kStamp));
		TS_ASSERT(s.screen.present(b, kScreenXrgb8888, Graphics::TextCompose::kThreshold));
		int text = 0;
		bool same = true;
		for (int y = 0; y < a.h; ++y) {
			for (int x = 0; x < a.w; ++x) {
				const byte i = *(const byte *)a.getBasePtr(x, y);
				text += i == 1;
				const uint32 want = s.screen.palette[i * 3] << 16 | s.screen.palette[i * 3 + 1] << 8 | s.screen.palette[i * 3 + 2];
				same = same && *(const uint32 *)b.getBasePtr(x, y) == want;
			}
		}
		TS_ASSERT(same);
		TS_ASSERT(text > 500);
		a.free();
		b.free();
	}

	// Text keeps its index: a palette change recolours it without a redraw; a cleared box takes it away.
	void test_paletteAndClear() {
		Scene s;
		int x0 = -1, y0 = -1;
		for (int y = 0; y < s.screen.plane.getHeight() && x0 < 0; ++y)
			for (int x = 0; x < s.screen.plane.getWidth() && x0 < 0; ++x)
				if (s.screen.plane.getCoverage(x, y) == 255) {
					x0 = x;
					y0 = y;
				}
		TS_ASSERT(x0 >= 0);
		Graphics::Surface out;
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(*(const uint16 *)out.getBasePtr(x0, y0), 0xFFE0);
		s.screen.setColor(1, 255, 0, 0);
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(*(const uint16 *)out.getBasePtr(x0, y0), 0xF800);
		s.screen.plane.clearLowresRect(Common::Rect(160, 50));
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kBlend);
		TS_ASSERT_EQUALS(*(const uint16 *)out.getBasePtr(x0, y0), 0x0010);
		TS_ASSERT(s.screen.plane.isEmpty());
		out.free();
	}

	// The 3/2 line has coverage between the 2 bpp levels: blended on RGB, stamped at 128 on CLUT8.
	void test_scaledCoverageBlends() {
		Scene s;
		bool partial = false;
		int px = 0, py = 0;
		for (int y = 60; y < 100 && !partial; ++y)
			for (int x = 0; x < 320 && !partial; ++x) {
				const byte c = s.screen.plane.getCoverage(x, y);
				partial = c >= 30 && c <= 225 && c % 17;
				px = x;
				py = y;
			}
		TS_ASSERT(partial);
		// Presented on RGB565 the pixel is a blend: neither the navy under it nor the yellow text.
		Graphics::Surface out;
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kBlend);
		const uint16 blended = *(const uint16 *)out.getBasePtr(px, py);
		TS_ASSERT(blended != 0x0010);
		TS_ASSERT(blended != 0xFFE0);
		// By threshold it is one of the two.
		s.screen.present(out, kScreenRgb565, Graphics::TextCompose::kThreshold);
		const uint16 hard = *(const uint16 *)out.getBasePtr(px, py);
		TS_ASSERT(hard == 0x0010 || hard == 0xFFE0);
		out.free();
	}

	// Developer aid: with a directory hires-dump in the working directory, writes the scene as PNG files.
	void test_dumpWhenAsked() {
		Common::install_null_g_system();
		if (Common::FSNode(Common::Path("hires-dump")).isDirectory()) {
			Scene s;
			const Graphics::PixelFormat formats[] = { Graphics::PixelFormat::createFormatCLUT8(), kScreenRgb565, kScreenXrgb8888 };
			const Graphics::TextCompose::Mode modes[] = { Graphics::TextCompose::kStamp, Graphics::TextCompose::kBlend,
			                                              Graphics::TextCompose::kThreshold };
			const char *names[] = { "hires-dump/clut8-stamp.png", "hires-dump/rgb565-blend.png", "hires-dump/rgb888-threshold.png" };
			for (int i = 0; i < 3; ++i) {
				Graphics::Surface out;
				TS_ASSERT(s.screen.present(out, formats[i], modes[i]));
				Common::DumpFile f;
				TS_ASSERT(f.open(Common::Path(names[i])));
				TS_ASSERT(Image::writePNG(f, out, i ? nullptr : s.screen.palette, 3));
				out.free();
			}
		}
		Common::uninstall_null_g_system();
	}
};
