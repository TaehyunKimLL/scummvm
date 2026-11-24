#include <cxxtest/TestSuite.h>

#include "common/str.h"
#include "common/ustr.h"

#include "agi/textcells.h"

// TextCells: one character is one cell of AGI's 40x25 grid. In UTF-8 text
// mode a character is a code point, else a byte (the Russian, Hebrew and
// French fan fonts). The tests compare against the upstream byte loop and a
// reference decoder over every input of a class or a large random sample:
// the review rounds of P3/P4/P6a found their bugs that way.
class AgiTextCellsTestSuite : public CxxTest::TestSuite {
	// A small deterministic generator (no g_system needed).
	struct Lcg {
		uint32 s;
		explicit Lcg(uint32 seed) : s(seed) {}
		uint32 next() { s = s * 1103515245u + 12345u; return s >> 8; }
		uint32 below(uint32 n) { return next() % n; }
	};

	// engines/agi/text.cpp stringWordWrap() of v2026.4.0, the loop verbatim
	// (only the static buffer became a local one).
	static void upstreamWrap(const char *originalText, int16 maxWidth, Common::String &out, int16 &w, int16 &h) {
		char resultWrappedBuffer[2000];
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

	// RFC 3629 by the letter: the code point and length of a well-formed
	// sequence at @p p, or 0 (TextCells::decode() then takes one byte).
	static int referenceDecode(const byte *p, uint32 &cp) {
		const byte b = p[0];
		int n;
		uint32 lo = 0x80, hi = 0xBF;
		if (b < 0x80) { cp = b; return 1; }
		else if (b >= 0xC2 && b <= 0xDF) { n = 2; cp = b & 0x1F; }
		else if (b == 0xE0) { n = 3; cp = 0; lo = 0xA0; }
		else if (b >= 0xE1 && b <= 0xEC) { n = 3; cp = b & 0x0F; }
		else if (b == 0xED) { n = 3; cp = 0x0D; hi = 0x9F; }
		else if (b >= 0xEE && b <= 0xEF) { n = 3; cp = b & 0x0F; }
		else if (b == 0xF0) { n = 4; cp = 0; lo = 0x90; }
		else if (b >= 0xF1 && b <= 0xF3) { n = 4; cp = b & 0x07; }
		else if (b == 0xF4) { n = 4; cp = 4; hi = 0x8F; }
		else return 0;
		for (int i = 1; i < n; i++) {
			const byte c = p[i];
			if (c < (i == 1 ? lo : 0x80) || c > (i == 1 ? hi : 0xBF))
				return 0;
			cp = (cp << 6) | (c & 0x3F);
		}
		return n;
	}

	static void randomAscii(Lcg &r, char *buf, uint maxLen) {
		static const char alphabet[] = "abcdefghij  \n.,!-";
		const uint len = r.below(maxLen);
		for (uint i = 0; i < len; i++)
			buf[i] = alphabet[r.below(sizeof(alphabet) - 1)];
		buf[len] = 0;
	}

public:
	// Every byte string of up to three bytes (and all four-byte leads with
	// sampled tails): decode() is the reference decoder, a malformed
	// sequence is its first byte, and it never reads past the NUL.
	void test_decodeIsTheReferenceOnEveryShortInput() {
		byte buf[5];
		uint mismatches = 0;
		for (uint a = 1; a < 256; a++) {
			for (uint b = 0; b < 256; b++) {
				for (uint c = 0; c < 256; c++) {
					if (a < 0x80 && (b || c))
						continue; // one cell whatever follows
					buf[0] = a; buf[1] = b; buf[2] = c; buf[3] = 0; buf[4] = 0;
					if (!b) buf[2] = 0;
					uint32 want = 0, got = 0;
					const int n = referenceDecode(buf, want);
					const int m = Agi::TextCells::decode((const char *)buf, true, got);
					if (n ? (m != n || got != want) : (m != 1 || got != a))
						mismatches++;
				}
			}
		}
		Lcg r(4);
		for (uint i = 0; i < 2000000; i++) {
			buf[0] = 0xF0 + r.below(16); buf[1] = r.below(256); buf[2] = r.below(256); buf[3] = r.below(256); buf[4] = 0;
			uint32 want = 0, got = 0;
			const int n = referenceDecode(buf, want);
			const int m = Agi::TextCells::decode((const char *)buf, true, got);
			if (n ? (m != n || got != want) : (m != 1 || got != buf[0]))
				mismatches++;
		}
		TS_ASSERT_EQUALS(mismatches, 0u);
	}

	// Upstream drops the byte after a word cut at the width when that word
	// ended in a line feed (the cut piece takes the line feed). The UTF-8 path
	// does not (the byte could be a lead byte); this is the only difference.
	static bool cutWordEndsInLineFeed(const char *text, int16 width) {
		int16 run = 0;
		for (const char *p = text; *p; p++) {
			if (*p == ' ') {
				run = 0;
			} else if (*p == '\n') {
				if (run > width)
					return true;
				run = 0;
			} else {
				run++;
			}
		}
		return false;
	}

	// The byte path (utf8 false) is upstream's loop on 300000 random lines and
	// every width; ASCII with utf8 true is too, apart from the one fix above.
	void test_wrapEqualsUpstreamOnRandomAscii() {
		Lcg r(1);
		char text[160];
		uint differ = 0, differUtf8 = 0, fixed = 0;
		for (uint i = 0; i < 300000; i++) {
			randomAscii(r, text, sizeof(text) - 1);
			const int16 width = 1 + r.below(40);
			Common::String a, b, c;
			int16 aw, ah, bw, bh, cw, ch;
			upstreamWrap(text, width, a, aw, ah);
			Agi::TextCells::wordWrap(text, width, false, false, b, bw, bh);
			Agi::TextCells::wordWrap(text, width, true, false, c, cw, ch);
			if (a != b || aw != bw || ah != bh)
				differ++;
			if (a != c || aw != cw || ah != ch) {
				if (cutWordEndsInLineFeed(text, width))
					fixed++;
				else
					differUtf8++;
			}
		}
		TS_ASSERT_EQUALS(differ, 0u);
		TS_ASSERT_EQUALS(differUtf8, 0u);
		TS_ASSERT(fixed > 0u); // the generator does reach the fixed case
	}

	// Mixed Hangul/ASCII: no line holds more cells than the width, no break
	// falls inside a character, the reported size is the text's, and every
	// character of the input comes out in order (spaces at breaks dropped).
	void test_wrapKeepsCharactersWholeOnRandomHangul() {
		Lcg r(2);
		static const char *const pieces[] = { "\xEA\xB0\x80", "\xED\x9E\xA3", "\xEB\xB4\x90", "a", "bc", " ", " ", "\n", "!" };
		uint bad = 0;
		for (uint i = 0; i < 100000; i++) {
			Common::String in;
			const uint n = r.below(120);
			for (uint k = 0; k < n; k++)
				in += pieces[r.below(ARRAYSIZE(pieces))];
			const int16 width = 1 + r.below(40);
			const bool any = r.below(2);
			Common::String out;
			int16 w, h;
			bool truncated = false;
			Agi::TextCells::wordWrap(in.c_str(), width, true, any, out, w, h, &truncated);
			if (!Agi::TextCells::isValidUtf8(out.c_str()))
				bad++;
			int16 maxCells = 0, lines = out.empty() ? 0 : 1;
			Common::String line;
			for (uint p = 0; p <= out.size(); p++) {
				if (p == out.size() || out[p] == '\n') {
					maxCells = MAX<int16>(maxCells, Agi::TextCells::cellLen(line.c_str(), true));
					line.clear();
					if (p < out.size())
						lines++;
				} else {
					line += out[p];
				}
			}
			// 21: upstream counts the line after a stop at HEIGHT_MAX (20) too
			if (maxCells > width || w != maxCells || h > 21)
				bad++;
			if (!truncated) {
				Common::String a, b;
				for (uint p = 0; p < in.size(); p++)
					if (in[p] != ' ' && in[p] != '\n')
						a += in[p];
				for (uint p = 0; p < out.size(); p++)
					if (out[p] != ' ' && out[p] != '\n')
						b += out[p];
				if (a != b)
					bad++;
			}
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// clipBytes() and clipCells() never cut a character and are the longest such prefix.
	void test_clipsAreTheLongestWholePrefix() {
		Lcg r(3);
		static const char *const pieces[] = { "\xEA\xB0\x80", "\xF0\x9F\x98\x80", "\xC3\xA9", "a" };
		uint bad = 0;
		for (uint i = 0; i < 50000; i++) {
			Common::String in;
			const uint n = r.below(30);
			for (uint k = 0; k < n; k++)
				in += pieces[r.below(ARRAYSIZE(pieces))];
			const int16 maxBytes = r.below(60);
			const Common::String c = Agi::TextCells::clipBytes(in.c_str(), maxBytes, true);
			if ((int)c.size() > maxBytes || !Agi::TextCells::isValidUtf8(c.c_str()) || !in.hasPrefix(c))
				bad++;
			if (c.size() < in.size()) {
				uint32 cp;
				const int next = Agi::TextCells::decode(in.c_str() + c.size(), true, cp);
				if ((int)c.size() + next <= maxBytes)
					bad++; // one more character would have fitted
			}
			const int16 maxCells = r.below(20);
			const Common::String d = Agi::TextCells::clipCells(in.c_str(), maxCells, true);
			if (Agi::TextCells::cellLen(d.c_str(), true) != MIN<int16>(maxCells, Agi::TextCells::cellLen(in.c_str(), true)) || !in.hasPrefix(d))
				bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
	}

	// The save list line: the byte path is SystemUI::createSavedGameDisplayText() of v2026.4.0; the UTF-8 path is 31 cells.
	void test_savedGameLine() {
		Lcg r(5);
		uint bad = 0;
		for (uint i = 0; i < 20000; i++) {
			char desc[40];
			randomAscii(r, desc, 36);
			for (char *p = desc; *p; p++)
				if (*p == '\n')
					*p = 'x';
			const int16 slot = r.below(100);
			const char fill = r.below(2) ? ' ' : 0;
			// upstream
			char up[32];
			memset(up, fill, 31);
			char id[3];
			Common::sprintf_s(id, "%02d", slot);
			memcpy(up, id, 2);
			up[2] = ':';
			int16 len = strlen(desc);
			if (len > 28)
				len = 28;
			if (len > 0)
				memcpy(up + 3, desc, len);
			up[31] = 0;
			if (Agi::TextCells::savedGameLine(desc, slot, fill, false) != Common::String(up))
				bad++;
		}
		TS_ASSERT_EQUALS(bad, 0u);
		const Common::String ko = Agi::TextCells::savedGameLine("\xEA\xB0\x80\xEA\xB0\x80\xEA\xB0\x80", 7, ' ', true);
		TS_ASSERT_EQUALS(Agi::TextCells::cellLen(ko.c_str(), true), 31);
		TS_ASSERT(ko.hasPrefix("07:\xEA\xB0\x80"));
	}
};
