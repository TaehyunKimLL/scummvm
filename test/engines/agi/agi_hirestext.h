#include <cxxtest/TestSuite.h>

#include "../../hires_test_fonts.h"
#include "graphics/hires_text/hires_options.h"
#include "agi/hirestext.h"

// AGI's hi-res text adapter: one [font], 16x16 cells at 640x400, 1 bpp
// (D1), Latin from AGI's own font (D5: range.basic-latin=original).
class AgiHiresTextTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}
	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_map_face_and_original_latin() {
#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE && defined(POSIX)
		Common::Array<uint32> cps;
		cps.push_back(0xAC00); // 가
		cps.push_back(0xB098); // 나
		cps.push_back(0x25A1); // □
		cps.push_back('A');
		const Common::String face = HiResTestFonts::bakeSvf("AGI16.SVF", 16, cps, 16, 16, false);
		TS_ASSERT(!face.empty());
		const char *map =
			"[map]\nversion=2\n[render]\nscale=2\nblend=off\n[fonts]\nui=AGI16.SVF\n"
			"[font]\nface=ui\nmissing=u+25a1\nalign=cell\nrange.basic-latin=original\n";
		const Common::String mapPath = HiResTestFonts::writeTemp("AGITEST.MAP", (const byte *)map, strlen(map));
		TS_ASSERT(!mapPath.empty());
		Agi::HiresText ht;
		Graphics::HiResIniOverrides ini;
		TS_ASSERT(ht.loadMap(Common::Path(mapPath, '/'), Common::Path(HiResTestFonts::tempDir(), '/'), ini));
		TS_ASSERT(ht.isActive());
		uint16 rows[16];
		TS_ASSERT(ht.glyph(0xAC00, rows));
		int ink = 0;
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++)
				ink += (rows[y] >> (15 - x)) & 1;
		TS_ASSERT_LESS_THAN(20, ink);
		TS_ASSERT(!ht.glyph('A', rows));        // AGI's own font
		TS_ASSERT(ht.glyph(0x4E00, rows));      // not baked: the missing box
		TS_ASSERT(ht.canDraw(0xAC00));
		TS_ASSERT(ht.canDraw('A'));
		TS_ASSERT(!ht.canDraw(0x4E00));
		TS_ASSERT(!ht.hangulAny());
#else
		TS_SKIP("needs FreeType and a file system to bake the test face");
#endif
	}

	void test_no_map_is_inactive() {
		Agi::HiresText ht;
		Graphics::HiResIniOverrides ini;
		TS_ASSERT(!ht.loadMap(Common::Path("/nonexistent/NOPE.MAP", '/'), Common::Path("/nonexistent", '/'), ini));
		TS_ASSERT(!ht.isActive());
		uint16 rows[16];
		TS_ASSERT(!ht.glyph(0xAC00, rows));
	}
};
