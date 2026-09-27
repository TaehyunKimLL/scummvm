#include <cxxtest/TestSuite.h>

#include "common/array.h"
#include "common/fs.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/system.h"
#include "graphics/hires_text/glyph_source.h"
#include "graphics/hires_text/glyph_source_fallback.h"
#include "graphics/hires_text/glyph_source_ttf.h"
#include "graphics/hires_text/text_layout.h"
#include "graphics/hires_text/unicode_props.h"
#include "sci/graphics/textlayout16.h"
#include "sci/utf8.h"

// The i18n test data (gamedata/: kq1-ko1/ ...) from SCUMMVM_TEST_I18N_DATA.
// The test runner is host code; common/forbidden.h's getenv guard is lifted
// for this one call only.
#pragma push_macro("getenv")
#undef getenv
static const char *i18nDataDir() {
	return getenv("SCUMMVM_TEST_I18N_DATA");
}
#pragma pop_macro("getenv")

/**
 * C11 Task 5: SCI decides hi-res text by the translation, measures glyphs
 * per glyph, and breaks UTF-8 lines with the shared layout stage
 * (I18N_TEXT_DESIGN.md sections 4.1, 4.2, 4.3).
 */
class SciI18nGatesTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		if (!g_system)
			Common::install_null_g_system();
	}

	void tearDown() {
		Common::uninstall_null_g_system();
	}

	// --- the gate -------------------------------------------------------

	void test_hires_text_applies() {
		Common::String why;
		TS_ASSERT(!Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kLatin1, false, why));
		TS_ASSERT_EQUALS(why, Common::String("no translation and no CJK code page"));

		why.clear();
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kLatin1, true, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_1, Common::kWindows949, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_01, Common::kWindows932, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_0_EARLY, Common::kWindows936, false, why));
		TS_ASSERT(Sci::hiresTextApplies(Sci::SCI_VERSION_1_LATE, Common::kWindows950, false, why));

		TS_ASSERT(!Sci::hiresTextApplies(Sci::SCI_VERSION_2, Common::kWindows949, true, why));
		TS_ASSERT_EQUALS(why, Common::String("SCI32 games do not support it yet"));
	}

	// --- the advance rule -----------------------------------------------

	void test_game_advance() {
		Graphics::GlyphMetrics hangul;
		hangul.advance = 16;
		hangul.wide = true;
		// The cell rule of today: a wide glyph advances by the wide cell.
		TS_ASSERT_EQUALS(Sci::gameAdvance(hangul, 4, 8, 2), 8);

		Graphics::GlyphMetrics mark;
		mark.advance = 0;
		mark.combining = true;
		TS_ASSERT_EQUALS(Sci::gameAdvance(mark, 4, 8, 2), 0);
		// Even a mark whose face gives it an advance (Thonburi) does not move.
		mark.advance = 12;
		TS_ASSERT_EQUALS(Sci::gameAdvance(mark, 4, 8, 2), 0);

		Graphics::GlyphMetrics koKai;      // U+0E01
		koKai.advance = 14;
		TS_ASSERT_EQUALS(Sci::gameAdvance(koKai, 4, 8, 2), 7);

		Graphics::GlyphMetrics saraAm;     // U+0E33, rounds half up
		saraAm.advance = 25;
		TS_ASSERT_EQUALS(Sci::gameAdvance(saraAm, 4, 8, 2), 13);

		// A face that cannot say keeps the narrow cell.
		Graphics::GlyphMetrics unknown;
		TS_ASSERT_EQUALS(Sci::gameAdvance(unknown, 4, 8, 2), 4);
	}

	// --- a bitmap bundle behind a TrueType face ---------------------------

	/** 8x8 at 1 bpp; 'A' has its first column-byte set in every row. */
	class OneBitSource : public Graphics::UnicodeGlyphSource {
	public:
		OneBitSource() { _row[0] = 0xF0; _row[1] = 0x01; }
		byte cellWidth() const override { return 8; }
		byte cellHeight() const override { return 8; }
		byte advanceNarrow() const override { return 8; }
		byte advanceWide() const override { return 16; }
		int bitsPerPixel() const override { return 1; }
		int cells(uint32 cp) override { return cp == 'A' ? 1 : 0; }
		const byte *row(uint32 cp, int y) override { return _row; }
		uint32 glyphCount() const override { return 1; }
	private:
		byte _row[2];
	};

	void test_normalized_source_pads_a_smaller_cell_at_8bpp() {
		Common::String error;
		Graphics::NormalizedGlyphSource *n =
			Graphics::NormalizedGlyphSource::create(new OneBitSource(), 12, 10, DisposeAfterUse::YES, error);
		TS_ASSERT(n);
		if (!n)
			return;
		TS_ASSERT_EQUALS(n->cellWidth(), 12);
		TS_ASSERT_EQUALS(n->cellHeight(), 10);
		TS_ASSERT_EQUALS(n->bitsPerPixel(), 8);
		TS_ASSERT_EQUALS(n->cells('A'), 1);
		TS_ASSERT_EQUALS(n->advanceNarrow(), 8);

		const byte *r = n->row('A', 3);
		for (int x = 0; x < 24; x++) {
			// 0xF0 0x01 at 1 bpp: pixels 0-3 and 15 set; 16..23 are padding.
			const byte want = (x < 4 || x == 15) ? 255 : 0;
			TS_ASSERT_EQUALS(r[x], want);
		}
		// Rows below the source's cell are empty.
		r = n->row('A', 9);
		for (int x = 0; x < 24; x++)
			TS_ASSERT_EQUALS(r[x], 0);

		// It joins a chain of a 12x10, 8 bpp face.
		Common::Array<Graphics::UnicodeGlyphSource *> chain;
		chain.push_back(n);
		Graphics::FallbackGlyphSource fb(chain, DisposeAfterUse::YES);
		TS_ASSERT_EQUALS(fb.cells('A'), 1);
	}

	void test_normalized_source_refuses_a_larger_cell() {
		Common::String error;
		TS_ASSERT(!Graphics::NormalizedGlyphSource::create(new OneBitSource(), 6, 10, DisposeAfterUse::YES, error));
		TS_ASSERT(!error.empty());
	}

	// SARA AM (U+0E33) is a spacing vowel (Lo) whose nikhahit ring sits over
	// the base before it, left of its own origin: like a combining mark it
	// is drawn with its origin moved right, so the ring is not cut off.
	void test_ttf_sara_am_keeps_its_ring() {
#ifdef USE_FREETYPE2
		// Face 0 of the system's Sukhumvit Set (loadTTFFont opens face 0);
		// quietly nothing to check where it is absent, like the shared
		// coverage tests.
		Common::FSNode node("/System/Library/Fonts/Supplemental/SukhumvitSet.ttc");
		if (!node.exists())
			return;
		Common::String error;
		Graphics::TtfGlyphSource *src = Graphics::TtfGlyphSource::create(node.createReadStream(), DisposeAfterUse::YES,
																		  16, error, false);
		TS_ASSERT(src);
		if (!src)
			return;
		Graphics::GlyphMetrics am, aa;
		TS_ASSERT(src->metrics(0x0E33, am));
		TS_ASSERT(src->metrics(0x0E32, aa));
		TS_ASSERT(am.originX > 0);
		TS_ASSERT(!am.combining);
		TS_ASSERT(am.advance > 0);
		TS_ASSERT_EQUALS(aa.originX, 0);   // SARA AA is left alone
		delete src;
#endif
	}

	// --- legacy widths, the anchor, the chain key (fix round 1) -------------

	/** A SCVMUNI-like bundle: 9x16 cells, advances 9/18, 1 bpp. */
	class OddCellSource : public Graphics::UnicodeGlyphSource {
	public:
		byte cellWidth() const override { return 9; }
		byte cellHeight() const override { return 16; }
		byte advanceNarrow() const override { return 9; }
		byte advanceWide() const override { return 18; }
		int bitsPerPixel() const override { return 1; }
		int cells(uint32 cp) override { return cp == 0xAC00 ? 2 : (cp >= 0x80 ? 1 : 0); }
		const byte *row(uint32 cp, int y) override { return _row; }
		uint32 glyphCount() const override { return 3; }
	private:
		byte _row[3] = { 0, 0, 0 };
	};

	void test_legacy_font_keeps_cell_widths() {
		OddCellSource src;
		// Without a UTF-8 translation: the cell halved as it always was
		// (9 >> 1 = 4), a mark included; wide 18 >> 1 = 9.
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x00E9, 2, false), 4);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x0301, 2, false), 4);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0xAC00, 2, false), 9);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x00E9, 1, false), 9);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x0041, 2, false), 0);   // no glyph
		// With one: the face's advance rounded half up (9 / 2 -> 5), a mark 0,
		// wide still the cell.
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x00E9, 2, true), 5);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0x0301, 2, true), 0);
		TS_ASSERT_EQUALS(Sci::glyphGameWidth(&src, 0xAC00, 2, true), 9);
	}

	void test_combining_anchor_is_reset_per_string() {
		Sci::CombiningAnchor a;
		Graphics::GlyphMetrics base, mark;
		base.advance = 13;          // hi-res px; 7 game px
		mark.combining = true;
		mark.originX = 4;

		TS_ASSERT_EQUALS(a.place(base, true, 10, 5, 7), 20);
		// The mark drawn where the base left the pen: against the base's
		// hi-res pen (20 + 13), not its own game-px position (17 * 2).
		TS_ASSERT_EQUALS(a.place(mark, true, 17, 5, 0), 20 + 13 - 4);
		// Elsewhere: at its own position.
		TS_ASSERT_EQUALS(a.place(mark, true, 30, 5, 0), 60 - 4);

		// A new string starting where the last one ended: no anchor.
		a.place(base, true, 10, 5, 7);
		a.reset();
		TS_ASSERT_EQUALS(a.place(mark, true, 17, 5, 0), 34 - 4);
		// No metrics: at the game position.
		TS_ASSERT_EQUALS(a.place(mark, false, 17, 5, 0), 34);
	}

	void test_face_chain_key_separates_sizes() {
		// [font.300] size=20 and [hires] size=16 on the same face: two chains.
		Common::Array<Common::String> faces;
		faces.push_back("/fonts/a.ttf");
		faces.push_back("/fonts/b.ttf");
		TS_ASSERT_DIFFERS(Sci::faceChainKey(faces, 16, true), Sci::faceChainKey(faces, 20, true));
		TS_ASSERT_EQUALS(Sci::faceChainKey(faces, 16, true), Sci::faceChainKey(faces, 16, true));
		TS_ASSERT_DIFFERS(Sci::faceChainKey(faces, 16, true), Sci::faceChainKey(faces, 16, false));
		Common::Array<Common::String> other;
		other.push_back("/fonts/b.ttf");
		other.push_back("/fonts/a.ttf");
		TS_ASSERT_DIFFERS(Sci::faceChainKey(faces, 16, true), Sci::faceChainKey(other, 16, true));
	}

	// --- the decoder ----------------------------------------------------

	void test_sci_decoder_escapes() {
		// "|c1|" is a text code (SCI1.1), CR LF one newline, 가 U+AC00.
		const char *text = "|c1|\xEA\xB0\x80\r\nA";
		Graphics::TextRun run;
		const Sci::SciTextDecoder dec(true);
		run.decode((const byte *)text, strlen(text), dec);

		TS_ASSERT_EQUALS(run.size(), 4u);
		TS_ASSERT_EQUALS(run.cp(0), Graphics::kControlUnit);
		TS_ASSERT(run.flags(0) & Graphics::kUnitControl);
		TS_ASSERT(!(run.flags(0) & Graphics::kUnitNewline));
		TS_ASSERT_EQUALS(run.byteOffset(1) - run.byteOffset(0), 4u);

		TS_ASSERT_EQUALS(run.cp(1), 0xAC00u);
		TS_ASSERT_EQUALS(run.byteOffset(2) - run.byteOffset(1), 3u);

		TS_ASSERT(run.flags(2) & Graphics::kUnitNewline);
		TS_ASSERT_EQUALS(run.byteOffset(3) - run.byteOffset(2), 2u);

		TS_ASSERT_EQUALS(run.cp(3), (uint32)'A');
		TS_ASSERT_EQUALS(run.byteOffset(4), 10u);
	}

	void test_sci_decoder_before_sci11_and_other_line_ends() {
		// Before SCI1.1 '|' is an ordinary character; a lone CR, a lone LF
		// and U+FF20 (SQ4 Japanese) end a line.
		const char *text = "|\rx\ny\xEF\xBC\xA0z";
		Graphics::TextRun run;
		const Sci::SciTextDecoder dec(false);
		run.decode((const byte *)text, strlen(text), dec);

		TS_ASSERT_EQUALS(run.size(), 7u);
		TS_ASSERT_EQUALS(run.cp(0), (uint32)'|');
		TS_ASSERT(!(run.flags(0) & Graphics::kUnitControl));
		TS_ASSERT(run.flags(1) & Graphics::kUnitNewline);
		TS_ASSERT(run.flags(3) & Graphics::kUnitNewline);
		TS_ASSERT(run.flags(5) & Graphics::kUnitNewline);
		TS_ASSERT_EQUALS(run.byteOffset(6) - run.byteOffset(5), 3u);

		// An unterminated code runs to the end of the text, as
		// GfxText16::CodeProcessing() reads it.
		const char *open = "a|f1";
		const Sci::SciTextDecoder dec11(true);
		run.decode((const byte *)open, strlen(open), dec11);
		TS_ASSERT_EQUALS(run.size(), 2u);
		TS_ASSERT(run.flags(1) & Graphics::kUnitControl);
		TS_ASSERT_EQUALS(run.byteOffset(2) - run.byteOffset(1), 3u);
	}

	// --- GetLongest() on the layout stage --------------------------------

	/**
	 * Widths in game px: a fixed 4/8 rule, or a proportional ASCII table;
	 * font N (set by a '|fN|' code, '|f|' back to 0) adds N px to every
	 * glyph that has a width.
	 */
	class TestMetrics : public Sci::SciLayoutMetrics {
	public:
		explicit TestMetrics(bool proportional) : font(0), _proportional(proportional) {}
		int charWidth(uint32 cp) override { return width(cp, _proportional, font); }
		void textCode(const byte *code, int bytes) override {
			// GfxText16::CodeProcessing(), from after the '|'.
			const char *p = (const char *)code + 1;
			applyCode(p, font);
		}

		static int width(uint32 cp, bool proportional, int font) {
			if (Graphics::Unicode::isCombining(cp))
				return 0;
			if (Graphics::Unicode::isWide(cp))
				return 8 + font;
			if (cp < 0x80 && proportional)
				return (cp == ' ' ? 3 : 3 + (int)(cp % 3)) + font;
			return 4 + font;
		}

		/** CodeProcessing() at @p text (after the '|'): its byte count; 'f' codes set @p font. */
		static int16 applyCode(const char *&text, int &font) {
			const char *textCode = text;
			int16 textCodeSize = 1;
			while ((*text != 0) && (*text++ != 0x7C))
				textCodeSize++;
			const char curCode = textCode[0];
			int curCodeParm = strtol(textCode + 1, nullptr, 10);
			if (!Common::isDigit(textCode[1]))
				curCodeParm = -1;
			if (curCode == 'f') {
				if (curCodeParm == -1)
					font = 0;
				else if (curCodeParm < 4)
					font = curCodeParm;
			}
			return textCodeSize;
		}

		int font;

	private:
		bool _proportional;
	};

	/**
	 * The byte-walking GetLongest() of GfxText16 before this task, for UTF-8
	 * text (no PQ2 escape): the reference the layout stage must reproduce.
	 * @p textCodes is SCI1.1's '|' code handling (CodeProcessing()).
	 */
	static int16 oldGetLongest(const char *&textPtr, int16 maxWidth, bool early, bool proportional,
							   bool textCodes = false) {
		uint32 curChar = 0;
		int16 lastSpaceCharCount = 0;
		const char *lastSpacePtr = nullptr;
		int16 curCharCount = 0, resultCharCount = 0;
		uint16 tempWidth = 0;
		int curCharBytes = 0;
		int font = 0;

		for (;;) {
			curChar = Sci::decodeUtf8Char((const byte *)textPtr, curCharBytes);
			switch (curChar) {
			case 0x7C:
				if (textCodes) {
					curCharCount++; textPtr++;
					curCharCount += TestMetrics::applyCode(textPtr, font);
					continue;
				}
				break;
			case 0xD:
				if ((*(const byte *)(textPtr + 1)) == 0xA) {
					curCharCount++; textPtr++;
				}
				// fall through
			case 0xA:
			case 0xFF20:
				curCharCount += curCharBytes; textPtr += curCharBytes;
				// fall through
			case 0:
				return curCharCount;
			case ' ':
				lastSpaceCharCount = curCharCount;
				lastSpacePtr = textPtr + 1;
				break;
			default:
				break;
			}
			tempWidth += TestMetrics::width(curChar, proportional, font);
			if (tempWidth > maxWidth)
				break;
			if (early && lastSpaceCharCount == 0 && tempWidth == maxWidth)
				break;
			curCharCount += curCharBytes; textPtr += curCharBytes;
		}

		if (lastSpaceCharCount) {
			resultCharCount = lastSpaceCharCount;
			textPtr = lastSpacePtr;
			while (*textPtr == ' ')
				textPtr++;
		} else {
			if (early) {
				curCharCount += curCharBytes; textPtr += curCharBytes;
			}
			resultCharCount = curCharCount;
		}
		return resultCharCount;
	}

	/**
	 * @p s with SCI1.1 text codes put in: '|c1|' before the second word,
	 * '|f1|' before the third, '|f|' before the fifth, '|c|' glued inside
	 * the first word and '|f2|' at the very end.
	 */
	static Common::String withCodes(const char *s) {
		Common::String out;
		int word = 0;
		bool inWord = false;
		int charsInFirst = 0;
		for (const char *p = s; *p; p++) {
			const bool space = *p == ' ';
			if (!space && !inWord) {
				if (word == 1)
					out += "|c1|";
				else if (word == 2)
					out += "|f1|";
				else if (word == 4)
					out += "|f|";
				word++;
			}
			inWord = !space;
			out += *p;
			// After the first byte-sequence start of the first word's second character.
			if (word == 1 && inWord && (((byte)p[1] & 0xC0) != 0x80) && ++charsInFirst == 1)
				out += "|c|";
		}
		out += "|f2|";
		return out;
	}

	/** The line-by-line comparison of the two GetLongest()s; differences listed in @p report. */
	static void compareLines(const char *str, const char *name, int16 width, bool early, bool proportional,
							 bool textCodes, Sci::SciLayoutText &lt, uint &lines, uint &differences,
							 Common::String &report) {
		const Graphics::BreakRules rules;   // hangul=word, kinsoku on, Thai on
		TestMetrics m(proportional);
		const char *oldPtr = str;
		while (*oldPtr) {
			const char *lineStart = oldPtr;
			const int16 oldCount = oldGetLongest(oldPtr, width, early, proportional, textCodes);
			uint32 next = 0;
			m.font = 0;   // GetLongest() starts each line in the caller's font
			const int16 newCount = Sci::getLongestLayout((const byte *)lineStart, width, textCodes,
														 early, m, rules, lt, next);
			lines++;
			if (oldCount != newCount || (uint32)(oldPtr - lineStart) != next) {
				if (differences < 20)
					report += Common::String::format("\n%s @%u w%d early%d prop%d codes%d: old %d/%d new %d/%u: \"%s\"",
						name, (uint)(lineStart - str), width, early, proportional, textCodes,
						oldCount, (int)(oldPtr - lineStart), newCount, next,
						Common::String(lineStart, MIN<uint32>(60, strlen(lineStart))).c_str());
				differences++;
				oldPtr = lineStart + next;   // follow the new layout
			}
			if (!oldCount)
				break;
		}
	}

	/**
	 * $SCUMMVM_TEST_I18N_DATA/kq1-ko1 (the KQ1 Korean UTF-8 patch, its
	 * text.NNN files), or an empty node when the variable or the directory
	 * is absent.
	 */
	static Common::FSNode kq1koDir() {
		const char *root = i18nDataDir();
		if (!root || !*root)
			return Common::FSNode();
		Common::FSNode n = Common::FSNode(Common::Path(root, Common::Path::kNativeSeparator)).getChild("kq1-ko1");
		return (n.exists() && n.isDirectory()) ? n : Common::FSNode();
	}

	void test_fitline_matches_getlongest_on_kq1ko() {
		const Common::FSNode dir = kq1koDir();
		if (!dir.exists()) {
			TS_SKIP("SCUMMVM_TEST_I18N_DATA does not name a directory holding kq1-ko1/");
			return;
		}

		Common::FSList files;
		dir.getChildren(files, Common::FSNode::kListFilesOnly);
		Sci::SciLayoutText lt;
		uint nStrings = 0, lines = 0, differences = 0;
		Common::String report;

		for (uint f = 0; f < files.size(); f++) {
			const Common::String name = files[f].getName();
			if (!name.hasPrefixIgnoreCase("text."))
				continue;
			Common::SeekableReadStream *in = files[f].createReadStream();
			if (!in)
				continue;
			Common::Array<char> data(in->size() + 1);
			in->read(data.begin(), in->size());
			data[in->size()] = 0;
			const uint32 size = in->size();
			delete in;

			// An SCI0 patch: 2 header bytes, then NUL-separated strings.
			for (uint32 pos = 2; pos < size; ) {
				const char *str = data.begin() + pos;
				const uint32 len = strlen(str);
				pos += len + 1;
				if (!len)
					continue;
				nStrings++;
				// The same string with SCI1.1 '|c|'/'|f|' codes, read as SCI1.1.
				const Common::String coded = withCodes(str);
				static const int16 widths[] = { 120, 160, 200 };
				for (int w = 0; w < 3; w++) {
					for (int mode = 0; mode < 4; mode++) {
						const bool early = mode & 1;
						const bool proportional = mode & 2;
						compareLines(str, name.c_str(), widths[w], early, proportional, false,
									 lt, lines, differences, report);
						compareLines(coded.c_str(), name.c_str(), widths[w], early, proportional, true,
									 lt, lines, differences, report);
					}
				}
			}
		}

		TS_ASSERT(nStrings > 1000);
		TS_ASSERT(lines > 80000);
		TSM_ASSERT_EQUALS(Common::String::format("%u of %u lines differ:%s", differences, lines, report.c_str()).c_str(),
						  differences, 0u);
	}

	/** Every width from 1 to 60 on strings built for the old code's corners. */
	void test_getlongest_layout_matches_old_on_edge_cases() {
		static const char *const texts[] = {
			"abcd efg",                  // a first word that fits exactly (early)
			"aa  bb   cc",               // several spaces: the count keeps all but the last
			"hello world  ",             // trailing spaces that overflow at the end
			" leading space here",       // a space at the start is no break
			"  two leading",
			"word\r\nnext line",         // CR LF
			"one\ntwo\rthree",           // LF, CR
			"abcdefghijklmnopqrstuvwxyz", // no space: split
			"x \xEA\xB0\x80\xEA\xB0\x81\xEA\xB0\x82 y \xEA\xB0\x83",   // Hangul words
			"\xEA\xB0\x80\xEA\xB0\x81\xEA\xB0\x82\xEA\xB0\x83\xEA\xB0\x84\xEA\xB0\x85",
			"a b",
			"ab ",
			" ",
			"a  ",
			"|c1|hello |f1|world",       // SCI1.1 codes (read as codes in modes 4..7)
			"|f2|abc def|f| ghi",        // font changes mid-line
			"a|c1|b c|f1|d e"
		};
		Sci::SciLayoutText lt;
		uint differences = 0, lines = 0;
		Common::String report;
		for (uint t = 0; t < ARRAYSIZE(texts); t++) {
			for (int16 w = 1; w <= 60; w++) {
				for (int mode = 0; mode < 8; mode++)
					compareLines(texts[t], "edge", w, mode & 1, mode & 2, mode & 4, lt, lines, differences, report);
			}
		}
		TSM_ASSERT_EQUALS(Common::String::format("%u of %u lines differ:%s", differences, lines, report.c_str()).c_str(),
						  differences, 0u);
	}

	// The one intended difference from the old code (design section 4.3, the
	// T2 ruling): in "space, code, space, text" the code ends the line
	// before, instead of opening the next line with a space.
	void test_getlongest_layout_code_between_spaces_ends_the_line() {
		TestMetrics m(false);
		const Graphics::BreakRules rules;
		Sci::SciLayoutText lt;
		uint32 next = 0;
		const char *text = "hello |c| world";
		const int16 n = Sci::getLongestLayout((const byte *)text, 20, true, false, m, rules, lt, next);
		TS_ASSERT_EQUALS(n, 9);           // "hello |c|"
		TS_ASSERT_EQUALS(next, 10u);      // "world"
		const char *old = text;
		TS_ASSERT_EQUALS(oldGetLongest(old, 20, false, false, true), 5);   // "hello" / "|c| world"
	}

	void test_getlongest_layout_japanese_kinsoku_and_thai() {
		TestMetrics m(false);
		const Graphics::BreakRules rules;
		Sci::SciLayoutText lt;
		uint32 next = 0;

		// 8 px per kanji, 40 px: 5 fit. "あいうえ。お" must not start a line
		// with the full stop: the break moves before え.
		const char *ja = "\xE3\x81\x82\xE3\x81\x84\xE3\x81\x86\xE3\x81\x88\xE3\x81\x8A\xE3\x80\x82\xE3\x81\x8B";
		// あいうえお。か - 7 units of 8 px, width 40 fits あいうえお, 。 cannot start
		int16 n = Sci::getLongestLayout((const byte *)ja, 40, false, true, m, rules, lt, next);
		TS_ASSERT_EQUALS(n, 12);          // あいうえ, 4 x 3 bytes
		TS_ASSERT_EQUALS(next, 12u);

		// Thai: "ที่นี่" (U+0E17 U+0E35 U+0E48 U+0E19 U+0E35 U+0E48), 4 px
		// per base, marks 0: a line of 4 px holds one base and its marks.
		const char *th = "\xE0\xB8\x97\xE0\xB8\xB5\xE0\xB9\x88\xE0\xB8\x99\xE0\xB8\xB5\xE0\xB9\x88";
		n = Sci::getLongestLayout((const byte *)th, 4, false, false, m, rules, lt, next);
		TS_ASSERT_EQUALS(n, 9);           // ที่: the marks stay with their base
		TS_ASSERT_EQUALS(next, 9u);
	}
};
