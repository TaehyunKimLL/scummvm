#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "agi/textcells.h"

#include "support/read_source.h"

// Final review Minor 5: a translated drop-down is placed by its longest item
// (cells) in UTF-8 text mode, not by its first, so no item runs past column
// 39. English keeps the original first-item rule.
class AgiMenuColumnTestSuite : public CxxTest::TestSuite {
	// What addMenuItem() gives each item of one menu: the first item's
	// length (English) or the longest so far (UTF-8), applied to them all.
	static int16 lastEnd(int16 menuColumn, const int16 *lens, int n, bool utf8) {
		int16 column = 0, maxLen = 0;
		for (int i = 0; i < n; i++) {
			if (lens[i] > maxLen)
				maxLen = lens[i];
			if (utf8)
				column = Agi::TextCells::dropDownColumn(menuColumn, maxLen, 40);
			else if (i == 0)
				column = Agi::TextCells::dropDownColumn(menuColumn, lens[i], 40);
		}
		return column + maxLen - 1; // the right-most cell an item uses
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

	void test_the_original_first_item_rule() {
		using namespace Agi;
		TS_ASSERT_EQUALS(TextCells::dropDownColumn(1, 10, 40), 1);
		TS_ASSERT_EQUALS(TextCells::dropDownColumn(28, 10, 40), 28);  // 10 + 28 = 38 < 39
		TS_ASSERT_EQUALS(TextCells::dropDownColumn(29, 10, 40), 29);  // 39: = 39 - 10
		TS_ASSERT_EQUALS(TextCells::dropDownColumn(30, 10, 40), 29);
		TS_ASSERT_EQUALS(TextCells::dropDownColumn(35, 20, 40), 19);
	}

	void test_utf8_places_by_the_longest_item() {
		// The review's scenario: the right-most menu's first item is 12 cells, a later one 20.
		const int16 lens[] = { 12, 20, 8 };
		TS_ASSERT_EQUALS(lastEnd(30, lens, 3, true), 38);
		// English keeps the first-item rule (and its overflow, as the original).
		TS_ASSERT_EQUALS(lastEnd(30, lens, 3, false), 27 + 20 - 1);
		// A menu that fits under its name stays there in both modes.
		const int16 small[] = { 6, 9, 7 };
		TS_ASSERT_EQUALS(lastEnd(2, small, 3, true), 2 + 9 - 1);
		TS_ASSERT_EQUALS(lastEnd(2, small, 3, false), 2 + 9 - 1);
	}

	void test_add_menu_item_wiring() {
		const Common::String m = AgiTest::readSource("engines/agi/menu.cpp");
		const int add = m.find("void GfxMenu::addMenuItem(");
		const int utf8 = m.find("if (_vm->textIsUtf8() && !_vm->isLanguageRTL()) {", add);
		const int longest = m.find("TextCells::dropDownColumn(curMenuEntry->column, curMenuEntry->maxItemTextLen, FONT_COLUMN_CHARACTERS);", utf8);
		const int moveAll = m.find("_itemArray[nr]->column = _setupMenuItemColumn;", longest);
		const int english = m.find("} else if (curMenuEntry->itemCount == 0) {", moveAll);
		const int first = m.find("TextCells::dropDownColumn(curMenuEntry->column, menuItemEntry->textLen, FONT_COLUMN_CHARACTERS);", english);
		TS_ASSERT_LESS_THAN(0, add);
		TS_ASSERT_LESS_THAN(add, utf8);
		TS_ASSERT_LESS_THAN(utf8, longest);
		TS_ASSERT_LESS_THAN(longest, moveAll);
		TS_ASSERT_LESS_THAN(moveAll, english);
		TS_ASSERT_LESS_THAN(english, first);
	}
};
