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

#include "common/fs.h"
#include "common/memstream.h"
#include "graphics/surface.h"
#include "graphics/hires_text/glyph_source_ttf.h"

#include "../../system/null_osystem.h"

#include "ags/shared/font/glyph_font_draw.h"
#include "ags/shared/font/hires_font_chain.h"

namespace {

void agsPut16(Common::Array<byte> &b, uint pos, uint16 v) {
	b[pos] = v & 0xff;
	b[pos + 1] = (v >> 8) & 0xff;
}

void agsPut32(Common::Array<byte> &b, uint pos, uint32 v) {
	for (int i = 0; i < 4; i++)
		b[pos + i] = (v >> (8 * i)) & 0xff;
}

/** A version 2, 1 bpp SVFN font of two inked glyphs in a 16 x @p cellH cell. */
Common::Array<byte> agsMakeSvf(int cellH, uint32 cp0, uint32 cp1) {
	const int kGlyphs = 2, cellW = 16, rowPitch = 2;
	const uint32 cps[kGlyphs] = { cp0, cp1 };
	const uint32 glyphStride = rowPitch * cellH;
	const uint32 dataOff = 36;
	const uint32 dataSize = glyphStride * kGlyphs;
	const uint32 cmapOff = dataOff + dataSize;
	Common::Array<byte> b(cmapOff + kGlyphs * 8, (byte)0);
	b[0] = 'S'; b[1] = 'V'; b[2] = 'F'; b[3] = 'N';
	agsPut16(b, 4, 2);
	agsPut16(b, 6, 0);
	b[8] = 1;
	agsPut16(b, 12, kGlyphs);
	b[14] = cellW;
	b[15] = cellH;
	b[16] = cellH - 3;
	agsPut32(b, 24, dataOff);
	agsPut32(b, 28, dataSize);
	agsPut32(b, 32, cmapOff);
	for (int i = 0; i < kGlyphs; i++) {
		for (int y = 2; y < cellH - 2; y++)
			b[dataOff + i * glyphStride + y * rowPitch] = 0x7e;
		agsPut32(b, cmapOff + i * 8, cps[i]);
		agsPut32(b, cmapOff + i * 8 + 4, i);
	}
	return b;
}

/** In-memory files by path; anything else from the real filesystem. */
struct AgsChainFiles {
	static Common::HashMap<Common::String, Common::Array<byte> > &files() {
		static Common::HashMap<Common::String, Common::Array<byte> > f;
		return f;
	}
	static Common::SeekableReadStream *open(const Common::Path &path, int32 &faceIndex, Common::String &error) {
		const Common::String key = path.toString('/');
		if (files().contains(key)) {
			faceIndex = 0;
			const Common::Array<byte> &b = files()[key];
			byte *copy = (byte *)malloc(b.size());
			memcpy(copy, b.begin(), b.size());
			return new Common::MemoryReadStream(copy, b.size(), DisposeAfterUse::YES);
		}
		return Graphics::openFontFace(path, faceIndex, error);
	}
};

const char *agsChainTtf() {
#ifdef AGS_TEST_SRCDIR
	static const Common::String kNanum =
		Common::String(AGS_TEST_SRCDIR) + "/dists/engine-data/hires_text/fonts/nanumgothic/NanumGothic-Bold.ttf";
	if (Common::FSNode(Common::Path(kNanum, '/')).exists())
		return kNanum.c_str();
#endif
	return nullptr;
}

/** Inked pixels of @p cp drawn by @p src into a 32-bit surface. */
int agsInk(Graphics::UnicodeGlyphSource *src, uint32 cp) {
	Graphics::Surface s;
	s.create(64, 64, Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0));
	memset(s.getPixels(), 0, s.pitch * s.h);
	AGS3::GlyphTextDrawer d(src, true);
	d.drawText(s, Common::Rect(0, 0, 64, 64), &cp, 1, 4, 4, 0xffffffff, nullptr);
	int n = 0;
	for (int y = 0; y < s.h; y++)
		for (int x = 0; x < s.w; x++)
			n += *(const uint32 *)s.getBasePtr(x, y) ? 1 : 0;
	s.free();
	return n;
}

bool agsChainWarned(const AGS3::HiResFontChain &c, const char *text) {
	for (uint i = 0; i < c.warnings.size(); i++)
		if (c.warnings[i].contains(text))
			return true;
	return false;
}

} // End of anonymous namespace

