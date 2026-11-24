#include <cxxtest/TestSuite.h>

#include "common/scummsys.h"
#include "common/str.h"
#include "common/util.h"

#include "agi/agi.h"
#include "agi/textcells.h"

/**
 * Randomized differential tests of the TextCells conversions against an
 * independent model (a table-driven RFC 3629 decoder), run for both bounds
 * of every buffer: the capacity of the destination and the NUL terminator of
 * the source (a string cut inside a multibyte sequence). Strings live in
 * heap blocks sized exactly strlen + 1, so a read past the terminator is an
 * AddressSanitizer report in the sanitizer build.
 */
class AgiCellBoundsTestSuite : public CxxTest::TestSuite {
	uint32 _seed;

	uint32 rnd() {
		_seed = _seed * 1664525u + 1013904223u;
		return _seed >> 8;
	}

	// The model: decode the character at s (NUL terminated), the answer in
	// bytes (>= 1) and code point; a malformed sequence is its lead byte.
	static int modelDecode(const byte *s, uint32 &cp) {
		const byte b = s[0];
		struct Form {
			byte lo, hi;
			int len;
			uint32 min;
			byte mask;
		};
		static const Form forms[] = {
			{ 0xC2, 0xDF, 2, 0x80, 0x1F },
			{ 0xE0, 0xEF, 3, 0x800, 0x0F },
			{ 0xF0, 0xF4, 4, 0x10000, 0x07 }
		};
		cp = b;
		if (b < 0x80)
			return 1;
		for (int f = 0; f < 3; f++) {
			if (b < forms[f].lo || b > forms[f].hi)
				continue;
			uint32 v = b & forms[f].mask;
			for (int i = 1; i < forms[f].len; i++) {
				if (s[i] < 0x80 || s[i] > 0xBF)
					return 1; // includes the NUL: the sequence is cut, the lead byte alone
				v = (v << 6) | (s[i] & 0x3F);
			}
			if (v < forms[f].min || (v >= 0xD800 && v <= 0xDFFF) || v > 0x10FFFF)
				return 1;
			cp = v;
			return forms[f].len;
		}
		return 1;
	}

