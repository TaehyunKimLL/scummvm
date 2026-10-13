#include <cxxtest/TestSuite.h>

#include <stdlib.h>

#include "common/file.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/ustr.h"
#include "agi/textcells.h"
#include "../../system/null_osystem.h"

// common/forbidden.h bans getenv() in engine code; a test may read its input path.
#pragma push_macro("getenv")
#undef getenv
static const char *wrapCorpusPath() {
	return getenv("AGI_WRAP_CORPUS");
}
#pragma pop_macro("getenv")

// AGI's text grid is 40x25 cells and one character is one cell; in UTF-8
// text mode a Hangul syllable is three bytes and one cell. The 22 cases
// below are K5's (harness/k5wrap.cpp at fad8a5c, engine 293fb64383b): the
// reported width is the width the screen needs, the reported height is the
// lines produced, and no break lands inside a character.

#define KO_GA  "\xEA\xB0\x80"
#define KO_GAK "\xEA\xB0\x81"
#define KO_GAN "\xEA\xB0\x84"
#define KO_GAL "\xEA\xB0\x88"
#define KO_GAM "\xEA\xB0\x90"
#define KO14 KO_GA KO_GAK KO_GAN KO_GAL KO_GAM KO_GA KO_GAK KO_GAN KO_GAL KO_GAM KO_GA KO_GAK KO_GAN KO_GAL
#define KO10 KO_GA KO_GAK KO_GAN KO_GAL KO_GAM KO_GA KO_GAK KO_GAN KO_GAL KO_GAM

// The pre-K5 byte loop, verbatim from engines/agi/text.cpp at 4600ec8a2b9
// (stringWordWrap), for the English A/B: it must agree with wordWrap(utf8
// false) byte for byte.
static void legacyWrap(const char *originalText, int16 maxWidth, Common::String &out, int16 &w, int16 &h) {
	static char resultWrappedBuffer[2000];
	int16 boxWidth = 0, boxHeight = 0, lineWidth = 0, lineWidthLeft = maxWidth;
	int16 wordStartPos = 0, curReadPos = 0, curWritePos = 0;
	while (originalText[curReadPos]) {
		if (originalText[curReadPos] == ' ')
			curReadPos++;
		while (originalText[curReadPos]) {
			if (originalText[curReadPos] == ' ')
				break;
			if (originalText[curReadPos] == 0x0A)
				break;
			curReadPos++;
		}
		byte wordEndChar = originalText[curReadPos];
		int16 wordLen = curReadPos - wordStartPos;
		if (wordLen >= lineWidthLeft) {
			if (wordLen) {
				if (originalText[wordStartPos] == ' ') {
					wordStartPos++;
					wordLen--;
				}
			}
			if (wordLen > maxWidth) {
				curReadPos = curReadPos - (wordLen - maxWidth);
				wordLen = maxWidth;
			}
			resultWrappedBuffer[curWritePos++] = 0x0A;
			if (lineWidth > boxWidth)
				boxWidth = lineWidth;
			boxHeight++; lineWidth = 0;
			lineWidthLeft = maxWidth;
			if (boxHeight >= 20)
				break;
		}
		memcpy(&resultWrappedBuffer[curWritePos], &originalText[wordStartPos], wordLen);
		lineWidth += wordLen;
		lineWidthLeft -= wordLen;
		curWritePos += wordLen;
		if (wordEndChar == 0x0A) {
			curReadPos++;
			resultWrappedBuffer[curWritePos++] = 0x0A;
			if (lineWidth > boxWidth)
				boxWidth = lineWidth;
			boxHeight++; lineWidth = 0;
			lineWidthLeft = maxWidth;
			if (boxHeight >= 20)
				break;
		}
		wordStartPos = curReadPos;
	}
	resultWrappedBuffer[curWritePos] = 0;
	if (curReadPos > 0) {
		if (lineWidth > boxWidth)
			boxWidth = lineWidth;
		boxHeight++;
	}
	out = resultWrappedBuffer;
	w = boxWidth;
	h = boxHeight;
}