/** Font N's faces opened into one chain: SVFN fonts and TrueType faces, sniffed. */
class AgsHiResFontChainTestSuite : public CxxTest::TestSuite {
	AGS3::HiResFontPlan plan(const char *a, const char *b = nullptr) {
		AGS3::HiResFontPlan p;
		p.kind = AGS3::HiResFontPlan::kFaces;
		p.source = "[font] face";
		p.faces.push_back(Common::Path(a, '/'));
		if (b)
			p.faces.push_back(Common::Path(b, '/'));
		return p;
	}

public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
		AgsChainFiles::files()["/mem/KO.SVF"] = agsMakeSvf(16, 0xE000, 'A');
		AgsChainFiles::files()["/mem/TALL.SVF"] = agsMakeSvf(18, 0xE001, 'C');
		Common::Array<byte> broken(16, (byte)0);
		broken[0] = 'S'; broken[1] = 'V'; broken[2] = 'F'; broken[3] = 'N';
		AgsChainFiles::files()["/mem/BROKEN.SVF"] = broken;
	}

	void tearDown() {
		AgsChainFiles::files().clear();
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_svf_alone_is_the_chain() {
		AGS3::HiResFontChain c;
		TS_ASSERT(AGS3::openHiResFontChain(plan("/mem/KO.SVF"), 0, 14, Common::Array<uint32>(), c, AgsChainFiles::open));
		TS_ASSERT(c.bitmap);
		TS_ASSERT_EQUALS(c.faces.size(), 1u);
		TS_ASSERT_EQUALS(c.source, c.faces[0]);   // a lone face is not wrapped
		TS_ASSERT_EQUALS(c.source->cellHeight(), 16);
		TS_ASSERT_EQUALS(c.source->bitsPerPixel(), 1);
		TS_ASSERT(agsInk(c.source, 'A') > 0);
		TS_ASSERT(c.warnings.empty());
		c.free();
	}

	void test_unreadable_svf_is_warned_and_dropped() {
		AGS3::HiResFontChain c;
		TS_ASSERT(!AGS3::openHiResFontChain(plan("/mem/BROKEN.SVF"), 0, 14, Common::Array<uint32>(), c, AgsChainFiles::open));
		TS_ASSERT(c.faces.empty());
		TS_ASSERT(agsChainWarned(c, "is not a readable SVFN font"));
	}

	void test_svf_cell_height_must_match_the_first_svf() {
		AGS3::HiResFontChain c;
		TS_ASSERT(AGS3::openHiResFontChain(plan("/mem/KO.SVF", "/mem/TALL.SVF"), 3, 14, Common::Array<uint32>(), c,
										   AgsChainFiles::open));
		TS_ASSERT_EQUALS(c.faces.size(), 1u);
		TS_ASSERT(agsChainWarned(c, "HIRESTXT.MAP: /mem/TALL.SVF: cell height 18 differs from /mem/KO.SVF's 16 on 3; not used"));
		c.free();
	}

	void test_ttf_alone() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = agsChainTtf();
		if (!ttf) {
			TS_SKIP("NanumGothic-Bold.ttf not found");
			return;
		}
		AGS3::HiResFontChain c;
		TS_ASSERT(AGS3::openHiResFontChain(plan(ttf), 0, 16, Common::Array<uint32>(), c, AgsChainFiles::open));
		TS_ASSERT(!c.bitmap);
		TS_ASSERT_EQUALS(c.faces.size(), 1u);
		TS_ASSERT_EQUALS(c.size, 16);
		TS_ASSERT(agsInk(c.source, 'B') > 0);
		c.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	void test_svf_then_ttf_draws_what_the_svf_lacks() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = agsChainTtf();
		if (!ttf) {
			TS_SKIP("NanumGothic-Bold.ttf not found");
			return;
		}
		AGS3::HiResFontChain c;
		TS_ASSERT(AGS3::openHiResFontChain(plan("/mem/KO.SVF", ttf), 0, 16, Common::Array<uint32>(), c,
										   AgsChainFiles::open));
		TS_ASSERT_EQUALS(c.faces.size(), 2u);   // neither face is left out
		TS_ASSERT(c.bitmap);
		TS_ASSERT_EQUALS(c.names.size(), 2u);
		TS_ASSERT(c.source->cells('B') > 0);
		TS_ASSERT(agsInk(c.source, 'B') > 0);     // the TrueType face draws it
		TS_ASSERT(agsInk(c.source, 'A') > 0);     // the SVF draws its own
		TS_ASSERT(c.warnings.empty());
		c.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}

	void test_ttf_then_svf_draws_what_the_ttf_lacks() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
		const char *ttf = agsChainTtf();
		if (!ttf) {
			TS_SKIP("NanumGothic-Bold.ttf not found");
			return;
		}
		AGS3::HiResFontChain c;
		TS_ASSERT(AGS3::openHiResFontChain(plan(ttf, "/mem/KO.SVF"), 0, 16, Common::Array<uint32>(), c,
										   AgsChainFiles::open));
		TS_ASSERT_EQUALS(c.faces.size(), 2u);
		TS_ASSERT(c.bitmap);
		TS_ASSERT_EQUALS(c.size, 16);             // the TrueType face opens at the id's size
		TS_ASSERT(c.source->cells(0xE000) > 0);
		TS_ASSERT(agsInk(c.source, 0xE000) > 0);  // the SVF's private-use glyph
		TS_ASSERT(agsInk(c.source, 'B') > 0);
		c.free();
#else
		TS_SKIP("needs FreeType and a real filesystem");
#endif
	}
};
