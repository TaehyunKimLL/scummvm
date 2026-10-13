#include <cxxtest/TestSuite.h>

#include "support/read_source.h"

// The UTF-8 text mode wiring that needs a running engine to exercise, pinned
// by reading the engine back (the pattern of test/engines/sci/korean_input.h).
// The behaviour itself is covered by the pure units (korean_text.h) and by the
// harness play checks (harness/agi/lsl1_play.py).
class AgiTextWiringTestSuite : public CxxTest::TestSuite {
	static Common::String readSource(const char *rel) { return AgiTest::readSource(rel); }

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

	void test_string_table_and_answers() {
		const Common::String h = readSource("engines/agi/agi.h");
		TS_ASSERT(h.contains("#define MAX_STRINGBYTES            (MAX_STRINGLEN * 4 + 1)"));
		TS_ASSERT(h.contains("char strings[MAX_STRINGS + 1][MAX_STRINGBYTES];"));
		TS_ASSERT(h.contains("Common::String stringAnswer[MAX_STRINGS + 1];"));
		const Common::String a = readSource("engines/agi/agi.cpp");
		TS_ASSERT(a.contains("TextCells::clipCells(str, MAX_STRINGLEN - 1, true)"));
		TS_ASSERT(a.contains("Common::strlcpy(strings[number], str, MAX_STRINGLEN);"));
		const Common::String op = readSource("engines/agi/op_cmd.cpp");
		TS_ASSERT(op.contains("state->setStringFromMessage(stringNr, textNr);"));
		const Common::String t = readSource("engines/agi/op_test.cpp");
		TS_ASSERT(t.contains("TextCells::agiStringsEqual(_game.stringAnswer[s1].c_str(), b)"));
		TS_ASSERT(t.contains("TextCells::agiStringsEqual(a, _game.stringAnswer[s2].c_str())"));
	}

	void test_edit_lines_hold_code_points() {
		const Common::String h = readSource("engines/agi/text.h");
		TS_ASSERT(h.contains("uint32 _prompt[42];"));
		TS_ASSERT(h.contains("uint32 _promptPrevious[42];"));
		TS_ASSERT(h.contains("uint32 _inputString[42];"));
		const Common::String k = readSource("engines/agi/keyboard.cpp");
		TS_ASSERT(k.contains("keyEnqueue(AGI_KEY_CODEPOINT | event.kbd.ascii);"));
		const Common::String kh = readSource("engines/agi/keyboard.h");
		TS_ASSERT(kh.contains("#define AGI_KEY_CODEPOINT     0x10000"));
		TS_ASSERT(kh.contains("#define AGI_KEY_HANGUL_TOGGLE 0x20000"));
		const Common::String c = readSource("engines/agi/cycle.cpp");
		TS_ASSERT(c.contains("int AgiEngine::processAGIEvents() {"));
		TS_ASSERT(c.contains("const bool wideKey = rawKey >= AGI_KEY_CODEPOINT;"));
		const Common::String op = readSource("engines/agi/op_cmd.cpp");
		TS_ASSERT(op.contains("textMgr->stringGetText().c_str()"));
		TS_ASSERT(!op.contains("(char *)textMgr->_inputString"));
		TS_ASSERT(!readSource("engines/agi/systemui.cpp").contains("(char *)_text->_inputString"));
		// D8: the save name is ASCII only, even in a Korean session
		TS_ASSERT(readSource("engines/agi/systemui.cpp").contains("_text->_inputStringAscii = true;\n\t_text->stringEdit(30);"));
		TS_ASSERT(readSource("engines/agi/text.cpp").contains("acceptableInput = utf8 && cell >= 0x80 && !_inputStringAscii;"));
	}

	// Task 13 rulings on the composer's place in the edit lines.
	void test_korean_mode_rulings() {
		const Common::String t = readSource("engines/agi/text.cpp");
		// D8: never in the save name
		TS_ASSERT(t.contains("_hangulInput && _hangulMode && !_inputStringAscii &&\n\t\t_vm->_game.cycleInnerLoopType == CYCLE_INNERLOOP_GETSTRING"));
		// F4: the echoed line is replayed in English, then the mode is back
		const int echo = t.find("void TextMgr::promptEchoLine()");
		const int off = t.find("_hangulMode = false;", echo);
		const int replay = t.find("promptKeyPress(", echo);
		const int back = t.find("_hangulMode = hangulMode;", echo);
		TS_ASSERT_LESS_THAN(echo, off);
		TS_ASSERT_LESS_THAN(off, replay);
		TS_ASSERT_LESS_THAN(replay, back);
		// F12/D10: the badge needs agi_hangul_input in UTF-8 mode, not the composer
		TS_ASSERT(t.contains("_hangulBadge = _vm->textIsUtf8() && Graphics::confManGameBool(\"agi_hangul_input\", domain, false);"));
		// F13: one helper edits the run for both lines
		int calls = 0;
		for (int at = t.find("hangulEditKey("); at >= 0; at = t.find("hangulEditKey(", at + 1))
			calls++;
		TS_ASSERT_EQUALS(calls, 3); // the definition, the prompt, get.string
		TS_ASSERT_LESS_THAN(0, (int)t.find("_hangulLine.feed("));
		TS_ASSERT_EQUALS(t.find("_hangulLine.feed("), t.rfind("_hangulLine.feed("));
	}

	void test_korean_parse_wiring() {
		const Common::String w = readSource("engines/agi/words.cpp");
		TS_ASSERT(w.contains("void Words::analyze(const char *rawUserInput, ParseResult &out) const"));
		TS_ASSERT(w.contains("_korean.load(\"agikor.dat\", \"AGIKOR\")"));
		TS_ASSERT(w.contains("buildKoreanCandidates(typed, _preps, MAX_WORDS, out.candidates);"));
		const Common::String t = readSource("engines/agi/op_test.cpp");
		TS_ASSERT(t.contains("bool AgiEngine::testSaidSpec(const uint16 *spec, int nwords)"));
		TS_ASSERT(t.contains("_words->commitCandidate(i);"));
		const Common::String c = readSource("engines/agi/console.cpp");
		TS_ASSERT(c.contains("registerCmd(\"saids\""));
		TS_ASSERT(c.contains("registerCmd(\"said\""));
		TS_ASSERT(c.contains("registerCmd(\"parse\""));
	}
};