class AgiKoreanTextTestSuite : public CxxTest::TestSuite {
	static int widest(const Common::String &s, bool utf8) {
		int best = 0;
		uint start = 0;
		for (uint i = 0; i <= s.size(); i++) {
			if (i == s.size() || s[i] == '\n') {
				const Common::String line(s.c_str() + start, i - start);
				best = MAX<int>(best, Agi::TextCells::cellLen(line.c_str(), utf8));
				start = i + 1;
			}
		}
		return best;
	}
	static int lines(const Common::String &s, const char *input) {
		if (!*input)
			return 0;
		int n = 1;
		for (uint i = 0; i < s.size(); i++)
			n += s[i] == '\n';
		return n;
	}
	static bool split(const Common::String &s) {
		for (uint i = 0; i + 1 < s.size(); i++)
			if (s[i] == '\n' && ((byte)s[i + 1] & 0xC0) == 0x80)
				return true;
		return false;
	}
	static void check(const char *text, int16 maxWidth, bool utf8 = true) {
		Common::String out;
		int16 w = 0, h = 0;
		Agi::TextCells::wordWrap(text, maxWidth, utf8, false, out, w, h);
		TS_ASSERT_EQUALS(w, widest(out, utf8));
		TS_ASSERT_EQUALS(h, lines(out, text));
		// without UTF-8 a byte is a cell, and a cut between bytes is the
		// original behaviour: only a UTF-8 wrap must never split a character
		if (utf8)
			TS_ASSERT(!split(out));
	}

public:
	// FSNode (the corpus leg) needs a g_system
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

	void test_k5_ascii() {
		check("open door", 40);
		check("the quick brown fox jumps over the lazy dog", 40);
		check("  Lake Michigan!", 9);
		check("supercalifragilistic", 10);
		check("line one\nline two\nline three", 40);
		check("padded out with spaces      ", 40);
		check("", 40);
		check(" ", 40);
	}

	void test_k5_korean() {
		check("\xEB\xAC\xB8 \xEC\x97\xB4\xEC\x96\xB4", 40);
		check("\xED\x95\x9C\xEA\xB8\x80", 40);
		check(KO14, 40);
		check(KO14, 10);
		check(KO10 KO10 KO10, 25);
	}

	void test_k5_mixed_and_boundaries() {
		check("Roger " KO_GA KO_GAK " 42 " KO_GAN KO_GAL KO_GAM " ok", 40);
		check("Roger " KO_GA KO_GAK " 42 " KO_GAN KO_GAL KO_GAM " ok", 12);
		check(KO10, 11);
		check(KO10, 10);
	}

	void test_k5_malformed() {
		check("ab\x80" "cd", 40);
		check("ab\xEA\xB0" "cd", 40);
		check("abcd\xEA", 40);
	}

	void test_k5_bytes_without_utf8() {
		check("the quick brown fox jumps over the lazy dog", 40, false);
		check(KO14, 40, false);
	}

	void test_fourteen_syllables_are_fourteen_cells() {
		Common::String out;
		int16 w, h;
		Agi::TextCells::wordWrap(KO14, 40, true, false, out, w, h);
		TS_ASSERT_EQUALS(w, 14);
		TS_ASSERT_EQUALS(h, 1);
	}

	void test_hangul_any_fills_the_line() {
		Common::String out;
		int16 w, h;
		Agi::TextCells::wordWrap("AB " KO_GA KO_GAK KO_GAN KO_GAL KO_GAM, 6, true, false, out, w, h);
		TS_ASSERT_EQUALS(out, "AB\n" KO_GA KO_GAK KO_GAN KO_GAL KO_GAM);
		Agi::TextCells::wordWrap("AB " KO_GA KO_GAK KO_GAN KO_GAL KO_GAM, 6, true, true, out, w, h);
		TS_ASSERT_EQUALS(out, "AB " KO_GA KO_GAK "\n" KO_GAN KO_GAL KO_GAM);
		TS_ASSERT_EQUALS(w, 5);
		TS_ASSERT_EQUALS(h, 2);
		// Latin words never break inside, hangul=any or not
		Agi::TextCells::wordWrap("AB CDEFG", 6, true, true, out, w, h);
		TS_ASSERT_EQUALS(out, "AB\nCDEFG");
	}

	void test_truncated_flag() {
		Common::String text, out;
		for (int i = 0; i < 25; i++)
			text += "line\n";
		int16 w, h;
		bool truncated = false;
		Agi::TextCells::wordWrap(text.c_str(), 30, true, false, out, w, h, &truncated);
		TS_ASSERT(truncated);
		// the byte loop counts the line it stopped in: 20 breaks + 1
		TS_ASSERT_EQUALS(h, 21);
		Agi::TextCells::wordWrap("short", 30, true, false, out, w, h, &truncated);
		TS_ASSERT(!truncated);
	}

	void test_truncated_when_the_last_word_is_dropped() {
		// 19 lines of "line\n", then a word that needs line 21 and is the last text
		Common::String text, out;
		for (int i = 0; i < 19; i++)
			text += "line\n";
		text += "ab";
		int16 w, h;
		bool truncated = false;
		Agi::TextCells::wordWrap(text.c_str(), 30, true, false, out, w, h, &truncated);
		TS_ASSERT(!truncated); // fits on line 20
		text = Common::String();
		for (int i = 0; i < 19; i++)
			text += "line\n";
		text += "aaaa " + Common::String(30, 'x');
		Agi::TextCells::wordWrap(text.c_str(), 30, true, false, out, w, h, &truncated);
		TS_ASSERT(truncated);
	}