	static void encode(uint32 cp, Common::String &out) {
		if (cp < 0x80) {
			out += (char)cp;
		} else if (cp < 0x800) {
			out += (char)(0xC0 | (cp >> 6));
			out += (char)(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += (char)(0xE0 | (cp >> 12));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		} else {
			out += (char)(0xF0 | (cp >> 18));
			out += (char)(0x80 | ((cp >> 12) & 0x3F));
			out += (char)(0x80 | ((cp >> 6) & 0x3F));
			out += (char)(0x80 | (cp & 0x3F));
		}
	}

	uint32 randomCodePoint() {
		switch (rnd() % 5) {
		case 0:
			return 0x20 + rnd() % 0x5F;
		case 1:
			return 0x80 + rnd() % 0x780;
		case 2:
			return 0xAC00 + rnd() % 11172;
		case 3: {
			uint32 cp = 0x800 + rnd() % 0xF800;
			return (cp >= 0xD800 && cp <= 0xDFFF) ? 0x3042 : cp;
		}
		default:
			return 0x10000 + rnd() % 0x10000;
		}
	}

	// A random text: valid characters and, with @p junk, stray bytes.
	Common::String randomText(int maxChars, bool junk) {
		Common::String s;
		const int n = rnd() % (maxChars + 1);
		for (int i = 0; i < n; i++) {
			if (junk && rnd() % 8 == 0)
				s += (char)(0x80 + rnd() % 0x80);
			else
				encode(randomCodePoint(), s);
		}
		return s;
	}

	// An exact-size heap copy (the terminator is the last byte of the block).
	static char *exactCopy(const Common::String &s) {
		char *p = (char *)malloc(s.size() + 1);
		memcpy(p, s.c_str(), s.size() + 1);
		return p;
	}

	static bool isEqualModel(const char *text, uint32 *outCps, int &count, int *outBytes = nullptr) {
		count = 0;
		const byte *p = (const byte *)text;
		while (*p) {
			uint32 cp;
			const int n = modelDecode(p, cp);
			if (outCps)
				outCps[count] = cp;
			if (outBytes)
				outBytes[count] = n;
			count++;
			p += n;
		}
		return true;
	}

public:
	void setUp() {
		_seed = 0x5eed1234;
	}

	// decode/cellLen/measure/clipCells/clipBytes against the model, on
	// valid and malformed text, every string also cut at every byte (a cut
	// inside a multibyte sequence) in an exact-size block.
	void test_conversionsEqualTheModelAtBothBounds() {
		static uint32 cps[4000];
		static int bytes[4000];
		for (int iter = 0; iter < 4000; iter++) {
			const Common::String full = randomText(12, iter % 2 == 1);
			const uint cuts = full.size();
			for (uint cut = 0; cut <= cuts; cut++) {
				const Common::String part(full.c_str(), cut);
				char *s = exactCopy(part);
				int count;
				isEqualModel(s, cps, count, bytes);

				TS_ASSERT_EQUALS((int)Agi::TextCells::cellLen(s, true), count);
				TS_ASSERT_EQUALS((int)Agi::TextCells::cellLen(s, false), (int)part.size());

				int pos = 0;
				for (int i = 0; i < count; i++) {
					uint32 cp = 0;
					const int n = Agi::TextCells::decode(s + pos, true, cp);
					TS_ASSERT_EQUALS(n, bytes[i]);
					TS_ASSERT_EQUALS(cp, cps[i]);
					pos += n;
				}

				// measure(): the bytes of the first k cells, for every k
				for (int k = 0; k <= count + 1; k++) {
					int16 b = -1;
					const int16 cells = Agi::TextCells::measure(s, (int16)part.size(), true, (int16)k, &b);
					TS_ASSERT_EQUALS((int)cells, count);
					int want = 0;
					for (int i = 0; i < k && i < count; i++)
						want += bytes[i];
					if (k > count)
						want = (int)part.size(); // never reached: the whole text
					TS_ASSERT_EQUALS((int)b, want);
				}

				// clipCells(): the first k cells
				for (int k = 0; k <= count + 1; k++) {
					int want = 0;
					for (int i = 0; i < k && i < count; i++)
						want += bytes[i];
					const Common::String c = Agi::TextCells::clipCells(s, (int16)k, true);
					TS_ASSERT_EQUALS((int)c.size(), want);
					TS_ASSERT(memcmp(c.c_str(), s, want) == 0);
				}

				// clipBytes(): the longest whole prefix within the budget
				for (int budget = 0; budget <= (int)part.size() + 2; budget++) {
					int want = 0;
					for (int i = 0; i < count && want + bytes[i] <= budget; i++)
						want += bytes[i];
					const Common::String c = Agi::TextCells::clipBytes(s, (int16)budget, true);
					TS_ASSERT_EQUALS((int)c.size(), want);
					TS_ASSERT(memcmp(c.c_str(), s, want) == 0);
					// the byte path is a plain cut
					const Common::String b2 = Agi::TextCells::clipBytes(s, (int16)budget, false);
					TS_ASSERT_EQUALS((int)b2.size(), MIN<int>(budget, part.size()));
				}
				free(s);
			}
		}
	}

	// toCells() holds capacity - 1 cells and a terminator and writes nothing
	// past capacity; fromCells() inverts it for valid text.
	void test_toCellsStaysInItsBufferAndRoundTrips() {
		for (int iter = 0; iter < 20000; iter++) {
			const Common::String text = randomText(60, false);
			const int capacity = 1 + rnd() % 64;
			uint32 buf[64 + 8];
			for (int i = 0; i < 64 + 8; i++)
				buf[i] = 0xDEADBEEF;
			char *s = exactCopy(text);
			const int16 n = Agi::TextCells::toCells(s, buf, (int16)capacity, true);
			uint32 cps[400];
			int count;
			isEqualModel(s, cps, count);
			TS_ASSERT_EQUALS((int)n, MIN(count, capacity - 1));
			TS_ASSERT_EQUALS(buf[n], 0u);
			for (int i = 0; i < n; i++)
				TS_ASSERT_EQUALS(buf[i], cps[i]);
			for (int i = capacity; i < 64 + 8; i++)
				TS_ASSERT_EQUALS(buf[i], 0xDEADBEEFu);
			TS_ASSERT_EQUALS((int)Agi::TextCells::cellCount(buf), (int)n);

			// the cells back: the text clipped to n cells
			const Common::String back = Agi::TextCells::fromCells(buf, true);
			const Common::String want = Agi::TextCells::clipCells(s, n, true);
			TS_ASSERT_EQUALS(back, want);
			TS_ASSERT_EQUALS((int)Agi::TextCells::utf8Bytes(buf, n), (int)back.size());
			TS_ASSERT(Agi::TextCells::isValidUtf8(back.c_str()));
			free(s);
		}
	}

	// isValidUtf8() is "the model decodes every character from its own bytes
	// with no fallback", and a cut valid text is invalid exactly when it ends
	// inside a character.
	void test_isValidUtf8EqualsTheModel() {
		for (int iter = 0; iter < 20000; iter++) {
			const Common::String text = randomText(10, iter % 3 == 0);
			for (uint cut = 0; cut <= text.size(); cut++) {
				const Common::String part(text.c_str(), cut);
				char *s = exactCopy(part);
				bool ok = true;
				const byte *p = (const byte *)s;
				while (*p) {
					uint32 cp;
					const int n = modelDecode(p, cp);
					if (n == 1 && *p >= 0x80) {
						ok = false;
						break;
					}
					p += n;
				}
				TS_ASSERT_EQUALS(Agi::TextCells::isValidUtf8(s), ok);
				free(s);
			}
		}
	}

	// The menu hooks of a9: dropDownColumn is upstream's two-way expression,
	// and clipCells on the byte path is upstream's delete-one-at-a-time loop.
	void test_dropDownColumnAndMenuClipEqualUpstream() {
		for (int col = 0; col < 40; col++) {
			for (int len = 0; len < 70; len++) {
				const int16 upstream = (len + col < 39) ? col : (39 - len);
				TS_ASSERT_EQUALS((int)Agi::TextCells::dropDownColumn((int16)col, (int16)len, 40), (int)upstream);
			}
		}
		for (int iter = 0; iter < 20000; iter++) {
			Common::String text = randomText(30, true);
			int16 textLen = (int16)text.size();
			int16 curColumnEnd = (int16)(rnd() % 70);
			// upstream: while (textLen && curColumnEnd > 40) delete the last byte
			Common::String up = text;
			int16 upLen = textLen, upEnd = curColumnEnd;
			while (upLen && upEnd > 40) {
				up.deleteLastChar();
				upLen--;
				upEnd--;
			}
			// the hook
			Common::String mine = text;
			int16 myLen = textLen, myEnd = curColumnEnd;
			if (myLen && myEnd > 40) {
				const int16 keep = MAX<int16>(0, myLen - (myEnd - 40));
				mine = Agi::TextCells::clipCells(mine.c_str(), keep, false);
				myEnd -= myLen - keep;
				myLen = keep;
			}
			TS_ASSERT_EQUALS(mine, up);
			TS_ASSERT_EQUALS((int)myLen, (int)upLen);
			TS_ASSERT_EQUALS((int)myEnd, (int)upEnd);
		}
	}

	// compare.strings: agiStringsEqual() is upstream's testCompareStrings()
	// (op_test.cpp, v2026.4.0: drop space, tab, - . , : ; ! and ', tolower the
	// rest, strcmp) for every pair of strings of up to 39 bytes.
	static bool upstreamCompare(const char *s1, const char *s2) {
		char ms1[40], ms2[40];
		int j, k, l;
		Common::strlcpy(ms1, s1, 40);
		Common::strlcpy(ms2, s2, 40);
		l = strlen(ms1);
		for (k = 0, j = 0; k < l; k++) {
			switch (ms1[k]) {
			case 0x20: case 0x09: case '-': case '.': case ',': case ':': case ';': case '!': case '\'':
				break;
			default:
				ms1[j++] = tolower(ms1[k]);
				break;
			}
		}
		ms1[j] = 0;
		l = strlen(ms2);
		for (k = 0, j = 0; k < l; k++) {
			switch (ms2[k]) {
			case 0x20: case 0x09: case '-': case '.': case ',': case ':': case ';': case '!': case '\'':
				break;
			default:
				ms2[j++] = tolower(ms2[k]);
				break;
			}
		}
		ms2[j] = 0;
		return !strcmp(ms1, ms2);
	}

	void test_compareStringsEqualsUpstream() {
		static const char alphabet[] = { 'a', 'A', 'b', 'B', 'z', ' ', '\t', '-', '.', ',', ':', ';', '!', '\'', '1', (char)0xE9, (char)0xC9, (char)0xEA, (char)0xB0 };
		uint same = 0, different = 0;
		for (int iter = 0; iter < 200000; iter++) {
			char a[40], b[40];
			const int la = rnd() % 8, lb = rnd() % 8;
			for (int i = 0; i < la; i++)
				a[i] = alphabet[rnd() % ARRAYSIZE(alphabet)];
			a[la] = 0;
			if (rnd() % 3 == 0) { // mostly equal up to the dropped characters and case
				int n = 0;
				for (int i = 0; i < la && n < 30; i++) {
					if (rnd() % 4 == 0)
						b[n++] = ' ';
					b[n++] = (rnd() % 2) ? (char)toupper((byte)a[i]) : a[i];
				}
				b[n] = 0;
			} else {
				for (int i = 0; i < lb; i++)
					b[i] = alphabet[rnd() % ARRAYSIZE(alphabet)];
				b[lb] = 0;
			}
			const bool up = upstreamCompare(a, b);
			TS_ASSERT_EQUALS(Agi::TextCells::agiStringsEqual(a, b), up);
			(up ? same : different)++;
		}
		TS_ASSERT(same > 5000u && different > 5000u);
	}

	// SystemUI's save list line in UTF-8: valid UTF-8, "NN:" first, exactly 31
	// cells (the description cut to 28 and padded), never more than the
	// 31 * 4 + 1 bytes of the display buffer, and the kept description is a
	// prefix of the given one.
	void test_savedGameLineUtf8KeepsItsShape() {
		for (int iter = 0; iter < 20000; iter++) {
			const Common::String desc = randomText(45, false);
			const int slot = 1 + rnd() % 99;
			char *d = exactCopy(desc);
			const Common::String line = Agi::TextCells::savedGameLine(d, (int16)slot, ' ', true);
			TS_ASSERT(Agi::TextCells::isValidUtf8(line.c_str()));
			TS_ASSERT_EQUALS((int)Agi::TextCells::cellLen(line.c_str(), true), 31);
			TS_ASSERT(line.size() <= 31u * 4u);
			TS_ASSERT_EQUALS(line[0], (char)('0' + slot / 10));
			TS_ASSERT_EQUALS(line[1], (char)('0' + slot % 10));
			TS_ASSERT_EQUALS(line[2], ':');
			const Common::String kept = Agi::TextCells::clipCells(d, 28, true);
			TS_ASSERT(memcmp(line.c_str() + 3, kept.c_str(), kept.size()) == 0);
			free(d);
		}
	}

	// A save name the edit line accepts (utf8Bytes <= SAVEDGAME_DESCRIPTION_BYTES)
	// must be what saveGame() keeps: it copies the description with
	// strlcpy(description, text, SAVEDGAME_DESCRIPTION_LEN), which leaves room for
	// the terminator, so a budget of the whole field cut the last syllable of a
	// 30-byte name after the player had verified it.
	void test_saveNameBudgetSurvivesTheDescriptionCopy() {
		uint thirty = 0;
		for (int iter = 0; iter < 20000; iter++) {
			const Common::String name = randomText(12, false);
			uint32 cells[64];
			char *s = exactCopy(name);
			const int16 n = Agi::TextCells::toCells(s, cells, 64, true);
			free(s);
			if (Agi::TextCells::utf8Bytes(cells, n) > SAVEDGAME_DESCRIPTION_BYTES)
				continue; // refused by the edit line
			char description[SAVEDGAME_DESCRIPTION_LEN + 1];
			memset(description, 0, sizeof(description));
			Common::strlcpy(description, Agi::TextCells::fromCells(cells, true).c_str(), SAVEDGAME_DESCRIPTION_LEN);
			TS_ASSERT_EQUALS(Common::String(description), Agi::TextCells::fromCells(cells, true));
			thirty += Agi::TextCells::utf8Bytes(cells, n) >= 29;
		}
		// the generator must reach names at the limit
		TS_ASSERT(thirty > 20u);
	}

	// set.simple's automatic save description (op_cmd.cpp): on the byte path the
	// buffer is upstream's exactly - strncpy(dst, src, size) and a terminator at
	// the end, so every byte after the text is zero and the save file does not
	// change - and in UTF-8 it is the same cut on a character boundary.
	void test_automaticDescriptionIsUpstreamsOnTheBytePath() {
		for (int iter = 0; iter < 20000; iter++) {
			const Common::String text = randomText(14, true);
			char *src = exactCopy(text);
			char upstream[31], mine[31];
			for (int i = 0; i < 31; i++)
				upstream[i] = mine[i] = (char)(1 + rnd() % 255); // stale content of an earlier description
			strncpy(upstream, src, sizeof(upstream));
			upstream[sizeof(upstream) - 1] = '\0';
			Agi::TextCells::copyDescription(mine, sizeof(mine), src, false);
			TS_ASSERT(memcmp(upstream, mine, sizeof(mine)) == 0);

			Agi::TextCells::copyDescription(mine, sizeof(mine), src, true);
			TS_ASSERT(Agi::TextCells::isValidUtf8(mine) || !Agi::TextCells::isValidUtf8(src));
			const Common::String kept = Agi::TextCells::clipBytes(src, 30, true);
			TS_ASSERT_EQUALS(Common::String(mine), kept);
			for (uint i = kept.size(); i < sizeof(mine); i++)
				TS_ASSERT_EQUALS(mine[i], 0);
			free(src);
		}
	}
};
