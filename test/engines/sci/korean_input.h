#include <cxxtest/TestSuite.h>

#include "sci/korean_input.h"

// U+BB38 문 = EB AC B8, U+C744 을 = EC 9D 84, U+C5F4 열 = EC 97 B4, U+3141 ㅁ = E3 85 81
class SciKoreanInputTestSuite : public CxxTest::TestSuite {
	typedef Sci::KoreanEditLine L;

	static void type(L &line, const char *keys, Common::String &text, uint &cursor, uint max = 40) {
		for (const char *k = keys; *k; ++k)
			line.key((byte)*k, false, true, 0, text, cursor, max);
	}

public:
	void test_composesSyllablesInPlace() {
		L line;
		Common::String text;
		uint cursor = 0;
		line.key('a', false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "\xE3\x85\x81");     // pre-edit ㅁ is in the text
		TS_ASSERT(line.isComposing());
		type(line, "ns", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");     // 문
		type(line, "dmf", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8\xEC\x9D\x84"); // 문을
		TS_ASSERT_EQUALS(cursor, text.size());
		line.key(' ', false, true, 0, text, cursor, 40);
		TS_ASSERT(!line.isComposing());
		type(line, "duf", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8\xEC\x9D\x84 \xEC\x97\xB4"); // 문을 열
	}

	void test_backspaceTakesJamoThenSyllables() {
		L line;
		Common::String text;
		uint cursor = 0;
		type(line, "ans", text, cursor);
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB4");     // 무
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		TS_ASSERT(text.empty());
		TS_ASSERT(!line.isComposing());
		text = "ab\xEB\xAC\xB8";
		cursor = text.size();
		line.key(L::kKeyBackspace, false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "ab");               // a whole code point
		TS_ASSERT_EQUALS(cursor, 2u);
	}

	void test_cursorStepsByCodePoint() {
		L line;
		Common::String text("\xEB\xAC\xB8x");
		uint cursor = text.size();
		line.key(L::kKeyLeft, false, false, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(cursor, 3u);
		line.key(L::kKeyLeft, false, false, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(cursor, 0u);
		line.key(L::kKeyDelete, false, false, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "x");
	}

	void test_englishModeAndOverflow() {
		L line;
		Common::String text;
		uint cursor = 0;
		TS_ASSERT_EQUALS(line.key('a', false, false, 0, text, cursor, 2), L::kChanged);
		TS_ASSERT_EQUALS(text, "a");
		TS_ASSERT_EQUALS(line.key('a', false, true, 0, text, cursor, 2), L::kRefused); // 3 bytes do not fit
		TS_ASSERT_EQUALS(text, "a");
		TS_ASSERT(!line.isComposing());
	}

	void test_osImeSyllable() {
		L line;
		Common::String text("x");
		uint cursor = 1;
		// The script opened the prompt with the surrogate 'x' in it.
		TS_ASSERT(line.adopt(text, cursor, false, 0xBB38, 40));
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");
		line.remember(text);
		TS_ASSERT_EQUALS(line.key('x', false, false, 0xC744, text, cursor, 40), L::kChanged);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8\xEC\x9D\x84");
	}

	void test_firstKeyOfANewPromptIsComposed() {
		L line;
		Common::String text("a");
		uint cursor = 1;
		TS_ASSERT(line.adopt(text, cursor, true, 0, 40));
		TS_ASSERT_EQUALS(text, "\xE3\x85\x81");
		line.remember(text);
		TS_ASSERT(!line.adopt(text, cursor, true, 0, 40));
		type(line, "ns", text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");
	}

	void test_undo() {
		L line;
		Common::String text;
		uint cursor = 0;
		type(line, "an", text, cursor);
		line.key('s', false, true, 0, text, cursor, 40);
		line.undo(text, cursor);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB4");     // 무 again
		line.key('s', false, true, 0, text, cursor, 40);
		TS_ASSERT_EQUALS(text, "\xEB\xAC\xB8");
	}
};