	void test_english_ab_against_the_byte_loop() {
		static const char *const corpus[] = {
			"open door", "  Lake Michigan!", "supercalifragilisticexpialidocious and more",
			"line one\nline two\nline three", "padded out with spaces      ", "", " ", "a\n\nb",
			"You are in a small room. There is a door to the north and a window to the east.",
			"%v3 of %v7", "x  y   z    w", "endswithnewline\n", "\n", "word\nanotherveryveryverylongword here",
		};
		static const int16 widths[] = { 9, 20, 25, 30, 35, 40 };
		for (uint i = 0; i < ARRAYSIZE(corpus); i++) {
			for (uint k = 0; k < ARRAYSIZE(widths); k++) {
				Common::String a, b;
				int16 aw, ah, bw, bh;
				legacyWrap(corpus[i], widths[k], a, aw, ah);
				Agi::TextCells::wordWrap(corpus[i], widths[k], false, false, b, bw, bh);
				TS_ASSERT_EQUALS(a, b);
				TS_ASSERT_EQUALS(aw, bw);
				TS_ASSERT_EQUALS(ah, bh);
				// pure ASCII is also identical with utf8 on
				Agi::TextCells::wordWrap(corpus[i], widths[k], true, false, b, bw, bh);
				TS_ASSERT_EQUALS(a, b);
			}
		}
	}

	// K5's 57378-case leg: every ASCII message of the fan games, if the
	// harness wrote the corpus (harness/agi/agi_kr_extract.py --corpus).
	void test_english_ab_on_the_fan_game_corpus() {
		const char *path = wrapCorpusPath();
		if (!path) {
			TS_SKIP("AGI_WRAP_CORPUS not set");
			return;
		}
		Common::FSNode node((Common::Path(path, '/')));
		Common::SeekableReadStream *in = node.createReadStream();
		TS_ASSERT(in);
		if (!in)
			return;
		static const int16 widths[] = { 9, 20, 25, 30, 35, 40 };
		uint cases = 0, differ = 0;
		while (!in->eos()) {
			Common::String line = in->readLine();
			if (line.empty())
				continue;
			const Common::String &text = line;
			Common::String msg;
			for (uint i = 0; i < text.size(); i++) {
				if (text[i] == '\\' && i + 1 < text.size()) {
					msg += text[i + 1] == 'n' ? '\n' : text[i + 1];
					i++;
				} else {
					msg += text[i];
				}
			}
			for (uint k = 0; k < ARRAYSIZE(widths); k++) {
				Common::String a, b;
				int16 aw, ah, bw, bh;
				legacyWrap(msg.c_str(), widths[k], a, aw, ah);
				Agi::TextCells::wordWrap(msg.c_str(), widths[k], true, false, b, bw, bh);
				cases++;
				if (a != b || aw != bw || ah != bh)
					differ++;
			}
		}
		delete in;
		TS_ASSERT_LESS_THAN(1000u, cases);
		TS_ASSERT_EQUALS(differ, 0u);
	}

	void test_cell_helpers() {
		using namespace Agi::TextCells;
		TS_ASSERT_EQUALS(cellLen("a" KO_GA "b", true), 3);
		TS_ASSERT_EQUALS(cellLen("a" KO_GA "b", false), 5);
		TS_ASSERT_EQUALS(clipCells("a" KO_GA KO_GAK, 2, true), "a" KO_GA);
		TS_ASSERT_EQUALS(clipCells("abc", 2, false), "ab");
		uint32 cp = 0;
		TS_ASSERT_EQUALS(decode("\x8D", false, cp), 1);
		TS_ASSERT_EQUALS(cp, 0x8Du);
		uint32 cells[8];
		TS_ASSERT_EQUALS(toCells("x" KO_GA, cells, 8, true), 2);
		TS_ASSERT_EQUALS(cells[1], 0xAC00u);
		TS_ASSERT_EQUALS(fromCells(cells, true), "x" KO_GA);
		TS_ASSERT_EQUALS(toCells("\x82" "a", cells, 8, false), 2);
		TS_ASSERT_EQUALS(cells[0], 0x82u);
		TS_ASSERT_EQUALS(fromCells(cells, false), "\x82" "a");
		TS_ASSERT_EQUALS(toCells("abcdef", cells, 4, false), 3);
		TS_ASSERT_EQUALS(cellCount(cells), 3);
		TS_ASSERT(isHangulSyllable(0xAC00));
		TS_ASSERT(!isHangulSyllable(0x3131));
	}

