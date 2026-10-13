#include <cxxtest/TestSuite.h>

#include "common/fs.h"
#include "common/str.h"
#include "common/stream.h"
#include "../../system/null_osystem.h"

// The UTF-8 text mode wiring that needs a running engine to exercise, pinned
// by reading the engine back (the pattern of test/engines/sci/korean_input.h).
// The behaviour itself is covered by the pure units (korean_text.h) and by the
// harness play checks (harness/agi/lsl1_play.py).
class AgiTextWiringTestSuite : public CxxTest::TestSuite {
	Common::String readSource(const char *rel) {
#if !NULL_OSYSTEM_IS_AVAILABLE
		TS_SKIP("no file access in this test environment");
		return Common::String();
#else
		Common::FSNode node(Common::Path(Common::String(AGI_TEST_SRCDIR) + "/" + rel, '/'));
		Common::SeekableReadStream *in = node.createReadStream();
		if (!in) {
			TS_FAIL(Common::String::format("cannot read %s", rel).c_str());
			return Common::String();
		}
		const uint32 len = (uint32)in->size();
		char *buf = new char[len + 1];
		const uint32 got = in->read(buf, len);
		buf[got] = 0;
		Common::String out(buf, got);
		delete[] buf;
		delete in;
		return out;
#endif
	}

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

	void test_drawing_walks_characters_not_bytes() {
		const Common::String txt = readSource("engines/agi/text.cpp");
		TS_ASSERT(txt.contains("curTextPtr += TextCells::decode(curTextPtr, utf8, character);"));
		TS_ASSERT(txt.contains("void TextMgr::displayCharacter(uint32 character, bool disabledLook)"));
		TS_ASSERT(txt.contains("TextCells::wordWrap(originalText, maxWidth, utf8,"));
		const Common::String gfx = readSource("engines/agi/graphics.cpp");
		TS_ASSERT(gfx.contains("void GfxMgr::drawCharacterOnDisplay(int16 x, int16 y, uint32 character,"));
		TS_ASSERT(gfx.contains("text += TextCells::decode(text, utf8, character);"));
	}

	void test_layout_sites_count_cells() {
		const Common::String txt = readSource("engines/agi/text.cpp");
		TS_ASSERT(txt.contains("TEXT_STRING_MAX_SIZE - TextCells::cellLen(_vm->_game.getString(0), _vm->textIsUtf8())"));
		TS_ASSERT(txt.contains("Common::resolveKoreanParticles(resultString);"));
		const Common::String menu = readSource("engines/agi/menu.cpp");
		TS_ASSERT(menu.contains("menuEntry->textLen = TextCells::cellLen(menuEntry->text.c_str(), utf8);"));
		TS_ASSERT(menu.contains("menuItemEntry->textLen = TextCells::cellLen(menuItemEntry->text.c_str(), _vm->textIsUtf8());"));
		TS_ASSERT(!menu.contains("menuEntry->text.deleteLastChar();"));
		const Common::String inv = readSource("engines/agi/inv.cpp");
		TS_ASSERT(!inv.contains("strnlen(inventoryEntry.name"));
		TS_ASSERT(!inv.contains("strlen(inventoryEntry.name)"));
		const Common::String sys = readSource("engines/agi/systemui.cpp");
		TS_ASSERT(!sys.contains("strlen(button1Text)"));
		TS_ASSERT(!sys.contains("strlen(button2Text)"));
		// Save descriptions are ASCII only (D8): that path stays bytes.
		TS_ASSERT(sys.contains("actualDescriptionLen = strlen(actualDescription);"));
	}
};