	void test_clip_bytes_keeps_whole_characters() {
		using Agi::TextCells::clipBytes;
		// "래리 첫번째 저장 게임": 9 syllables of 3 bytes and 3 spaces = 30 bytes.
		const char *desc = "\xEB\x9E\x98\xEB\xA6\xAC \xEC\xB2\xAB\xEB\xB2\x88\xEC\xA7\xB8 "
		                   "\xEC\xA0\x80\xEC\x9E\xA5 \xEA\xB2\x8C\xEC\x9E\x84";
		// A 28-byte cut lands inside the last syllable (bytes 27..29).
		const Common::String c28 = clipBytes(desc, 28, true);
		TS_ASSERT_LESS_THAN_EQUALS(c28.size(), 28u);
		TS_ASSERT_EQUALS(Common::String(desc, c28.size()), c28);
		TS_ASSERT_EQUALS(c28.size(), 27u); // 8 syllables + 3 spaces
		TS_ASSERT_EQUALS(Agi::TextCells::cellLen(c28.c_str(), true), 11);
		TS_ASSERT_EQUALS(clipBytes(desc, 29, true).size(), 27u);
		TS_ASSERT_EQUALS(clipBytes(desc, 30, true).size(), 30u);
		// Every cut lands on a code-point boundary: no malformed tail.
		for (int16 n = 0; n <= 30; n++) {
			const Common::String c = clipBytes(desc, n, true);
			TS_ASSERT_LESS_THAN_EQUALS((int)c.size(), (int)n);
			TS_ASSERT_EQUALS(Common::U32String(c.c_str(), Common::kUtf8).encode(Common::kUtf8), c);
		}
		// Byte mode (English) is the old byte cut.
		TS_ASSERT_EQUALS(clipBytes("0123456789012345678901234567890123", 28, false), "0123456789012345678901234567");
		TS_ASSERT_EQUALS(clipBytes(desc, 28, false), Common::String(desc, 28));
		TS_ASSERT_EQUALS(clipBytes("short", 28, true), "short");
	}

	void test_decode_rejects_overlong_surrogate_and_out_of_range() {
		using Agi::TextCells::decode;
		uint32 cp = 0;
		// Well-formed edges still decode.
		TS_ASSERT_EQUALS(decode("\xC2\x80", true, cp), 2);
		TS_ASSERT_EQUALS(cp, 0x80u);
		TS_ASSERT_EQUALS(decode("\xE0\xA0\x80", true, cp), 3);
		TS_ASSERT_EQUALS(cp, 0x800u);
		TS_ASSERT_EQUALS(decode("\xED\x9F\xBF", true, cp), 3);
		TS_ASSERT_EQUALS(cp, 0xD7FFu);
		TS_ASSERT_EQUALS(decode("\xEE\x80\x80", true, cp), 3);
		TS_ASSERT_EQUALS(cp, 0xE000u);
		TS_ASSERT_EQUALS(decode("\xF0\x90\x80\x80", true, cp), 4);
		TS_ASSERT_EQUALS(cp, 0x10000u);
		TS_ASSERT_EQUALS(decode("\xF4\x8F\xBF\xBF", true, cp), 4);
		TS_ASSERT_EQUALS(cp, 0x10FFFFu);
		// Overlong forms: the lead byte is one cell.
		TS_ASSERT_EQUALS(decode("\xC0\x80", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xC0u);
		TS_ASSERT_EQUALS(decode("\xC1\xBF", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xC1u);
		TS_ASSERT_EQUALS(decode("\xE0\x9F\xBF", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xE0u);
		TS_ASSERT_EQUALS(decode("\xF0\x8F\xBF\xBF", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xF0u);
		// Surrogates.
		TS_ASSERT_EQUALS(decode("\xED\xA0\x80", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xEDu);
		TS_ASSERT_EQUALS(decode("\xED\xBF\xBF", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xEDu);
		// Past U+10FFFF.
		TS_ASSERT_EQUALS(decode("\xF4\x90\x80\x80", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xF4u);
		TS_ASSERT_EQUALS(decode("\xF7\xBF\xBF\xBF", true, cp), 1);
		TS_ASSERT_EQUALS(cp, 0xF7u);
		// A rejected sequence is one cell per byte, as other malformed input.
		TS_ASSERT_EQUALS(Agi::TextCells::cellLen("a\xED\xA0\x80" "b", true), 5);
	}

	void test_compare_strings_fold() {
		using Agi::TextCells::agiStringsEqual;
		TS_ASSERT(agiStringsEqual("Open-Sesame!", "open sesame"));
		TS_ASSERT(!agiStringsEqual("open", "opened"));
		TS_ASSERT(agiStringsEqual("\xEC\x97\xB4\xEC\x87\xA0.", "\xEC\x97\xB4 \xEC\x87\xA0"));
		TS_ASSERT(!agiStringsEqual("\xEC\x97\xB4", "\xEC\x87\xA0"));
	}
};
